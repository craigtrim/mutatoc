/* Update equivalence, transaction failure, and cache reuse (craigtrim/axiom#129). */
#include "mc.h"
#include "generated.h"
static int failures;
#define CHECK(c)                                                        \
	do {                                                            \
		if (!(c)) {                                             \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c); \
			failures++;                                     \
		}                                                       \
	} while (0)
static J *call(mc_engine *e, J *q, int ok)
{
	char *wire = cJSON_PrintUnformatted(q);
	mc_error err;
	char *response = mc_request(e, wire, &err);
	J *out = response ? cJSON_Parse(response) : NULL;
	CHECK(out && cJSON_IsTrue(GET(out, "ok")) == ok && (!err.code) == ok);
	if ((!err.code) != ok)
		fprintf(stderr, "request %.160s\nresponse %s\n", wire,
			response ? response : "null");
	free(wire);
	mc_free(response);
	DEL(q);
	return out;
}
static J *operation(mc_engine *e, const char *op)
{
	J *q = OBJ();
	PUT(q, "op", STR(op));
	return call(e, q, 1);
}
static void load(mc_engine *e, const char *text, int live)
{
	J *q = OBJ();
	PUT(q, "op", STR("load"));
	PUT(q, "turtle", STR(text));
	PUT(q, "name", STR("update-test"));
	PUT(q, "class_based", BOOL(1));
	PUT(q, "interface", STR(live ? "data" : "json"));
	J *r = call(e, q, 1);
	DEL(r);
}
static J *update(const char *iri, const char *text)
{
	J *q = OBJ();
	PUT(q, "op", STR("update"));
	if (text) {
		J *replace = OBJ();
		PUT(replace, iri, STR(text));
		PUT(q, "replace", replace);
	} else {
		J *remove = ARR();
		ADD(remove, STR(iri));
		PUT(q, "remove", remove);
	}
	return q;
}
static void equivalent(mc_engine *a, mc_engine *b)
{
	J *x = operation(a, "snapshot"), *y = operation(b, "snapshot");
	CHECK(cJSON_Compare(x, y, 1));
	DEL(x);
	DEL(y);
	/* Data-view arrays must also agree after their snapshot owners change. */
	if (a->live_mode && b->live_mode)
		CHECK(cJSON_Compare(a->live, b->live, 1));
	/* Lookup group order chooses alias precedence in each view. */
	J *av = a->live_mode ? a->live : a->snapshot;
	J *bv = b->live_mode ? b->live : b->snapshot;
	J *ag = GET(GET(av, "synonyms"), "lookup");
	J *bg = GET(GET(bv, "synonyms"), "lookup");
	J *ak = ag ? ag->child : NULL, *bk = bg ? bg->child : NULL;
	for (; ak && bk; ak = ak->next, bk = bk->next)
		CHECK(!strcmp(ak->string, bk->string));
	CHECK(!ak && !bk);
	const char *texts[] = {
		"red green blue",
		"bred green blues",
		"red green red green",
		"red green",
		"district regional school",
		"dog/cat health",
		"dog / cat health",
		"alpha beta gamma delta epsilon zeta",
		"deep learning course",
		"learning deep",
		"district00042 regional school of applied sciences9",
		"district00042 regional academy training center"
	};
	for (size_t i = 0; i < sizeof(texts) / sizeof(*texts); i++) {
		J *q = OBJ();
		PUT(q, "op", STR("parse"));
		PUT(q, "text", STR(texts[i]));
		x = call(a, DUP(q), 1);
		y = call(b, q, 1);
		CHECK(cJSON_Compare(x, y, 1));
		DEL(x);
		DEL(y);
	}
}
static char *entity(int id, const char *label, const char *alias)
{
	char iri[80];
	snprintf(iri, sizeof(iri), "<http://example.org/#id%d> ", id);
	J *l = STR(label), *a = STR(alias);
	char *ls = cJSON_PrintUnformatted(l), *as = cJSON_PrintUnformatted(a);
	Buf b = { 0 };
	buf_put(&b, iri);
	buf_put(&b, "<http://www.w3.org/2000/01/rdf-schema#label> ");
	buf_put(&b, ls);
	buf_put(&b, "; <http://www.w3.org/2000/01/rdf-schema#seeAlso> ");
	buf_put(&b, as);
	buf_put(&b, " .\n");
	DEL(l);
	DEL(a);
	free(ls);
	free(as);
	return buf_take(&b);
}
static char *document(char **entities, int n)
{
	Buf b = { 0 };
	for (int i = 0; i < n; i++)
		if (entities[i])
			buf_put(&b, entities[i]);
	return buf_take(&b);
}
static char *ordered_document(char **entities, int *order)
{
	Buf b = { 0 };
	for (int i = 0; i < 24; i++)
		if (entities[order[i]])
			buf_put(&b, entities[order[i]]);
	return buf_take(&b);
}
static void sequences(int live)
{
	const char *terms[] = { "red green blue",
				"bred green blues",
				"red green red green",
				"red green",
				"district regional school",
				"dog/cat health",
				"alpha beta gamma delta epsilon zeta",
				"deep learning course",
				"learning deep",
				"alpha beta gamma delta epsilon zeta eta theta",
				"dog /cat health",
				"dog / cat health" };
	int term_count = (int)(sizeof(terms) / sizeof(*terms));
	char *entities[24];
	int order[24];
	for (int i = 0; i < 24; i++)
		order[i] = i;
	for (int i = 0; i < 24; i++)
		entities[i] = entity(i, terms[i % term_count],
				     terms[(i + 2) % term_count]);
	mc_engine *e = mc_create(), *fresh = mc_create();
	char *text = document(entities, 24);
	load(e, text, live);
	free(text);
	unsigned random = 129;
	for (int step = 0; step < 80; step++) {
		random = random * 1664525u + 1013904223u;
		int id = (int)(random % 24);
		char iri[80];
		snprintf(iri, sizeof(iri), "http://example.org/#id%d", id);
		int absent = entities[id] == NULL;
		free(entities[id]);
		if (step % 13 == 0)
			entities[id] = NULL;
		else
			entities[id] =
				entity(id, terms[(random >> 8) % term_count],
				       terms[(random >> 16) % term_count]);
		if (absent && entities[id]) {
			int at = 0;
			while (order[at] != id)
				at++;
			for (; at < 23; at++)
				order[at] = order[at + 1];
			order[23] = id;
		}
		J *r = call(e, update(iri, entities[id]), 1);
		DEL(r);
		text = ordered_document(entities, order);
		load(fresh, text, live);
		free(text);
		equivalent(e, fresh);
		if (failures) {
			fprintf(stderr, "sequence live=%d step=%d id=%d\n",
				live, step, id);
			break;
		}
	}
	/* An error after one valid fragment must leave every view and triple intact. */
	J *before = operation(e, "snapshot"),
	  *triples = operation(e, "triples");
	J *q = update("http://example.org/#id1",
		      entities[1] ? entities[1] : "");
	PUT(GET(q, "replace"), "http://example.org/#id2",
	    STR("this is not Turtle"));
	J *r = call(e, q, 0);
	DEL(r);
	J *after = operation(e, "snapshot"),
	  *after_triples = operation(e, "triples");
	CHECK(cJSON_Compare(before, after, 1));
	CHECK(cJSON_Compare(triples, after_triples, 1));
	DEL(before);
	DEL(after);
	DEL(triples);
	DEL(after_triples);
	equivalent(e, fresh);
	q = update("http://example.org/#id1",
		   "<http://example.org/#other> <urn:p> \"bad\" .");
	r = call(e, q, 0);
	DEL(r);
	q = update("http://example.org/#id1", "");
	J *remove = ARR();
	ADD(remove, STR("http://example.org/#id1"));
	PUT(q, "remove", remove);
	r = call(e, q, 0);
	DEL(r);
	q = update("relative", "");
	r = call(e, q, 0);
	DEL(r);
	for (int i = 0; i < 24; i++)
		free(entities[i]);
	mc_destroy(e);
	mc_destroy(fresh);
}
static void indexed_replace(int live)
{
	mc_engine *e = mc_create(), *fresh = mc_create();
	char *text = generated_turtle(160);
	load(e, text, live);
	free(text);
	CHECK(e->updates != NULL);
	Graph *graph = e->graph;
	MatchIndex *index = e->index;
	char *entities[160];
	for (int i = 0; i < 160; i++)
		entities[i] = generated_entity(i, 0);
	for (int step = 1; step <= 16; step++) {
		int id = step % 2 ? 42 : 19;
		char iri[80];
		snprintf(iri, sizeof(iri), "http://example.org/#id%d", id);
		free(entities[id]);
		entities[id] = generated_entity(id, step);
		if (step % 4 == 3) {
			char *shorter = replace(entities[id],
						"school of applied", "school");
			free(entities[id]);
			entities[id] = shorter;
		}
		J *r = call(e, update(iri, entities[id]), 1);
		DEL(r);
		CHECK(e->graph == graph && e->index == index);
		text = document(entities, 160);
		load(fresh, text, live);
		free(text);
		equivalent(e, fresh);
	}
	for (int i = 0; i < 160; i++)
		free(entities[i]);
	mc_destroy(e);
	mc_destroy(fresh);
}
static void indexed_swap(int live)
{
	const char *documents[] = {
		"<urn:school> <http://www.w3.org/2000/01/rdf-schema#label> \"red green\", \"blue green\" .",
		"<urn:school> <http://www.w3.org/2000/01/rdf-schema#label> \"blue green\", \"red green\" ."
	};
	mc_engine *e = mc_create(), *fresh = mc_create();
	load(e, documents[0], live);
	Graph *graph = e->graph;
	CHECK(e->updates != NULL);
	for (int step = 1; step <= 4; step++) {
		J *r = call(e, update("urn:school", documents[step % 2]), 1);
		DEL(r);
		CHECK(e->graph == graph);
		load(fresh, documents[step % 2], live);
		equivalent(e, fresh);
		/* Adding either existing triple must still be a no-op. */
		size_t n = e->graph->n;
		for (size_t i = 0; i < fresh->graph->n; i++) {
			Triple *t = &fresh->graph->ts[i];
			rdf_add(e->graph, &t->s, &t->p, &t->o);
		}
		CHECK(e->graph->n == n);
	}
	mc_destroy(e);
	mc_destroy(fresh);
}
static void write_term(Buf *b, const Term *t)
{
	if (t->kind == 0) {
		buf_put(b, "<");
		buf_put(b, t->value);
		buf_put(b, ">");
	} else if (t->kind == 2) {
		buf_put(b, "_:b");
		for (const unsigned char *p = (const unsigned char *)t->value;
		     *p; p++) {
			char hex[3];
			snprintf(hex, sizeof(hex), "%02x", *p);
			buf_put(b, hex);
		}
	} else {
		J *v = STR(t->value);
		char *quoted = cJSON_PrintUnformatted(v);
		buf_put(b, quoted);
		free(quoted);
		DEL(v);
		if (rdf_language(t)) {
			buf_put(b, "@");
			buf_put(b, rdf_language(t));
		} else if (rdf_datatype(t)) {
			buf_put(b, "^^<");
			buf_put(b, rdf_datatype(t));
			buf_put(b, ">");
		}
	}
}
static void write_triple(Buf *b, const Triple *t)
{
	write_term(b, &t->s);
	buf_put(b, " ");
	write_term(b, &t->p);
	buf_put(b, " ");
	write_term(b, &t->o);
	buf_put(b, " .\n");
}
static void fixture_updates(const char *root, int live)
{
	const char *files[] = { "animals-test.owl", "econ-20160218.owl",
				"acanames-20251028.owl" };
	for (size_t f = 0; f < sizeof(files) / sizeof(*files); f++) {
		fprintf(stderr, "fixture %s live %d\n", files[f], live);
		Buf path = { 0 };
		buf_put(&path, root);
		buf_put(&path, "/tests/fixtures/ontologies/");
		buf_put(&path, files[f]);
		mc_error err = { 0 };
		char *text = read_file(path.p, &err);
		free(path.p);
		CHECK(text != NULL);
		if (!text)
			continue;
		mc_engine *e = mc_create(), *fresh = mc_create();
		load(e, text, live);
		free(text);
		char *iri = NULL;
		for (size_t i = 0; i < e->graph->n && !iri; i++) {
			Triple *t = &e->graph->ts[i];
			if (strcmp(t->p.value, RDFS "label") ||
			    t->s.kind != 0 || t->o.kind != 1)
				continue;
			int blank = 0;
			J *ids = map_get(&e->graph->subjects, t->s.value);
			EACH(id, ids)
				blank |= e->graph->ts[(size_t)id->valuedouble]
						 .o.kind == 2;
			if (!blank)
				iri = copy(t->s.value);
		}
		CHECK(iri != NULL);
		if (iri) {
			Buf replacement = { 0 };
			J *ids = map_get(&e->graph->subjects, iri);
			EACH(id, ids) {
				Triple t =
					e->graph->ts[(size_t)id->valuedouble];
				if (!strcmp(t.p.value, RDFS "label") &&
				    t.o.kind == 1) {
					Buf label = { 0 };
					buf_put(&label, t.o.value);
					buf_put(&label, " revised label");
					t.o.value = label.p;
					write_triple(&replacement, &t);
					free(label.p);
				} else
					write_triple(&replacement, &t);
			}
			for (int remove = 0; remove < 2; remove++) {
				J *r = call(e,
					    update(iri, remove ? NULL :
								       replacement.p),
					    1);
				DEL(r);
				Buf resulting = { 0 };
				for (size_t i = 0; i < e->graph->n; i++)
					write_triple(&resulting,
						     &e->graph->ts[i]);
				load(fresh, resulting.p, live);
				free(resulting.p);
				equivalent(e, fresh);
			}
			free(replacement.p);
			free(iri);
		}
		mc_destroy(e);
		mc_destroy(fresh);
	}
}

