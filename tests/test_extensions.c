/*
 * test_extensions.c - Collections, optional stages and external synonyms.
 *
 * Covers the optional token stages, the exact-window regression corpus,
 * multi-ontology collections, live external synonyms, default query
 * prefixes, blank-node scoping and source-order tie breaking.
 * craigtrim/mutatoc#1
 */

#include "testlib.h"

static int failed = 0, total = 0;
#define CHECK(x)                                                      \
	do {                                                          \
		total++;                                              \
		if (!(x)) {                                           \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, \
				__LINE__, #x);                        \
			failed++;                                     \
		}                                                     \
	} while (0)

static const char *root;

/* Loads an ontology request built from printf arguments; returns ok. */
static int load(mc_engine *e, const char *format, ...)
{
	char wire[4096];
	va_list ap;
	va_start(ap, format);
	vsnprintf(wire, sizeof(wire), format, ap);
	va_end(ap);
	J *r = test_call(e, wire);
	DEL(r);
	return r != NULL;
}

static J *token(const char *text, int i)
{
	J *t = OBJ();
	char *normal = lower(text);
	PUT(t, "id", NUM(i));
	PUT(t, "text", STR(text));
	PUT(t, "normal", STR(normal));
	PUT(t, "x", NUM(i * 10));
	PUT(t, "y", NUM(i * 10 + (int)strlen(text)));
	free(normal);
	return t;
}

static J *parse_tokens(mc_engine *e, const char *a, const char *b)
{
	J *q = OBJ(), *tokens = ARR();
	PUT(q, "op", STR("parse_tokens"));
	ADD(tokens, token(a, 0));
	if (b)
		ADD(tokens, token(b, 1));
	PUT(q, "tokens", tokens);
	char *wire = cJSON_PrintUnformatted(q);
	J *r = test_call(e, wire);
	free(wire);
	DEL(q);
	return r;
}

static void optional_stages(void)
{
	mc_engine *e = mc_create();
	CHECK(load(
		e,
		"{\"op\":\"load\",\"path\":\"%s/tests/fixtures/api/proper.owl\","
		"\"interface\":\"data\",\"class_based\":true}",
		root));
	J *rows = test_read_json(root, "tests/fixtures/api/stages.json");
	int count = 0;
	EACH(row, rows) {
		J *r = test_request(e, GET(row, "request"));
		count++;
		if (GET(row, "error")) {
			CHECK(cJSON_IsFalse(GET(r, "ok")));
		} else {
			const J *result = GET(r, "result");
			if (!strcmp(S(GET(GET(row, "request"), "op")),
				    "parse_tokens"))
				result = GET(result, "tokens");
			int same =
				cJSON_IsTrue(GET(r, "ok")) &&
				cJSON_Compare(result, GET(row, "expected"), 1);
			if (!same)
				test_print_diff(
					S(GET(GET(row, "request"), "stage")),
					GET(row, "expected"), result);
			CHECK(same);
		}
		DEL(r);
	}
	printf("Optional stages and live matching: %d cases.\n", count);
	DEL(rows);
	mc_destroy(e);
}

static void exact_windows(void)
{
	mc_engine *e = mc_create();
	J *corpus = test_read_json(
		root, "tests/fixtures/api/matching-regressions.json");
	J *q = OBJ();
	PUT(q, "op", STR("load"));
	PUT(q, "name", STR("window-regression"));
	PUT(q, "snapshot", DUP(GET(corpus, "snapshot")));
	J *r = test_request(e, q);
	CHECK(cJSON_IsTrue(GET(r, "ok")));
	DEL(r);
	DEL(q);
	int count = 0;
	EACH(c, GET(corpus, "cases")) {
		q = OBJ();
		PUT(q, "op", STR("transform_tokens"));
		PUT(q, "stage", STR("exact"));
		PUT(q, "tokens", DUP(GET(c, "tokens")));
		r = test_request(e, q);
		CHECK(cJSON_Compare(GET(r, "result"), GET(c, "expected"), 1));
		DEL(r);
		DEL(q);
		count++;
	}
	printf("Exact window regressions: %d complete token cases.\n", count);
	DEL(corpus);
	mc_destroy(e);
}

