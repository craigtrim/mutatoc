/*
 * ontology.c - Ontology extraction and derived lookup tables.
 *
 * Builds JSON views from RDF graphs and derives synonym and hierarchy data.
 */

#include "mc.h"
#include "patterns.h"
/* Ordered substring rewrites and hashed builders: craigtrim/axiom#129. */

static int count(const char *s, char c)
{
	int n = 0;
	for (; *s; s++)
		n += *s == c;
	return n;
}


/*
 * Each index lives only while its JSON object is being built. JSON remains the
 * public representation, including insertion order and duplicate handling.
 */
static void indexed_push(J *object, Map *index, const char *key,
			 const char *value)
{
	unique(indexed_ensure(object, index, key, 1), value);
}


static int stopword(const char *s)
{
	static const char *words =
		" a about above after again against all am an and any are as at be because been before "
		"between both but by can did do does doing don down during each few for from further had "
		"has have having he her here hers herself him himself his how i if in into is it its "
		"itself just me more most my myself no nor not now of off on once only or other our ours "
		"ourselves out over own s same she should so some such t than that the their theirs them "
		"themselves then there these they this those through to too under until up very was we "
		"were what when where which while who whom why will with you your yours yourself "
		"yourselves ";
	Buf b = { 0 };
	buf_put(&b, " ");
	buf_put(&b, s);
	buf_put(&b, " ");
	int yes = strstr(words, b.p) != NULL;
	free(b.p);
	return yes;
}

static J *reverse_synonyms(J *forward)
{
	J *reverse = OBJ();
	Map keys = { 0 };
	size_t entries = 0;
	EACH(k, forward)
		entries += (size_t)SIZE(k);
	map_reserve(&keys, entries);
	EACH(k, forward)
		EACH(v, k)
			indexed_push(reverse, &keys, S(v), k->string);
	map_free(&keys);
	return reverse;
}

static J *synonyms_build(J *raw, int reverse, int consume)
{
	J *f = consume ? raw : OBJ();
	Map forward = { 0 };
	if (!consume)
		map_reserve(&forward, (size_t)SIZE(raw));
	EACH(k, raw) {
		/* Extraction tables already own unique lowercase canonical keys.
		 * Retain their wrappers and transform their children in place. */
		char *key = consume ? NULL : lower(k->string);
		J *a = consume ? k : indexed_ensure(f, &forward, key, 1);
		J input_array = { 0 }, *input = k;
		if (consume) {
			input_array.type = cJSON_Array;
			input_array.child = k->child;
			k->child = NULL;
			input = &input_array;
		}
		for (J *v = input->child, *next; v; v = next) {
			next = v->next;
			char *replaced = strchr(S(v), '+') ?
						       replace(S(v), "+", " ") :
						       NULL;
			const char *r = replaced ? replaced : S(v);
			char *owned;
			const char *normal = norm_borrowed(r, &owned);
			/* A consumed source no longer needs unchanged nodes. Moving them
			 * preserves their string ownership and avoids a copy/free pair. */
			if (consume && !replaced && !owned) {
				if (!contains(a, normal))
					ADD(a, cJSON_DetachItemViaPointer(input,
									  v));
			} else
				unique(a, normal);
			free(owned);
			char *c;
			if (strchr(r, '_')) {
				char *t = replace(r, "_", " ");
				c = norm(t, 1, 1);
				unique(a, c);
				free(c);
				free(t);
			}
			size_t n = strlen(r);
			if (n && strchr("!?.", r[n - 1])) {
				char *t = slice(r, n - 1);
				unique(a, t);
				free(t);
			}
			if (strchr(r, '.') && !strstr(r, "...")) {
				char *t = replace(r, ".", " . ");
				c = norm(t, 0, 1);
				unique(a, c);
				free(c);
				free(t);
			}
			free(replaced);
		}
		DEL(input_array.child);
		sort_strings(a, 1);
		free(key);
	}
	map_free(&forward);
	if (!reverse)
		return f;
	J *r = reverse_synonyms(f);
	DEL(f);
	return r;
}

J *generate_synonyms(J *raw, int reverse)
{
	return synonyms_build(raw, reverse, 0);
}

/* Contiguous forms preserve insertion order without allocating a second set
 * of JSON nodes before the data view has proved its completed arrays equal. */
