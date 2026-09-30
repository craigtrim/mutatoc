/*
 * test_rdf.c - Turtle conformance, ontology graph fingerprints and literals.
 *
 * The W3C RDF 1.1 Turtle suite runs against expected graphs that were
 * converted once from the W3C result files (tests/fixtures/w3c-turtle.json),
 * so the parser is never its own oracle. The fixture ontologies must keep
 * their triple counts and a fingerprint that ignores blank-node labels. Typed
 * literals keep their lexical form unless a common XSD type normalizes it.
 * craigtrim/mutatoc#1
 */

#include "testlib.h"

static int failed = 0, total = 0;
#define CHECK(x)                                                      \
	do {                                                          \
		total++;                                              \
		if (!(x)) {                                           \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, \
				__LINE__, #x);                        \
			failed++;                                     \
		}                                                     \
	} while (0)

#define XSD_STRING "http://www.w3.org/2001/XMLSchema#string"

/* One term as text; blank nodes keep a marker so they can be mapped. */
static char *term_text(const J *t)
{
	const char *kind = S(GET(t, "kind")), *value = S(GET(t, "value"));
	Buf b = { 0 };
	if (!strcmp(kind, "iri")) {
		buf_put(&b, "<");
		buf_put(&b, value);
		buf_put(&b, ">");
	} else if (!strcmp(kind, "blank")) {
		buf_put(&b, "_:");
		buf_put(&b,
			value[0] == '_' && value[1] == ':' ? value + 2 : value);
	} else {
		buf_put(&b, "\"");
		buf_put(&b, value);
		buf_put(&b, "\"");
		if (*S(GET(t, "language"))) {
			char *lang = lower(S(GET(t, "language")));
			buf_put(&b, "@");
			buf_put(&b, lang);
			free(lang);
		} else {
			buf_put(&b, "^^");
			buf_put(&b, *S(GET(t, "datatype")) ?
					    S(GET(t, "datatype")) :
					    XSD_STRING);
		}
	}
	return buf_take(&b);
}

typedef struct {
	char *t[3];
} Triple3;

typedef struct {
	Triple3 *v;
	int n;
} Triples;

static int blank(const char *s)
{
	return s[0] == '_' && s[1] == ':';
}

static int triple_cmp(const void *a, const void *b)
{
	const Triple3 *x = a, *y = b;
	for (int i = 0; i < 3; i++) {
		int c = strcmp(x->t[i], y->t[i]);
		if (c)
			return c;
	}
	return 0;
}

/* Converts [{subject,predicate,object}] or [[s,p,o]] into a sorted set. */
static Triples triples_of(const J *rows)
{
	Triples g = { calloc((size_t)SIZE(rows) + 1, sizeof(Triple3)), 0 };
	static const char *keys[] = { "subject", "predicate", "object" };
	EACH(row, rows) {
		for (int i = 0; i < 3; i++)
			g.v[g.n].t[i] = term_text(cJSON_IsArray(row) ?
							  AT(row, i) :
							  GET(row, keys[i]));
		g.n++;
	}
	qsort(g.v, (size_t)g.n, sizeof(Triple3), triple_cmp);
	int out = 0;
	for (int i = 0; i < g.n; i++) {
		if (out && !triple_cmp(&g.v[out - 1], &g.v[i])) {
			for (int k = 0; k < 3; k++)
				free(g.v[i].t[k]);
			continue;
		}
		g.v[out++] = g.v[i];
	}
	g.n = out;
	return g;
}

static void triples_free(Triples *g)
{
	for (int i = 0; i < g->n; i++)
		for (int k = 0; k < 3; k++)
			free(g->v[i].t[k]);
	free(g->v);
}

static uint64_t fnv(uint64_t h, const void *data, size_t n)
{
	const unsigned char *p = data;
	for (size_t i = 0; i < n; i++) {
		h ^= p[i];
		h *= 0x100000001b3ull;
	}
	return h;
}

static uint64_t fnv_u64(uint64_t h, uint64_t v)
{
	return fnv(h, &v, sizeof(v));
}

static int u64_cmp(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
	return x < y ? -1 : x > y;
}

/*
 * Colors every blank node by repeatedly hashing the terms around it, in the
 * same way for any labeling. Returns the color for a term: fixed terms hash
 * their text; blank nodes take their refined color.
 */