/* More than eight groups exercise ordering storage growth and recycled nodes. */
static char *many_group_entity(int revision)
{
	Buf text = { 0 };
	for (int n = 1; n <= 14; n++) {
		buf_put(&text,
			"<http://example.org/#many> <http://www.w3.org/2000/01/rdf-schema#label> \"");
		for (int word = 0; word < n + revision % 3; word++) {
			if (word)
				buf_put(&text, " ");
			buf_put(&text, revision % 2 ? "violet" : "orange");
		}
		buf_put(&text, "\" .\n");
	}
	return buf_take(&text);
}
static void many_group_updates(int live)
{
	mc_engine *e = mc_create(), *fresh = mc_create();
	char *text = many_group_entity(0);
	load(e, text, live);
	free(text);
	Graph *original = e->graph;
	MatchIndex *index = e->index;
	for (int revision = 1; revision <= 18; revision++) {
		text = many_group_entity(revision);
		J *result =
			call(e, update("http://example.org/#many", text), 1);
		DEL(result);
		CHECK(e->graph == original && e->index == index);
		load(fresh, text, live);
		free(text);
		equivalent(e, fresh);
		J *a = operation(e, "snapshot"),
		  *b = operation(fresh, "snapshot");
		char *actual = cJSON_PrintUnformatted(
			     GET(GET(GET(a, "result"), "synonyms"), "lookup")),
		     *expected = cJSON_PrintUnformatted(
			     GET(GET(GET(b, "result"), "synonyms"), "lookup"));
		CHECK(!strcmp(actual, expected));
		free(actual);
		free(expected);
		DEL(a);
		DEL(b);
	}
	mc_destroy(e);
	mc_destroy(fresh);
}

