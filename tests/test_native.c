/*
 * test_native.c - Native ontology loading and matching checks.
 *
 * Exercises JSON requests, prepared tokens, and error responses.
 */

#include "mc.h"

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
static J *request(mc_engine *e, const char *s)
{
	mc_error err;
	char *r = mc_request(e, s, &err);
	CHECK(r != NULL);
	J *j = r ? cJSON_Parse(r) : NULL;
	mc_free(r);
	return j;
}

int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	mc_engine *e = mc_create();
	CHECK(e != NULL);
	J *r = request(e, "{\"op\":\"version\"}");
	CHECK(cJSON_IsTrue(GET(r, "ok")));
	DEL(r);
	r = request(e, "{bad");
	CHECK(cJSON_IsFalse(GET(r, "ok")));
	DEL(r);
	r = request(
		e,
		"{\"op\":\"load\",\"turtle\":\"@prefix : <http://test/#> . @prefix rdfs: "
		"<http://www.w3.org/2000/01/rdf-schema#> . :dog rdfs:label \\\"Dog\\\" ; rdfs:seeAlso "
		"\\\"hound\\\" .\",\"name\":\"test\"}"); /* Fixture paths below cover ontology loading. */
	DEL(r);
	const char *names[] = { "animals-test", "colors-test", "music-test",
				"geography-test", "econ-20160218" };
	for (size_t i = 0; i < sizeof(names) / sizeof(*names); i++) {
		Buf b = { 0 };
		buf_put(&b, argv[1]);
		buf_put(&b, "/tests/fixtures/ontologies/");
		buf_put(&b, names[i]);
		buf_put(&b, ".owl");
		J *q = OBJ();
		PUT(q, "op", STR("load"));
		PUT(q, "path", STR(b.p));
		char *s = cJSON_PrintUnformatted(q);
		r = request(e, s);
		if (!cJSON_IsTrue(GET(r, "ok")))
			fprintf(stderr, "%s\n",
				S(GET(GET(r, "error"), "message")));
		CHECK(cJSON_IsTrue(GET(r, "ok")));
		DEL(q);
		DEL(r);
		free(s);
		free(b.p);
	}
	r = request(e,
		    "{\"op\":\"parse\",\"text\":\"Fiscal Policy Analysis\"}");
	CHECK(cJSON_IsFalse(
		GET(r, "ok"))); /* No approximate raw-text fallback. */
	DEL(r);
	r = request(
		e,
		"{\"op\":\"parse_tokens\",\"tokens\":[{\"id\":1,\"text\":\"Fiscal\",\"normal\":"
		"\"fiscal\",\"x\":0,\"y\":6,\"other\":{\"orth\":18446744073709551615}},{\"id\":"
		"2,\"text\":\"Policy\",\"normal\":\"policy\",\"x\":7,\"y\":13},{\"id\":3,"
		"\"text\":\"Analysis\",\"normal\":\"analysis\",\"x\":14,\"y\":22}]}");
	CHECK(cJSON_IsTrue(GET(r, "ok")));
	CHECK(!strcmp(S(GET(GET(r, "result"), "text")),
		      "fiscal_policy_analysis"));
	char *wire = cJSON_PrintUnformatted(r);
	CHECK(strstr(wire, "18446744073709551615") != NULL);
	free(wire);
	DEL(r);
	mc_destroy(e);
	printf("%d checks, %d failures\n", total, failed);
	return failed ? 1 : 0;
}