typedef struct {
	char *text;
	J *json;
	size_t length;
	int borrowed;
} LookupForm;
typedef struct {
	J *array;
	Map values;
	LookupForm *forms;
	size_t count, capacity;
} LookupBuilder;
static LookupBuilder *lookup_group(J *d, Map *seen, const char *key)
{
	LookupBuilder *group = map_get(seen, key);
	if (!group) {
		group = calloc(1, sizeof(*group));
		group->array = ensure(d, key, 1);
		map_put(seen, key, group);
	}
	return group;
}
static LookupForm *lookup_form(LookupBuilder *group, char *text, int traced,
			       int borrowed)
{
	if (group->count == group->capacity) {
		group->capacity = group->capacity ? group->capacity * 2 : 64;
		group->forms = realloc(group->forms,
				       group->capacity * sizeof(*group->forms));
	}
	LookupForm *form = &group->forms[group->count++];
	*form = (LookupForm){ text, NULL, 0, borrowed };
	if (traced) {
		form->json = cJSON_CreateStringReference(text);
		form->json->type &= ~cJSON_IsReference;
	}
	return form;
}
/* Takes ownership of text, including when the form already exists. */
static LookupForm *lookup_group_push(LookupBuilder *group, char *text,
				     int traced)
{
	Slot *slot = map_entry_ref(&group->values, text);
	size_t position = (size_t)(uintptr_t)slot->value;
	if (position) {
		free(text);
		return &group->forms[position - 1];
	}
	LookupForm *form = lookup_form(group, text, traced, 0);
	slot->value = (void *)(uintptr_t)group->count;
	return form;
}
static void lookup_append(J *d, Map *seen, const char *key, const char *value,
			  int traced, int borrow)
{
	int borrowed = borrow && !traced;
	lookup_form(lookup_group(d, seen, key),
		borrowed ? (char *)value : copy(value), traced, borrowed);
}
static void lookup_seed(Map *seen)
{
	for (size_t i = 0; i < seen->cap; i++) {
		if (!seen->slots[i].key)
			continue;
		LookupBuilder *group = seen->slots[i].value;
		map_reserve(&group->values, group->count);
		size_t count = 0;
		for (size_t j = 0; j < group->count; j++) {
			LookupForm form = group->forms[j];
			Slot *slot = map_entry_ref(&group->values, form.text);
			if (slot->value) {
				if (form.json)
					DEL(form.json);
				else if (!form.borrowed)
					free(form.text);
			} else {
				group->forms[count++] = form;
				slot->value = (void *)(uintptr_t)count;
			}
		}
		group->count = count;
	}
}
static int lookup_form_compare(const void *a, const void *b)
{
	const LookupForm *x = a, *y = b;
	return x->length < y->length ? -1 :
	       x->length > y->length ? 1 :
					     strcmp(x->text, y->text);
}
static void lookup_finish(Map *seen, J *reference, int sorted)
{
	for (size_t i = 0; i < seen->cap; i++) {
		if (!seen->slots[i].key)
			continue;
		LookupBuilder *group = seen->slots[i].value;
		for (size_t j = 0; sorted && j < group->count; j++)
			group->forms[j].length = ulen(group->forms[j].text);
		int order = sorted ? -1 : 1, ordered = 1;
		if (sorted && group->count > 1) {
			order = lookup_form_compare(&group->forms[0],
						    &group->forms[1]);
			ordered = order != 0;
		}
		for (size_t j = 2; sorted && j < group->count && ordered; j++) {
			int next = lookup_form_compare(&group->forms[j - 1],
						       &group->forms[j]);
			ordered = next != 0 && (next > 0) == (order > 0);
		}
		if (!ordered) {
			qsort(group->forms, group->count, sizeof(*group->forms),
			      lookup_form_compare);
			order = -1;
		}
		J *source = GET(reference, seen->slots[i].key);
		int identical = cJSON_IsArray(source) &&
				(size_t)SIZE(source) == group->count;
		J *entry = source ? source->child : NULL;
		for (size_t j = 0; j < group->count && identical;
		     j++, entry = entry->next) {
			LookupForm *form =
				&group->forms[order < 0 ? group->count - 1 - j :
								j];
			identical = !strcmp(form->text, S(entry));
		}
		if (identical) {
			group->array->child = source->child;
			group->array->type |= cJSON_IsReference;
		}
		for (size_t j = 0; j < group->count; j++) {
			LookupForm *form =
				&group->forms[order < 0 ? group->count - 1 - j :
								j];
			if (identical) {
				if (!form->borrowed)
					free(form->text);
			} else {
				J *value = form->json;
				if (form->borrowed)
					value = STR(form->text);
				else if (!value) {
					value = cJSON_CreateStringReference(
						form->text);
					value->type &= ~cJSON_IsReference;
				}
				ADD(group->array, value);
			}
		}
		map_free(&group->values);
		free(group->forms);
		free(group);
	}
	map_free(seen);
}
static void lookup_add(J *d, Map *seen, const char *s, int traced, int borrow)
{
	char *replaced = strchr(s, '_') ? replace(s, "_", " ") : NULL;
	const char *text = replaced ? replaced : s;
	char key[32];
	snprintf(key, sizeof(key), "%d", count(text, ' ') + 1);
	lookup_append(d, seen, key, text, traced, borrow && !replaced);
	free(replaced);
}

static int gram_stopword(const char *text, size_t length)
{
	return (length == 2 &&
		(!memcmp(text, "of", 2) || !memcmp(text, "to", 2))) ||
	       (length == 3 &&
		(!memcmp(text, "and", 3) || !memcmp(text, "for", 3)));
}
/* An (n-1)-word substring can cut the first two and last two words of an
 * n-word value. Every word between them is necessarily present in full.
 * A forbidden gram word there makes any rewrite impossible, even after edits. */
int lookup_rewritable(const char *text, int words)
{
	const char *at = text;
	for (int i = 0; i < words - 2; i++) {
		const char *end = strchr(at, ' ');
		if (!end)
			return 1;
		if (i >= 2 && gram_stopword(at, (size_t)(end - at)))
			return 0;
		at = end + 1;
	}
	return 1;
}

/* Groups contain exactly one more space-delimited word than their grams.
 * Slice the leading words directly; temporary JSON token arrays are unnecessary. */
char *lookup_leading_gram(const char *text, int words)
{
	const char *at = text, *end = NULL;
	for (int i = 0; i < words; i++) {
		end = strchr(at, ' ');
		if (!end)
			return NULL;
		size_t length = (size_t)(end - at);
		if (gram_stopword(at, length))
			return NULL;
		at = end + 1;
	}
	return end ? slice(text, (size_t)(end - text)) : NULL;
}

/* Replacing gram spaces with underscores preserves byte length. Match against
 * the original text and advance past each occurrence, as replace() does. */
char *lookup_rewrite(const char *text, const char *gram)
{
	char *out = copy(text);
	size_t length = strlen(gram);
	if (!length)
		return out;
	const char *at = text, *found;
	while ((found = strstr(at, gram)) != NULL) {
		char *target = out + (found - text);
		for (size_t i = 0; i < length; i++)
			if (gram[i] == ' ')
				target[i] = '_';
		at = found + length;
	}
	return out;
}

