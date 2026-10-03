/*
 * test_sources.c - Graph, finder, and matching parity across native readers.
 *
 * Converts fixtures only inside the test harness. Runtime readers construct
 * their graphs directly and keep the previous engine after failed loads.
 */
#include "testlib.h"
#include "source_testlib.h"

static int failed, total, graphs, queries, parses;
static const char *root;
#define CHECK(value)                                                  \
	do {                                                          \
		total++;                                              \
		if (!(value)) {                                       \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, \
				__LINE__, #value);                    \
			failed++;                                     \
		}                                                     \
	} while (0)

static int text_equal(const char *a, const char *b)
{
	return (!a && !b) || (a && b && !strcmp(a, b));
}

static int term_equal(const Term *a, const Term *b)
{
	return a->kind == b->kind && text_equal(a->value, b->value) &&
	       text_equal(a->datatype, b->datatype) &&
	       text_equal(a->language, b->language);
}

static int graph_equal(Graph *a, Graph *b)
{
	if (!a || !b || a->n != b->n ||
	    !cJSON_Compare(a->prefixes, b->prefixes, 1))
		return 0;
	for (size_t i = 0; i < a->n; i++)
		if (!term_equal(&a->ts[i].s, &b->ts[i].s) ||
		    !term_equal(&a->ts[i].p, &b->ts[i].p) ||
		    !term_equal(&a->ts[i].o, &b->ts[i].o))
			return 0;
	return 1;
}

static J *load(mc_engine *engine, const char *path, const char *content,
	       const char *format, const char *interface, int class_based)
{
	J *request = OBJ();
	PUT(request, "op", STR("load"));
	PUT(request, "name", STR("source"));
	if (path)
		PUT(request, "path", STR(path));
	if (content)
		PUT(request, "content", STR(content));
	if (format)
		PUT(request, "format", STR(format));
	PUT(request, "interface", STR(interface));
	PUT(request, "class_based", BOOL(class_based));
	J *response = test_request(engine, request);
	DEL(request);
	return response;
}

static void same_request(mc_engine *a, mc_engine *b, const J *request)
{
	J *left = test_request(a, request), *right = test_request(b, request);
	int same = cJSON_Compare(left, right, 1);
	if (!same)
		test_print_diff(S(GET(request, "method")), left, right);
	CHECK(same);
	DEL(left);
	DEL(right);
}

static void roundtrip(Graph *graph)
{
	for (int lines = 0; lines < 2; lines++) {
		char *wire = source_test_encode(graph, lines);
		mc_error error = { 0 };
		J *snapshot = NULL;
		Graph *read = source_read(wire, lines ? "jsonl" : "json", "",
					  &snapshot, &error);
		if (error.code)
			fprintf(stderr, "Source parse: %s at %zu:%zu\n",
				error.message, error.line, error.column);
		CHECK(!error.code && !snapshot && graph_equal(graph, read));
		rdf_free(read);
		DEL(snapshot);
		free(wire);
		graphs++;
	}
}

static void graph_fixtures(void)
{
	J *manifest = test_read_json(root, "tests/fixtures/rdf-graphs.json");
	CHECK(test_require_cases(GET(manifest, "graphs"), "rdf-graphs"));
	EACH(row, GET(manifest, "graphs")) {
		char *folder = test_path(root, "tests/fixtures/ontologies");
		char *path = test_path(folder, S(GET(row, "file")));
		free(folder);
		mc_error error = { 0 };
		char *text = read_file(path, &error), *base = file_uri(path);
		Graph *graph = text ? rdf_parse(text, base, &error) : NULL;
		CHECK(graph && !error.code);
		if (graph)
			roundtrip(graph);
		rdf_free(graph);
		free(text);
		free(base);
		free(path);
	}
	DEL(manifest);
	manifest = test_read_json(root, "tests/fixtures/w3c-turtle.json");
	CHECK(test_require_cases(GET(manifest, "tests"), "w3c-turtle"));
	EACH(row, GET(manifest, "tests")) {
		if (strncmp(S(GET(row, "type")), "negative", 8) == 0 ||
		    !GET(row, "expected"))
			continue;
		char *folder = test_path(root, "tests/w3c-turtle");
		char *path = test_path(folder, S(GET(row, "name")));
		free(folder);
		mc_error error = { 0 };
		char *text = read_file(path, &error);
		char *base = uri_resolve(S(GET(manifest, "base")),
					 S(GET(row, "name")));
		Graph *graph = text ? rdf_parse(text, base, &error) : NULL;
		CHECK(graph && !error.code);
		if (graph)
			roundtrip(graph);
		rdf_free(graph);
		free(text);
		free(base);
		free(path);
	}
	DEL(manifest);
}

