/* Persistent lookup dependencies for entity edits (craigtrim/axiom#129). */
#include "mc.h"
#include <assert.h>
#include "patterns.h"
#define SLOTS(i, m)                          \
	for (size_t i = 0; i < (m).cap; i++) \
		if ((m).slots[i].key)
/* Record each source-to-output relationship while lookup construction already
 * has both JSON nodes. Only the records are owned here; strings stay in JSON. */
struct LookupTrace {
	Map first, rest;
};
LookupTrace *lookup_trace_create(void)
{
	return calloc(1, sizeof(LookupTrace));
}
void lookup_trace_reserve(LookupTrace *trace, size_t additional)
{
	map_reserve(&trace->first, trace->first.size + additional);
}
void lookup_trace_add(LookupTrace *trace, J *source, J *output)
{
	J *first = map_get(&trace->first, S(source));
	if (!first)
		map_put_ref(&trace->first, S(source), output);
	else if (strcmp(S(first), S(output))) {
		Map *rest = map_get(&trace->rest, S(source));
		if (!rest) {
			rest = calloc(1, sizeof(*rest));
			map_put_ref(&trace->rest, S(source), rest);
		}
		map_reserve(rest, 1);
		map_put_ref(rest, S(output), output);
	}
}
void lookup_trace_free(LookupTrace *trace)
{
	if (!trace)
		return;
	SLOTS(i, trace->rest)
	{
		Map *rest = trace->rest.slots[i].value;
		map_free(rest);
		free(rest);
	}
	map_free(&trace->first);
	map_free(&trace->rest);
	free(trace);
}

typedef struct LookupValue LookupValue;
typedef struct Posting Posting;
typedef struct {
	Posting *head;
	size_t size;
	int parent, child, next;
	char *text;
	size_t length;
} PostingNode;
typedef struct {
	PostingNode *nodes;
	int count, capacity, available;
} PostingIndex;
struct Posting {
	LookupValue *value;
	int node, ends;
	Posting *next, *prev;
};
/* Only the first two and last two words need postings for an n-word gram. */
typedef struct {
	int count;
	Posting items[4];
} PostingSet;
typedef struct {
	Map values, grams, dirty;
	PostingIndex starts, ends;
	LookupValue *root;
	J *array, *mirror;
	int words;
} LookupGroup;
/* Most source values produce one distinct rewrite. Keep that reference inline;
 * additional rewrites use borrowed keys from their retained output values. */
typedef struct {
	LookupValue *first;
	Map *rest;
} DerivedRefs;
struct LookupValue {
	char *text, *gram;
	size_t chars, refs;
	unsigned priority;
	int indexed;
	LookupValue *left, *right;
	LookupGroup *group;
	J *entry, *mirror_entry;
	DerivedRefs derived;
	PostingSet *postings;
};
typedef struct LookupValueBlock {
	struct LookupValueBlock *next;
	LookupValue values[256];
} LookupValueBlock;
struct LookupState {
	Map groups;
	LookupValueBlock *blocks;
	LookupValue *available;
	J *json, *mirror;
	unsigned random;
	int attached, building;
	LookupTrace *trace;
	void *context;
	void (*change)(void *, int, const char *, int);
};
/* Keep mutable lookup records together and recycle removed slots. The strings
 * retain their individual ownership; blocks are released with the engine. */
static LookupValue *value_create(LookupState *s, const char *text)
{
	if (!s->available) {
		LookupValueBlock *block = calloc(1, sizeof(*block));
		block->next = s->blocks;
		s->blocks = block;
		for (int i = 255; i >= 0; i--) {
			block->values[i].left = s->available;
			s->available = &block->values[i];
		}
	}
	LookupValue *v = s->available;
	s->available = v->left;
	memset(v, 0, sizeof(*v));
	v->text = copy(text);
	v->chars = ulen(text);
	return v;
}