/* Literal token sequences suppress aliases; removing them restores the best
 * remaining source, including after candidate records have been recycled. */
static void alias_precedence(int live)
{
	const char *labels[] = { "dog/cat health", "dog / cat health",
				 "dog /cat health" };
	const char *markers[] = { "marker zero", "marker one", "marker two" };
	const int subjects[] = { 1, 0, 0, 2, 1, 2, 1, 0 };
	const char *replacements[] = { "unrelated one",	   "unrelated zero",
				       "dog/cat health",   "dog / cat health",
				       "dog / cat health", "dog /cat health",
				       "unrelated one",	   "unrelated zero" };
	const char *expected[] = { "id1", "id0", "id2", "id0", "id2",
				   "id1", "id1", "id0", "id2" };
	char *entities[3];
	for (int i = 0; i < 3; i++)
		entities[i] = entity(i, labels[i], markers[i]);
	mc_engine *e = mc_create(), *fresh = mc_create();
	char *text = document(entities, 3);
	load(e, text, live);
	free(text);
	Graph *graph = e->graph;
	MatchIndex *index = e->index;
	for (int step = 0; step <= 8; step++) {
		if (step) {
			int id = subjects[step - 1];
			char iri[80];
			snprintf(iri, sizeof(iri), "http://example.org/#id%d",
				 id);
			free(entities[id]);
			entities[id] =
				entity(id, replacements[step - 1], markers[id]);
			J *r = call(e, update(iri, entities[id]), 1);
			DEL(r);
			CHECK(e->graph == graph && e->index == index);
		}
		J *q = OBJ();
		PUT(q, "op", STR("parse"));
		PUT(q, "text", STR("dog/cat health"));
		J *r = call(e, q, 1);
		CHECK(!strcmp(S(GET(GET(r, "result"), "text")),
			      expected[step]));
		DEL(r);
		text = document(entities, 3);
		load(fresh, text, live);
		free(text);
		equivalent(e, fresh);
	}
	for (int i = 0; i < 3; i++)
		free(entities[i]);
	mc_destroy(e);
	mc_destroy(fresh);
}

int main(int argc, char **argv)
{
	for (int live = 0; live < 2; live++) {
		sequences(live);
		indexed_replace(live);
		indexed_swap(live);
		many_group_updates(live);
		alias_precedence(live);
		if (argc > 1)
			fixture_updates(argv[1], live);
	}
	mc_engine *empty = mc_create();
	J *r = call(empty, update("urn:a", ""), 0);
	DEL(r);
	mc_destroy(empty);
	printf("update checks: %d failures\n", failures);
	return failures ? 1 : 0;
}