static void finder_parity(void)
{
	J *rows = test_read_json(root, "tests/fixtures/api/queries.json");
	CHECK(test_require_cases(rows, "finder queries"));
	for (int lines = 0; lines < 2; lines++) {
		mc_engine *a = mc_create(), *b = mc_create();
		char *previous = NULL;
		EACH(row, rows) {
			const char *name = S(GET(row, "fixture"));
			const J *request = GET(row, "request");
			const char *interface = S(GET(request, "interface"));
			Buf key = { 0 };
			buf_put(&key, name);
			buf_put(&key, ":");
			buf_put(&key, interface);
			if (!previous || strcmp(previous, key.p)) {
				free(previous);
				previous = copy(key.p);
				Buf path = { 0 };
				buf_put(&path, root);
				buf_put(&path,
					!strcmp(name, "animals-test") ?
						"/tests/fixtures/ontologies/" :
						"/tests/fixtures/api/");
				buf_put(&path, name);
				buf_put(&path, ".owl");
				J *response = load(a, path.p, NULL, NULL,
						   interface, 1);
				CHECK(cJSON_IsTrue(GET(response, "ok")));
				DEL(response);
				if (!a->graph) {
					free(path.p);
					free(key.p);
					break;
				}
				char *wire =
					source_test_encode(a->graph, lines);
				response = load(b, path.p, wire,
						lines ? "jsonl" : "json",
						interface, 1);
				CHECK(cJSON_IsTrue(GET(response, "ok")));
				CHECK(graph_equal(a->graph, b->graph));
				DEL(response);
				free(wire);
				free(path.p);
			}
			free(key.p);
			same_request(a, b, request);
			queries++;
		}
		free(previous);
		mc_destroy(a);
		mc_destroy(b);
	}
	DEL(rows);
}

static void matching_parity(void)
{
	J *manifest = test_read_json(root, "tests/fixtures/parity/index.json");
	CHECK(test_require_cases(manifest, "matching corpus"));
	EACH(row, manifest) {
		const char *name = S(GET(row, "name"));
		Buf path = { 0 }, cases_path = { 0 };
		buf_put(&path, root);
		buf_put(&path, "/tests/fixtures/ontologies/");
		buf_put(&path, name);
		buf_put(&path, ".owl");
		buf_put(&cases_path, "tests/fixtures/parity/");
		buf_put(&cases_path, S(GET(row, "cases")));
		J *cases = test_read_json(root, cases_path.p);
		mc_engine *a = mc_create(), *b = mc_create();
		J *response = load(a, path.p, NULL, NULL, "", 0);
		CHECK(cJSON_IsTrue(GET(response, "ok")));
		DEL(response);
		for (int lines = 0; a->graph && lines < 2; lines++) {
			char *wire = source_test_encode(a->graph, lines);
			response = load(b, path.p, wire, NULL, "", 0);
			CHECK(cJSON_IsTrue(GET(response, "ok")));
			CHECK(cJSON_Compare(a->snapshot, b->snapshot, 1));
			DEL(response);
			free(wire);
			EACH(test, cases) {
				J *request = OBJ();
				PUT(request, "op", STR("parse"));
				PUT(request, "text", DUP(GET(test, "text")));
				PUT(request, "ctr", DUP(GET(test, "ctr")));
				same_request(a, b, request);
				set(request, "op", STR("parse_tokens"));
				PUT(request, "tokens",
				    DUP(GET(test, "tokens")));
				same_request(a, b, request);
				parses += 2;
				DEL(request);
			}
		}
		mc_destroy(a);
		mc_destroy(b);
		DEL(cases);
		free(path.p);
		free(cases_path.p);
	}
	DEL(manifest);
}

