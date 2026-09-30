/*
 * test_upstream.c - Replay of Mutato's upstream test suite.
 *
 * Every request Mutato's 961 upstream tests sent to the engine was recorded
 * with its response (tests/fixtures/upstream/trace.json). Each recorded
 * session replays in a fresh engine. Responses must match structurally;
 * parse responses must match on their entities and rendered text.
 * craigtrim/mutatoc#1
 */

#include "testlib.h"

/* Deep copy with {"$ref": id} blobs resolved and ${FIXTURES} expanded. */
static J *resolve(const J *v, const J *blobs, const char *fixtures)
{
	if (cJSON_IsObject(v)) {
		J *ref = GET(v, "$ref");
		if (ref && SIZE(v) == 1)
			return resolve(GET(blobs, S(ref)), blobs, fixtures);
		J *out = OBJ();
		EACH(k, v)
			PUT(out, k->string, resolve(k, blobs, fixtures));
		return out;
	}
	if (cJSON_IsArray(v)) {
		J *out = ARR();
		EACH(x, v)
			ADD(out, resolve(x, blobs, fixtures));
		return out;
	}
	if (cJSON_IsString(v) && !strncmp(S(v), "${FIXTURES}", 11)) {
		Buf b = { 0 };
		buf_put(&b, fixtures);
		buf_put(&b, S(v) + 11);
		J *out = STR(b.p);
		free(b.p);
		return out;
	}
	return DUP(v);
}

typedef struct {
	J **sessions;
	const J *blobs;
	const char *fixtures;
	int *calls, *failed;
} Replay;

/* Sessions are independent engines, so they replay on parallel threads. */
static void replay_session(void *ctx, int index)
{
	Replay *run = ctx;
	mc_engine *e = mc_create();
	EACH(call, run->sessions[index]) {
		J *q = resolve(GET(call, "request"), run->blobs, run->fixtures);
		J *r = test_request(e, q);
		J *expected = GET(call, "parse") ?
				      DUP(GET(call, "parse")) :
				      resolve(GET(call, "response"), run->blobs,
					      run->fixtures);
		J *actual = NULL;
		if (GET(call, "parse"))
			actual = cJSON_IsTrue(GET(r, "ok")) ?
					 test_projection(GET(r, "result")) :
					 DUP(r);
		run->calls[index]++;
		if (!cJSON_Compare(actual ? actual : r, expected, 1) &&
		    run->failed[index]++ < 2) {
			char label[512];
			snprintf(label, sizeof(label), "%s | op=%s method=%s",
				 S(GET(call, "test")), S(GET(q, "op")),
				 S(GET(q, "method")));
			test_print_diff(label, expected, actual ? actual : r);
		}
		DEL(actual);
		DEL(expected);
		DEL(r);
		DEL(q);
	}
	mc_destroy(e);
}

int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	J *trace =
		test_read_json(argv[1], "tests/fixtures/upstream/trace.json");
	if (!test_require_cases(GET(trace, "sessions"), "upstream trace"))
		return 2;
	char *fixtures = test_path(argv[1], "tests/fixtures");
	int count = SIZE(GET(trace, "sessions"));
	Replay run = { calloc((size_t)count, sizeof(J *)), GET(trace, "blobs"),
		       fixtures, calloc((size_t)count, sizeof(int)),
		       calloc((size_t)count, sizeof(int)) };
	int i = 0;
	EACH(session, GET(trace, "sessions"))
		run.sessions[i++] = session;
	test_parallel(8, count, replay_session, &run);
	int total = 0, failed = 0;
	for (i = 0; i < count; i++) {
		total += run.calls[i];
		failed += run.failed[i];
	}
	printf("Mutato upstream replay: %d/%d recorded calls matched across %d sessions.\n",
	       total - failed, total, count);
	free(run.sessions);
	free(run.calls);
	free(run.failed);
	free(fixtures);
	DEL(trace);
	return failed ? 1 : 0;
}