static J *lookup_build(J *f, LookupTrace **trace_out, J *reference)
{
	LookupTrace *trace = trace_out ? lookup_trace_create() : NULL;
	if (trace_out)
		*trace_out = trace;
	J *d = OBJ();
	Map seen = { 0 };
	EACH(k, f) {
		lookup_add(d, &seen, k->string, trace != NULL, 1);
		EACH(v, k) {
			char *owned;
			const char *s = norm_borrowed(S(v), &owned);
			lookup_add(d, &seen, s, trace != NULL, owned == NULL);
			size_t n = strlen(s);
			if (n && strchr("!?.", s[n - 1])) {
				char key[32];
				snprintf(key, sizeof(key), "%d",
					 count(s, ' ') + 1);
				char *trimmed = slice(s, n - 1);
				lookup_append(d, &seen, key, trimmed, trace != NULL, 0);
				free(trimmed);
			}
			free(owned);
		}
	}
	lookup_seed(&seen);
	for (int n = 1; n <= 6; n++) {
		char k[8];
		snprintf(k, sizeof(k), "%d", n);
		ensure(d, k, 1);
	}
	for (int n = 5; n >= 2; n--) {
		char k[8];
		snprintf(k, sizeof(k), "%d", n + 1);
		LookupBuilder *values = map_get(&seen, k);
		if (!values || !values->count)
			continue;
		J *grams = ARR();
		size_t source_count = values->count;
		Map gram_set = { 0 };
		map_reserve(&gram_set, source_count);
		for (size_t j = 0; j < source_count; j++) {
			char *gram =
				lookup_leading_gram(values->forms[j].text, n);
			if (gram) {
				unique_indexed(grams, &gram_set, gram);
				free(gram);
			}
		}
		map_free(&gram_set);
		Patterns *patterns = patterns_build(grams);
		if (trace)
			lookup_trace_reserve(trace, source_count);
		for (size_t j = 0; j < source_count; j++) {
			LookupForm *v = &values->forms[j];
			int matches = patterns_find(patterns, v->text);
			for (int i = 0; i < matches; i++) {
				const char *g =
					patterns->text[patterns->hits[i]];
				char *s = lookup_rewrite(v->text, g);
				int size = count(s, ' ') + 1;
				if (size >= 2 && size <= 5) {
					snprintf(k, sizeof(k), "%d", size);
					LookupBuilder *group =
						lookup_group(d, &seen, k);
					if (!group->values.cap)
						map_reserve(&group->values,
							    source_count);
					LookupForm *entry = lookup_group_push(
						group, s, trace != NULL);
					if (trace)
						lookup_trace_add(trace, v->json,
								 entry->json);
					s = NULL;
				}
				free(s);
			}
		}
		patterns_free(patterns);
		DEL(grams);
	}
	lookup_finish(&seen, reference, 1);
	return d;
}
J *generate_lookup_traced(J *f, LookupTrace **trace_out)
{
	return lookup_build(f, trace_out, NULL);
}
J *generate_lookup(J *f)
{
	return lookup_build(f, NULL, NULL);
}
/* Recompute every form and its order; only final identical arrays share storage. */
J *generate_lookup_shared(J *f, J *reference)
{
	return lookup_build(f, NULL, reference);
}

/* Identical ordered forward inputs prove the deterministic lookup is identical.
 * Compare keys and array order explicitly; object equality ignores key order. */
static int lookup_inputs_equal(J *a, J *b)
{
	if (!cJSON_IsObject(a) || !cJSON_IsObject(b))
		return 0;
	J *left = a->child, *right = b->child;
	for (; left && right; left = left->next, right = right->next) {
		if (!left->string || !right->string ||
		    strcmp(left->string, right->string) ||
		    !cJSON_IsArray(left) || !cJSON_IsArray(right))
			return 0;
		if (left->child == right->child)
			continue;
		J *x = left->child, *y = right->child;
		for (; x && y; x = x->next, y = y->next)
			if (!cJSON_IsString(x) || !cJSON_IsString(y) ||
			    strcmp(S(x), S(y)))
				return 0;
		if (x || y)
			return 0;
	}
	return !left && !right;
}
/* previous_lookup is the completed result for previous_fwd and must outlive
 * the returned view, as with generate_lookup_shared(). */
J *generate_lookup_cached(J *fwd, J *previous_fwd, J *previous_lookup)
{
	if (!cJSON_IsObject(previous_lookup) ||
	    !lookup_inputs_equal(fwd, previous_fwd))
		return generate_lookup_shared(fwd, previous_lookup);
	J *out = OBJ();
	EACH(group, previous_lookup)
		PUT(out, group->string,
		    cJSON_IsArray(group) ?
				  cJSON_CreateArrayReference(group->child) :
				  DUP(group));
	return out;
}

static J *rule(const char *canon, J *content, int distance)
{
	J *r = OBJ();
	PUT(r, "content", content);
	PUT(r, "distance", NUM(distance));
	PUT(r, "forward", BOOL(1));
	PUT(r, "reverse", BOOL(1));
	PUT(r, "canon", STR(canon));
	return r;
}

static void add_rule(J *d, const char *key, J *r, Map *seen, Map *keys,
		     int include_key)
{
	char *json = cJSON_PrintUnformatted(r);
	Buf b = { 0 };
	if (include_key)
		buf_put(&b, key);
	buf_put(&b, json);
	free(json);
	if (!map_get(seen, b.p)) {
		map_put(seen, b.p, (void *)1);
		ADD(indexed_ensure(d, keys, key, 1), r);
	} else
		DEL(r);
	free(b.p);
}