static void authored_and_errors(void)
{
	const char *turtle =
		"@prefix : <http://test/#> . @prefix rdfs: <" RDFS "> . "
		":Dog rdfs:label \"Dog\"; rdfs:seeAlso \"hound + collar\"; "
		"rdfs:subClassOf :Animal . :Dog :note \"001\", \"yes\", \"line\\nbreak\", "
		"\"nul\\u0000value\"; :count \"001\"^^<" XSD "integer> .";
	const char *json =
		"[{\"namespace\":\"http://test/#\"},{\"id\":\"Dog\","
		"\"label\":\"Dog\",\"spans\":[\"hound + collar\"],\"parents\":[\"Animal\"],"
		"\"properties\":{\":note\":[\"001\",\"yes\",\"line\\nbreak\",\"nul\\u0000value\"],"
		"\":count\":{\"value\":\"001\",\"datatype\":\"xsd:integer\"}}}]";
	mc_engine *a = mc_create(), *b = mc_create();
	J *response = load(a, NULL, turtle, "turtle", "data", 1);
	CHECK(cJSON_IsTrue(GET(response, "ok")));
	DEL(response);
	response = load(b, NULL, json, "json", "data", 1);
	CHECK(cJSON_IsTrue(GET(response, "ok")));
	CHECK(graph_equal(a->graph, b->graph));
	DEL(response);
	const char *invalid[] = {
		"[",
		"[{}]",
		"[{\"id\":\"Dog\",\"label\":\"A\",\"label\":\"B\"}]",
		"[{\"id\":\"Dog\",\"typo\":\"A\"}]",
		"[{\"id\":\"Dog\",\"label\":null}]",
		"[{\"id\":\"Dog\",\"label\":7}]",
		"[{\"id\":\"Dog\",\"parents\":[[\"A\"]]}]",
		"[{\"id\":\"Dog\",\"label\":{\"value\":\"a\",\"datatype\":\"xsd:integer\",\"language\":\"en\"}}]",
		"[{\"id\":\"Dog\",\"label\":{\"value\":\"a\",\"kind\":\"other\"}}]",
		"[{\"id\":\"Dog\",\"label\":{\"value\":\"a\",\"language\":\"en-\"}}]",
		"[{\"id\":\"Dog\",\"parents\":[{\"blank\":\"\"}]}]",
		"[{\"id\":\"Dog\",\"label\":{\"value\":\"a\",\"value\":\"b\"}}]",
		"[{\"id\":\"Dog\",\"properties\":{\"p\":\"a\",\"p\":\"b\"}}]",
		"[{\"format\":\"mutatoc/99\"}]",
		"[{\"namespace\":false}]",
		"[{\"namespace\":\"http://test/\",\"prefixes\":{\"\":\"http://other/\"}}]",
		"[{\"subject\":{\"value\":\"literal\"},\"predicate\":\"p\",\"object\":\"v\"}]",
		"[{\"subject\":\"s\",\"predicate\":{\"blank\":\"p\"},\"object\":\"v\"}]",
		"[{\"id\":\"Dog\",\"label\":\"cat\"},]",
		"[] trailing",
		"[{\"id\":\"Dog\",\"label\":\"\\uD800\"}]",
		"[{\"id\":\"Dog\",\"label\":\"line\nbreak\"}]",
		"[{\"id\":\"Dog\",\"label\":\"raw\ttab\"}]",
		"[{\"id\":\"Dog\",\v\"label\":\"dog\"}]",
		"[{\"id\":\"Dog\",\"label\":\"cat\"},{\"id\":\"broken\",\"label\":false}]",
		"{\"id\":\"Dog\"}{\"id\":\"Cat\"}",
	};
	J *check = OBJ();
	PUT(check, "op", STR("parse"));
	PUT(check, "text", STR("Dog"));
	J *before = test_request(b, check);
	for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
		response = load(b, NULL, invalid[i], NULL, "", 0);
		if (!cJSON_IsFalse(GET(response, "ok")))
			fprintf(stderr, "Accepted invalid source: %s\n",
				invalid[i]);
		CHECK(cJSON_IsFalse(GET(response, "ok")));
		CHECK(GET(GET(response, "error"), "line") != NULL);
		DEL(response);
		response = test_request(b, check);
		CHECK(cJSON_Compare(before, response, 1));
		DEL(response);
	}
	DEL(before);
	DEL(check);
	const char *cycle =
		"[{\"subject\":{\"blank\":\"a\"},\"predicate\":\"http://test/p\",\"object\":{\"blank\":\"a\"}}]";
	J *request = OBJ();
	PUT(request, "op", STR("load"));
	PUT(request, "content", STR(cycle));
	PUT(request, "graph_only", BOOL(1));
	response = test_request(b, request);
	CHECK(cJSON_IsTrue(GET(response, "ok")) && b->graph &&
	      b->graph->n == 1);
	DEL(response);
	set(request, "op", STR("snapshot"));
	response = test_request(b, request);
	CHECK(cJSON_IsFalse(GET(response, "ok")));
	DEL(response);
	DEL(request);
	char *path = test_path(root, "tests/fixtures/api/source.json");
	response = load(b, path, NULL, NULL, "", 0);
	CHECK(cJSON_IsTrue(GET(response, "ok")) && b->graph &&
	      b->graph->n == 41);
	DEL(response);
	free(path);
	mc_destroy(a);
	mc_destroy(b);
}

