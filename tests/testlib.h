/*
 * testlib.h - Shared helpers for the native test drivers.
 *
 * Reads JSON fixtures, sends requests through the public API, and projects
 * parse results onto the entities they contain.
 * craigtrim/mutatoc#1
 */

#ifndef MUTATOC_TESTLIB_H
#define MUTATOC_TESTLIB_H
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif
#include "mc.h"

typedef struct {
	void (*fn)(void *ctx, int index);
	void *ctx;
	int worker, workers, count;
} TestWork;

#ifdef _WIN32
static DWORD WINAPI test_worker(LPVOID arg)
#else
static void *test_worker(void *arg)
#endif
{
	TestWork *w = arg;
	for (int i = w->worker; i < w->count; i += w->workers)
		w->fn(w->ctx, i);
#ifdef _WIN32
	return 0;
#else
	return NULL;
#endif
}

/* Calls fn(ctx, i) for every i below count, spread over worker threads. */
static void test_parallel(int workers, int count,
			  void (*fn)(void *ctx, int index), void *ctx)
{
	TestWork *work = calloc((size_t)workers, sizeof(*work));
#ifdef _WIN32
	HANDLE *threads = calloc((size_t)workers, sizeof(*threads));
#else
	pthread_t *threads = calloc((size_t)workers, sizeof(*threads));
#endif
	for (int i = 0; i < workers; i++) {
		work[i] = (TestWork){ fn, ctx, i, workers, count };
#ifdef _WIN32
		threads[i] =
			CreateThread(NULL, 0, test_worker, &work[i], 0, NULL);
#else
		pthread_create(&threads[i], NULL, test_worker, &work[i]);
#endif
	}
	for (int i = 0; i < workers; i++) {
#ifdef _WIN32
		WaitForSingleObject(threads[i], INFINITE);
		CloseHandle(threads[i]);
#else
		pthread_join(threads[i], NULL);
#endif
	}
	free(threads);
	free(work);
}

static char *test_path(const char *root, const char *relative)
{
	Buf b = { 0 };
	buf_put(&b, root);
	buf_put(&b, "/");
	buf_put(&b, relative);
	return buf_take(&b);
}

static J *test_read_json(const char *root, const char *relative)
{
	char *path = test_path(root, relative);
	mc_error err = { 0 };
	char *text = read_file(path, &err);
	J *out = text ? json_parse(text, &err) : NULL;
	if (!out)
		fprintf(stderr, "%s: %s\n", path, err.message);
	free(text);
	free(path);
	return out;
}

/*
 * A reference fixture that is missing, malformed or empty must fail its
 * suite rather than let it pass with no cases. Returns 1 for a nonempty array.
 */
static int test_require_cases(const J *cases, const char *label)
{
	if (cJSON_IsArray(cases) && SIZE(cases) > 0)
		return 1;
	fprintf(stderr, "FAIL %s: reference cases missing or empty\n", label);
	return 0;
}

/* Sends one request and returns the whole {"ok":...} response. */
static J *test_request(mc_engine *e, const J *q)
{
	char *wire = cJSON_PrintUnformatted(q);
	mc_error err;
	char *r = mc_request(e, wire, &err);
	free(wire);
	J *out = r ? cJSON_Parse(r) : NULL;
	mc_free(r);
	return out;
}

/* Sends a request given as JSON text and returns its result, or NULL. */
static J *test_call(mc_engine *e, const char *wire)
{
	mc_error err;
	char *r = mc_request(e, wire, &err);
	J *out = r ? cJSON_Parse(r) : NULL;
	mc_free(r);
	if (!cJSON_IsTrue(GET(out, "ok"))) {
		fprintf(stderr, "request failed: %.300s\n%.300s\n", wire,
			out ? S(GET(GET(out, "error"), "message")) : "");
		DEL(out);
		return NULL;
	}
	J *result = cJSON_DetachItemFromObjectCaseSensitive(out, "result");
	DEL(out);
	return result;
}

/* The matched entities of a parse result, plus its rendered text. */
static J *test_projection(const J *result)
{
	J *out = OBJ(), *entities = ARR();
	EACH(t, GET(result, "tokens")) {
		J *swaps = GET(t, "swaps");
		if (!swaps)
			continue;
		J *e = OBJ();
		PUT(e, "canon", DUP(GET(swaps, "canon")));
		PUT(e, "type", DUP(GET(swaps, "type")));
		PUT(e, "ner", GET(t, "ner") ? DUP(GET(t, "ner")) : NIL());
		PUT(e, "x", DUP(GET(t, "x")));
		PUT(e, "y", DUP(GET(t, "y")));
		PUT(e, "text", DUP(GET(t, "text")));
		PUT(e, "confidence", DUP(GET(swaps, "confidence")));
		ADD(entities, e);
	}
	PUT(out, "entities", entities);
	PUT(out, "text", DUP(GET(result, "text")));
	return out;
}

static void test_print_diff(const char *label, const J *expected,
			    const J *actual)
{
	char *a = cJSON_PrintUnformatted(expected),
	     *b = cJSON_PrintUnformatted(actual);
	fprintf(stderr, "%s\n  expected: %.600s\n  actual:   %.600s\n", label,
		a ? a : "null", b ? b : "null");
	free(a);
	free(b);
}
#endif