static int word_count(const char *s)
{
	int n = 1;
	for (; *s; s++)
		n += *s == ' ';
	return n;
}
static int before(LookupValue *a, LookupValue *b)
{
	return a->chars != b->chars ? a->chars > b->chars :
					    strcmp(a->text, b->text) > 0;
}
static LookupValue *tree_merge(LookupValue *a, LookupValue *b)
{
	if (!a)
		return b;
	if (!b)
		return a;
	if (a->priority < b->priority) {
		a->right = tree_merge(a->right, b);
		return a;
	}
	b->left = tree_merge(a, b->left);
	return b;
}
static LookupValue *tree_insert(LookupValue *root, LookupValue *v)
{
	if (!root)
		return v;
	if (before(v, root)) {
		root->left = tree_insert(root->left, v);
		if (root->left->priority < root->priority) {
			LookupValue *x = root->left;
			root->left = x->right;
			x->right = root;
			return x;
		}
	} else {
		root->right = tree_insert(root->right, v);
		if (root->right->priority < root->priority) {
			LookupValue *x = root->right;
			root->right = x->left;
			x->left = root;
			return x;
		}
	}
	return root;
}
static LookupValue *tree_remove(LookupValue *root, LookupValue *v)
{
	if (root == v)
		return tree_merge(root->left, root->right);
	if (before(v, root))
		root->left = tree_remove(root->left, v);
	else
		root->right = tree_remove(root->right, v);
	return root;
}
static LookupGroup *group_at(LookupState *s, int words)
{
	char key[32];
	snprintf(key, sizeof(key), "%d", words);
	LookupGroup *g = map_get(&s->groups, key);
	if (!g) {
		g = calloc(1, sizeof(*g));
		g->words = words;
		g->array = ARR();
		PUT(s->json, key, g->array);
		map_put(&s->groups, key, g);
		if (s->mirror) {
			g->mirror = ARR();
			PUT(s->mirror, key, g->mirror);
		}
	}
	if (!g->array) {
		g->array = ARR();
		PUT(s->json, key, g->array);
	}
	if (s->mirror && !g->mirror) {
		g->mirror = ARR();
		PUT(s->mirror, key, g->mirror);
	}
	return g;
}

static J *insert_json(J *array, J *prev, const char *text)
{
	J *entry = STR(text);
	if (!array->child)
		ADD(array, entry);
	else if (!prev) {
		J *first = array->child;
		entry->prev = first->prev;
		entry->next = first;
		first->prev = entry;
		array->child = entry;
	} else {
		entry->prev = prev;
		entry->next = prev->next;
		if (prev->next)
			prev->next->prev = entry;
		else
			array->child->prev = entry;
		prev->next = entry;
	}
	return entry;
}
static void show_value(LookupValue *v)
{
	LookupGroup *g = v->group;
	LookupValue *at = g->root, *prev = NULL;
	while (at) {
		if (before(at, v)) {
			prev = at;
			at = at->right;
		} else
			at = at->left;
	}
	v->entry = insert_json(g->array, prev ? prev->entry : NULL, v->text);
	if (g->mirror)
		v->mirror_entry = insert_json(
			g->mirror, prev ? prev->mirror_entry : NULL, v->text);
	v->left = v->right = NULL;
	g->root = tree_insert(g->root, v);
}

static LookupValue *adjust(LookupState *s, int words, const char *text,
			   int delta)
{
	LookupGroup *g = group_at(s, words);
	LookupValue *v = map_get(&g->values, text);
	if (!v) {
		assert(delta > 0);
		v = value_create(s, text);
		v->group = g;
		s->random ^= s->random << 13;
		s->random ^= s->random >> 17;
		s->random ^= s->random << 5;
		v->priority = s->random;
		map_put_ref(&g->values, v->text, v);
	}
	assert(delta > 0 || v->refs);
	if (delta > 0) {
		if (!v->refs++) {
			if (!v->entry)
				show_value(v);
			if (!s->building)
				map_put_ref(&g->dirty, v->text, v);
			if (s->change)
				s->change(s->context, words, text, 1);
		}
	} else if (!--v->refs) {
		g->root = tree_remove(g->root, v);
		v->left = v->right = NULL;
		DEL(cJSON_DetachItemViaPointer(g->array, v->entry));
		v->entry = NULL;
		if (v->mirror_entry) {
			DEL(cJSON_DetachItemViaPointer(g->mirror,
						       v->mirror_entry));
			v->mirror_entry = NULL;
		}
		map_put_ref(&g->dirty, v->text, v);
		if (s->change)
			s->change(s->context, words, text, -1);
	}
	return v;
}