typedef struct {
	Map index; /* blank label -> slot + 1 */
	uint64_t *color;
	int n;
} Colors;

static uint64_t color_of(const Colors *c, const char *term)
{
	if (!blank(term))
		return fnv(0xcbf29ce484222325ull, term, strlen(term));
	size_t slot = (size_t)(uintptr_t)map_get((Map *)&c->index, term);
	return c->color[slot - 1];
}

static void refine(Colors *c, const Triples *g)
{
	memset(c, 0, sizeof(*c));
	for (int i = 0; i < g->n; i++)
		for (int k = 0; k < 3; k += 2)
			if (blank(g->v[i].t[k]) &&
			    !map_get(&c->index, g->v[i].t[k]))
				map_put(&c->index, g->v[i].t[k],
					(void *)(uintptr_t)++c->n);
	c->color = calloc((size_t)c->n + 1, sizeof(uint64_t));
	uint64_t *next = calloc((size_t)c->n + 1, sizeof(uint64_t));
	uint64_t **entries = calloc((size_t)c->n + 1, sizeof(uint64_t *));
	int *count = calloc((size_t)c->n + 1, sizeof(int)),
	    *cap = calloc((size_t)c->n + 1, sizeof(int));
	for (int i = 0; i < c->n; i++)
		c->color[i] = 1;
	int distinct = 1;
	for (int round = 0; round < 64; round++) {
		for (int i = 0; i < c->n; i++)
			count[i] = 0;
		for (int i = 0; i < g->n; i++)
			for (int k = 0; k < 3; k += 2) {
				const char *self = g->v[i].t[k];
				if (!blank(self))
					continue;
				int s = (int)(uintptr_t)map_get(&c->index,
								self) -
					1;
				uint64_t h = fnv_u64(0xcbf29ce484222325ull,
						     (uint64_t)k);
				for (int j = 0; j < 3; j++)
					h = fnv_u64(
						h,
						j == k && !strcmp(g->v[i].t[j],
								  self) ?
							0 :
							color_of(c,
								 g->v[i].t[j]));
				if (count[s] == cap[s]) {
					cap[s] = cap[s] ? cap[s] * 2 : 4;
					entries[s] = realloc(
						entries[s],
						(size_t)cap[s] *
							sizeof(uint64_t));
				}
				entries[s][count[s]++] = h;
			}
		for (int i = 0; i < c->n; i++) {
			qsort(entries[i], (size_t)count[i], sizeof(uint64_t),
			      u64_cmp);
			uint64_t h =
				fnv_u64(0xcbf29ce484222325ull, c->color[i]);
			for (int e = 0; e < count[i]; e++)
				h = fnv_u64(h, entries[i][e]);
			next[i] = h;
		}
		memcpy(c->color, next, (size_t)c->n * sizeof(uint64_t));
		uint64_t *sorted =
			malloc(((size_t)c->n + 1) * sizeof(uint64_t));
		memcpy(sorted, c->color, (size_t)c->n * sizeof(uint64_t));
		qsort(sorted, (size_t)c->n, sizeof(uint64_t), u64_cmp);
		int now = c->n ? 1 : 0;
		for (int i = 1; i < c->n; i++)
			now += sorted[i] != sorted[i - 1];
		free(sorted);
		if (now == distinct && round)
			break;
		distinct = now;
	}
	for (int i = 0; i < c->n; i++)
		free(entries[i]);
	free(entries);
	free(count);
	free(cap);
	free(next);
}

static void colors_free(Colors *c)
{
	map_free(&c->index);
	free(c->color);
}

/* A fingerprint of the graph that no blank-node relabeling can change. */
static uint64_t fingerprint(const Triples *g)
{
	Colors c;
	refine(&c, g);
	uint64_t *lines = malloc(((size_t)g->n + 1) * sizeof(uint64_t));
	for (int i = 0; i < g->n; i++) {
		uint64_t h = 0xcbf29ce484222325ull;
		for (int k = 0; k < 3; k++)
			h = fnv_u64(h, color_of(&c, g->v[i].t[k]));
		lines[i] = h;
	}
	qsort(lines, (size_t)g->n, sizeof(uint64_t), u64_cmp);
	uint64_t h = fnv_u64(0xcbf29ce484222325ull, (uint64_t)g->n);
	for (int i = 0; i < g->n; i++)
		h = fnv_u64(h, lines[i]);
	free(lines);
	colors_free(&c);
	return h;
}