static void ordinary_rule(J *d, const char *canon, const char *term,
			  int distance, Map *seen, Map *keys)
{
	J *ts = split(term, "_"), *content = ARR();
	for (int i = 1; i < SIZE(ts); i++) {
		char *c = norm(S(AT(ts, i)), 0, 1);
		char *l = lower(c);
		if (!stopword(l))
			unique(content, c);
		free(l);
		free(c);
	}
	sort_strings(content, 0);
	if (SIZE(content))
		add_rule(d, S(AT(ts, 0)), rule(canon, content, distance), seen,
			 keys, 0);
	else
		DEL(content);
	DEL(ts);
}

/* Commas delimit opt-in lists, except between numeric code points (#15). */
static J *literal_parts(const char *text, int comma_lists)
{
	J *parts = ARR();
	const char *start = text, *p = text;
	uint32_t prev = 0;
	for (;;) {
		const char *at = p;
		uint32_t c = *p ? uread(&p) : 0;
		const char *next = p;
		if (!c ||
		    (comma_lists && c == ',' &&
		     !(unumeric(prev) && unumeric(*next ? uread(&next) : 0)))) {
			char *piece = slice(start, (size_t)(at - start));
			char *clean = norm(piece, 0, 0);
			if (*clean)
				ADD(parts, STR(clean));
			free(clean);
			free(piece);
			start = p;
		}
		if (!c)
			return parts;
		prev = c;
	}
}

static J *spans_build(J *raw, int distance, int plus_only, int comma_lists,
		      Map *external_keys)
{
	/* Both the plus and ordinary passes consume the same list items. */
	if (comma_lists) {
		J *expanded = OBJ();
		EACH(k, raw) {
			J *values = ARR();
			EACH(v, k) {
				J *parts = literal_parts(S(v), 1);
				EACH(part, parts)
					unique(values, S(part));
				DEL(parts);
			}
			PUT(expanded, k->string, values);
		}
		J *out = spans_build(expanded, distance, plus_only, 0,
				     external_keys);
		DEL(expanded);
		return out;
	}
	J *d = OBJ();
	Map seen = { 0 }, raw_index = { 0 }, keys = { 0 };
	/* A bigram key always contains an underscore. Without such a canonical
	 * key there can be no implicit plus rule, so retain the original values. */
	int compounds = 0;
	if (external_keys) {
		/* Edits probe the retained index using their few changed values. */
		compounds = external_keys->size != 0;
	} else {
		size_t count = 0;
		EACH(k, raw)
			count += k->string && strchr(k->string, '_') != NULL;
		map_reserve(&raw_index, count);
		EACH(k, raw)
			if (k->string && strchr(k->string, '_') &&
			    !map_get(&raw_index, k->string))
				map_put_ref(&raw_index, k->string, k);
		compounds = count != 0;
	}
	EACH(k, raw) {
		J *syns = compounds ? DUP(k) : k;
		if (compounds)
			EACH(v, k) {
				J *ts = split(S(v), " ");
				char *working = copy(S(v));
				for (int i = 0; i + 1 < SIZE(ts); i++) {
					Buf b = { 0 };
					buf_put(&b, S(AT(ts, i)));
					buf_put(&b, "_");
					buf_put(&b, S(AT(ts, i + 1)));
					if (map_get(external_keys ?
								  external_keys :
								  &raw_index,
						    b.p)) {
						char *phrase =
							replace(b.p, "_", " ");
						buf_put(&b, " +");
						char *r = replace(working,
								  phrase, b.p);
						free(working);
						working = r;
						free(phrase);
						if (working[strlen(working) -
							    1] != '+')
							unique(syns, working);
					}
					free(b.p);
				}
				free(working);
				DEL(ts);
			}
		EACH(v, syns) {
			if (!strchr(S(v), '+'))
				continue;
			J *ts = split(S(v), "+"), *content = ARR();
			for (int i = 1; i < SIZE(ts); i++) {
				char *c = norm(S(AT(ts, i)), 0, 1);
				unique(content, c);
				free(c);
			}
			sort_strings(content, 0);
			if (SIZE(content))
				add_rule(d, S(AT(ts, 0)),
					 rule(k->string, content, distance),
					 &seen, &keys, 1);
			else
				DEL(content);
			DEL(ts);
		}
		if (compounds)
			DEL(syns);
	}
	map_free(&seen);
	map_free(&raw_index);
	if (plus_only) {
		map_free(&keys);
		return d;
	}
	EACH(k, raw) {
		if (!strchr(k->string, ' ') && !strchr(k->string, '_'))
			continue;
		ordinary_rule(d, k->string, k->string, distance, &seen, &keys);
		J *values = ARR();
		EACH(v, k) {
			char *s = norm(S(v), 0, 0);
			unique(values, s);
			free(s);
		}
		sort_strings(values, 0);
		EACH(v, values) {
			if (!strchr(S(v), ' ') && !strchr(S(v), '_'))
				continue;
			char *l = lower(S(v)), *s = replace(l, " ", "_");
			if (strcmp(s, k->string))
				ordinary_rule(d, k->string, s, distance, &seen,
					      &keys);
			free(l);
			free(s);
		}
		DEL(values);
	}
	map_free(&seen);
	map_free(&keys);
	J *trimmed = OBJ();
	Map trimmed_index = { 0 };
	EACH(k, d) {
		char *s = norm(k->string, 0, 0);
		indexed_set(trimmed, &trimmed_index, s, DUP(k));
		free(s);
	}
	map_free(&trimmed_index);
	DEL(d);
	return trimmed;
}

