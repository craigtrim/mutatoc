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
		/* Replay the reference's list convention, with repaired plus rules. */
		if (!strcmp(S(GET(q, "op")), "load"))
			set(q, "comma_lists", BOOL(1));
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

/*
 * Applies one file of authored corrections to the trace's blobs. A change
 * replaces a key whose recorded value is "before", adds a key the recording
 * lacks when it has no "before", or inserts a value into a list blob between
 * two recorded neighbours. Each change checks the recording first, so a
 * correction can never land on a value it was not written for.
 * Plus-literal corrections: craigtrim/mutatoc#28
 */
static int correct(J *trace, const char *root, const char *relative,
		   const char *label)
{
	J *corrections = test_read_json(root, relative);
	J *changes = GET(corrections, "changes");
	if (!test_require_cases(changes, label)) {
		DEL(corrections);
		return 0;
	}
	int ok = 1;
	EACH(change, changes) {
		J *blob = GET(GET(trace, "blobs"), S(GET(change, "blob")));
		J *insert = GET(change, "insert");
		if (insert) {
			int at = (int)GET(change, "index")->valuedouble;
			J *between = GET(change, "between");
			ok = cJSON_IsArray(blob) && at > 0 && at < SIZE(blob) &&
			     cJSON_Compare(AT(blob, at - 1), AT(between, 0), 1) &&
			     cJSON_Compare(AT(blob, at), AT(between, 1), 1);
			if (ok)
				cJSON_InsertItemInArray(blob, at, DUP(insert));
		} else {
			const char *key = S(GET(change, "key"));
			J *before = GET(change, "before");
			ok = before ? cJSON_Compare(GET(blob, key), before, 1) :
				      cJSON_IsObject(blob) && !GET(blob, key);
			if (ok)
				set(blob, key, DUP(GET(change, "after")));
		}
		if (!ok) {
			fprintf(stderr, "%s: correction no longer matches reference: %s %s\n",
				relative, S(GET(change, "blob")),
				insert ? S(insert) : S(GET(change, "key")));
			break;
		}
	}
	DEL(corrections);
	return ok;
}

int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	J *trace =
		test_read_json(argv[1], "tests/fixtures/upstream/trace.json");
	if (!test_require_cases(GET(trace, "sessions"), "upstream trace"))
		return 2;
	/* Keep the frozen trace and check each authored correction's old value.
	 * Plus-literal corrections build on the comma-list ones. */
	if (!correct(trace, argv[1], "tests/fixtures/upstream/comma-lists.json",
		     "comma-list span corrections") ||
	    !correct(trace, argv[1], "tests/fixtures/upstream/plus-literals.json",
		     "plus-literal corrections")) {
		DEL(trace);
		return 2;
	}
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
