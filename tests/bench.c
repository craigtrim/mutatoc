/*
 * bench.c - Load and raw-text parse timings through the public API.
 *
 * Times ontology loading, the first parse after loading, and repeated parses
 * of a fixed document built from the parity corpus. Any leading --setup
 * requests run in every fresh engine before loading, so older builds that
 * need configuration can be measured with the same program.
 * craigtrim/mutatoc#1
 */

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#define _POSIX_C_SOURCE 200809L
#include <sys/resource.h>
#include <time.h>
#endif
#include "mc.h"

#define DOCUMENT_CHARS 2400

static const char *ontologies[] = { "animals-test", "econ-20160218",
				    "medicopilot", "courses-20251028" };

static double now_ms(void)
{
#ifdef _WIN32
	LARGE_INTEGER f, t;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&t);
	return (double)t.QuadPart * 1000.0 / (double)f.QuadPart;
#else
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
#endif
}

static double peak_mb(void)
{
#ifdef _WIN32
	PROCESS_MEMORY_COUNTERS c;
	return GetProcessMemoryInfo(GetCurrentProcess(), &c, sizeof(c)) ?
		       (double)c.PeakWorkingSetSize / 1048576.0 :
		       0;
#else
	struct rusage u;
	getrusage(RUSAGE_SELF, &u);
	return (double)u.ru_maxrss / 1024.0;
#endif
}

static int compare(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}

static double median(double *v, int n)
{
	qsort(v, (size_t)n, sizeof(*v), compare);
	return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

/* Returns 1 when the engine answered ok; failures are reported and fatal. */
static int request(mc_engine *e, const char *wire)
{
	mc_error err;
	char *r = mc_request(e, wire, &err);
	int ok = r && err.code == 0;
	if (!ok)
		fprintf(stderr, "request failed: %s\n%.200s\n", err.message,
			wire);
	mc_free(r);
	return ok;
}

static char *path_join(const char *root, const char *a, const char *b)
{
	Buf p = { 0 };
	buf_put(&p, root);
	buf_put(&p, a);
	buf_put(&p, b);
	return p.p;
}

/* A fixed document: the ontology's parity texts, cycled to the target size. */
static char *document(const char *root, const char *name)
{
	char *cases_name = path_join(name, ".cases.json", "");
	char *path = path_join(root, "/tests/fixtures/parity/", cases_name);
	mc_error err = { 0 };
	char *text = read_file(path, &err);
	J *cases = text ? json_parse(text, &err) : NULL;
	free(text);
	free(path);
	free(cases_name);
	Buf b = { 0 };
	while (cases && SIZE(cases) && b.n < DOCUMENT_CHARS) {
		EACH(c, cases) {
			if (b.n >= DOCUMENT_CHARS)
				break;
			if (b.n)
				buf_put(&b, ". ");
			buf_put(&b, S(GET(c, "text")));
		}
	}
	DEL(cases);
	return b.p;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr,
			"usage: mutatoc_bench ROOT [--runs N] [--setup JSON]...\n");
		return 2;
	}
	const char *root = argv[1];
	int runs = 5;
	const char *setup[16];
	int setups = 0;
	for (int i = 2; i + 1 < argc; i += 2) {
		if (!strcmp(argv[i], "--runs"))
			runs = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "--setup") && setups < 16)
			setup[setups++] = argv[i + 1];
	}
	if (runs < 1)
		runs = 1;
	double *load = calloc((size_t)runs, sizeof(double)),
	       *first = calloc((size_t)runs, sizeof(double)),
	       *warm = calloc((size_t)runs * 4, sizeof(double));
	int failed = 0;
	for (size_t o = 0; o < sizeof(ontologies) / sizeof(*ontologies); o++) {
		char *owl = path_join(root, "/tests/fixtures/ontologies/",
				      ontologies[o]);
		char *doc = document(root, ontologies[o]);
		J *q = OBJ();
		PUT(q, "op", STR("load"));
		char *owl_path = path_join(owl, ".owl", "");
		PUT(q, "path", STR(owl_path));
		PUT(q, "class_based", BOOL(1));
		PUT(q, "interface", STR("data"));
		char *load_wire = cJSON_PrintUnformatted(q);
		DEL(q);
		q = OBJ();
		PUT(q, "op", STR("parse"));
		PUT(q, "text", STR(doc ? doc : ""));
		char *parse_wire = cJSON_PrintUnformatted(q);
		DEL(q);
		for (int r = 0; r < runs && !failed; r++) {
			mc_engine *e = mc_create();
			for (int s = 0; s < setups && !failed; s++)
				failed |= !request(e, setup[s]);
			double t0 = now_ms();
			failed |= !request(e, load_wire);
			double t1 = now_ms();
			failed |= !request(e, parse_wire);
			double t2 = now_ms();
			load[r] = t1 - t0;
			first[r] = t2 - t1;
			for (int w = 0; w < 4 && !failed; w++) {
				double t3 = now_ms();
				failed |= !request(e, parse_wire);
				warm[r * 4 + w] = now_ms() - t3;
			}
			mc_destroy(e);
		}
		if (!failed)
			printf("{\"ontology\":\"%s\",\"document_chars\":%zu,\"runs\":%d,"
			       "\"load_ms\":%.1f,\"first_parse_ms\":%.1f,\"parse_ms\":%.2f}\n",
			       ontologies[o], doc ? strlen(doc) : 0, runs,
			       median(load, runs), median(first, runs),
			       median(warm, runs * 4));
		free(owl);
		free(owl_path);
		free(doc);
		free(load_wire);
		free(parse_wire);
		if (failed)
			break;
	}
	printf("{\"peak_working_set_mb\":%.1f}\n", peak_mb());
	free(load);
	free(first);
	free(warm);
	return failed;
}