static void collections(void)
{
	mc_engine *e = mc_create();
	CHECK(load(
		e,
		"{\"op\":\"load\",\"paths\":[\"%s/tests/fixtures/ontologies/animals-test.owl\","
		"\"%s/tests/fixtures/ontologies/colors-test.owl\"],"
		"\"interface\":\"data\",\"class_based\":true}",
		root, root));
	J *names = test_call(e, "{\"op\":\"query\",\"method\":\"ontologies\"}");
	J *expected = cJSON_Parse("[\"animals-test\",\"colors-test\"]");
	CHECK(cJSON_Compare(names, expected, 1));
	DEL(names);
	J *out = parse_tokens(e, "dog", "red");
	CHECK(!strcmp(S(GET(out, "text")), "dog red"));
	CHECK(SIZE(GET(out, "tokens")) == 2);
	EACH(t, GET(out, "tokens"))
		CHECK(cJSON_Compare(GET(GET(t, "swaps"), "ontologies"),
				    expected, 1));
	DEL(expected);
	DEL(out);
	J *labels = test_call(
		e,
		"{\"op\":\"query\",\"interface\":\"data\",\"method\":\"labels\"}");
	CHECK(GET(labels, "dog") && GET(labels, "red"));
	DEL(labels);
	J *before = test_call(e, "{\"op\":\"snapshot\"}");
	char wire[2048];
	snprintf(
		wire, sizeof(wire),
		"{\"op\":\"load\",\"paths\":[\"%s/tests/fixtures/ontologies/animals-test.owl\","
		"\"%s/missing.owl\"]}",
		root, root);
	J *q = cJSON_Parse(wire), *r = test_request(e, q);
	CHECK(cJSON_IsFalse(GET(r, "ok")));
	DEL(r);
	DEL(q);
	J *after = test_call(e, "{\"op\":\"snapshot\"}");
	CHECK(cJSON_Compare(before, after, 1));
	DEL(before);
	DEL(after);
	mc_destroy(e);
}

static void live_synonyms(void)
{
	mc_engine *e = mc_create();
	CHECK(load(
		e,
		"{\"op\":\"load\",\"paths\":[\"%s/tests/fixtures/api/proper.owl\","
		"\"%s/tests/fixtures/ontologies/animals-test.owl\"],"
		"\"interface\":\"data\",\"class_based\":true}",
		root, root));
	J *out = parse_tokens(e, "pupper", "kitty");
	CHECK(!strcmp(S(GET(out, "text")), "dog cat"));
	DEL(out);
	J *syn = test_call(
		e,
		"{\"op\":\"query\",\"interface\":\"data\",\"method\":\"synonyms\"}");
	CHECK(contains(GET(syn, "dog"), "pupper"));
	DEL(syn);
	CHECK(load(
		e,
		"{\"op\":\"load\",\"path\":\"%s/tests/fixtures/api/proper.owl\","
		"\"class_based\":true}",
		root));
	syn = test_call(
		e,
		"{\"op\":\"query\",\"interface\":\"owl\",\"method\":\"synonyms\"}");
	CHECK(syn && !contains(GET(syn, "dog"), "pupper"));
	DEL(syn);
	mc_destroy(e);
}

static int query_equals(mc_engine *e, const char *wire, const char *expected)
{
	J *r = test_call(e, wire), *x = cJSON_Parse(expected);
	int same = cJSON_Compare(r, x, 1);
	if (!same)
		test_print_diff(wire, x, r);
	DEL(r);
	DEL(x);
	return same;
}

