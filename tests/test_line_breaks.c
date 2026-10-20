/* Request-local mandatory line boundaries: craigtrim/mutatoc#19. */
#include "testlib.h"

static int checks, failures;

static void check(int ok, const char *label)
{
	checks++;
	if (!ok) {
		failures++;
		fprintf(stderr, "FAIL %s\n", label);
	}
}

static void equal(const J *a, const J *b, const char *label)
{
	int ok = a && b && cJSON_Compare(a, b, 1);
	check(ok, label);
	if (!ok)
		test_print_diff(label, a, b);
}

/* Own the request, detach the result, and surface protocol failures. */
static J *request(mc_engine *e, J *q)
{
	J *r = test_request(e, q);
	DEL(q);
	check(cJSON_IsTrue(GET(r, "ok")), "request succeeds");
	J *out = cJSON_DetachItemFromObjectCaseSensitive(r, "result");
	DEL(r);
	return out;
}

static J *operation(const char *op)
{
	J *q = OBJ();
	PUT(q, "op", STR(op));
	return q;
}

static mc_engine *load(J *q)
{
	mc_engine *e = mc_create();
	J *r = request(e, q);
	if (!r)
		exit(2);
	DEL(r);
	return e;
}

static J *parse(mc_engine *e, const char *text, const char *mode, int ctr)
{
	J *q = operation("parse");
	PUT(q, "text", STR(text));
	if (mode)
		PUT(q, "line_breaks", STR(mode));
	if (ctr)
		PUT(q, "ctr", NUM(ctr));
	return request(e, q);
}

static J *tokens(mc_engine *e, const char *text)
{
	J *q = operation("tokenize");
	PUT(q, "text", STR(text));
	return request(e, q);
}

static J *prepared(mc_engine *e, const J *ts, const char *stage,
		   const char *mode)
{
	J *q = operation(stage ? "transform_tokens" : "parse_tokens");
	PUT(q, "tokens", DUP(ts));
	if (stage)
		PUT(q, "stage", STR(stage));
	if (mode)
		PUT(q, "line_breaks", STR(mode));
	return request(e, q);
}

static J *found(const J *ts, int offset)
{
	J *out = ARR();
	EACH(t, ts) {
		J *s = GET(t, "swaps");
		if (!s)
			continue;
		J *row = ARR();
		ADD(row, DUP(GET(s, "canon")));
		ADD(row, DUP(GET(s, "type")));
		ADD(row, NUM(GET(t, "x")->valueint + offset));
		ADD(row, NUM(GET(t, "y")->valueint + offset));
		ADD(out, row);
	}
	return out;
}

static void leaves(const J *ts, Buf *out)
{
	EACH(t, ts) {
		J *children = GET(GET(t, "swaps"), "tokens");
		if (children)
			leaves(children, out);
		else
			buf_put(out, S(GET(t, "text")));
	}
}

/* Byte strings make the reference split independent of matcher code. */
static const char *separators[] = {
	"\n", "\r", "\v", "\f", "\xc2\x85", "\xe2\x80\xa8",
	"\xe2\x80\xa9", "\r\n", " \r\n\t\n "
};

static int break_bytes(const char *p)
{
	for (int i = 0; i < 7; i++)
		if (!strncmp(p, separators[i], strlen(separators[i])))
			return (int)strlen(separators[i]);
	return 0;
}

/* Context-free fixture oracle: independent default parses of each line. */
static J *by_line(mc_engine *e, const char *text, int ctr)
{
	J *out = ARR();
	const char *start = text, *p = text;
	int offset = 0, position = 0;
	for (;;) {
		int width = break_bytes(p);
		if (!*p || width) {
			size_t n = (size_t)(p - start);
			char *line = malloc(n + 1);
			memcpy(line, start, n);
			line[n] = 0;
			J *r = parse(e, line, NULL, ctr);
			J *entities = found(GET(r, "tokens"), offset);
			while (SIZE(entities))
				ADD(out, cJSON_DetachItemFromArray(entities, 0));
			DEL(entities);
			DEL(r);
			free(line);
			if (!*p)
				break;
			p += width;
			offset = ++position;
			start = p;
		} else {
			if (((unsigned char)*p & 0xc0) != 0x80)
				position++;
			p++;
		}
	}
	return out;
}

static void fixture_case(mc_engine *e, const char *text, int ctr,
			 const J *expected)
{
	J *a = parse(e, text, NULL, ctr), *b = parse(e, text, "ignore", ctr);
	equal(a, b, "default equals explicit ignore");
	J *entities = found(GET(a, "tokens"), 0);
	if (expected)
		equal(expected, entities, "existing fixture result");
	DEL(entities);
	DEL(a);
	DEL(b);
	a = parse(e, text, "boundary", ctr);
	entities = found(GET(a, "tokens"), 0);
	J *lines = by_line(e, text, ctr);
	equal(lines, entities, "boundary equals independent line parses");
	Buf original = { 0 };
	leaves(GET(a, "tokens"), &original);
	check(!strcmp(text, original.p ? original.p : ""),
	      "original whitespace survives in leaves");
	free(original.p);
	DEL(lines);
	DEL(entities);
	DEL(a);
}

