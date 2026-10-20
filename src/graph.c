/*
 * graph.c - Shared RDF terms, graph storage, and query indexes.
 *
 * Native readers add the same triples here without serialization conversion.
 */

#include "mc.h"
#include "rdf_namespaces.h"
#include <assert.h>
#include <limits.h>

/* Subject lists are append-only until the graph is discarded. Keep their
 * array/number wrappers together; callers read them but never delete a node. */
struct GraphIds {
	struct GraphIds *next;
	size_t used;
	J nodes[256];
};
static J *graph_id_node(Graph *g, int type)
{
	if (!g->subject_ids || g->subject_ids->used == 256) {
		GraphIds *block = calloc(1, sizeof(*block));
		block->next = g->subject_ids;
		g->subject_ids = block;
	}
	J *node = &g->subject_ids->nodes[g->subject_ids->used++];
	node->type = type;
	return node;
}

/* Original graph strings have one lifetime. Replacements own their new
 * strings separately, so repeated edits never append to this initial pool. */
struct GraphStrings {
	struct GraphStrings *next;
	size_t used, capacity;
	char bytes[];
};
static char *graph_string(Graph *g, const char *text)
{
	size_t length = strlen(text) + 1;
	if (!g->strings || g->strings->capacity - g->strings->used < length) {
		size_t capacity = g->strings ? 65536 : 4096;
		if (capacity < length)
			capacity = length;
		GraphStrings *block = malloc(sizeof(*block) + capacity);
		block->next = g->strings;
		block->used = 0;
		block->capacity = capacity;
		g->strings = block;
	}
	char *value = g->strings->bytes + g->strings->used;
	memcpy(value, text, length);
	g->strings->used += length;
	return value;
}

Term rdf_term(const char *s, int kind)
{
	Term t = { copy(s), NULL, NULL, kind, 0 };
	return t;
}

void rdf_term_free(Term *t)
{
	if (!t->shared_value)
		free(t->value);
	free(t->datatype);
	free(t->language);
	memset(t, 0, sizeof(*t));
}

Term rdf_term_copy(const Term *t)
{
	Term v = { copy(t->value), t->datatype ? copy(t->datatype) : NULL,
		   t->language ? copy(t->language) : NULL, t->kind, 0 };
	return v;
}

/* Resource terms recur across triples. Keep one graph-owned string while
 * independent term copies and mutable literal values retain their ownership. */
static Term graph_term(Graph *g, const Term *source)
{
	char *value;
	if (source->kind == 1)
		value = graph_string(g, source->value);
	else {
		value = map_get(&g->resources, source->value);
		if (!value) {
			value = graph_string(g, source->value);
			map_put_ref(&g->resources, value, value);
		}
	}
	Term term = { value, source->datatype ? copy(source->datatype) : NULL,
		      source->language ? copy(source->language) : NULL,
		      source->kind, 1 };
	return term;
}

/* Hashes select a chain of stable triple IDs; full terms decide equality.
 * This avoids retaining a second serialized copy of every RDF statement. */
