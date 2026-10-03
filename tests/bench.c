/*
 * bench.c - Load and raw-text parse timings through the public API.
 *
 * Times ontology loading, the first parse after loading, and repeated parses
 * of a fixed document built from the parity corpus. Any leading --setup
 * requests run in every fresh engine before loading, so older builds that
 * need configuration can be measured with the same program.
 *
 * --check turns the benchmark into a gate: each median must stay under a
 * user-facing ceiling, wide enough for slow shared CI runners and tight
 * enough to fail an order-of-magnitude regression. CTest runs it as
 * `performance` in optimized, unsanitized builds.
 * craigtrim/mutatoc#1, craigtrim/mutatoc#2
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
				    "medicopilot", "acanames-20251028" };

/*
 * Ceilings for --check, on medians. Loads allow six times the time measured
 * when the gate was set (3, 67, 110 and 861 ms), with a 500 ms floor. Parses
 * of the 2,400-character document measured 9 to 26 ms; the out-of-process
 * tokenizer that 0.3.0 replaced took 122 to 248 ms warm and about 1.9 s for
 * the first parse, and about 211 MB across its two processes.
 */
static const double load_budget_ms[] = { 500, 500, 700, 6000 };
#define FIRST_PARSE_BUDGET_MS 300.0
#define PARSE_BUDGET_MS 150.0
#define PEAK_BUDGET_MB 128.0

static int within(const char *what, const char *ontology, double value,
		  double budget, const char *unit)
{
	int ok = value <= budget;
	printf("%-4s %-17s %-12s %8.1f %s (ceiling %.0f %s)\n",
	       ok ? "ok" : "OVER", ontology, what, value, unit, budget, unit);
	return ok;
}

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
			"usage: mutatoc_bench ROOT [--runs N] [--check] [--source-dir DIR] [--extension EXT] [--setup JSON]...\n");
		return 2;
	}
	const char *root = argv[1];
	const char *source_dir = NULL, *extension = ".owl";
	int runs = 5, check = 0, over = 0;
	const char *setup[16];
	int setups = 0;
	for (int i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "--check"))
			check = 1;
		else if (!strcmp(argv[i], "--runs") && i + 1 < argc)
			runs = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--source-dir") && i + 1 < argc)
			source_dir = argv[++i];
		else if (!strcmp(argv[i], "--extension") && i + 1 < argc)
			extension = argv[++i];
		else if (!strcmp(argv[i], "--setup") && i + 1 < argc &&
			 setups < 16)
			setup[setups++] = argv[++i];
		else {
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
			return 2;
		}
	}
	if (runs < 1)
		runs = 1;
	double *load = calloc((size_t)runs, sizeof(double)),
	       *first = calloc((size_t)runs, sizeof(double)),
	       *warm = calloc((size_t)runs * 4, sizeof(double));
	int failed = 0;
	for (size_t o = 0; o < sizeof(ontologies) / sizeof(*ontologies); o++) {
		char *owl = path_join(
			source_dir ? source_dir : root,
			source_dir ? "/" : "/tests/fixtures/ontologies/",
			ontologies[o]);
		char *doc = document(root, ontologies[o]);
		J *q = OBJ();
		PUT(q, "op", STR("load"));
		char *owl_path = path_join(owl, extension, "");
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
		if (!failed && check) {
			over += !within("load", ontologies[o],
					median(load, runs), load_budget_ms[o],
					"ms");
			over += !within("first parse", ontologies[o],
					median(first, runs),
					FIRST_PARSE_BUDGET_MS, "ms");
			over += !within("parse", ontologies[o],
					median(warm, runs * 4), PARSE_BUDGET_MS,
					"ms");
		} else if (!failed)
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
	if (check) {
		over += !within("peak memory", "all", peak_mb(), PEAK_BUDGET_MB,
				"MB");
		printf("Performance gate: %s.\n",
		       failed ? "a request failed" :
		       over   ? "a ceiling was exceeded" :
				"every median is under its ceiling");
	} else
		printf("{\"peak_working_set_mb\":%.1f}\n", peak_mb());
	free(load);
	free(first);
	free(warm);
	return failed || over;
}