static void prepared_case(mc_engine *e, const char *text, const char *stage,
			  int expected)
{
	J *ts = tokens(e, text);
	J *a = prepared(e, ts, stage, NULL);
	J *b = prepared(e, ts, stage, "ignore");
	equal(a, b, "prepared default equals ignore");
	DEL(a);
	DEL(b);
	a = prepared(e, ts, stage, "boundary");
	J *entities = found(stage ? a : GET(a, "tokens"), 0);
	check(SIZE(entities) == expected, "prepared boundary entity count");
	if (!stage) {
		b = parse(e, text, "boundary", 0);
		J *raw = found(GET(b, "tokens"), 0);
		equal(raw, entities, "raw and prepared offsets and matches agree");
		DEL(raw);
		DEL(b);
	}
	DEL(entities);
	DEL(a);
	DEL(ts);
}

static void invalid_modes(mc_engine *e)
{
	const char *bad[] = {"null", "false", "true", "0", "{}", "[]",
		"\"\"", "\"BOUNDARY\"", "\"unknown\""};
	const char *stages[] = {NULL, "exact", "spans", "hierarchy", "augment"};
	J *before = request(e, operation("snapshot"));
	J *ts = tokens(e, "survey\nglacier");
	for (int i = 0; i < 9; i++)
		for (int s = -1; s < 5; s++) {
			J *q = operation(s < 0 ? "parse" :
					 s == 0 ? "parse_tokens" : "transform_tokens");
			if (s < 0)
				PUT(q, "text", STR("survey\nglacier"));
			else
				PUT(q, "tokens", DUP(ts));
			if (s > 0)
				PUT(q, "stage", STR(stages[s]));
			PUT(q, "line_breaks", cJSON_Parse(bad[i]));
			J *r = test_request(e, q);
			check(cJSON_IsFalse(GET(r, "ok")) &&
			      GET(GET(r, "error"), "code") &&
			      GET(GET(r, "error"), "code")->valueint == 2,
			      "bad mode is input error on every matching operation");
			DEL(r);
			DEL(q);
		}
	J *after = request(e, operation("snapshot"));
	equal(before, after, "invalid modes preserve ontology");
	DEL(before);
	DEL(after);
	DEL(ts);
	fixture_case(e, "survey\nglacier", 0, NULL);
}

static mc_engine *snapshot_engine(J *snapshot)
{
	J *q = operation("load");
	PUT(q, "snapshot", snapshot);
	return load(q);
}

static void authored(const J *base)
{
	/* Context stays document-wide even though candidate windows cannot cross. */
	J *s = DUP(base);
	J *spans = cJSON_Parse("{\"glacier\":[{\"content\":[\"survey\"],"
		"\"canon\":\"glacier_survey\",\"distance\":4,\"forward\":true,"
		"\"reverse\":true,\"context\":[\"needle\"]}]}");
	cJSON_ReplaceItemInObjectCaseSensitive(s, "spans", spans);
	mc_engine *e = snapshot_engine(s);
	prepared_case(e, "needle\nsurvey glacier", "spans", 1);
	prepared_case(e, "survey glacier", "spans", 0);
	prepared_case(e, "needle survey\nglacier", "spans", 0);
	mc_destroy(e);

	/* Direction flags and a large distance still select only valid windows. */
	for (int flags = 0; flags < 4; flags++) {
		s = DUP(base);
		spans = cJSON_Parse("{\"glacier\":[{\"content\":[\"survey\"],"
			"\"canon\":\"glacier_survey\",\"distance\":1000}]}");
		J *rule = AT(GET(spans, "glacier"), 0);
		PUT(rule, "forward", BOOL(flags & 1));
		PUT(rule, "reverse", BOOL(flags & 2));
		cJSON_ReplaceItemInObjectCaseSensitive(s, "spans", spans);
		e = snapshot_engine(s);
		fixture_case(e, "survey\nglacier oak survey", 0, NULL);
		fixture_case(e, "glacier\nsurvey oak glacier", 0, NULL);
		fixture_case(e, "survey\n\n\nglacier", 0, NULL);
		mc_destroy(e);
	}

	/* Hierarchy metadata on whitespace must not bridge a line boundary. */
	s = DUP(base);
	cJSON_DeleteItemFromObjectCaseSensitive(s, "entities");
	PUT(s, "entities", cJSON_Parse("[\"glacier_bridge_survey\"]"));
	e = snapshot_engine(s);
	J *ts = tokens(e, "glacier\nsurvey");
	PUT(AT(ts, 1), "ancestors", cJSON_Parse("[\"bridge\"]"));
	PUT(AT(ts, 1), "custom", cJSON_Parse("{\"keep\":[1,2,3]}"));
	J *r = prepared(e, ts, "hierarchy", "ignore");
	J *entities = found(r, 0);
	check(SIZE(entities) == 1, "hierarchy fixture can match across newline");
	DEL(entities);
	DEL(r);
	r = prepared(e, ts, "hierarchy", "boundary");
	equal(ts, r, "hierarchy boundary retains caller token metadata");
	DEL(r);
	DEL(ts);
	mc_destroy(e);
}