J *generate_spans(J *raw, int distance, int plus_only, int comma_lists)
{
	return spans_build(raw, distance, plus_only, comma_lists, NULL);
}
J *generate_spans_indexed(J *raw, int distance, int plus_only, int comma_lists,
			  Map *keys)
{
	return spans_build(raw, distance, plus_only, comma_lists, keys);
}

static J *trie_build(J *a)
{
	J *groups = OBJ(), *result = OBJ();
	Map keys = { 0 }, seen = { 0 };
	EACH(v, a) {
		const char *s = S(v), *p = strchr(s, ' ');
		char *k = p ? slice(s, (size_t)(p - s)) : copy(s);
		J *group = indexed_ensure(groups, &keys, k, 1);
		if (!map_get(&seen, s)) {
			map_put(&seen, s, (void *)1);
			ADD(group, STR(p ? p + 1 : ""));
		}
		free(k);
	}
	map_free(&keys);
	map_free(&seen);
	EACH(g, groups) {
		if (strchr(S(g->child), ' '))
			PUT(result, g->string, trie_build(g));
		else {
			J *x = DUP(g);
			sort_strings(x, -1);
			PUT(result, g->string, x);
		}
	}
	DEL(groups);
	return result;
}

J *generate_trie(J *entities)
{
	if (!SIZE(entities))
		return NIL();
	J *grams = OBJ(), *r = OBJ();
	Map seen = { 0 };
	EACH(v, entities) {
		char *s = replace(S(v), "_", " ");
		char k[16];
		snprintf(k, sizeof(k), "%d", count(s, ' ') + 1);
		lookup_group_push(lookup_group(grams, &seen, k), s, 0);
	}
	lookup_finish(&seen, NULL, 0);
	for (int i = 1; i <= 6; i++) {
		char k[16];
		snprintf(k, sizeof(k), "%d", i);
		J *a = ensure(grams, k, 1);
		if (i == 1) {
			J *x = DUP(a);
			sort_strings(x, -1);
			PUT(r, k, x);
		} else
			PUT(r, k, trie_build(a));
	}
	EACH(k, grams) {
		if (atoi(k->string) > 6)
			PUT(r, k->string, DUP(k));
	}
	DEL(grams);
	return r;
}

static char *predicate_key(const char *p)
{
	const char *prefix = "";
	if (!strncmp(p, RDFS, strlen(RDFS)))
		prefix = "rdfs";
	else if (!strncmp(p, SKOS, strlen(SKOS)))
		prefix = "skos";
	else if (!strncmp(p, OWL, strlen(OWL)))
		prefix = "owl";
	else if (!strncmp(p, RDF, strlen(RDF)))
		prefix = "rdf";
	Buf b = { 0 };
	buf_put(&b, prefix);
	buf_put(&b, ":");
	buf_put(&b, local(p));
	return buf_take(&b);
}

