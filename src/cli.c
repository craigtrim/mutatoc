/*
 * cli.c - Command-line entry point and JSON request server.
 *
 * Supports one-shot and persistent requests; everything runs in process.
 * craigtrim/mutatoc#1
 */

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
#include "mc.h"
#include <time.h>

static double now_seconds(void)
{
	struct timespec t;
	if (!timespec_get(&t, TIME_UTC))
		return 0;
	return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* Written to stderr so stdout stays valid JSON when it is piped. */
static void print_elapsed(double seconds)
{
	fflush(stdout);
	if (seconds < 1)
		fprintf(stderr, "Elapsed: %.1f ms\n", seconds * 1000);
	else if (seconds < 60)
		fprintf(stderr, "Elapsed: %.2f s\n", seconds);
	else {
		int minutes = (int)(seconds / 60);
		fprintf(stderr, "Elapsed: %d min %.1f s\n", minutes,
			seconds - minutes * 60);
	}
}

static char *read_line(FILE *f)
{
	Buf b = { 0 };
	int c;
	while ((c = fgetc(f)) != EOF && c != '\n') {
		if (b.n >= 256u * 1024u * 1024u) {
			free(b.p);
			fputs("Request exceeds 256 MiB\n", stderr);
			return NULL;
		}
		char ch = (char)c;
		buf_add(&b, &ch, 1);
	}
	if (!b.n && c == EOF) {
		free(b.p);
		return NULL;
	}
	return buf_take(&b);
}

static J *call(mc_engine *e, J *q)
{
	char *s = cJSON_PrintUnformatted(q);
	mc_error err;
	char *r = mc_request(e, s, &err);
	free(s);
	DEL(q);
	J *j = r ? cJSON_Parse(r) : NULL;
	mc_free(r);
	if (!j || !cJSON_IsTrue(GET(j, "ok"))) {
		fprintf(stderr, "%s\n",
			j ? S(GET(GET(j, "error"), "message")) :
			    "Out of memory");
		DEL(j);
		return NULL;
	}
	J *out = cJSON_DetachItemFromObjectCaseSensitive(j, "result");
	DEL(j);
	return out;
}

static void indent(Buf *b, int depth)
{
	for (int i = 0; i < depth; i++)
		buf_put(b, "  ");
}

/*
 * One member or element per line, indented two spaces per level. Scalars and
 * keys go through cJSON's printer, which keeps escaping and large integers
 * exact.
 */
static void pretty_value(Buf *b, const J *v, int depth)
{
	if (!cJSON_IsArray(v) && !cJSON_IsObject(v)) {
		char *s = cJSON_PrintUnformatted(v);
		buf_put(b, s ? s : "null");
		free(s);
		return;
	}
	int object = cJSON_IsObject(v);
	if (!v->child) {
		buf_put(b, object ? "{}" : "[]");
		return;
	}
	buf_put(b, object ? "{\n" : "[\n");
	EACH(item, v) {
		indent(b, depth + 1);
		if (object) {
			J *key = STR(item->string);
			char *s = cJSON_PrintUnformatted(key);
			buf_put(b, s ? s : "\"\"");
			buf_put(b, ": ");
			free(s);
			DEL(key);
		}
		pretty_value(b, item, depth + 1);
		buf_put(b, item->next ? ",\n" : "\n");
	}
	indent(b, depth);
	buf_put(b, object ? "}" : "]");
}

static char *pretty(const J *v)
{
	Buf b = { 0 };
	pretty_value(&b, v, 0);
	return buf_take(&b);
}

static void usage(void)
{
	puts("mutatoc " MUTATOC_VERSION "\nUsage:\n"
	     "  mutatoc --ontology FILE --input-text TEXT [--json | --jsonf] [--live]\n"
	     "          [--stopwatch]\n"
	     "  mutatoc --ontology FILE --snapshot FILE [--stopwatch]\n"
	     "  mutatoc --serve\n  mutatoc --version\n\n"
	     "--json prints the full result as compact JSON; --jsonf pretty-prints it.\n"
	     "--stopwatch prints the total run time to stderr after the output.\n"
	     "--serve accepts one JSON request per line and retains the ontology.\n"
	     "--live uses the reference class-based extraction path.\n"
	     "--force-cache rebuilds from OWL; this runtime has no implicit disk cache.\n"
	     "--namespace is accepted for compatibility; RDF prefixes define IRIs.");
}

static int run(int argc, char **argv)
{
	double start = now_seconds();
	const char *path = NULL, *text = NULL, *snapshot = NULL;
	int serve = 0, json = 0, live = 0, force = 0, stopwatch = 0;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
			usage();
			return 0;
		}
		if (!strcmp(a, "--version")) {
			puts(MUTATOC_VERSION);
			return 0;
		}
		if (!strcmp(a, "--serve"))
			serve = 1;
		else if (!strcmp(a, "--json"))
			json = json ? json : 1;
		else if (!strcmp(a, "--jsonf"))
			json = 2;
		else if (!strcmp(a, "--live"))
			live = 1;
		else if (!strcmp(a, "--force-cache"))
			force = 1;
		else if (!strcmp(a, "--stopwatch"))
			stopwatch = 1;
		else if (!strcmp(a, "--ontology") ||
			 !strcmp(a, "--input-text") ||
			 !strcmp(a, "--snapshot") ||
			 !strcmp(a, "--namespace")) {
			if (++i == argc) {
				fprintf(stderr, "Missing value for %s\n", a);
				return 2;
			}
			if (!strcmp(a, "--ontology"))
				path = argv[i];
			else if (!strcmp(a, "--input-text"))
				text = argv[i];
			else if (!strcmp(a, "--snapshot"))
				snapshot = argv[i];
		} else if (strcmp(a, "parse")) {
			fprintf(stderr, "Unknown option: %s\n", a);
			return 2;
		}
	}
	if (live && force) {
		fputs("--live and --force-cache are mutually exclusive\n",
		      stderr);
		return 2;
	}
	if (serve && (path || text || snapshot || stopwatch)) {
		fputs("--serve cannot be combined with one-shot options\n",
		      stderr);
		return 2;
	}
	mc_engine *e = mc_create();
	if (!e)
		return 1;
	mc_error err;
	if (serve) {
		char *q;
		while ((q = read_line(stdin))) {
			char *r = mc_request(e, q, &err);
			free(q);
			if (!r) {
				mc_destroy(e);
				return 1;
			}
			puts(r);
			fflush(stdout);
			mc_free(r);
		}
		mc_destroy(e);
		return ferror(stdin) ? 1 : 0;
	}
	if (!path || (!text && !snapshot)) {
		usage();
		mc_destroy(e);
		return argc == 1 ? 0 : 2;
	}
	J *q = OBJ();
	PUT(q, "op", STR("load"));
	PUT(q, "path", STR(path));
	PUT(q, "class_based", BOOL(live));
	if (live)
		PUT(q, "interface", STR("data"));
	J *r = call(e, q);
	if (!r) {
		mc_destroy(e);
		return 1;
	}
	DEL(r);
	if (snapshot) {
		q = OBJ();
		PUT(q, "op", STR("snapshot"));
		r = call(e, q);
		if (!r) {
			mc_destroy(e);
			return 1;
		}
		char *s = cJSON_Print(r);
		DEL(r);
		FILE *f = mc_fopen(snapshot, "wb");
		if (!f) {
			fprintf(stderr, "Cannot write snapshot: %s\n",
				snapshot);
			free(s);
			mc_destroy(e);
			return 1;
		}
		size_t n = strlen(s);
		int ok = fwrite(s, 1, n, f) == n;
		if (fclose(f))
			ok = 0;
		free(s);
		if (!ok) {
			mc_destroy(e);
			return 1;
		}
	}
	if (text) {
		q = OBJ();
		PUT(q, "op", STR("parse"));
		PUT(q, "text", STR(text));
		r = call(e, q);
		if (!r) {
			mc_destroy(e);
			return 1;
		}
		if (json) {
			char *s = json == 2 ? pretty(r) :
					      cJSON_PrintUnformatted(r);
			puts(s ? s : "");
			free(s);
		} else
			puts(S(GET(r, "text")));
		DEL(r);
	}
	mc_destroy(e);
	if (stopwatch)
		print_elapsed(now_seconds() - start);
	return 0;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
	int wide_argc;
	wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
	if (!wide)
		return 1;
	char **utf8 = calloc((size_t)wide_argc, sizeof(char *));
	if (!utf8) {
		LocalFree(wide);
		return 1;
	}
	for (int i = 0; i < wide_argc; i++) {
		int n = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, NULL, 0,
					    NULL, NULL);
		utf8[i] = malloc((size_t)n);
		if (!utf8[i]) {
			for (int j = 0; j < i; j++)
				free(utf8[j]);
			free(utf8);
			LocalFree(wide);
			return 1;
		}
		WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, utf8[i], n, NULL,
				    NULL);
	}
	int code = run(wide_argc, utf8);
	for (int i = 0; i < wide_argc; i++)
		free(utf8[i]);
	free(utf8);
	LocalFree(wide);
	(void)argc;
	(void)argv;
	return code;
#else
	return run(argc, argv);
#endif
}