static void atomic_tokens(mc_engine *e)
{
	J *ts = tokens(e, "glacier survey");
	/* A caller can retain a break inside a visible token. */
	J *one = ARR(), *t = DUP(AT(ts, 0));
	cJSON_ReplaceItemInObjectCaseSensitive(t, "text", STR("glacier\nsurvey"));
	cJSON_ReplaceItemInObjectCaseSensitive(t, "normal", STR("glacier"));
	PUT(t, "custom", cJSON_Parse("{\"keep\":true}"));
	ADD(one, t);
	ADD(one, DUP(AT(ts, 1)));
	J *a = prepared(e, one, "spans", "ignore");
	J *b = prepared(e, one, "spans", "boundary");
	equal(a, b, "caller non-whitespace token stays atomic");
	J *entities = found(b, 0);
	check(SIZE(entities) == 1, "atomic supplied phrase still matches");
	DEL(entities);
	DEL(a);
	DEL(b);
	DEL(one);
	DEL(ts);
	/* A newline carrying a visible normal is a position, not whitespace. */
	ts = tokens(e, "survey\nglacier");
	cJSON_ReplaceItemInObjectCaseSensitive(AT(ts, 1), "normal", STR("oak"));
	a = prepared(e, ts, "spans", "ignore");
	b = prepared(e, ts, "spans", "boundary");
	equal(a, b, "nonempty normal is not a boundary");
	DEL(a);
	DEL(b);
	a = prepared(e, ts, "augment", "ignore");
	b = prepared(e, ts, "augment", "boundary");
	equal(a, b, "augment annotations are unchanged");
	DEL(a);
	DEL(b);
	/* Existing swaps are also caller-owned atomic tokens. */
	cJSON_ReplaceItemInObjectCaseSensitive(AT(ts, 1), "normal", STR(""));
	PUT(AT(ts, 1), "swaps", cJSON_Parse("{\"canon\":\"old\",\"tokens\":[]}"));
	a = prepared(e, ts, "spans", "ignore");
	b = prepared(e, ts, "spans", "boundary");
	equal(a, b, "existing swaps are not reinterpreted as boundaries");
	DEL(a);
	DEL(b);
	DEL(ts);
}

int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	J *fixture = test_read_json(argv[1], "tests/fixtures/api/span-whitespace-cases.json");
	J *cases = GET(fixture, "cases");
	if (!test_require_cases(cases, "whitespace cases"))
		return 2;
	check(SIZE(cases) == 935, "all 935 whitespace fixtures present");
	char *path = test_path(argv[1], "tests/fixtures/ontologies/span-distance-test.ttl");
	for (int d = 0; d <= 8; d++) {
		mc_engine *engines[3];
		for (int l = 0; l < 2; l++) {
			J *q = operation("load");
			PUT(q, "path", STR(path));
			PUT(q, "distance", NUM(d));
			if (l) {
				PUT(q, "interface", STR("data"));
				PUT(q, "class_based", BOOL(1));
			}
			engines[l] = load(q);
		}
		J *base = request(engines[0], operation("snapshot"));
		engines[2] = snapshot_engine(DUP(base));
		for (int l = 0; l < 3; l++) {
			EACH(c, cases) {
				if (GET(c, "distance")->valueint != d)
					continue;
				int ctr = GET(c, "ctr") ? GET(c, "ctr")->valueint : 0;
				fixture_case(engines[l], S(GET(c, "text")), ctr,
					     GET(c, "expected"));
			}
			if (d == 4) {
				invalid_modes(engines[l]);
				atomic_tokens(engines[l]);
				for (int i = 0; i < 9; i++) {
					char text[256];
					snprintf(text, sizeof(text), "glacier%ssurvey", separators[i]);
					fixture_case(engines[l], text, 0, NULL);
					prepared_case(engines[l], text, "exact", 0);
					prepared_case(engines[l], text, NULL, 0);
					snprintf(text, sizeof(text), "survey%sglacier", separators[i]);
					fixture_case(engines[l], text, 0, NULL);
					prepared_case(engines[l], text, "spans", 0);
					snprintf(text, sizeof(text), "survey%sglacier oak survey", separators[i]);
					fixture_case(engines[l], text, 1, NULL);
					prepared_case(engines[l], text, "spans", 1);
					prepared_case(engines[l], text, NULL, 1);
				}
				const char *texts[] = {"", "\n\r\n\t", "survey\tglacier",
					"survey  glacier", "survey\xc2\xa0glacier",
					"survey glacier\nsurvey glacier\nsurvey glacier",
					"Harmonic Analysis Applied\nHarmonic Analysis Applied"};
				for (int i = 0; i < 7; i++)
					fixture_case(engines[l], texts[i], 0, NULL);
			}
		}
		if (d == 4)
			authored(base);
		DEL(base);
		for (int l = 0; l < 3; l++)
			mc_destroy(engines[l]);
	}
	free(path);
	DEL(fixture);
	printf("Line breaks: %d/%d checks passed.\n", checks - failures, checks);
	return failures ? 1 : 0;
}