static uint64_t triple_hash(const Term *s, const Term *p, const Term *o)
{
	const uint64_t mix = UINT64_C(0xc6a4a7935bd1e995);
	const Term *terms[] = { s, p, o };
	uint64_t hash = 129;
	for (size_t i = 0; i < 3; i++) {
		hash = (hash ^ (unsigned)terms[i]->kind) * mix;
		const char *parts[] = { terms[i]->value, terms[i]->datatype,
					terms[i]->language };
		for (size_t j = 0; j < 3; j++)
			hash = (hash ^
				(parts[j] && *parts[j] ? hash_text(parts[j]) :
							       0)) *
			       mix;
	}
	return hash ^ (hash >> 47);
}
static int term_equal(const Term *a, const Term *b)
{
	return a->kind == b->kind && !strcmp(a->value, b->value) &&
	       !strcmp(a->datatype ? a->datatype : "",
		       b->datatype ? b->datatype : "") &&
	       !strcmp(a->language ? a->language : "",
		       b->language ? b->language : "");
}
static void triple_grow(Graph *g)
{
	g->cap = g->cap ? g->cap * 2 : 256;
	g->ts = realloc(g->ts, g->cap * sizeof(*g->ts));
	g->triple_links =
		realloc(g->triple_links, g->cap * sizeof(*g->triple_links));
	g->triple_capacity = g->cap;
	free(g->triple_heads);
	g->triple_heads = calloc(g->triple_capacity, sizeof(*g->triple_heads));
	for (size_t i = 0; i < g->n; i++) {
		size_t bucket = (size_t)g->triple_links[i].hash &
				(g->triple_capacity - 1);
		g->triple_links[i].next = g->triple_heads[bucket];
		g->triple_heads[bucket] = i + 1;
	}
}
void rdf_add(Graph *g, const Term *s, const Term *p, const Term *o)
{
	uint64_t hash = triple_hash(s, p, o);
	if (g->triple_capacity) {
		size_t bucket = (size_t)hash & (g->triple_capacity - 1);
		for (size_t id = g->triple_heads[bucket]; id;
		     id = g->triple_links[id - 1].next) {
			if (g->triple_links[id - 1].hash != hash)
				continue;
			Triple *t = &g->ts[id - 1];
			if (term_equal(&t->s, s) && term_equal(&t->p, p) &&
			    term_equal(&t->o, o))
				return;
		}
	}
	/* Copy before reallocating: callers may supply terms from this graph. */
	Triple triple = { graph_term(g, s), graph_term(g, p),
			  graph_term(g, o) };
	if (g->n == g->cap)
		triple_grow(g);
	size_t n = g->n++;
	g->ts[n] = triple;
	size_t bucket = (size_t)hash & (g->triple_capacity - 1);
	g->triple_links[n] = (TripleLink){ hash, g->triple_heads[bucket] };
	g->triple_heads[bucket] = n + 1;
	J *a = map_get(&g->subjects, triple.s.value);
	if (!a) {
		a = graph_id_node(g, cJSON_Array);
		map_put_ref(&g->subjects, triple.s.value, a);
	}
	J *id = graph_id_node(g, cJSON_Number);
	id->valuedouble = (double)n;
	id->valueint = n > INT_MAX ? INT_MAX : (int)n;
	ADD(a, id);
}

void rdf_bind_prefix(Graph *g, const char *prefix, const char *namespace)
{
	if (!GET(g->prefixes, prefix) ||
	    !strcmp(S(GET(g->prefixes, prefix)), namespace)) {
		set(g->prefixes, prefix, STR(namespace));
		return;
	}
	for (unsigned i = 1;; i++) {
		Buf candidate = { 0 };
		char suffix[24];
		snprintf(suffix, sizeof(suffix), "%u", i);
		buf_put(&candidate, prefix);
		buf_put(&candidate, suffix);
		int available =
			!GET(g->prefixes, candidate.p) ||
			!strcmp(S(GET(g->prefixes, candidate.p)), namespace);
		if (available)
			set(g->prefixes, candidate.p, STR(namespace));
		free(candidate.p);
		if (available)
			return;
	}
}

Graph *rdf_create(mc_error *e)
{
	Graph *g = calloc(1, sizeof(*g));
	if (!g) {
		fail(e, 5, "Out of memory");
		return NULL;
	}
	g->prefixes = OBJ();
	for (size_t i = 0; i < sizeof(rdf_defaults) / sizeof(*rdf_defaults);
	     i++)
		PUT(g->prefixes, rdf_defaults[i][0], STR(rdf_defaults[i][1]));
	return g;
}

void rdf_free(Graph *g)
{
	if (!g)
		return;
	lookup_trace_free(g->lookup_trace);
	for (size_t i = 0; i < g->n; i++) {
		rdf_term_free(&g->ts[i].s);
		rdf_term_free(&g->ts[i].p);
		rdf_term_free(&g->ts[i].o);
	}
	while (g->subject_ids) {
		GraphIds *next = g->subject_ids->next;
		free(g->subject_ids);
		g->subject_ids = next;
	}
	map_free(&g->subjects);
	free(g->triple_heads);
	free(g->triple_links);
	map_free(&g->resources);
	while (g->strings) {
		GraphStrings *next = g->strings->next;
		free(g->strings);
		g->strings = next;
	}
	free(g->ts);
	DEL(g->prefixes);
	free(g);
}

J *rdf_values(Graph *g, const char *s, const char *p)
{
	J *r = ARR(), *ids = map_get(&g->subjects, s);
	EACH(v, ids) {
		Triple *t = &g->ts[(size_t)v->valuedouble];
		if (!p || !strcmp(t->p.value, p))
			ADD(r, NUM(v->valuedouble));
	}
	return r;
}

