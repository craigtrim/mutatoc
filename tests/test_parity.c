/*
 * test_parity.c - Matching against the recorded reference corpus.
 *
 * Runs each parity case twice: its recorded tokens through parse_tokens,
 * compared with the complete reference output, and its text through parse,
 * compared on the entities found and the rendered text.
 * craigtrim/mutatoc#1
 */

#include "testlib.h"

static J *read_json_path(const char *root, const char *name)
{
	Buf p = { 0 };
	buf_put(&p, "tests/fixtures/parity/");
	buf_put(&p, name);
	J *j = test_read_json(root, p.p);
	free(p.p);
	return j;
}

int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	J *index = read_json_path(argv[1], "index.json");
	if (!test_require_cases(index, "parity index"))
		return 2;
	int total = 0, failed = 0, raw_total = 0, raw_failed = 0;
	EACH(group, index) {
		mc_engine *engine = mc_create();
		J *snapshot =
			read_json_path(argv[1], S(GET(group, "snapshot")));
		J *cases = read_json_path(argv[1], S(GET(group, "cases")));
		if (!test_require_cases(cases, S(GET(group, "cases"))))
			failed++;
		J *q = OBJ();
		PUT(q, "op", STR("load"));
		PUT(q, "name", DUP(GET(group, "name")));
		PUT(q, "snapshot", snapshot);
		J *response = test_request(engine, q);
		DEL(q);
		if (!cJSON_IsTrue(GET(response, "ok"))) {
			fprintf(stderr, "load failed\n");
			return 2;
		}
		DEL(response);
		EACH(c, cases) {
			q = OBJ();
			PUT(q, "op", STR("parse_tokens"));
			PUT(q, "tokens", DUP(GET(c, "tokens")));
			PUT(q, "ctr", DUP(GET(c, "ctr")));
			response = test_request(engine, q);
			DEL(q);
			total++;
			int equal =
				GET(c, "expected_error") ?
					cJSON_IsFalse(GET(response, "ok")) :
					cJSON_IsTrue(GET(response, "ok")) &&
						cJSON_Compare(
							GET(GET(response,
								"result"),
							    "tokens"),
							GET(c,
							    "expected_tokens"),
							1);
			if (!equal) {
				failed++;
				fprintf(stderr, "%s | %s\n",
					S(GET(group, "name")),
					S(GET(c, "text")));
			}
			DEL(response);
			q = OBJ();
			PUT(q, "op", STR("parse"));
			PUT(q, "text", DUP(GET(c, "text")));
			PUT(q, "ctr", DUP(GET(c, "ctr")));
			response = test_request(engine, q);
			DEL(q);
			raw_total++;
			const J *raw = GET(c, "raw");
			J *actual = cJSON_IsTrue(GET(response, "ok")) ?
					    test_projection(
						    GET(response, "result")) :
					    NULL;
			equal = GET(raw, "error") ?
					actual == NULL &&
						GET(GET(response, "error"),
						    "code")
								->valueint ==
							GET(raw, "error")
								->valueint :
					cJSON_Compare(actual, raw, 1);
			if (!equal) {
				raw_failed++;
				test_print_diff(S(GET(c, "text")), raw, actual);
			}
			DEL(actual);
			DEL(response);
		}
		DEL(cases);
		mc_destroy(engine);
	}
	DEL(index);
	printf("Prepared token parity: %d/%d passed.\n", total - failed, total);
	printf("Raw text parity: %d/%d passed (entities and rendered text).\n",
	       raw_total - raw_failed, raw_total);
	return failed || raw_failed ? 1 : 0;
}