/* Backtracking isomorphism for the small W3C graphs. */
typedef struct {
	const Triples *a, *e;
	char **from, **to;
	int n;
	int *mapped;
	Map *expected;
} Search;

static const char *mapped_term(const Search *s, const char *term)
{
	if (!blank(term))
		return term;
	for (int i = 0; i < s->n; i++)
		if (s->mapped[i] >= 0 && !strcmp(s->from[i], term))
			return s->to[s->mapped[i]];
	return NULL;
}

static int consistent(const Search *s)
{
	for (int i = 0; i < s->a->n; i++) {
		const char *t[3];
		int ready = 1;
		for (int k = 0; k < 3 && ready; k++)
			ready = (t[k] = mapped_term(s, s->a->v[i].t[k])) !=
				NULL;
		if (!ready)
			continue;
		Buf key = { 0 };
		for (int k = 0; k < 3; k++) {
			buf_put(&key, t[k]);
			buf_put(&key, "\x1f");
		}
		int found = map_get(s->expected, key.p) != NULL;
		free(key.p);
		if (!found)
			return 0;
	}
	return 1;
}

static int assign(Search *s, int i, int *used)
{
	if (i == s->n)
		return consistent(s);
	for (int j = 0; j < s->n; j++) {
		if (used[j])
			continue;
		s->mapped[i] = j;
		used[j] = 1;
		if (consistent(s) && assign(s, i + 1, used))
			return 1;
		used[j] = 0;
		s->mapped[i] = -1;
	}
	return 0;
}

static void collect_blanks(const Triples *g, char ***out, int *n)
{
	Map seen = { 0 };
	*out = calloc((size_t)g->n * 2 + 1, sizeof(char *));
	*n = 0;
	for (int i = 0; i < g->n; i++)
		for (int k = 0; k < 3; k += 2)
			if (blank(g->v[i].t[k]) &&
			    !map_get(&seen, g->v[i].t[k])) {
				map_put(&seen, g->v[i].t[k], (void *)1);
				(*out)[(*n)++] = g->v[i].t[k];
			}
	map_free(&seen);
}

static int isomorphic(const Triples *a, const Triples *e)
{
	if (a->n != e->n)
		return 0;
	Search s = { a, e, NULL, NULL, 0, NULL, NULL };
	int m = 0;
	collect_blanks(a, &s.from, &s.n);
	collect_blanks(e, &s.to, &m);
	int ok = s.n == m;
	Map expected = { 0 };
	for (int i = 0; i < e->n; i++) {
		Buf key = { 0 };
		for (int k = 0; k < 3; k++) {
			buf_put(&key, e->v[i].t[k]);
			buf_put(&key, "\x1f");
		}
		map_put(&expected, key.p, (void *)1);
		free(key.p);
	}
	s.expected = &expected;
	if (ok) {
		s.mapped = malloc(((size_t)s.n + 1) * sizeof(int));
		int *used = calloc((size_t)s.n + 1, sizeof(int));
		for (int i = 0; i < s.n; i++)
			s.mapped[i] = -1;
		ok = assign(&s, 0, used);
		free(used);
		free(s.mapped);
	}
	map_free(&expected);
	free(s.from);
	free(s.to);
	return ok;
}

