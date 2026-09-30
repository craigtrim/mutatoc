/*
 * test_concurrency.c - Independent engines on concurrent threads.
 *
 * Twenty-four engines on eight threads each load an ontology, match prepared
 * tokens twenty times, and parse raw text, through the public C API only.
 * Large integer caller fields survive untouched.
 * craigtrim/mutatoc#1
 */

#include "testlib.h"

typedef struct {
	const char *root;
	int failures[24];
	int prepared[24];
} Run;

static int check_call(mc_engine *e, const char *wire, const char *text,
		      const char *needle)
{
	mc_error err;
	char *r = mc_request(e, wire, &err);
	int ok = r && strstr(r, "\"ok\":true") != NULL;
	if (ok && text) {
		J *j = cJSON_Parse(r);
		ok = !strcmp(S(GET(GET(j, "result"), "text")), text);
		DEL(j);
	}
	if (ok && needle)
		ok = strstr(r, needle) != NULL;
	mc_free(r);
	return ok;
}

static void engine_run(void *ctx, int index)
{
	Run *run = ctx;
	mc_engine *e = mc_create();
	Buf load = { 0 };
	buf_put(&load, "{\"op\":\"load\",\"path\":\"");
	buf_put(&load, run->root);
	buf_put(&load, "/tests/fixtures/ontologies/animals-test.owl\"}");
	run->failures[index] += !check_call(e, load.p, NULL, NULL);
	free(load.p);
	char tokens[256];
	snprintf(
		tokens, sizeof(tokens),
		"{\"op\":\"parse_tokens\",\"tokens\":[{\"id\":%d,\"text\":\"Dog\","
		"\"normal\":\"dog\",\"x\":0,\"y\":3,\"other\":{\"orth\":"
		"18446744073709551615}}]}",
		index);
	for (int i = 0; i < 20; i++) {
		run->failures[index] +=
			!check_call(e, tokens, "dog", "18446744073709551615");
		run->prepared[index]++;
	}
	run->failures[index] += !check_call(
		e, "{\"op\":\"parse\",\"text\":\"Dog walks through London.\"}",
		"dog walks through London .", NULL);
	mc_destroy(e);
}

int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	Run run = { argv[1], { 0 }, { 0 } };
	test_parallel(8, 24, engine_run, &run);
	int failures = 0, prepared = 0;
	for (int i = 0; i < 24; i++) {
		failures += run.failures[i];
		prepared += run.prepared[i];
	}
	printf("C API: 24 concurrent engines on 8 threads, %d prepared requests and 24 raw-text parses, %d failures.\n",
	       prepared, failures);
	return failures ? 1 : 0;
}