J *ontology_build(Graph *g, int distance, int force_class, int comma_lists,
		  mc_error *e)
{
	J *r = OBJ(), *raw = OBJ(), *synraw = comma_lists ? OBJ() : raw,
	  *by = OBJ(), *labels = OBJ(), *ner = OBJ(), *parents = OBJ(),
	  *children = OBJ(), *equivs = OBJ(), *grams = OBJ(),
	  *subentities = ARR(), *preds = ARR();
	Map label_index = { 0 }, ner_index = { 0 }, raw_index = { 0 },
	    synraw_index = { 0 }, parent_index = { 0 }, child_index = { 0 },
	    equiv_index = { 0 }, predicate_indexes = { 0 },
	    subentity_index = { 0 };
	/* Bound growing indexes from the graph before allocating their entries. */
	size_t label_rows = 0, vocabulary_rows = 0;
	for (size_t i = 0; i < g->n; i++) {
		const char *p = g->ts[i].p.value;
		label_rows += !strcmp(p, RDFS "label");
		vocabulary_rows += !strcmp(p, RDFS "label") ||
				   !strcmp(p, RDFS "seeAlso") ||
				   !strcmp(p, SKOS "altLabel") ||
				   !strcmp(local(p), "inflection");
	}
	map_reserve(&label_index, label_rows);
	map_reserve(&ner_index, label_rows);
	size_t vocabulary_subjects = vocabulary_rows < g->subjects.size ?
						   vocabulary_rows :
						   g->subjects.size;
	map_reserve(&raw_index, vocabulary_subjects);
	if (comma_lists)
		map_reserve(&synraw_index, vocabulary_subjects);
	int individuals = 0, subclass = 0, skos = 0, span_candidates = 0;
	for (size_t i = 0; i < g->n; i++) {
		Triple *t = &g->ts[i];
		if (!strcmp(t->p.value, RDF "type")) {
			individuals |=
				!strcmp(t->o.value, OWL "NamedIndividual");
			skos |= !strcmp(t->o.value, SKOS "Concept");
		}
		subclass |= !strcmp(t->p.value, RDFS "subClassOf");
		if (!strcmp(t->p.value, RDFS "label")) {
			char *owned_key, *owned_value;
			const char *k = norm_view(local(t->s.value), 0, 0,
						  &owned_key),
				   *v = norm_view(t->o.value, 0, 0,
						  &owned_value);
			indexed_set(labels, &label_index, k, STR(v));
			indexed_set(ner, &ner_index, v, STR("NER"));
			free(owned_key);
			free(owned_value);
		}
	}
	int mixed = individuals && subclass && !skos && !force_class;
	Map predicate_names = { 0 };
	const char *previous_subject = NULL, *key = NULL;
	char *owned_key = NULL;
	for (size_t i = 0; i < g->n && !e->code; i++) {
		Triple *t = &g->ts[i];
		if (!previous_subject || strcmp(previous_subject, t->s.value)) {
			free(owned_key);
			key = norm_view(local(t->s.value), 1, 0, &owned_key);
			previous_subject = t->s.value;
		}
		char *pk = map_get(&predicate_names, t->p.value);
		if (!pk) {
			pk = predicate_key(t->p.value);
			map_put_ref(&predicate_names, t->p.value, pk);
		}
		/* Ordinary terms flatten to one value. Borrow stack wrappers while
		 * these builders copy the value into their retained JSON views. */
		J value_array = { 0 }, value_item = { 0 };
		char *owned_value = NULL;
		J *values;
		if (t->o.kind == 2)
			values = rdf_flatten(g, &t->o, 1, e);
		else {
			value_item.type = cJSON_String;
			value_item.valuestring = (char *)norm_view(
				t->o.kind == 0 ? local(t->o.value) : t->o.value,
				1, 0, &owned_value);
			value_array.type = cJSON_Array;
			value_array.child = &value_item;
			values = &value_array;
		}
		if (strcmp(pk, "rdfs:comment")) {
			J *pd = ensure(by, pk, 0);
			Map *subject_index = map_get(&predicate_indexes, pk);
			if (!subject_index) {
				subject_index =
					calloc(1, sizeof(*subject_index));
				map_put(&predicate_indexes, pk, subject_index);
			}
			J subject_array = { 0 }, subject_item = { 0 };
			J *subjects;
			if (t->s.kind != 0)
				subjects = rdf_flatten(g, &t->s, 1, e);
			else {
				subject_item.type = cJSON_String;
				subject_item.valuestring = (char *)key;
				subject_array.type = cJSON_Array;
				subject_array.child = &subject_item;
				subjects = &subject_array;
			}
			EACH(subject, subjects) {
				const char *subject_key = S(subject);
				if (strcmp(subject_key, "class") &&
				    strcmp(subject_key, "nil")) {
					J *a = indexed_ensure(pd, subject_index,
							      subject_key, 1);
					EACH(v, values)
						if (strcmp(S(v), subject_key) &&
						    strcmp(S(v), "nil") &&
						    *S(v))
							unique(a, S(v));
				}
			}
			if (t->s.kind != 0)
				DEL(subjects);
			if (pk[0] == ':')
				unique(preds, pk);
		}
		int synonym = !strcmp(t->p.value, RDFS "label") ||
			      !strcmp(t->p.value, RDFS "seeAlso") ||
			      !strcmp(t->p.value, SKOS "altLabel") ||
			      !strcmp(local(t->p.value), "inflection");
		if (synonym) {
			EACH(v, values) {
				if (!*S(v) || !strcmp(S(v), "nil"))
					continue;
				indexed_push(raw, &raw_index, key, S(v));
				span_candidates |= strchr(key, ' ') != NULL ||
						   strchr(key, '_') != NULL ||
						   strchr(S(v), '+') != NULL;
				/* Without list expansion, flattening has already trimmed
				 * these values; the synonym source is exactly raw. */
				if (!comma_lists)
					continue;
				J *parts = literal_parts(S(v), comma_lists);
				EACH(p, parts) {
					char *s = norm(S(p), 0, 0);
					if (*s)
						indexed_push(synraw,
							     &synraw_index, key,
							     s);
					free(s);
				}
				DEL(parts);
			}
		}
		if (!strcmp(t->p.value, RDFS "subClassOf")) {
			unique_indexed(subentities, &subentity_index, key);
			const char *child = local(t->s.value);
			J *orig = rdf_flatten(g, &t->o, 0, e);
			EACH(v, orig) {
				if (!strcmp(S(v), "nil"))
					continue;
				if (map_get(&label_index, child) &&
				    (!mixed || t->o.kind == 0))
					indexed_push(parents, &parent_index,
						     child, S(v));
				if (map_get(&label_index, S(v)))
					indexed_push(children, &child_index,
						     S(v), child);
			}
			DEL(orig);
		}
		if (mixed && !strcmp(t->p.value, RDF "type") &&
		    t->o.kind == 0) {
			const char *child = local(t->s.value),
				   *parent = local(t->o.value);
			if (strcmp(parent, "NamedIndividual") &&
			    strcmp(parent, "Thing") &&
			    strcmp(parent, "Class") &&
			    strcmp(parent, "Ontology") &&
			    strcmp(parent, "ObjectProperty") &&
			    strcmp(parent, "DatatypeProperty") &&
			    strcmp(parent, "AnnotationProperty")) {
				if (map_get(&label_index, child))
					indexed_push(parents, &parent_index,
						     child, parent);
				J *types =
					rdf_values(g, t->s.value, RDF "type");
				int named = 0;
				EACH(v, types)
					named |= !strcmp(
						g->ts[(size_t)v->valuedouble]
							.o.value,
						OWL "NamedIndividual");
				if (named && map_get(&label_index, parent))
					indexed_push(children, &child_index,
						     parent, child);
				DEL(types);
			}
		}
		if (!strcmp(t->p.value, OWL "equivalentClass") &&
		    t->o.kind == 0 &&
		    map_get(&label_index, local(t->s.value)) &&
		    map_get(&label_index, local(t->o.value))) {
			char *v = lower(local(t->o.value));
			indexed_push(equivs, &equiv_index, key, v);
			indexed_push(equivs, &equiv_index, v, key);
			free(v);
		}
		if (t->o.kind == 2)
			DEL(values);
		else
			free(owned_value);
	}
	free(owned_key);
	for (size_t i = 0; i < predicate_names.cap; i++)
		free(predicate_names.slots[i].value);
	map_free(&predicate_names);
	map_free(&label_index);
	map_free(&ner_index);
	map_free(&raw_index);
	map_free(&synraw_index);
	map_free(&parent_index);
	map_free(&child_index);
	map_free(&equiv_index);
	map_free(&subentity_index);
	for (size_t i = 0; i < predicate_indexes.cap; i++) {
		Map *index = predicate_indexes.slots[i].value;
		if (index) {
			map_free(index);
			free(index);
		}
	}
	map_free(&predicate_indexes);
	EACH(p, by) {
		EACH(a, p)
			sort_strings(a, 0);
	}
	J *ngentities = ARR();
	if (mixed) {
		Map seen = { 0 };
		EACH(k, labels) {
			char *s = lower(k->string);
			unique_indexed(ngentities, &seen, s);
			free(s);
		}
		map_free(&seen);
	} else {
		DEL(ngentities);
		ngentities = DUP(subentities);
	}
	for (int i = 1; i <= 9; i++) {
		char k[8];
		snprintf(k, sizeof(k), "%d", i);
		J *a = ARR();
		EACH(v, ngentities)
			if (count(S(v), '_') == i - 1)
				ADD(a, STR(S(v)));
		if (!SIZE(a) && !mixed && !SIZE(ngentities)) {
			DEL(a);
			a = NIL();
		}
		PUT(grams, k, a);
	}
	DEL(ngentities);
	J *syns = OBJ(),
	  *fwd = synonyms_build(synraw, 0, synraw != raw || !span_candidates);
	lookup_trace_free(g->lookup_trace);
	g->lookup_trace = NULL;
	PUT(syns, "lookup", generate_lookup_traced(fwd, &g->lookup_trace));
	PUT(syns, "fwd", fwd);
	PUT(syns, "rev", reverse_synonyms(fwd));
	PUT(r, "children", children);
	PUT(r, "parents", parents);
	PUT(r, "trie", generate_trie(subentities));
	PUT(r, "ngrams", grams);
	/* Ordinary rules need a compound key; plus rules need such a key or
	 * an explicit plus literal. Their absence is known while collecting raw. */
	PUT(r, "spans",
	    span_candidates ? generate_spans(raw, distance, 0, comma_lists) :
				    OBJ());
	PUT(r, "labels", labels);
	if (!SIZE(equivs)) {
		DEL(equivs);
		equivs = NIL();
	}
	PUT(r, "equivalents", equivs);
	PUT(r, "predicates", preds);
	PUT(r, "by_predicate", by);
	PUT(r, "ner", ner);
	PUT(r, "synonyms", syns);
	if (synraw != raw && synraw != fwd)
		DEL(synraw);
	if (raw != fwd)
		DEL(raw);
	DEL(subentities);
	if (e->code) {
		DEL(r);
		return NULL;
	}
	return r;
}