static void stages_parity(void)
{
	char *path = test_path(root, "tests/fixtures/api/proper.owl");
	mc_engine *a = mc_create(), *b = mc_create();
	J *response = load(a, path, NULL, NULL, "data", 1);
	CHECK(cJSON_IsTrue(GET(response, "ok")));
	DEL(response);
	J *cases = test_read_json(root, "tests/fixtures/api/stages.json");
	CHECK(test_require_cases(cases, "optional stages"));
	for (int lines = 0; a->graph && lines < 2; lines++) {
		char *wire = source_test_encode(a->graph, lines);
		response = load(b, path, wire, NULL, "data", 1);
		CHECK(cJSON_IsTrue(GET(response, "ok")));
		DEL(response);
		free(wire);
		EACH(row, cases)
			same_request(a, b, GET(row, "request"));
	}
	DEL(cases);
	free(path);
	mc_destroy(a);
	mc_destroy(b);
}

static void mixed_collection(void)
{
	const char *parts[] = { "@prefix : <http://test/#> . :A <" RDFS
				"label> \"first\"; <" RDFS
				"seeAlso> \"shared\" . _:same :p \"one\" .",
				"@prefix : <http://test/#> . :B <" RDFS
				"label> \"second\"; <" RDFS
				"seeAlso> \"shared\" . _:same :p \"two\" .",
				"@prefix : <http://test/#> . :C <" RDFS
				"label> \"third\" . _:same :p \"three\" ." };
	mc_engine *a = mc_create(), *b = mc_create();
	J *q = OBJ(), *sources = ARR();
	PUT(q, "op", STR("load"));
	PUT(q, "sources", sources);
	for (int i = 0; i < 3; i++) {
		J *part = OBJ();
		PUT(part, "name",
		    STR(i == 0 ? "first" :
			i == 1 ? "second" :
				 "third"));
		PUT(part, "content", STR(parts[i]));
		ADD(sources, part);
	}
	J *r = test_request(a, q);
	CHECK(cJSON_IsTrue(GET(r, "ok")));
	DEL(r);
	for (int i = 1; i < 3; i++) {
		mc_error error = { 0 };
		Graph *g = rdf_parse(parts[i], "", &error);
		CHECK(g && !error.code);
		if (!g)
			continue;
		char *wire = source_test_encode(g, i == 2);
		set(AT(sources, i), "content", STR(wire));
		set(AT(sources, i), "format", STR(i == 2 ? "jsonl" : "json"));
		rdf_free(g);
		free(wire);
	}
	r = test_request(b, q);
	CHECK(cJSON_IsTrue(GET(r, "ok")));
	CHECK(graph_equal(a->graph, b->graph));
	CHECK(cJSON_Compare(a->snapshot, b->snapshot, 1));
	DEL(r);
	J *parse = OBJ();
	PUT(parse, "op", STR("parse"));
	PUT(parse, "text", STR("shared first second third"));
	same_request(a, b, parse);
	set(AT(sources, 2), "content", STR("{broken}"));
	r = test_request(b, q);
	CHECK(cJSON_IsFalse(GET(r, "ok")));
	DEL(r);
	same_request(a, b, parse);
	DEL(parse);
	DEL(q);
	mc_destroy(a);
	mc_destroy(b);
}