static int post_node(PostingIndex *index, int parent, const char *text,
		     size_t length, int reversed)
{
	int id;
	if (index->available) {
		id = index->available;
		index->available = index->nodes[id].next;
	} else {
		if (index->count == index->capacity) {
			index->capacity =
				index->capacity ? index->capacity * 2 : 64;
			index->nodes = realloc(index->nodes,
					       (size_t)index->capacity *
						       sizeof(*index->nodes));
		}
		id = index->count++;
	}
	index->nodes[id] = (PostingNode){ 0 };
	index->nodes[id].parent = parent;
	index->nodes[id].text = length ? slice(text, length) : NULL;
	index->nodes[id].length = length;
	if (reversed)
		for (size_t i = 0; i < length / 2; i++) {
			char *label = index->nodes[id].text;
			char byte = label[i];
			label[i] = label[length - i - 1];
			label[length - i - 1] = byte;
		}
	return id;
}
static int post_child(PostingIndex *index, int node, unsigned char byte)
{
	for (int at = index->nodes[node].child; at; at = index->nodes[at].next)
		if ((unsigned char)index->nodes[at].text[0] == byte)
			return at;
	return 0;
}
/* Compressed edges retain terminal node IDs when split, so existing postings
 * never need to move. Boundary words still support partial-word substrings. */
static void post_add(PostingIndex *index, const char *text, size_t length,
		     int reversed, LookupValue *v)
{
	if (!length)
		return;
	if (!index->count)
		post_node(index, 0, "", 0, 0);
	int node = 0;
	size_t offset = 0;
	index->nodes[node].size++;
	while (offset < length) {
		int next = post_child(
			index, node,
			(unsigned char)
				text[reversed ? length - offset - 1 : offset]);
		if (!next) {
			next = post_node(index, node,
					 reversed ? text : text + offset,
					 length - offset, reversed);
			index->nodes[next].next = index->nodes[node].child;
			index->nodes[node].child = next;
			index->nodes[next].size = 1;
			node = next;
			break;
		}
		PostingNode old = index->nodes[next];
		size_t common = 0;
		while (common < old.length && offset + common < length &&
		       old.text[common] ==
			       text[reversed ? length - offset - common - 1 :
						     offset + common])
			common++;
		if (common < old.length) {
			int branch =
				post_node(index, node, old.text, common, 0);
			int *link = &index->nodes[node].child;
			while (*link != next)
				link = &index->nodes[*link].next;
			*link = branch;
			index->nodes[branch].next = old.next;
			index->nodes[branch].child = next;
			index->nodes[branch].size = old.size;
			index->nodes[next].parent = branch;
			index->nodes[next].next = 0;
			index->nodes[next].text =
				slice(old.text + common, old.length - common);
			index->nodes[next].length = old.length - common;
			free(old.text);
			next = branch;
		}
		node = next;
		index->nodes[node].size++;
		offset += common;
	}
	if (!v->postings)
		v->postings = calloc(1, sizeof(*v->postings));
	assert(v->postings->count < 4);
	Posting *p = &v->postings->items[v->postings->count++];
	p->value = v;
	p->ends = index == &v->group->ends;
	p->node = node;
	p->next = index->nodes[node].head;
	if (p->next)
		p->next->prev = p;
	index->nodes[node].head = p;
}