static void prefixes(void)
{
	mc_engine *e = mc_create();
	CHECK(load(e, "{\"op\":\"load\",\"turtle\":\"<urn:s> "
		      "<http://xmlns.com/foaf/0.1/name> \\\"Name\\\" .\"}"));
	CHECK(query_equals(
		e,
		"{\"op\":\"query\",\"interface\":\"owl\",\"method\":\"by_predicate\",\"args\":[\"foaf:name\"]}",
		"{\"urn:s\":[\"name\"]}"));
	CHECK(load(e, "{\"op\":\"load\",\"turtle\":\"@prefix rdf: "
		      "<http://test/#> . rdf:s rdf:p rdf:o .\"}"));
	CHECK(query_equals(
		e,
		"{\"op\":\"query\",\"interface\":\"owl\",\"method\":\"by_predicate\",\"args\":[\"rdf1:p\"]}",
		"{\"s\":[\"o\"]}"));
	CHECK(load(e,
		   "{\"op\":\"load\",\"turtle\":\"@prefix ex: <http://a/#> . "
		   "ex:s ex:p \\\"first\\\" . @prefix ex: <http://b/#> . ex:s "
		   "ex:p \\\"second\\\" .\"}"));
	CHECK(query_equals(
		e,
		"{\"op\":\"query\",\"interface\":\"owl\",\"method\":\"by_predicate\",\"args\":[\"ex:p\"]}",
		"{\"s\":[\"first\"]}"));
	CHECK(query_equals(
		e,
		"{\"op\":\"query\",\"interface\":\"owl\",\"method\":\"by_predicate\",\"args\":[\"ex1:p\"]}",
		"{\"s\":[\"second\"]}"));
	J *q = cJSON_Parse(
		"{\"op\":\"read_rdf\",\"turtle\":\"<urn:s> foaf:name \\\"Name\\\" .\"}");
	J *r = test_request(e, q);
	CHECK(cJSON_IsFalse(GET(r, "ok")));
	DEL(r);
	DEL(q);
	mc_destroy(e);
}

static void blank_nodes(void)
{
	mc_engine *e = mc_create();
	CHECK(load(
		e,
		"{\"op\":\"load\",\"sources\":["
		"{\"name\":\"first\",\"turtle\":\"@prefix : <http://example/#> . @prefix rdfs: "
		"<http://www.w3.org/2000/01/rdf-schema#> . _:same rdfs:label \\\"first\\\" .\"},"
		"{\"name\":\"second\",\"turtle\":\"@prefix : <http://example/#> . @prefix rdfs: "
		"<http://www.w3.org/2000/01/rdf-schema#> . _:same rdfs:label \\\"second\\\" .\"}]}"));
	J *triples = test_call(e, "{\"op\":\"triples\"}");
	CHECK(SIZE(triples) == 2);
	CHECK(SIZE(triples) == 2 &&
	      strcmp(S(GET(GET(AT(triples, 0), "subject"), "value")),
		     S(GET(GET(AT(triples, 1), "subject"), "value"))));
	DEL(triples);
	mc_destroy(e);
}

static void source_order(void)
{
	mc_engine *e = mc_create();
	const char *
		first =
		       "{\"name\":\"a\",\"snapshot\":{\"synonyms\":{\"fwd\":{\"first\":[\"shared\"]},"
		       "\"rev\":{\"shared\":[\"first\"]},\"lookup\":{\"1\":[\"shared\",\"first\"]}},"
		       "\"labels\":{\"first\":\"first\"},\"spans\":{}}}",
	       *second =
		       "{\"name\":\"b\",\"snapshot\":{\"synonyms\":{\"fwd\":{\"second\":[\"shared\"]},"
		       "\"rev\":{\"shared\":[\"second\"]},\"lookup\":{\"1\":[\"shared\",\"second\"]}},"
		       "\"labels\":{\"second\":\"second\"},\"spans\":{}}}";
	CHECK(load(e, "{\"op\":\"load\",\"sources\":[%s,%s]}", first, second));
	J *out = parse_tokens(e, "shared", NULL);
	CHECK(!strcmp(S(GET(out, "text")), "first"));
	DEL(out);
	CHECK(load(e, "{\"op\":\"load\",\"sources\":[%s,%s]}", second, first));
	out = parse_tokens(e, "shared", NULL);
	CHECK(!strcmp(S(GET(out, "text")), "second"));
	DEL(out);
	mc_destroy(e);
}

int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	root = argv[1];
	optional_stages();
	exact_windows();
	collections();
	live_synonyms();
	prefixes();
	blank_nodes();
	source_order();
	printf("Extensions: %d checks, %d failures.\n", total, failed);
	return failed ? 1 : 0;
}