static void flatten(Graph *g, Term *t, int lc, J *r, Map *active, int depth,
		    mc_error *e)
{
	if (depth > 128) {
		fail(e, 3, "Blank-node expansion exceeds 128 levels");
		return;
	}
	if (t->kind != 2) {
		char *s =
			norm(t->kind == 0 ? local(t->value) : t->value, lc, 0);
		ADD(r, STR(s));
		free(s);
		return;
	}
	if (map_get(active, t->value)) {
		fail(e, 3, "Cycle in blank-node expansion");
		return;
	}
	map_put(active, t->value, (void *)1);
	J *ids = map_get(&g->subjects, t->value);
	EACH(v, ids)
		flatten(g, &g->ts[(size_t)v->valuedouble].o, lc, r, active,
			depth + 1, e);
	map_put(active, t->value, NULL);
}

J *rdf_flatten(Graph *g, Term *t, int lc, mc_error *e)
{
	J *r = ARR();
	Map active = { 0 };
	flatten(g, t, lc, r, &active, 0, e);
	map_free(&active);
	return r;
}

static J *term_json(Term *t)
{
	J *j = OBJ();
	PUT(j, "value", STR(t->value));
	PUT(j, "kind",
	    STR(t->kind == 0 ? "iri" :
		t->kind == 1 ? "literal" :
				       "blank"));
	if (t->datatype)
		PUT(j, "datatype", STR(t->datatype));
	if (t->language)
		PUT(j, "language", STR(t->language));
	return j;
}

J *rdf_json(Graph *g)
{
	J *a = ARR();
	for (size_t i = 0; i < g->n; i++) {
		J *t = OBJ();
		PUT(t, "subject", term_json(&g->ts[i].s));
		PUT(t, "predicate", term_json(&g->ts[i].p));
		PUT(t, "object", term_json(&g->ts[i].o));
		ADD(a, t);
	}
	return a;
}

Graph *rdf_merge(Graph **graphs, size_t count)
{
	Graph *out = calloc(1, sizeof(*out));
	if (!out)
		return NULL;
	out->prefixes = OBJ();
	for (size_t i = 0; i < count; i++) {
		Graph *g = graphs[i];
		if (!g) {
			rdf_free(out);
			return NULL;
		}
		EACH(prefix, g->prefixes)
			if (!GET(out->prefixes, prefix->string))
				PUT(out->prefixes, prefix->string, DUP(prefix));
		for (size_t j = 0; j < g->n; j++) {
			Term terms[3] = { rdf_term_copy(&g->ts[j].s),
					  rdf_term_copy(&g->ts[j].p),
					  rdf_term_copy(&g->ts[j].o) };
			for (int k = 0; k < 3; k++)
				if (terms[k].kind == 2) {
					Buf b = { 0 };
					char prefix[40];
					snprintf(prefix, sizeof(prefix),
						 "_:source%zu_", i);
					buf_put(&b, prefix);
					buf_put(&b, terms[k].value);
					free(terms[k].value);
					terms[k].value = buf_take(&b);
				}
			rdf_add(out, &terms[0], &terms[1], &terms[2]);
			for (int k = 0; k < 3; k++)
				rdf_term_free(&terms[k]);
		}
	}
	return out;
}

/* Existing subject slots stay stable for vocabulary replacements (craigtrim/axiom#129). */
void rdf_replace_object(Graph *g, size_t at, const Term *object)
{
	Triple *t = &g->ts[at];
	Term replacement = rdf_term_copy(object);
	size_t bucket = (size_t)g->triple_links[at].hash &
			(g->triple_capacity - 1);
	size_t *link = &g->triple_heads[bucket];
	while (*link && *link != at + 1)
		link = &g->triple_links[*link - 1].next;
	assert(*link == at + 1);
	*link = g->triple_links[at].next;
	rdf_term_free(&t->o);
	t->o = replacement;
	g->triple_links[at].hash = triple_hash(&t->s, &t->p, &t->o);
	bucket = (size_t)g->triple_links[at].hash & (g->triple_capacity - 1);
	/* Every slot has its own link, including temporary duplicates in swaps. */
	g->triple_links[at].next = g->triple_heads[bucket];
	g->triple_heads[bucket] = at + 1;
}
