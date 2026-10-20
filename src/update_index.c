/* Persistent vocabulary view indexes (craigtrim/axiom#129).
 * Structural edits use the transactional graph builder. Replacing literal
 * vocabulary in existing subject slots patches only the affected view buckets.
 */
#include "mc.h"
#define SLOTS(i, m)                          \
	for (size_t i = 0; i < (m).cap; i++) \
		if ((m).slots[i].key)
typedef struct {
	J *object;
	Map members;
} ObjectIndex;
typedef struct SourceGroups SourceGroups;
static SourceGroups *source_groups_build(J *, Map *, int);
static void source_groups_free(SourceGroups *);
static void source_groups_change(SourceGroups *, J *, Map *, int, int);
static J *source_groups_order(SourceGroups *);
struct UpdateState {
	Map objects, ordinals, owners, ner_refs, plus_rules;
	LookupState *snapshot;
	int mirrored;
	SourceGroups *cached_order, *live_order;
};
static int vocabulary(const char *p)
{
	return !strcmp(p, RDFS "label") || !strcmp(p, RDFS "seeAlso") ||
	       !strcmp(p, SKOS "altLabel") || !strcmp(local(p), "inflection");
}
static ObjectIndex *object_index(UpdateState *u, J *object)
{
	char key[64];
	snprintf(key, sizeof(key), "%p", (void *)object);
	ObjectIndex *x = map_get(&u->objects, key);
	if (!x) {
		x = calloc(1, sizeof(*x));
		x->object = object;
		map_reserve(&x->members, (size_t)SIZE(object));
		EACH(v, object)
			map_put(&x->members, v->string, v);
		map_put(&u->objects, key, x);
	}
	return x;
}
static void member_set(UpdateState *u, J *object, const char *key, J *value)
{
	ObjectIndex *x = object_index(u, object);
	if (value)
		indexed_set(object, &x->members, key, value);
	else {
		J *old = map_get(&x->members, key);
		if (old) {
			DEL(cJSON_DetachItemViaPointer(object, old));
			map_delete(&x->members, key);
		}
	}
}
static J *member_get(UpdateState *u, J *object, const char *key)
{
	return map_get(&object_index(u, object)->members, key);
}
static void prepare_view(UpdateState *u, J *view)
{
	const char *names[] = { "labels", "ner", "spans", "_labels" };
	for (size_t i = 0; i < sizeof(names) / sizeof(*names); i++) {
		J *o = GET(view, names[i]);
		if (cJSON_IsObject(o))
			object_index(u, o);
	}
	J *syns = GET(view, "synonyms");
	object_index(u, GET(syns, "fwd"));
	object_index(u, GET(syns, "rev"));
	EACH(p, GET(view, "by_predicate"))
		object_index(u, p);
}
static void ner_ref(UpdateState *u, const char *text, int delta)
{
	char *key = norm(text, 0, 0);
	size_t n = (size_t)(uintptr_t)map_get(&u->ner_refs, key);
	if (delta > 0)
		map_put(&u->ner_refs, key, (void *)(uintptr_t)(n + 1));
	else if (n > 1)
		map_put(&u->ner_refs, key, (void *)(uintptr_t)(n - 1));
	else
		map_delete(&u->ner_refs, key);
	free(key);
}
static J *raw_subject(Graph *g, J *ids)
{
	J *raw = OBJ();
	EACH(id, ids) {
		Triple *t = &g->ts[(size_t)id->valuedouble];
		if (!vocabulary(t->p.value))
			continue;
		char *key = norm(local(t->s.value), 1, 0),
		     *value = norm(t->o.value, 1, 0);
		if (*value && strcmp(value, "nil"))
			unique(ensure(raw, key, 1), value);
		free(key);
		free(value);
	}
	return raw;
}
static char *rule_key(const char *key, J *r)
{
	char *clean = norm(key, 0, 0), *json = cJSON_PrintUnformatted(r);
	Buf b = { 0 };
	buf_put(&b, clean);
	buf_put(&b, "\x1f");
	buf_put(&b, json);
	free(clean);
	free(json);
	return buf_take(&b);
}
static void plus_refs(UpdateState *u, J *plus, int add)
{
	EACH(key, plus)
		EACH(r, key) {
			char *s = rule_key(key->string, r);
			if (add)
				map_put(&u->plus_rules, s, (void *)1);
			else
				map_delete(&u->plus_rules, s);
			free(s);
		}
}
void update_state_free(UpdateState *u)
{
	if (!u)
		return;
	source_groups_free(u->cached_order);
	source_groups_free(u->live_order);
	lookup_state_free(u->snapshot);
	SLOTS(i, u->objects)
	{
		ObjectIndex *x = u->objects.slots[i].value;
		map_free(&x->members);
		free(x);
	}
	map_free(&u->objects);
	map_free(&u->ordinals);
	map_free(&u->owners);
	map_free(&u->ner_refs);
	map_free(&u->plus_rules);
	free(u);
}
void update_state_build(mc_engine *e)
{
	if (e->updates || !e->graph || e->part_count || e->graph_only ||
	    !e->snapshot || (e->live_mode && !e->live))
		return;
	if (e->source_path && *e->source_path) {
		Buf path = { 0 };
		buf_put(&path, e->source_path);
		buf_put(&path, ".txt");
		FILE *file = mc_fopen(path.p, "rb");
		free(path.p);
		if (file) {
			fclose(file);
			return;
		}
	}
	/* Blank expansion and literal plus syntax can change other source views.
     * These graphs retain the fully validated transactional path. */
	for (size_t i = 0; i < e->graph->n; i++) {
		Triple *t = &e->graph->ts[i];
		if (t->s.kind == 2 || t->o.kind == 2 ||
		    (vocabulary(t->p.value) &&
		     (t->o.kind != 1 || strchr(t->o.value, '+'))))
			return;
	}
	UpdateState *u = calloc(1, sizeof(*u));
	map_reserve(&u->owners, e->graph->subjects.size);
	J *raw = OBJ();
	Map raw_keys = { 0 };
	for (size_t i = 0; i < e->graph->n; i++) {
		Triple *t = &e->graph->ts[i];
		char *canon = norm(local(t->s.value), 1, 0);
		const char *owner = map_get(&u->owners, canon);
		if (owner && strcmp(owner, t->s.value)) {
			free(canon);
			DEL(raw);
			map_free(&raw_keys);
			update_state_free(u);
			return;
		}
		map_put(&u->owners, canon, t->s.value);
		if (!strcmp(t->p.value, RDFS "label"))
			ner_ref(u, t->o.value, 1);
		if (vocabulary(t->p.value)) {
			char *value = norm(t->o.value, 1, 0);
			if (*value && strcmp(value, "nil"))
				unique(indexed_ensure(raw, &raw_keys, canon, 1),
				       value);
			free(value);
		}
		free(canon);
	}
	size_t ordinal = 0;
	J *syns = GET(e->snapshot, "synonyms");
	map_reserve(&u->ordinals, (size_t)SIZE(GET(syns, "fwd")));
	EACH(k, GET(syns, "fwd"))
		map_put(&u->ordinals, k->string, (void *)(uintptr_t)++ordinal);
	J *plus = generate_spans(raw, e->distance, 1, e->comma_lists);
	plus_refs(u, plus, 1);
	DEL(plus);
	DEL(raw);
	map_free(&raw_keys);
	prepare_view(u, e->snapshot);
	if (e->live)
		prepare_view(u, e->live);
	/* Build mutable maps directly on load; avoid constructing and discarding
	 * a complete read-only lookup index first. */
	if (!e->index)
		e->index = match_index_build_mutable(
			e->live_mode ? e->live : e->snapshot);
	if (!e->index) {
		update_state_free(u);
		return;
	}
	match_index_mutable(e->index);
	u->snapshot = lookup_state_reuse(GET(syns, "fwd"), GET(syns, "lookup"));
	if (e->live) {
		J *live_syns = GET(e->live, "synonyms");
		lookup_state_mirror(u->snapshot, live_syns);
		u->mirrored = 1;
	}
	u->cached_order =
		source_groups_build(GET(syns, "fwd"), &u->ordinals, 0);
	if (e->live)
		u->live_order =
			source_groups_build(GET(syns, "fwd"), &u->ordinals, 1);
	e->updates = u;
}
static int term_same(const Term *a, const Term *b)
{
	return a->kind == b->kind && !strcmp(a->value, b->value) &&
	       !strcmp(a->datatype ? a->datatype : "",
		       b->datatype ? b->datatype : "") &&
	       !strcmp(a->language ? a->language : "",
		       b->language ? b->language : "");
}