static void post_remove(Posting *p)
{
	PostingIndex *index = p->ends ? &p->value->group->ends :
					      &p->value->group->starts;
	if (p->prev)
		p->prev->next = p->next;
	else
		index->nodes[p->node].head = p->next;
	if (p->next)
		p->next->prev = p->prev;
	int node = p->node;
	while (node) {
		PostingNode *at = &index->nodes[node];
		int parent = at->parent;
		if (!--at->size) {
			int *link = &index->nodes[parent].child;
			while (*link != node)
				link = &index->nodes[*link].next;
			*link = at->next;
			free(at->text);
			at->text = NULL;
			at->next = index->available;
			index->available = node;
		}
		node = parent;
	}
	index->nodes[0].size--;
}
static int post_find(PostingIndex *index, const char *text, size_t length,
		     int reversed)
{
	if (!index->count)
		return 0;
	int node = 0;
	size_t offset = 0;
	while (offset < length) {
		node = post_child(
			index, node,
			(unsigned char)
				text[reversed ? length - offset - 1 : offset]);
		if (!node)
			return 0;
		PostingNode *edge = &index->nodes[node];
		for (size_t i = 0; i < edge->length && offset < length;
		     i++, offset++)
			if (edge->text[i] !=
			    text[reversed ? length - offset - 1 : offset])
				return 0;
	}
	/* A query ending inside an edge selects that edge's whole subtree. */
	return node;
}

static void post_values(PostingIndex *index, int root, const char *gram,
			Map *affected)
{
	int node = root;
	for (;;) {
		for (Posting *p = index->nodes[node].head; p; p = p->next)
			if (strstr(p->value->text, gram))
				map_put(affected, p->value->text, p->value);
		if (index->nodes[node].child) {
			node = index->nodes[node].child;
			continue;
		}
		while (node != root && !index->nodes[node].next)
			node = index->nodes[node].parent;
		if (node == root)
			break;
		node = index->nodes[node].next;
	}
}
/* Suffix/prefix postings preserve partial-word substring semantics. */
static void index_value(LookupValue *v, int add)
{
	if (!add) {
		if (v->postings) {
			for (int i = 0; i < v->postings->count; i++)
				post_remove(&v->postings->items[i]);
			free(v->postings);
			v->postings = NULL;
		}
		return;
	}
	const char *start = v->text;
	int word = 0;
	for (;;) {
		const char *space = strchr(start, ' '),
			   *end = space ? space : start + strlen(start);
		/* An n-word substring of an (n+1)-word value starts in its
         * first two words and ends in its last two words. */
		if (word < 2)
			post_add(&v->group->starts, start,
				 (size_t)(end - start), 1, v);
		if (word >= v->group->words - 2)
			post_add(&v->group->ends, start, (size_t)(end - start),
				 0, v);
		if (!space)
			break;
		start = space + 1;
		word++;
	}
}

static void gram_change(LookupGroup *g, const char *gram, int delta,
			Map *changed)
{
	if (!gram)
		return;
	size_t n = (size_t)(uintptr_t)map_get(&g->grams, gram);
	if (delta > 0) {
		map_put(&g->grams, gram, (void *)(uintptr_t)(n + 1));
		if (!n && changed)
			map_put(changed, gram, (void *)1);
	} else {
		assert(n);
		if (n == 1) {
			map_delete(&g->grams, gram);
			if (changed)
				map_put(changed, gram, (void *)1);
		} else
			map_put(&g->grams, gram, (void *)(uintptr_t)(n - 1));
	}
}
static void gram_values(LookupGroup *g, const char *gram, Map *affected)
{
	const char *space = strchr(gram, ' '), *last = strrchr(gram, ' ');
	if (!*gram || !space || space == gram || !last[1]) {
		SLOTS(i, g->values)
		{
			LookupValue *v = g->values.slots[i].value;
			if (v->refs && strstr(v->text, gram))
				map_put(affected, v->text, v);
		}
		return;
	}
	int a = post_find(&g->starts, gram, (size_t)(space - gram), 1),
	    b = post_find(&g->ends, last + 1, strlen(last + 1), 0);
	if (a && b) {
		if (g->starts.nodes[a].size < g->ends.nodes[b].size)
			post_values(&g->starts, a, gram, affected);
		else
			post_values(&g->ends, b, gram, affected);
	}
}