static void w3c_turtle(mc_engine *e, const char *root)
{
	J *table = test_read_json(root, "tests/fixtures/w3c-turtle.json");
	int passed = 0, count = 0;
	EACH(t, GET(table, "tests")) {
		const char *name = S(GET(t, "name")), *type = S(GET(t, "type"));
		Buf rel = { 0 };
		buf_put(&rel, "tests/w3c-turtle/");
		buf_put(&rel, name);
		char *path = test_path(root, rel.p);
		free(rel.p);
		mc_error err = { 0 };
		char *text = read_file(path, &err);
		free(path);
		Buf base = { 0 };
		buf_put(&base, S(GET(table, "base")));
		buf_put(&base, name);
		J *q = OBJ();
		PUT(q, "op", STR("read_rdf"));
		PUT(q, "turtle", STR(text ? text : ""));
		PUT(q, "base", STR(base.p));
		free(base.p);
		free(text);
		J *r = test_request(e, q);
		DEL(q);
		int ok;
		if (!strncmp(type, "negative", 8))
			ok = cJSON_IsFalse(GET(r, "ok"));
		else if (!strcmp(type, "positive_syntax"))
			ok = cJSON_IsTrue(GET(r, "ok"));
		else {
			ok = cJSON_IsTrue(GET(r, "ok"));
			if (ok) {
				Triples a = triples_of(GET(r, "result")),
					x = triples_of(GET(t, "expected"));
				ok = isomorphic(&a, &x);
				triples_free(&a);
				triples_free(&x);
			}
		}
		if (!ok)
			fprintf(stderr, "W3C Turtle %s (%s) failed\n", name,
				type);
		CHECK(ok);
		passed += ok;
		count++;
		DEL(r);
	}
	printf("W3C Turtle: %d/%d tests passed.\n", passed, count);
	DEL(table);
}

static void ontology_graphs(mc_engine *e, const char *root)
{
	J *expected = test_read_json(root, "tests/fixtures/rdf-graphs.json");
	int passed = 0, count = 0;
	EACH(row, GET(expected, "graphs")) {
		Buf rel = { 0 };
		buf_put(&rel, "tests/fixtures/ontologies/");
		buf_put(&rel, S(GET(row, "file")));
		char *path = test_path(root, rel.p);
		free(rel.p);
		J *q = OBJ();
		PUT(q, "op", STR("load"));
		PUT(q, "path", STR(path));
		free(path);
		J *r = test_request(e, q);
		DEL(q);
		CHECK(cJSON_IsTrue(GET(r, "ok")));
		DEL(r);
		J *rows = test_call(e, "{\"op\":\"triples\"}");
		Triples g = triples_of(rows);
		char digest[17];
		snprintf(digest, sizeof(digest), "%016llx",
			 (unsigned long long)fingerprint(&g));
		int ok = SIZE(rows) == GET(row, "triples")->valueint &&
			 !strcmp(digest, S(GET(row, "fingerprint")));
		if (!ok)
			fprintf(stderr, "%s: %d triples, fingerprint %s\n",
				S(GET(row, "file")), SIZE(rows), digest);
		CHECK(ok);
		passed += ok;
		count++;
		triples_free(&g);
		DEL(rows);
	}
	printf("Ontology graphs: %d/%d unchanged.\n", passed, count);
	DEL(expected);
}

static uint64_t turtle_fingerprint(mc_engine *e, const char *turtle)
{
	J *q = OBJ();
	PUT(q, "op", STR("read_rdf"));
	PUT(q, "turtle", STR(turtle));
	PUT(q, "base", STR("http://example/"));
	J *r = test_request(e, q);
	DEL(q);
	Triples g = triples_of(GET(r, "result"));
	uint64_t h = fingerprint(&g);
	triples_free(&g);
	DEL(r);
	return h;
}

/* Relabeling or reordering cannot move the fingerprint; an edit must. */
static void fingerprint_properties(mc_engine *e)
{
	const char
		*a = "_:x <urn:p> _:y . _:y <urn:p> _:z . _:z <urn:q> \"v\" ."
		     " _:w <urn:p> _:x .",
		*b = "_:k <urn:p> _:m . _:n <urn:q> \"v\" . _:m <urn:p> _:n ."
		     " _:j <urn:p> _:k .",
		*c = "_:x <urn:p> _:y . _:y <urn:p> _:z . _:z <urn:q> \"w\" ."
		     " _:w <urn:p> _:x .";
	uint64_t fa = turtle_fingerprint(e, a), fb = turtle_fingerprint(e, b),
		 fc = turtle_fingerprint(e, c);
	CHECK(fa == fb);
	CHECK(fa != fc);
}