static void framing_and_detection(void)
{
	const char *turtle[] = {
		"[] <http://test/p> <http://test/o> .",
		"[ <http://test/p> \"v\" ] <http://test/q> \"w\" .",
		"[ # comment\n <http://test/p> \"v\" ] ."
	};
	for (size_t i = 0; i < sizeof(turtle) / sizeof(*turtle); i++) {
		mc_error error = { 0 };
		J *snapshot = NULL;
		Graph *a = rdf_parse(turtle[i], "", &error);
		Graph *b = source_read(turtle[i], NULL, "", &snapshot, &error);
		CHECK(!error.code && graph_equal(a, b));
		rdf_free(a);
		rdf_free(b);
		DEL(snapshot);
	}
	const char *valid[] = {
		"{\"id\":\"http://test/Dog\",\"label\":\"dog\"}",
		"{\"id\":\"http://test/Dog\",\"label\":\"dog\"}\r\n",
		"{\"namespace\":\"http://test/\"}\r\n{\"id\":\"Dog\",\"label\":\"dog\"}\r\n"
	};
	mc_engine *e = mc_create();
	for (size_t i = 0; i < sizeof(valid) / sizeof(*valid); i++) {
		J *r = load(e, NULL, valid[i], "jsonl", "", 0);
		CHECK(cJSON_IsTrue(GET(r, "ok")) && e->graph->n == 1);
		DEL(r);
	}
	const char *invalid[] = { "\n{\"id\":\"Dog\"}\n",
				  "{\"id\":\"Dog\"}\n\n",
				  "{\"id\":\"Dog\"}\n \n{\"id\":\"Cat\"}",
				  "{\n\"id\":\"Dog\"\n}",
				  "[]",
				  "",
				  "{\"id\":\"Dog\"}\n " };
	for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
		J *r = load(e, NULL, invalid[i], "jsonl", "", 0);
		CHECK(cJSON_IsFalse(GET(r, "ok")));
		DEL(r);
	}
	char *path = test_path(root, "tests/fixtures/api/source.jsonl");
	J *r = load(e, path, NULL, NULL, "", 0);
	CHECK(cJSON_IsTrue(GET(r, "ok")) && e->graph->n == 41);
	DEL(r);
	free(path);
	J *q = OBJ();
	PUT(q, "op", STR("read_rdf"));
	PUT(q, "content", STR(valid[0]));
	PUT(q, "format", STR("jsonl"));
	r = test_request(e, q);
	CHECK(cJSON_IsTrue(GET(r, "ok")) && SIZE(GET(r, "result")) == 1);
	DEL(r);
	set(q, "op", STR("detect_schema"));
	r = test_request(e, q);
	CHECK(cJSON_IsTrue(GET(r, "ok")));
	DEL(r);
	set(q, "format", STR("yaml"));
	r = test_request(e, q);
	CHECK(cJSON_IsFalse(GET(r, "ok")));
	DEL(r);
	DEL(q);
	mc_destroy(e);
}

/* Fixture generation is a separate test process, outside benchmark timings. */
static int write_sources(const char *directory)
{
	J *manifest = test_read_json(root, "tests/fixtures/rdf-graphs.json");
	EACH(row, GET(manifest, "graphs")) {
		char *folder = test_path(root, "tests/fixtures/ontologies");
		char *path = test_path(folder, S(GET(row, "file")));
		free(folder);
		mc_error error = { 0 };
		char *text = read_file(path, &error), *base = file_uri(path);
		Graph *graph = text ? rdf_parse(text, base, &error) : NULL;
		CHECK(graph && !error.code);
		for (int lines = 0; graph && lines < 2; lines++) {
			char *stem = copy(S(GET(row, "file")));
			char *dot = strrchr(stem, '.');
			if (dot)
				*dot = 0;
			char *dest = test_path(directory, stem);
			Buf filename = { 0 };
			buf_put(&filename, dest);
			buf_put(&filename, lines ? ".jsonl" : ".json");
			char *wire = source_test_encode(graph, lines);
			FILE *out = fopen(filename.p, "wb");
			CHECK(out != NULL);
			if (out) {
				CHECK(fwrite(wire, 1, strlen(wire), out) ==
				      strlen(wire));
				CHECK(!fclose(out));
			}
			free(wire);
			free(filename.p);
			free(dest);
			free(stem);
		}
		rdf_free(graph);
		free(text);
		free(base);
		free(path);
	}
	DEL(manifest);
	return failed ? 1 : 0;
}

int main(int argc, char **argv)
{
	if (argc != 2 && !(argc == 4 && !strcmp(argv[2], "--write-sources")))
		return 2;
	root = argv[1];
	if (argc == 4)
		return write_sources(argv[3]);
	graph_fixtures();
	finder_parity();
	matching_parity();
	authored_and_errors();
	stages_parity();
	mixed_collection();
	framing_and_detection();
	printf("Native sources: %d checks, %d failures; %d graph round-trips, %d finder comparisons, %d parse comparisons.\n",
	       total, failed, graphs, queries, parses);
	return failed ? 1 : 0;
}