static LookupValue *derived_get(DerivedRefs *refs, const char *text)
{
	if (refs->first && !strcmp(refs->first->text, text))
		return refs->first;
	return refs->rest ? map_get(refs->rest, text) : NULL;
}
static void derived_add(DerivedRefs *refs, LookupValue *value)
{
	if (!refs->first)
		refs->first = value;
	else {
		if (!refs->rest)
			refs->rest = calloc(1, sizeof(*refs->rest));
		map_reserve(refs->rest, 1);
		map_put_ref(refs->rest, value->text, value);
	}
}
static void derived_free(DerivedRefs *refs)
{
	if (refs->rest) {
		map_free(refs->rest);
		free(refs->rest);
	}
	memset(refs, 0, sizeof(*refs));
}
static void derive_traced(LookupState *s, LookupValue *v)
{
	J *first = map_get(&s->trace->first, v->text);
	if (!first || !v->refs)
		return;
	const char *text = S(first);
	derived_add(&v->derived, adjust(s, word_count(text), text, 1));
	Map *rest = map_get(&s->trace->rest, v->text);
	if (rest) {
		SLOTS(i, *rest)
		{
			text = rest->slots[i].key;
			derived_add(&v->derived,
				    adjust(s, word_count(text), text, 1));
		}
	}
}

static void derive_gram(LookupState *s, LookupValue *v, DerivedRefs *outputs,
			const char *gram)
{
	char *u = replace(gram, " ", "_"), *text = replace(v->text, gram, u);
	int words = word_count(text);
	if (words >= 2 && words <= 5 && !derived_get(outputs, text)) {
		LookupValue *old = derived_get(&v->derived, text);
		derived_add(outputs, old ? old : adjust(s, words, text, 1));
	}
	free(u);
	free(text);
}