static void traverse(J *edges, const char *node, J *out, Map *active, int depth,
		     mc_error *e)
{
	if (depth > 1024) {
		fail(e, 4, "Hierarchy exceeds 1024 levels");
		return;
	}
	if (map_get(active, node)) {
		fail(e, 4, "Cycle in ontology hierarchy at %s", node);
		return;
	}
	map_put(active, node, (void *)1);
	EACH(v, GET(edges, node)) {
		ADD(out, DUP(v));
		traverse(edges, S(v), out, active, depth + 1, e);
		if (e->code)
			break;
	}
	map_put(active, node, NULL);
}

static J *canonical(J *d, const char *s)
{
	J *f = GET(GET(d, "synonyms"), "fwd"),
	  *rev = GET(GET(d, "synonyms"), "rev");
	if (GET(f, s))
		return STR(s);
	J *v = GET(rev, s);
	if (v)
		return STR(cJSON_IsArray(v) ? S(v->child) : S(v));
	char *r = replace(s, "_", " ");
	v = GET(rev, r);
	free(r);
	if (v)
		return STR(cJSON_IsArray(v) ? S(v->child) : S(v));
	if (strchr(s, ' ') || strchr(s, '\'')) {
		char *a = replace(s, " ", "_"), *tmp = replace(a, "'", ""),
		     *b = norm(tmp, 1, 0);
		free(tmp);
		free(a);
		J *out = strcmp(s, b) ? canonical(d, b) : NIL();
		free(b);
		return out;
	}
	return NIL();
}