static const char *literal_value(mc_engine *e, const char *datatype,
				 const char *lexical, char *out, size_t size)
{
	J *lit = STR(lexical);
	char *quoted = cJSON_PrintUnformatted(lit);
	DEL(lit);
	Buf turtle = { 0 };
	buf_put(&turtle, "<urn:s> <urn:p> ");
	buf_put(&turtle, quoted);
	buf_put(&turtle, "^^<");
	buf_put(&turtle, datatype);
	buf_put(&turtle, "> .");
	free(quoted);
	J *q = OBJ();
	PUT(q, "op", STR("load"));
	PUT(q, "turtle", STR(turtle.p));
	free(turtle.p);
	J *r = test_request(e, q);
	DEL(q);
	CHECK(cJSON_IsTrue(GET(r, "ok")));
	DEL(r);
	J *rows = test_call(e, "{\"op\":\"triples\"}");
	J *object = GET(AT(rows, 0), "object");
	CHECK(!strcmp(S(GET(object, "datatype")), datatype));
	snprintf(out, size, "%s", S(GET(object, "value")));
	DEL(rows);
	return out;
}

static void literals(mc_engine *e)
{
	/* Kept as written; the removed worker used to rewrite most of these. */
	static const char *kept[][2] = {
		{ "dateTime", "2020-01-01T01:02:03Z" },
		{ "dateTime", "2020-01-01T01:02:03.500+05:30" },
		{ "dateTime", "bogus" },
		{ "date", "2020-01-01Z" },
		{ "time", "01:02:03.25" },
		{ "gYear", "2020Z" },
		{ "gYearMonth", "2020-02Z" },
		{ "duration", "PT25H" },
		{ "dayTimeDuration", "PT36H" },
		{ "yearMonthDuration", "P2Y12M" },
		{ "base64Binary", "Y Q==" },
		{ "base64Binary", "!!!" },
	};
	char value[256], datatype[128];
	for (size_t i = 0; i < sizeof(kept) / sizeof(*kept); i++) {
		snprintf(datatype, sizeof(datatype), "%s%s", XSD, kept[i][0]);
		CHECK(!strcmp(literal_value(e, datatype, kept[i][1], value,
					    sizeof(value)),
			      kept[i][1]));
	}
	CHECK(!strcmp(literal_value(e, RDF "XMLLiteral", "<a  b='x'></a>",
				    value, sizeof(value)),
		      "<a  b='x'></a>"));
	/* The common XSD types still normalize natively. */
	static const char *normalized[][3] = {
		{ "integer", "+0001", "1" },
		{ "integer", "-000", "0" },
		{ "integer", "123456789123456789123456789",
		  "123456789123456789123456789" },
		{ "decimal", "+0001.20", "1.20" },
		{ "decimal", "1E3", "1000" },
		{ "double", "1e0", "1.0" },
		{ "double", "-0", "-0.0" },
		{ "double", "INF", "inf" },
		{ "double", "NaN", "nan" },
		{ "boolean", "0", "false" },
		{ "hexBinary", "ABEF", "abef" },
		{ "normalizedString", " a\tb\r\nc ", " a b  c " },
		{ "token", " a\tb\r\nc ", "a b c" },
	};
	for (size_t i = 0; i < sizeof(normalized) / sizeof(*normalized); i++) {
		snprintf(datatype, sizeof(datatype), "%s%s", XSD,
			 normalized[i][0]);
		CHECK(!strcmp(literal_value(e, datatype, normalized[i][1],
					    value, sizeof(value)),
			      normalized[i][2]));
	}
	/* Collections load the same way. */
	J *q = cJSON_Parse(
		"{\"op\":\"load\",\"sources\":[{\"turtle\":\"@prefix xsd: "
		"<http://www.w3.org/2001/XMLSchema#> . <urn:s> <urn:p> "
		"\\\"2020-01-01Z\\\"^^xsd:date .\"}]}");
	J *r = test_request(e, q);
	CHECK(cJSON_IsTrue(GET(r, "ok")));
	DEL(r);
	DEL(q);
	J *rows = test_call(e, "{\"op\":\"triples\"}");
	CHECK(!strcmp(S(GET(GET(AT(rows, 0), "object"), "value")),
		      "2020-01-01Z"));
	DEL(rows);
	printf("Typed literals: %d checks so far.\n", total);
}

int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	mc_engine *e = mc_create();
	w3c_turtle(e, argv[1]);
	fingerprint_properties(e);
	ontology_graphs(e, argv[1]);
	literals(e);
	mc_destroy(e);
	printf("RDF: %d checks, %d failures.\n", total, failed);
	return failed ? 1 : 0;
}