static void derive(LookupState *s, LookupValue *v, Patterns *bulk)
{
	DerivedRefs outputs = { 0 };
	if (v->refs && bulk) {
		int n = patterns_find(bulk, v->text);
		for (int i = 0; i < n; i++)
			derive_gram(s, v, &outputs, bulk->text[bulk->hits[i]]);
	} else if (v->refs) {
		/* Only substrings spanning n words can be one of this stage's grams.
         * Probe the gram hash directly; repeated occurrences produce one form. */
		int needed = v->group->words - 2;
		for (const char *a = v->text; *a; a++) {
			int spaces = 0;
			for (const char *b = a; *b; b++) {
				spaces += *b == ' ';
				if (spaces > needed)
					break;
				if (spaces != needed)
					continue;
				char *gram = slice(a, (size_t)(b - a + 1));
				if (map_get(&v->group->grams, gram)) {
					derive_gram(s, v, &outputs, gram);
				}
				free(gram);
			}
		}
	}
	LookupValue *first = v->derived.first;
	if (first && !derived_get(&outputs, first->text))
		adjust(s, first->group->words, first->text, -1);
	if (v->derived.rest) {
		SLOTS(i, *v->derived.rest)
		if (!derived_get(&outputs, v->derived.rest->slots[i].key)) {
			LookupValue *out = v->derived.rest->slots[i].value;
			adjust(s, out->group->words, out->text, -1);
		}
	}
	derived_free(&v->derived);
	v->derived = outputs;
}
static void release_value(LookupState *s, LookupValue *v)
{
	index_value(v, 0);
	free(v->text);
	free(v->gram);
	derived_free(&v->derived);
	memset(v, 0, sizeof(*v));
	v->left = s->available;
	s->available = v;
}
static void index_change(LookupValue *v, Map *changed)
{
	LookupGroup *g = v->group;
	if (!!v->refs == v->indexed)
		return;
	if (v->refs) {
		v->gram = lookup_leading_gram(v->text, g->words - 1);
		index_value(v, 1);
		gram_change(g, v->gram, 1, changed);
		v->indexed = 1;
	} else {
		index_value(v, 0);
		gram_change(g, v->gram, -1, changed);
		free(v->gram);
		v->gram = NULL;
		v->indexed = 0;
	}
}
void lookup_state_flush(LookupState *s)
{
	for (int words = 6; words >= 3; words--) {
		LookupGroup *g = group_at(s, words);
		if (s->building) {
			/* Loading visits every active value in each ordered group.
			 * Only edits need dirty/affected maps; walking the arrays keeps
			 * nearby patterns and values together during construction. */
			EACH(entry, g->array)
				index_change(map_get(&g->values, S(entry)),
					     NULL);
			if (s->trace) {
				EACH(entry, g->array)
					derive_traced(s, map_get(&g->values,
								 S(entry)));
				continue;
			}
			J *grams = ARR();
			SLOTS(i, g->grams)
			ADD(grams, STR(g->grams.slots[i].key));
			sort_strings(grams, -1);
			Patterns *bulk = patterns_build(grams);
			EACH(entry, g->array) {
				LookupValue *v = map_get(&g->values, S(entry));
				if (v->refs)
					derive(s, v, bulk);
			}
			patterns_free(bulk);
			DEL(grams);
			continue;
		}
		Map changed = { 0 }, affected = { 0 };
		map_reserve(&affected, g->dirty.size);
		SLOTS(i, g->dirty)
		{
			LookupValue *v = g->dirty.slots[i].value;
			if (!!v->refs == v->indexed)
				continue;
			map_put(&affected, v->text, v);
			index_change(v, &changed);
		}
		SLOTS(i, changed)
		gram_values(g, changed.slots[i].key, &affected);
		SLOTS(i, affected)
		derive(s, affected.slots[i].value, NULL);
		map_free(&changed);
		map_free(&affected);
	}
	SLOTS(i, s->groups)
	{
		LookupGroup *g = s->groups.slots[i].value;
		SLOTS(j, g->dirty)
		{
			LookupValue *v = g->dirty.slots[j].value;
			if (!v->refs) {
				map_delete(&g->values, v->text);
				release_value(s, v);
			}
		}
		map_free(&g->dirty);
	}
}
/* Each canonical contributes its distinct base forms once. Derived entries
 * carry one reference per source value, regardless of duplicate derivations. */
void lookup_state_entity(LookupState *s, J *fwd, int delta)
{
	Map seen = { 0 };
	EACH(k, fwd) {
		J *forms = ARR();
		char *key = replace(k->string, "_", " ");
		unique_indexed(forms, &seen, key);
		free(key);
		EACH(v, k) {
			char *normalized = norm(S(v), 1, 1),
			     *base = replace(normalized, "_", " ");
			unique_indexed(forms, &seen, base);
			free(base);
			size_t n = strlen(normalized);
			if (n && strchr("!?.", normalized[n - 1])) {
				normalized[n - 1] = 0;
				unique_indexed(forms, &seen, normalized);
			}
			free(normalized);
		}
		EACH(v, forms)
			adjust(s, word_count(S(v)), S(v), delta);
		DEL(forms);
		map_free(&seen);
	}
}
/* Existing lookup arrays already have the required length/lexical order.
 * Build their Cartesian trees in linear time and retain the JSON nodes. */