J *ontology_query(J *d, const char *method, const J *args, mc_error *e)
{
	const char *s = S(AT(args, 0));
	if (strstr(method, "_by_entity")) {
		const char *base =
			!strcmp(method, "requires_by_entity") ? "requires" :
			!strcmp(method, "required_by_entity") ? "required_by" :
			!strcmp(method, "similar_by_entity")  ? "similar" :
			!strcmp(method, "implies_by_entity")  ? "implies" :
			!strcmp(method, "implied_by_entity")  ? "implied_by" :
								NULL;
		if (base) {
			char *a = norm(s, 1, 0), *name = replace(a, " ", "_");
			free(a);
			J *view = ontology_query(d, base, NULL, e),
			  *v = GET(view, name);
			J *result =
				v ? DUP(v) :
				    (!strcmp(base, "similar") ? ARR() : NIL());
			if (!strcmp(base, "similar") && v)
				EACH(x, v)
					ADD(result, DUP(x));
			DEL(view);
			free(name);
			return result;
		}
	}
	if (!strcmp(method, "types_rev"))
		return ontology_query(d, "types", args, e);

	J *v = NULL;
	const char *key = method;
	if (!strcmp(method, "synonyms"))
		return DUP(GET(GET(d, "synonyms"), "fwd"));
	if (!strcmp(method, "synonyms_rev"))
		return DUP(GET(GET(d, "synonyms"), "rev"));
	if (!strcmp(method, "lookup") || !strcmp(method, "synonyms_lookup"))
		return DUP(GET(GET(d, "synonyms"), "lookup"));
	if (!strcmp(method, "find_canon"))
		return canonical(d, s);
	if (!strcmp(method, "is_canon"))
		return BOOL(GET(GET(GET(d, "synonyms"), "fwd"), s) != NULL);
	if (!strcmp(method, "is_variant"))
		return BOOL(GET(GET(GET(d, "synonyms"), "rev"), s) != NULL);
	if (!strcmp(method, "find_variants"))
		v = GET(GET(GET(d, "synonyms"), "fwd"), s);
	else if (!strcmp(method, "find_ner"))
		v = GET(GET(d, "ner"), s);
	else if (!strcmp(method, "label_by_entity")) {
		char *a = norm(s, 1, 0), *b = replace(a, " ", "_");
		v = GET(GET(d, "labels"), b);
		free(a);
		free(b);
	} else if (!strcmp(method, "by_predicate") ||
		   !strcmp(method, "by_predicate_rev")) {
		v = GET(GET(d, "by_predicate"), s);
		if (!strcmp(method, "by_predicate_rev")) {
			J *out = OBJ();
			EACH(k, v)
				if (SIZE(k))
					PUT(out, k->string, DUP(k));
			return out;
		}
		return v ? DUP(v) : ARR();
	} else if (!strcmp(method, "children") || !strcmp(method, "parents")) {
		v = GET(GET(d, method), s);
		return v ? DUP(v) : ARR();
	} else if (!strcmp(method, "ancestors") ||
		   !strcmp(method, "descendants") ||
		   strstr(method, "_and_self") ||
		   !strcmp(method, "has_ancestor") ||
		   !strcmp(method, "has_parent")) {
		int ancestor = !strncmp(method, "ancestor", 8) ||
			       !strncmp(method, "parent", 6) ||
			       strstr(method, "ancestor") ||
			       !strcmp(method, "has_parent");
		int direct = !strncmp(method, "children", 8) ||
			     !strncmp(method, "parents", 7) ||
			     !strcmp(method, "has_parent");
		J *out = ARR(),
		  *edges = GET(d, ancestor ? "parents" : "children");
		if (strstr(method, "_and_self"))
			ADD(out, STR(s));
		if (direct) {
			EACH(x, GET(edges, s))
				ADD(out, DUP(x));
		} else {
			Map active = { 0 };
			traverse(edges, s, out, &active, 0, e);
			map_free(&active);
		}
		if (!strncmp(method, "has_", 4)) {
			int yes = contains(out, S(AT(args, 1)));
			DEL(out);
			return BOOL(yes);
		}
		return out;
	} else if (!strcmp(method, "has_spans") ||
		   !strcmp(method, "has_data")) {
		J *value = !strcmp(method, "has_spans") ?
				   GET(d, "spans") :
				   GET(GET(d, "synonyms"), "fwd");
		return SIZE(value) ? NUM(SIZE(value)) :
		       value	   ? DUP(value) :
				     NIL();
	} else if (!strcmp(method, "entity_exists"))
		return BOOL(contains(GET(d, "entities"), s));
	else if (!strcmp(method, "span_keys")) {
		J *out = ARR();
		EACH(x, GET(d, "spans"))
			ADD(out, STR(x->string));
		sort_strings(out, 1);
		if (!SIZE(out)) {
			DEL(out);
			return NIL();
		}
		return out;
	} else if (!strcmp(method,
			   "ngrams")) { /* AskJsonAPI reads this location in the reference. */
		v = GET(GET(d, "synonyms"), s);
		return v ? DUP(v) : ARR();
	} else if (!strcmp(method, "types")) {
		v = GET(GET(d, "by_predicate"), "rdfs:subClassOf");
		return v ? DUP(v) : ARR();
	} else if (!strcmp(method, "labels_rev")) {
		J *out = OBJ();
		EACH(k, GET(d, "labels")) {
			const char *p = S(k);
			while (*p) {
				const char *start = p;
				uread(&p);
				char *c = slice(start, (size_t)(p - start));
				set(out, c, STR(k->string));
				free(c);
			}
		}
		return out;
	} else {
		if (!strcmp(method, "effects") ||
		    !strcmp(method, "effects_rev"))
			key = "effects";
		else if (!strcmp(method, "requires") ||
			 !strcmp(method, "required_by"))
			key = "requires";
		else if (!strcmp(method, "similar") ||
			 !strcmp(method, "similar_rev"))
			key = "similarTo";
		else if (!strcmp(method, "implies") ||
			 !strcmp(method, "implied_by"))
			key = "implies";
		else if (!strcmp(method, "uses") || !strcmp(method, "uses_rev"))
			key = "uses";
		if (strcmp(key, method) || !strcmp(method, "effects") ||
		    !strcmp(method, "requires") || !strcmp(method, "implies") ||
		    !strcmp(method, "uses")) {
			v = GET(GET(d, "by_predicate"), key);
			if (strstr(method, "_rev") ||
			    !strcmp(method, "required_by") ||
			    !strcmp(method, "implied_by")) {
				J *out = OBJ();
				EACH(k, v)
					if (SIZE(k))
						PUT(out, k->string, DUP(k));
				return out;
			}
			return v ? DUP(v) : ARR();
		}
		v = GET(d, method);
		if (!v) {
			fail(e, 4, "Unsupported ontology method: %s", method);
			return NULL;
		}
	}
	return v ? DUP(v) : NIL();
}
