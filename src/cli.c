/*
 * cli.c - Command-line entry point and JSON request server.
 *
 * Loads runtime settings and supports one-shot and persistent requests.
 */

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#elif defined(__linux__)
#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
#endif
#include "mc.h"

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

static const char *env_or(const char *key, const char *fallback)
{
	const char *v = getenv(key);
	return v && *v ? v : fallback;
}

static char *beside_executable(const char *argv0, const char *relative)
{
	char *exe = NULL;
#ifdef _WIN32
	wchar_t path[32768];
	DWORD count = GetModuleFileNameW(NULL, path, 32768);
	if (count && count < 32768) {
		int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, NULL, 0, NULL,
					    NULL);
		exe = malloc((size_t)n);
		if (exe)
			WideCharToMultiByte(CP_UTF8, 0, path, -1, exe, n, NULL,
					    NULL);
	}
#elif defined(__linux__)
	char path[4096];
	ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
	if (n > 0)
		exe = slice(path, (size_t)n);
#endif
	if (!exe)
		exe = copy(argv0);
	char *slash = strrchr(exe, '/'), *backslash = strrchr(exe, '\\');
	if (backslash && (!slash || backslash > slash))
		slash = backslash;
	if (slash)
		slash[1] = 0;
	else
		exe[0] = 0;
	Buf b = { 0 };
	buf_put(&b, exe);
	buf_put(&b, relative);
	free(exe);
	return buf_take(&b);
}

static void usage(void)
{
	puts("mutatoc " MUTATOC_VERSION "\nUsage:\n"
	     "  mutatoc --ontology FILE --input-text TEXT [--json] [--live]\n"
	     "  mutatoc --ontology FILE --snapshot FILE\n"
	     "  mutatoc --serve\n  mutatoc --version\n\n"
	     "Raw text uses the original spaCy/LingPatLab preprocessing. Options:\n"
	     "  --python FILE         Python executable (MUTATOC_PYTHON, bundled runtime, then python)\n"
	     "  --spacy-worker FILE   Worker script (MUTATOC_SPACY_WORKER, default beside executable)\n"
	     "  --sparql-worker FILE  RDFLib query worker (MUTATOC_SPARQL_WORKER)\n"
	     "  --spacy-model MODEL   Model package/path (MUTATOC_SPACY_MODEL, default en_core_web_sm)\n"
	     "  --spacy-timeout MS    Startup/request deadline (default 120000)\n\n"
	     "--serve accepts one JSON request per line and retains the ontology and model.\n"
	     "--live uses the reference class-based extraction path.\n"
	     "--force-cache rebuilds from OWL; this runtime has no implicit disk cache.\n"
	     "--namespace is accepted for compatibility; RDF prefixes define IRIs.");
}

static int run(int argc, char **argv)
{
	const char *path = NULL, *text = NULL, *snapshot = NULL;
	const char *python = env_or("MUTATOC_PYTHON", NULL);
	const char *worker = env_or("MUTATOC_SPACY_WORKER", NULL);
	const char *sparql_worker = env_or("MUTATOC_SPARQL_WORKER", NULL);
	const char *model = env_or("MUTATOC_SPACY_MODEL", "en_core_web_sm");
	unsigned timeout = 0;
	int serve = 0, json = 0, live = 0, force = 0;
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
			json = 1;
		else if (!strcmp(a, "--live"))
			live = 1;
		else if (!strcmp(a, "--force-cache"))
			force = 1;
		else if (!strcmp(a, "--ontology") ||
			 !strcmp(a, "--input-text") ||
			 !strcmp(a, "--snapshot") ||
			 !strcmp(a, "--namespace") || !strcmp(a, "--python") ||
			 !strcmp(a, "--spacy-worker") ||
			 !strcmp(a, "--spacy-model") ||
			 !strcmp(a, "--spacy-timeout") ||
			 !strcmp(a, "--sparql-worker")) {
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
			else if (!strcmp(a, "--python"))
				python = argv[i];
			else if (!strcmp(a, "--spacy-worker"))
				worker = argv[i];
			else if (!strcmp(a, "--spacy-model"))
				model = argv[i];
			else if (!strcmp(a, "--sparql-worker"))
				sparql_worker = argv[i];
			else if (!strcmp(a, "--spacy-timeout")) {
				char *end;
				unsigned long value =
					strtoul(argv[i], &end, 10);
				if (!*argv[i] || *end || value > 3600000) {
					fputs("Invalid --spacy-timeout\n",
					      stderr);
					return 2;
				}
				timeout = (unsigned)value;
			}
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
	if (serve && (path || text || snapshot)) {
		fputs("--serve cannot be combined with one-shot options\n",
		      stderr);
		return 2;
	}
	mc_engine *e = mc_create();
	if (!e)
		return 1;
	char *discovered =
		worker ? NULL :
			 beside_executable(argv[0], "runtime/spacy_worker.py");
	char *discovered_sparql =
		sparql_worker ?
			NULL :
			beside_executable(argv[0], "runtime/sparql_worker.py");
	char *bundled = beside_executable(argv[0], "runtime/python/python.exe");
	if (!python) {
		FILE *candidate = mc_fopen(bundled, "rb");
		if (candidate) {
			fclose(candidate);
			python = bundled;
		} else
			python = "python";
	}
	mc_error err;
	if (!mc_use_spacy(e, python, worker ? worker : discovered, model,
			  timeout, &err) ||
	    !mc_use_sparql(e, python,
			   sparql_worker ? sparql_worker : discovered_sparql,
			   timeout, &err)) {
		fprintf(stderr, "%s\n", err.message);
		free(discovered);
		free(discovered_sparql);
		free(bundled);
		mc_destroy(e);
		return 2;
	}
	free(discovered);
	free(discovered_sparql);
	free(bundled);
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
			char *s = cJSON_PrintUnformatted(r);
			puts(s);
			free(s);
		} else
			puts(S(GET(r, "text")));
		DEL(r);
	}
	mc_destroy(e);
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
