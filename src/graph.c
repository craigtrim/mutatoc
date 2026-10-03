/*
 * graph.c - Shared RDF terms, graph storage, and query indexes.
 *
 * Native readers add the same triples here without serialization conversion.
 */

#include "mc.h"
#include "rdf_namespaces.h"

Term rdf_term(const char *s, int kind)
{
	Term t = { copy(s), NULL, NULL, kind };
	return t;
}

void rdf_term_free(Term *t)
{
	free(t->value);
	free(t->datatype);
	free(t->language);
	memset(t, 0, sizeof(*t));
}

Term rdf_term_copy(const Term *t)
{
	Term v = { copy(t->value), t->datatype ? copy(t->datatype) : NULL,
		   t->language ? copy(t->language) : NULL, t->kind };
	return v;
}

void rdf_add(Graph *g, const Term *s, const Term *p, const Term *o)
{
	Buf key = { 0 };
	const Term *terms[] = { s, p, o };
	for (size_t i = 0; i < 3; i++) {
		const char *parts[] = { terms[i]->value, terms[i]->datatype,
					terms[i]->language };
		char length[64];
		snprintf(length, sizeof(length), "%d:", terms[i]->kind);
		buf_put(&key, length);
		for (size_t j = 0; j < 3; j++) {
			const char *part = parts[j] ? parts[j] : "";
			snprintf(length, sizeof(length), "%zu:", strlen(part));
			buf_put(&key, length);
			buf_put(&key, part);
		}
	}
	if (map_get(&g->triple_keys, key.p)) {
		free(key.p);
		return;
	}
	map_put(&g->triple_keys, key.p, (void *)1);
	free(key.p);

	if (g->n == g->cap) {
		g->cap = g->cap ? g->cap * 2 : 256;
		g->ts = realloc(g->ts, g->cap * sizeof(*g->ts));
	}
	size_t n = g->n++;
	g->ts[n] = (Triple){ rdf_term_copy(s), rdf_term_copy(p),
			     rdf_term_copy(o) };
	J *a = map_get(&g->subjects, s->value);
	if (!a) {
		a = ARR();
		map_put(&g->subjects, s->value, a);
	}
	ADD(a, NUM((double)n));
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
	for (size_t i = 0; i < g->n; i++) {
		rdf_term_free(&g->ts[i].s);
		rdf_term_free(&g->ts[i].p);
		rdf_term_free(&g->ts[i].o);
	}
	for (size_t i = 0; i < g->subjects.cap; i++)
		if (g->subjects.slots[i].key)
			DEL(g->subjects.slots[i].value);
	map_free(&g->subjects);
	map_free(&g->triple_keys);
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