static void seed_lookup(LookupState *s)
{
	EACH(array, s->json) {
		LookupGroup *g = calloc(1, sizeof(*g));
		g->words = atoi(array->string);
		g->array = array;
		map_put(&s->groups, array->string, g);
		size_t n = (size_t)SIZE(array), depth = 0;
		map_reserve(&g->values, n);
		LookupValue **stack = malloc((n ? n : 1) * sizeof(*stack));
		EACH(entry, array) {
			LookupValue *v = value_create(s, S(entry)),
				    *left = NULL;
			v->group = g;
			v->entry = entry;
			s->random ^= s->random << 13;
			s->random ^= s->random >> 17;
			s->random ^= s->random << 5;
			v->priority = s->random;
			while (depth &&
			       stack[depth - 1]->priority > v->priority)
				left = stack[--depth];
			v->left = left;
			if (depth)
				stack[depth - 1]->right = v;
			else
				g->root = v;
			stack[depth++] = v;
			map_put_ref(&g->values, v->text, v);
		}
		free(stack);
	}
}
static LookupState *build_lookup(J *fwd, J *existing, LookupTrace *trace)
{
	LookupState *s = calloc(1, sizeof(*s));
	s->json = existing ? existing : OBJ();
	s->attached = existing != NULL;
	s->random = 129;
	s->building = 1;
	s->trace = trace;
	if (existing)
		seed_lookup(s);
	lookup_state_entity(s, fwd, 1);
	for (int n = 1; n <= 6; n++)
		group_at(s, n);
	lookup_state_flush(s);
	s->building = 0;
	s->trace = NULL;
	return s;
}
LookupState *lookup_state_build(J *fwd)
{
	return build_lookup(fwd, NULL, NULL);
}
LookupState *lookup_state_reuse(J *fwd, J *existing)
{
	return build_lookup(fwd, existing, NULL);
}
LookupState *lookup_state_reuse_traced(J *fwd, J *existing, LookupTrace *trace)
{
	return build_lookup(fwd, existing, trace);
}
J *lookup_state_json(LookupState *s)
{
	return s->json;
}
void lookup_state_free(LookupState *s)
{
	if (!s)
		return;
	SLOTS(i, s->groups)
	{
		LookupGroup *g = s->groups.slots[i].value;
		SLOTS(j, g->values) release_value(s, g->values.slots[j].value);
		for (int n = 0; n < g->starts.count; n++)
			free(g->starts.nodes[n].text);
		for (int n = 0; n < g->ends.count; n++)
			free(g->ends.nodes[n].text);
		free(g->starts.nodes);
		free(g->ends.nodes);
		map_free(&g->values);
		map_free(&g->grams);
		map_free(&g->dirty);
		free(g);
	}
	map_free(&s->groups);
	while (s->blocks) {
		LookupValueBlock *next = s->blocks->next;
		free(s->blocks);
		s->blocks = next;
	}
	if (!s->attached)
		DEL(s->json);
	free(s);
}

void lookup_state_watch(LookupState *s, void *context,
			void (*change)(void *, int, const char *, int))
{
	s->context = context;
	s->change = change;
}

/* With no external synonyms the data view has the same forms. Keep its JSON
 * ordering while sharing the dependency graph and updating both arrays. */
void lookup_state_mirror(LookupState *s, J *synonyms)
{
	s->mirror = GET(synonyms, "lookup");
	EACH(old, s->mirror) {
		LookupGroup *g = map_get(&s->groups, old->string);
		g->mirror = old;
		EACH(v, g->mirror) {
			LookupValue *value = map_get(&g->values, S(v));
			value->mirror_entry = v;
		}
	}
}

/* Reorder the few word-count groups from their earliest source contribution. */
void lookup_state_order(LookupState *s, J *order, int mirror)
{
	J *object = mirror ? s->mirror : s->json;
	if (!object)
		return;
	while (object->child)
		cJSON_DetachItemFromArray(object, 0);
	Map retained = { 0 };
	EACH(key, order) {
		LookupGroup *g = group_at(s, atoi(S(key)));
		J *array = mirror ? g->mirror : g->array;
		/* group_at may have restored a previously absent group. */
		if (array->next || (object->child == array) ||
		    (array->prev && array->prev != array))
			cJSON_DetachItemViaPointer(object, array);
		ADD(object, array);
		map_put(&retained, S(key), (void *)1);
	}
	SLOTS(i, s->groups) if (!map_get(&retained, s->groups.slots[i].key))
	{
		LookupGroup *g = s->groups.slots[i].value;
		J **array = mirror ? &g->mirror : &g->array;
		assert(!g->values.size);
		DEL(*array);
		*array = NULL;
	}
	map_free(&retained);
}