static J *base_groups(J *fwd)
{
	J *groups = ARR();
	EACH(k, fwd) {
		char *key = replace(k->string, "_", " ");
		int n = 1;
		for (const char *p = key; *p; p++)
			n += *p == ' ';
		char num[32];
		snprintf(num, sizeof(num), "%d", n);
		unique(groups, num);
		free(key);
		EACH(v, k) {
			char *normal = norm(S(v), 1, 1),
			     *text = replace(normal, "_", " ");
			n = 1;
			for (const char *p = text; *p; p++)
				n += *p == ' ';
			snprintf(num, sizeof(num), "%d", n);
			unique(groups, num);
			size_t length = strlen(normal);
			if (length && strchr("!?.", normal[length - 1])) {
				n = 1;
				for (const char *p = normal; *p; p++)
					n += *p == ' ';
				snprintf(num, sizeof(num), "%d", n);
				unique(groups, num);
			}
			free(normal);
			free(text);
		}
	}
	return groups;
}

typedef struct SourceGroupNode {
	struct SourceGroupNode *left, *right, *parent;
	size_t ordinal;
	unsigned priority;
	int sequence;
} SourceGroupNode;
typedef struct {
	SourceGroupNode *root, *last;
	Map members;
} SourceGroup;
struct SourceGroups {
	Map groups;
	unsigned random;
	int building;
};
static SourceGroupNode *source_merge(SourceGroupNode *a, SourceGroupNode *b)
{
	if (!a)
		return b;
	if (!b)
		return a;
	if (a->priority < b->priority) {
		a->right = source_merge(a->right, b);
		return a;
	}
	b->left = source_merge(a, b->left);
	return b;
}
static SourceGroupNode *source_insert(SourceGroupNode *root, SourceGroupNode *v)
{
	if (!root)
		return v;
	if (v->ordinal < root->ordinal) {
		root->left = source_insert(root->left, v);
		if (root->left->priority < root->priority) {
			SourceGroupNode *x = root->left;
			root->left = x->right;
			x->right = root;
			return x;
		}
	} else {
		root->right = source_insert(root->right, v);
		if (root->right->priority < root->priority) {
			SourceGroupNode *x = root->right;
			root->right = x->left;
			x->left = root;
			return x;
		}
	}
	return root;
}
static SourceGroupNode *source_remove(SourceGroupNode *root, SourceGroupNode *v)
{
	if (root == v)
		return source_merge(root->left, root->right);
	if (v->ordinal < root->ordinal)
		root->left = source_remove(root->left, v);
	else
		root->right = source_remove(root->right, v);
	return root;
}
static void source_groups_change(SourceGroups *s, J *fwd, Map *ordinals,
				 int live, int delta)
{
	EACH(k, fwd) {
		J *one = OBJ(), *values = DUP(k);
		if (live)
			sort_strings(values, 0);
		PUT(one, k->string, values);
		J *groups = base_groups(one);
		DEL(one);
		int sequence = 0;
		EACH(key, groups) {
			SourceGroup *g = map_get(&s->groups, S(key));
			if (delta > 0) {
				if (!g) {
					g = calloc(1, sizeof(*g));
					map_put(&s->groups, S(key), g);
				}
				SourceGroupNode *v = calloc(1, sizeof(*v));
				v->ordinal = (size_t)(uintptr_t)map_get(
					ordinals, k->string);
				v->sequence = sequence;
				s->random ^= s->random << 13;
				s->random ^= s->random >> 17;
				s->random ^= s->random << 5;
				v->priority = s->random;
				if (s->building) {
					/* Source ordinals arrive sorted during load. */
					SourceGroupNode *at = g->last,
							*left = NULL;
					while (at &&
					       at->priority > v->priority) {
						left = at;
						at = at->parent;
					}
					v->left = left;
					if (left)
						left->parent = v;
					v->parent = at;
					if (at)
						at->right = v;
					else
						g->root = v;
					g->last = v;
				} else
					g->root = source_insert(g->root, v);
				map_put(&g->members, k->string, v);
			} else {
				SourceGroupNode *v =
					map_get(&g->members, k->string);
				g->root = source_remove(g->root, v);
				map_delete(&g->members, k->string);
				free(v);
				if (!g->root) {
					map_free(&g->members);
					free(g);
					map_delete(&s->groups, S(key));
				}
			}
			sequence++;
		}
		DEL(groups);
	}
}
static SourceGroups *source_groups_build(J *fwd, Map *ordinals, int live)
{
	SourceGroups *s = calloc(1, sizeof(*s));
	s->random = 129;
	s->building = 1;
	source_groups_change(s, fwd, ordinals, live, 1);
	s->building = 0;
	return s;
}
static void source_groups_free(SourceGroups *s)
{
	if (!s)
		return;
	SLOTS(i, s->groups)
	{
		SourceGroup *g = s->groups.slots[i].value;
		SLOTS(j, g->members) free(g->members.slots[j].value);
		map_free(&g->members);
		free(g);
	}
	map_free(&s->groups);
	free(s);
}
typedef struct {
	const char *key;
	size_t ordinal;
	int sequence;
} FirstGroup;
static int first_group_compare(const void *a, const void *b)
{
	const FirstGroup *x = a, *y = b;
	if (x->ordinal != y->ordinal)
		return x->ordinal < y->ordinal ? -1 : 1;
	return (x->sequence > y->sequence) - (x->sequence < y->sequence);
}
static J *source_groups_order(SourceGroups *s)
{
	FirstGroup *first = calloc(s->groups.size + 1, sizeof(*first));
	size_t n = 0;
	SLOTS(i, s->groups)
	{
		SourceGroup *g = s->groups.slots[i].value;
		SourceGroupNode *v = g->root;
		while (v->left)
			v = v->left;
		first[n++] = (FirstGroup){ s->groups.slots[i].key, v->ordinal,
					   v->sequence };
	}
	qsort(first, n, sizeof(*first), first_group_compare);
	J *order = ARR();
	for (size_t i = 0; i < n; i++)
		ADD(order, STR(first[i].key));
	for (int i = 1; i <= 6; i++) {
		char key[8];
		snprintf(key, sizeof(key), "%d", i);
		unique(order, key);
	}
	free(first);
	return order;
}
static int can_patch(mc_engine *e, Graph *added, Map *selected)
{
	if (!e->updates || !e->index || (e->live && !e->updates->mirrored))
		return 0;
	SLOTS(i, *selected)
	{
		const char *iri = selected->slots[i].key;
		J *old = map_get(&e->graph->subjects, iri),
		  *next = map_get(&added->subjects, iri);
		if (!old || !next || SIZE(old) != SIZE(next))
			return 0;
		size_t expected_id = (size_t)old->child->valuedouble;
		EACH(id, old)
			if ((size_t)id->valuedouble != expected_id++)
				return 0;
		J *a = old->child, *b = next->child;
		while (a && b) {
			Triple *x = &e->graph->ts[(size_t)a->valuedouble],
			       *y = &added->ts[(size_t)b->valuedouble];
			if (!term_same(&x->p, &y->p) ||
			    !term_same(&x->s, &y->s))
				return 0;
			if (vocabulary(x->p.value)) {
				if (x->o.kind != 1 || y->o.kind != 1 ||
				    strchr(y->o.value, '+'))
					return 0;
			} else if (!term_same(&x->o, &y->o))
				return 0;
			a = a->next;
			b = b->next;
		}
		J *old_raw = raw_subject(e->graph, old),
		  *new_raw = raw_subject(added, next);
		int same_key =
			SIZE(old_raw) == 1 && SIZE(new_raw) == 1 &&
			!strcmp(old_raw->child->string, new_raw->child->string);
		if (e->comma_lists)
			same_key = 0;
		DEL(old_raw);
		DEL(new_raw);
		if (!same_key)
			return 0;
	}
	return 1;
}
typedef struct {
	J *value;
	size_t ordinal;
	int phase, serial;
} Ordered;
static int order_compare(const void *a, const void *b)
{
	const Ordered *x = a, *y = b;
	if (x->phase != y->phase)
		return x->phase < y->phase ? -1 : 1;
	if (x->ordinal != y->ordinal)
		return x->ordinal < y->ordinal ? -1 : 1;
	return (x->serial > y->serial) - (x->serial < y->serial);
}
static void sort_sources(UpdateState *u, J *array, const char *span_key)
{
	int n = SIZE(array), at = 0;
	Ordered *items = calloc((size_t)n + 1, sizeof(*items));
	EACH(v, array) {
		const char *canon = span_key ? S(GET(v, "canon")) : S(v);
		items[at] = (Ordered){
			v, (size_t)(uintptr_t)map_get(&u->ordinals, canon), 1,
			at
		};
		if (span_key) {
			char *id = rule_key(span_key, v);
			items[at].phase = !map_get(&u->plus_rules, id);
			free(id);
		}
		at++;
	}
	while (array->child)
		cJSON_DetachItemFromArray(array, 0);
	qsort(items, (size_t)n, sizeof(*items), order_compare);
	for (int i = 0; i < n; i++)
		ADD(array, items[i].value);
	free(items);
}
static void collect_keys(Map *keys, J *object)
{
	EACH(v, object)
		map_put(keys, v->string, (void *)1);
}
static void patch_reverse(mc_engine *e, J *view, const char *canon, J *old_fwd,
			  J *new_fwd)
{
	UpdateState *u = e->updates;
	Map keys = { 0 };
	EACH(k, old_fwd)
		EACH(v, k)
			map_put(&keys, S(v), (void *)1);
	EACH(k, new_fwd)
		EACH(v, k)
			map_put(&keys, S(v), (void *)1);
	Map new_values = { 0 };
	EACH(k, new_fwd)
		EACH(v, k)
			map_put(&new_values, S(v), (void *)1);
	J *rev = GET(GET(view, "synonyms"), "rev");
	SLOTS(i, keys)
	{
		const char *key = keys.slots[i].key;
		J *bucket = member_get(u, rev, key);
		if (!bucket) {
			member_set(u, rev, key, ARR());
			bucket = member_get(u, rev, key);
		}
		J *v = bucket->child;
		while (v) {
			J *next = v->next;
			if (!strcmp(S(v), canon))
				DEL(cJSON_DetachItemViaPointer(bucket, v));
			v = next;
		}
		if (map_get(&new_values, key))
			ADD(bucket, STR(canon));
		if (view == e->live)
			sort_strings(bucket, 0);
		else
			sort_sources(u, bucket, NULL);
		if (!bucket->child) {
			member_set(u, rev, key, NULL);
			bucket = NULL;
		}
		if (match_index_view(e->index) == view)
			match_index_member(e->index, "rev", key, bucket);
	}
	map_free(&keys);
	map_free(&new_values);
}
static void patch_spans(mc_engine *e, J *view, const char *canon, J *old_spans,
			J *new_spans)
{
	UpdateState *u = e->updates;
	Map keys = { 0 };
	collect_keys(&keys, old_spans);
	collect_keys(&keys, new_spans);
	J *spans = GET(view, "spans");
	SLOTS(i, keys)
	{
		const char *key = keys.slots[i].key;
		J *bucket = member_get(u, spans, key);
		if (!bucket) {
			member_set(u, spans, key, ARR());
			bucket = member_get(u, spans, key);
		}
		J *v = bucket->child;
		while (v) {
			J *next = v->next;
			if (!strcmp(S(GET(v, "canon")), canon))
				DEL(cJSON_DetachItemViaPointer(bucket, v));
			v = next;
		}
		EACH(r, GET(new_spans, key))
			ADD(bucket, DUP(r));
		sort_sources(u, bucket, key);
		if (!bucket->child) {
			member_set(u, spans, key, NULL);
			bucket = NULL;
		}
		if (match_index_view(e->index) == view)
			match_index_member(e->index, "spans", key, bucket);
	}
	map_free(&keys);
}
static Graph *vocabulary_graph(Graph *g, J *ids, mc_error *err)
{
	Graph *out = rdf_create(err);
	EACH(id, ids) {
		Triple *t = &g->ts[(size_t)id->valuedouble];
		if (vocabulary(t->p.value))
			rdf_add(out, &t->s, &t->p, &t->o);
	}
	return out;
}
static void patch_view(mc_engine *e, J *view, J *old_local, J *new_local,
		       J *old_spans, J *new_spans, Graph *new_graph)
{
	UpdateState *u = e->updates;
	J *old_fwd = GET(GET(old_local, "synonyms"), "fwd"),
	  *new_fwd = GET(GET(new_local, "synonyms"), "fwd");
	const char *canon = new_fwd->child->string;
	patch_reverse(e, view, canon, old_fwd, new_fwd);
	J *fwd = GET(GET(view, "synonyms"), "fwd"),
	  *values = DUP(new_fwd->child);
	if (view == e->live)
		sort_strings(values, 0);
	member_set(u, fwd, canon, values);
	EACH(label, GET(new_local, "labels"))
		member_set(u, GET(view, "labels"), label->string, DUP(label));
	Map ner_keys = { 0 };
	collect_keys(&ner_keys, GET(old_local, "ner"));
	collect_keys(&ner_keys, GET(new_local, "ner"));
	SLOTS(i, ner_keys)
	{
		const char *key = ner_keys.slots[i].key;
		member_set(u, GET(view, "ner"), key,
			   map_get(&u->ner_refs, key) ? STR("NER") : NULL);
		if (match_index_view(e->index) == view)
			match_index_member(e->index, "ner", key,
					   member_get(u, GET(view, "ner"),
						      key));
	}
	map_free(&ner_keys);
	EACH(p, GET(new_local, "by_predicate")) {
		J *object = GET(GET(view, "by_predicate"), p->string);
		EACH(k, p)
			member_set(u, object, k->string, DUP(k));
	}
	patch_spans(e, view, canon, old_spans, new_spans);
	if (view == e->live) {
		mc_error ignored = { 0 };
		J *labels = graph_predicate(new_graph, RDFS "label", 0, 0,
					    &ignored);
		EACH(k, labels)
			member_set(u, GET(view, "_labels"), k->string, DUP(k));
		DEL(labels);
	}
}
int update_fast(mc_engine *e, Graph *added, Map *selected)
{
	if (!can_patch(e, added, selected))
		return 0;
	UpdateState *u = e->updates;
	lookup_state_watch(u->snapshot, e->index, match_index_word);
	SLOTS(i, *selected)
	{
		const char *iri = selected->slots[i].key;
		J *old_ids = map_get(&e->graph->subjects, iri),
		  *new_ids = map_get(&added->subjects, iri);
		mc_error err = { 0 };
		Graph *old_graph = vocabulary_graph(e->graph, old_ids, &err),
		      *new_graph = vocabulary_graph(added, new_ids, &err);
		J *old_local = ontology_build(old_graph, e->distance,
					      e->force_class, e->comma_lists,
					      &err);
		J *new_local = ontology_build(new_graph, e->distance,
					      e->force_class, e->comma_lists,
					      &err);
		J *old_raw = raw_subject(e->graph, old_ids),
		  *new_raw = raw_subject(added, new_ids);
		J *old_spans = generate_spans_indexed(
			old_raw, e->distance, 0, e->comma_lists, &u->ordinals);
		J *new_spans = generate_spans_indexed(
			new_raw, e->distance, 0, e->comma_lists, &u->ordinals);
		J *old_plus = generate_spans_indexed(
			old_raw, e->distance, 1, e->comma_lists, &u->ordinals);
		J *new_plus = generate_spans_indexed(
			new_raw, e->distance, 1, e->comma_lists, &u->ordinals);
		plus_refs(u, old_plus, 0);
		plus_refs(u, new_plus, 1);
		DEL(old_plus);
		DEL(new_plus);
		DEL(old_raw);
		DEL(new_raw);
		J *old_fwd = GET(GET(old_local, "synonyms"), "fwd"),
		  *new_fwd = GET(GET(new_local, "synonyms"), "fwd");
		source_groups_change(u->cached_order, old_fwd, &u->ordinals, 0,
				     -1);
		source_groups_change(u->cached_order, new_fwd, &u->ordinals, 0,
				     1);
		if (u->live_order) {
			source_groups_change(u->live_order, old_fwd,
					     &u->ordinals, 1, -1);
			source_groups_change(u->live_order, new_fwd,
					     &u->ordinals, 1, 1);
		}
		lookup_state_entity(u->snapshot, old_fwd, -1);
		lookup_state_entity(u->snapshot, new_fwd, 1);
		EACH(id, old_ids) {
			Triple *t = &e->graph->ts[(size_t)id->valuedouble];
			if (!strcmp(t->p.value, RDFS "label"))
				ner_ref(u, t->o.value, -1);
		}
		EACH(id, new_ids) {
			Triple *t = &added->ts[(size_t)id->valuedouble];
			if (!strcmp(t->p.value, RDFS "label"))
				ner_ref(u, t->o.value, 1);
		}
		patch_view(e, e->snapshot, old_local, new_local, old_spans,
			   new_spans, new_graph);
		if (e->live)
			patch_view(e, e->live, old_local, new_local, old_spans,
				   new_spans, new_graph);
		J *a = old_ids->child, *b = new_ids->child;
		while (a && b) {
			Triple *x = &e->graph->ts[(size_t)a->valuedouble],
			       *y = &added->ts[(size_t)b->valuedouble];
			if (!term_same(&x->o, &y->o))
				rdf_replace_object(e->graph,
						   (size_t)a->valuedouble,
						   &y->o);
			a = a->next;
			b = b->next;
		}
		DEL(old_local);
		DEL(new_local);
		DEL(old_spans);
		DEL(new_spans);
		rdf_free(old_graph);
		rdf_free(new_graph);
	}
	lookup_state_flush(u->snapshot);
	J *cached_order = source_groups_order(u->cached_order);
	lookup_state_order(u->snapshot, cached_order, 0);
	J *live_order = u->live_order ? source_groups_order(u->live_order) :
					      NULL;
	if (live_order)
		lookup_state_order(u->snapshot, live_order, 1);
	match_index_group_order(
		e->index, match_index_view(e->index) == e->live ? live_order :
									cached_order);
	DEL(cached_order);
	DEL(live_order);
	return 1;
}
