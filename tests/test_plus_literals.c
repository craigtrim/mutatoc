/*
 * test_plus_literals.c - Labels whose plus signs are text, such as C++.
 *
 * A + in a label, synonym or term is span syntax only when every part between
 * the plus signs holds a word. Otherwise the plus signs are text, the literal
 * matches only as written, and it makes no span rule. Hand-written families of
 * labels, each with the texts it must and must not match, run through ten
 * sentence frames and three casings, across Turtle, JSON and JSONL sources and
 * five predicates, on default, live, deferred and reloaded engines with
 * single-word and underscored class IDs, engines built by update, and
 * match_terms. Span literals with a word in every part keep their rules. No
 * expected match comes from the engine.
 * craigtrim/mutatoc#28
 */

#include "testlib.h"

static int assertions, failures, parses, terms_checked;
static const char *root;
/* Failures per family, and the family whose cases are running (-1: none). */
static int family_failed[64], current = -1;
/*
 * Whether class IDs carry an underscore. A class key with one also makes
 * ordinary multiword span rules from its labels, which single-word IDs skip,
 * so every family runs both ways.
 */
static int underscored;

static char *class_id(const char *id)
{
	Buf b = { 0 };
	buf_put(&b, id);
	if (underscored)
		buf_put(&b, "_Class");
	return buf_take(&b);
}

#define CHECK(value)                                                  \
	do {                                                          \
		assertions++;                                         \
		if (!(value)) {                                       \
			if (failures++ < 40)                          \
				fprintf(stderr, "FAIL line %d: %s\n", \
					__LINE__, #value);            \
		}                                                     \
	} while (0)

#define NS "http://example.org/plus#"

typedef struct {
	const char *id, *label;
} Class;

/* A text that matches one class across all of itself. */
typedef struct {
	const char *text, *id, *type;
} Hit;

/* A text with every entity it holds, at fixed offsets. */
typedef struct {
	const char *id, *surface;
	int x;
	const char *type;
} Entity;

typedef struct {
	const char *text;
	Entity entities[4];
} Scenario;

typedef struct {
	const char *name;
	Class classes[4];
	Hit positives[8];
	const char *negatives[8];
	Scenario scenarios[4];
	int comma_lists; /* load with comma_lists on */
	int skip_terms; /* load-only behavior, such as spans with spaced words */
	/*
	 * Span positives that fail on underscored IDs before and after #28: the
	 * plus rule is keyed "gamma " and the ordinary rule "gamma", and trimming
	 * the keys keeps only one bucket. Counted as expected fails, and reported
	 * if they start to pass.
	 */
	int known_underscore_span;
} Family;

static const Family families[] = {
	{ "C++ Programming",
	  { { "CppProgramming", "C++ Programming" } },
	  { { "C++ Programming", "CppProgramming", "exact" },
	    { "C + + Programming", "CppProgramming", "exact" },
	    { "C++Programming", "CppProgramming", "exact" },
	    { "C++  Programming", "CppProgramming", "exact" } },
	  { "C Programming", "C+ Programming", "C+++ Programming",
	    "Programming", "C++", "Programming C++", "C programming language" },
	  { { "C++ Programming and C Programming",
	      { { "CppProgramming", "C++ Programming", 0, "exact" } } },
	    { "C Programming, then C++ Programming",
	      { { "CppProgramming", "C++ Programming", 20, "exact" } } } } },
	/* Spaced plus signs give the label words of their own; they still make
	 * no span rule of any kind. */
	{ "C + + Programming",
	  { { "CppProgramming", "C + + Programming" } },
	  { { "C++ Programming", "CppProgramming", "exact" },
	    { "C + + Programming", "CppProgramming", "exact" },
	    { "c ++ programming", "CppProgramming", "exact" } },
	  { "C + Programming", "Programming + C", "Programming C++",
	    "C Programming", "C+ Programming", "Programming + + C",
	    "C + x + Programming" } },
	{ "Notepad + + Editor",
	  { { "NotepadEditor", "Notepad + + Editor" } },
	  { { "Notepad++ Editor", "NotepadEditor", "exact" },
	    { "notepad + + editor", "NotepadEditor", "exact" } },
	  { "Notepad + Editor", "Editor + Notepad", "Notepad Editor",
	    "Editor Notepad++", "Notepad + + x Editor" } },
	{ "+ alpha beta",
	  { { "LeadingSpaced", "+ alpha beta" } },
	  { { "+alpha beta", "LeadingSpaced", "exact" },
	    { "+ alpha beta", "LeadingSpaced", "exact" } },
	  { "alpha beta", "beta + alpha", "alpha + beta", "beta alpha +" } },
	{ "C++ beside C and C#",
	  { { "Cpp", "C++" }, { "C", "C" }, { "CSharp", "C#" } },
	  { { "C++", "Cpp", "exact" },
	    { "C ++", "Cpp", "exact" },
	    { "C", "C", "exact" },
	    { "C#", "CSharp", "exact" } },
	  { "D", "++" },
	  { { "I know C, C++ and C#.",
	      { { "C", "C", 7, "exact" },
		{ "Cpp", "C++", 10, "exact" },
		{ "CSharp", "C#", 18, "exact" } } },
	    { "C+", { { "C", "C", 0, "exact" } } },
	    { "C-", { { "C", "C", 0, "exact" } } },
	    { "C+++", { { "Cpp", "C++", 0, "exact" } } } } },
	{ "C and C# before C++",
	  { { "C", "C" }, { "CSharp", "C#" }, { "Cpp", "C++" } },
	  { { "C++", "Cpp", "exact" },
	    { "C", "C", "exact" },
	    { "C#", "CSharp", "exact" } },
	  { "D" },
	  { { "I know C, C++ and C#.",
	      { { "C", "C", 7, "exact" },
		{ "Cpp", "C++", 10, "exact" },
		{ "CSharp", "C#", 18, "exact" } } } } },
	{ "C++ Programming beside C Programming and C++",
	  { { "CppProgramming", "C++ Programming" },
	    { "CProgramming", "C Programming" },
	    { "Cpp", "C++" } },
	  { { "C++ Programming", "CppProgramming", "exact" },
	    { "C Programming", "CProgramming", "exact" },
	    { "C++", "Cpp", "exact" } },
	  { "Programming" },
	  { { "C++ and C Programming",
	      { { "Cpp", "C++", 0, "exact" },
		{ "CProgramming", "C Programming", 8, "exact" } } } } },
	{ "C+",
	  { { "CPlus", "C+" } },
	  { { "C+", "CPlus", "exact" }, { "C +", "CPlus", "exact" } },
	  { "C", "C-", "C#" },
	  { { "C++", { { "CPlus", "C+", 0, "exact" } } },
	    { "C+ and C+", { { "CPlus", "C+", 0, "exact" },
			     { "CPlus", "C+", 7, "exact" } } } } },
	{ "Notepad++",
	  { { "NotepadEditor", "Notepad++" } },
	  { { "Notepad++", "NotepadEditor", "exact" },
	    { "Notepad ++", "NotepadEditor", "exact" },
	    { "Notepad + +", "NotepadEditor", "exact" } },
	  { "Notepad", "Notepad+", "Notepad editor" },
	  { { "Notepad+++", { { "NotepadEditor", "Notepad++", 0, "exact" } } } } },
	{ "alpha+",
	  { { "AlphaPlus", "alpha+" } },
	  { { "alpha+", "AlphaPlus", "exact" }, { "alpha +", "AlphaPlus", "exact" } },
	  { "alpha", "alpha beta", "alpha-", "beta+" },
	  { { "alpha++", { { "AlphaPlus", "alpha+", 0, "exact" } } } } },
	{ "+beta",
	  { { "PlusBeta", "+beta" } },
	  { { "+beta", "PlusBeta", "exact" }, { "+ beta", "PlusBeta", "exact" } },
	  { "beta", "alpha beta", "-beta", "beta+" },
	  { { "++beta", { { "PlusBeta", "+beta", 1, "exact" } } } } },
	{ "alpha++beta",
	  { { "Doubled", "alpha++beta" } },
	  { { "alpha++beta", "Doubled", "exact" },
	    { "alpha + + beta", "Doubled", "exact" },
	    { "alpha+ +beta", "Doubled", "exact" } },
	  { "alpha beta", "alpha x beta", "beta alpha", "alpha+beta",
	    "alpha+++beta", "beta++alpha" } },
	{ "+alpha+beta",
	  { { "Leading", "+alpha+beta" } },
	  { { "+alpha+beta", "Leading", "exact" },
	    { "+ alpha + beta", "Leading", "exact" } },
	  { "alpha beta", "alpha+beta", "beta x alpha", "alpha x beta" } },
	{ "alpha+beta+",
	  { { "Trailing", "alpha+beta+" } },
	  { { "alpha+beta+", "Trailing", "exact" },
	    { "alpha + beta +", "Trailing", "exact" } },
	  { "alpha beta", "alpha+beta", "beta x alpha", "beta+alpha+" } },
	{ "+",
	  { { "PlusSign", "+" } },
	  { { "+", "PlusSign", "exact" } },
	  { "plus", "-", "&" },
	  { { "1 + 2 + 3", { { "PlusSign", "+", 2, "exact" },
			     { "PlusSign", "+", 6, "exact" } } },
	    { "C++", { { "PlusSign", "+", 1, "exact" },
		       { "PlusSign", "+", 2, "exact" } } } } },
	{ "++",
	  { { "PlusPlus", "++" } },
	  { { "++", "PlusPlus", "exact" }, { "+ +", "PlusPlus", "exact" } },
	  { "+", "plus plus" },
	  { { "+++", { { "PlusPlus", "++", 0, "exact" } } },
	    { "C++", { { "PlusPlus", "++", 1, "exact" } } } } },
	{ "Hypomenorrhea+",
	  { { "Menorrhagia", "Hypomenorrhea+" } },
	  { { "Hypomenorrhea+", "Menorrhagia", "exact" },
	    { "hypomenorrhea +", "Menorrhagia", "exact" } },
	  { "Hypomenorrhea", "Hypomenorrhea-" } },
	{ "alpha+beta keeps its span",
	  { { "SpanPair", "alpha+beta" } },
	  { { "alpha beta", "SpanPair", "exact" },
	    { "beta alpha", "SpanPair", "spans" },
	    { "beta x alpha", "SpanPair", "spans" },
	    { "alpha x x beta", "SpanPair", "spans" },
	    { "alpha + beta", "SpanPair", "spans" },
	    { "alpha+beta", "SpanPair", "spans" } },
	  { "alpha x x x x beta", "alpha", "beta", "alphabeta" } },
	{ "gamma + delta keeps its span",
	  { { "GammaDelta", "gamma + delta" } },
	  { { "gamma delta", "GammaDelta", "exact" },
	    { "delta x gamma", "GammaDelta", "spans" } },
	  { "gamma", "delta" },
	  { { 0 } },
	  0,
	  0,
	  1 },
	{ "ALPHA+BETA keeps its span",
	  { { "Upper", "ALPHA+BETA" } },
	  { { "alpha beta", "Upper", "exact" },
	    { "beta x alpha", "Upper", "spans" } },
	  { "alpha", "beta" } },
	{ "alpha+beta+gamma keeps its span",
	  { { "Three", "alpha+beta+gamma" } },
	  { { "alpha beta gamma", "Three", "exact" },
	    { "gamma beta alpha", "Three", "spans" },
	    { "gamma x beta alpha", "Three", "spans" } },
	  { "alpha beta", "beta gamma", "alpha gamma" } },
	{ "A+ Certification stays a span",
	  { { "APlus", "A+ Certification" } },
	  { { "A Certification", "APlus", "exact" },
	    { "A+ Certification", "APlus", "spans" },
	    { "Certification level A", "APlus", "spans" } },
	  { "Certification", "A" } },
	{ "decrease+ in a comma list",
	  { { "Lessening", "decrease+, Decreases" } },
	  { { "decrease+", "Lessening", "exact" },
	    { "decrease +", "Lessening", "exact" },
	    { "Decreases", "Lessening", "exact" } },
	  { "decrease", "decreased" },
	  { { "0" } },
	  1,
	  1 },
};
#define FAMILY_COUNT ((int)(sizeof(families) / sizeof(*families)))

static const char *const frames[][2] = {
	{ "", "" },
	{ "I studied ", "." },
	{ "", " today" },
	{ "Before ", ", after." },
	{ "(", ")" },
	{ "\"", "\"" },
	{ "note: ", "" },
	{ "", "\n" },
	{ "\t", " \t " },
	{ "We said ", " twice and left." },
};
#define FRAME_COUNT ((int)(sizeof(frames) / sizeof(*frames)))

/* As written, lowercase, uppercase. Every authored text is ASCII. */
static char *cased(const char *s, int casing)
{
	char *out = copy(s);
	for (char *p = out; *p; p++) {
		if (casing == 1 && *p >= 'A' && *p <= 'Z')
			*p = (char)(*p - 'A' + 'a');
		if (casing == 2 && *p >= 'a' && *p <= 'z')
			*p = (char)(*p - 'a' + 'A');
	}
	return out;
}

static char *lowered(const char *s)
{
	return cased(s, 1);
}

static int class_index(const Family *f, const char *id)
{
	for (int i = 0; i < 4 && f->classes[i].id; i++)
		if (!strcmp(f->classes[i].id, id))
			return i;
	return -1;
}

static void quoted(Buf *b, const char *s)
{
	buf_put(b, "\"");
	for (; *s; s++) {
		if (*s == '"' || *s == '\\')
			buf_put(b, "\\");
		char c[2] = { *s, 0 };
		buf_put(b, c);
	}
	buf_put(b, "\"");
}

/* Source forms: Turtle with three predicates, JSON with two fields, JSONL. */
enum { TTL_LABEL, TTL_SEE_ALSO, TTL_ALT_LABEL, JSON_LABEL, JSON_SYNONYMS,
       JSONL_LABEL, SOURCE_COUNT };
static const char *const source_names[] = { "turtle rdfs:label",
					    "turtle rdfs:seeAlso",
					    "turtle skos:altLabel",
					    "json label",
					    "json synonyms",
					    "jsonl label" };

static char *source_text(const Family *f, int source)
{
	Buf b = { 0 };
	if (source <= TTL_ALT_LABEL) {
		static const char *const predicates[] = { "rdfs:label",
							  "rdfs:seeAlso",
							  "skos:altLabel" };
		buf_put(&b,
			"@prefix rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .\n"
			"@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n"
			"@prefix owl: <http://www.w3.org/2002/07/owl#> .\n"
			"@prefix skos: <http://www.w3.org/2004/02/skos/core#> .\n"
			"@prefix : <" NS "> .\n");
		for (int i = 0; i < 4 && f->classes[i].id; i++) {
			char *id = class_id(f->classes[i].id);
			buf_put(&b, ":");
			buf_put(&b, id);
			free(id);
			buf_put(&b, " rdf:type owl:Class ; ");
			buf_put(&b, predicates[source]);
			buf_put(&b, " ");
			quoted(&b, f->classes[i].label);
			buf_put(&b, " .\n");
		}
		return buf_take(&b);
	}
	J *header = OBJ();
	PUT(header, "format", STR("mutatoc/1"));
	PUT(header, "namespace", STR(NS));
	J *records = ARR();
	ADD(records, header);
	for (int i = 0; i < 4 && f->classes[i].id; i++) {
		J *r = OBJ();
		char *id = class_id(f->classes[i].id);
		PUT(r, "id", STR(id));
		free(id);
		PUT(r, "type", STR("owl:Class"));
		if (source == JSON_SYNONYMS) {
			J *s = ARR();
			ADD(s, STR(f->classes[i].label));
			PUT(r, "synonyms", s);
		} else
			PUT(r, "label", STR(f->classes[i].label));
		ADD(records, r);
	}
	if (source == JSON_LABEL || source == JSON_SYNONYMS) {
		char *wire = cJSON_PrintUnformatted(records);
		DEL(records);
		return wire;
	}
	EACH(r, records) {
		char *wire = cJSON_PrintUnformatted(r);
		buf_put(&b, wire);
		buf_put(&b, "\n");
		free(wire);
	}
	DEL(records);
	return buf_take(&b);
}

static J *request(mc_engine *e, J *q)
{
	J *response = test_request(e, q);
	DEL(q);
	if (!cJSON_IsTrue(GET(response, "ok"))) {
		CHECK(0);
		if (failures <= 40)
			fprintf(stderr, "request: %s\n",
				S(GET(GET(response, "error"), "message")));
		DEL(response);
		return NULL;
	}
	J *result = cJSON_DetachItemFromObjectCaseSensitive(response, "result");
	DEL(response);
	return result;
}

enum { MODE_DEFAULT, MODE_LIVE, MODE_DEFERRED, MODE_RELOADED, MODE_COUNT };
static const char *const mode_names[] = { "default", "live", "deferred",
					  "reloaded" };

static mc_engine *load(const Family *f, int source, int mode)
{
	char *text = source_text(f, source);
	J *q = OBJ();
	PUT(q, "op", STR("load"));
	PUT(q, "content", STR(text));
	PUT(q, "format", STR(source <= TTL_ALT_LABEL ? "turtle" :
			     source == JSONL_LABEL ? "jsonl" :
						     "json"));
	PUT(q, "name", STR("plus-test"));
	PUT(q, "comma_lists", BOOL(f->comma_lists));
	if (mode == MODE_LIVE) {
		PUT(q, "interface", STR("data"));
		PUT(q, "class_based", BOOL(1));
	}
	if (mode == MODE_DEFERRED)
		PUT(q, "graph_only", BOOL(1));
	free(text);
	mc_engine *e = mc_create();
	J *r = request(e, q);
	if (!r) {
		fprintf(stderr, "load failed: %s, %s, %s\n", f->name,
			source_names[source], mode_names[mode]);
		mc_destroy(e);
		return NULL;
	}
	DEL(r);
	if (mode != MODE_RELOADED)
		return e;
	J *s = OBJ();
	PUT(s, "op", STR("snapshot"));
	J *snapshot = request(e, s);
	mc_destroy(e);
	q = OBJ();
	PUT(q, "op", STR("load"));
	PUT(q, "snapshot", snapshot);
	PUT(q, "comma_lists", BOOL(f->comma_lists));
	e = mc_create();
	r = request(e, q);
	if (!r) {
		mc_destroy(e);
		return NULL;
	}
	DEL(r);
	return e;
}

static J *entity(const char *id, const char *surface, int x, const char *type)
{
	J *e = OBJ();
	char *written = class_id(id), *canon = lowered(written);
	PUT(e, "canon", STR(canon));
	free(canon);
	free(written);
	PUT(e, "text", STR(surface));
	PUT(e, "x", NUM(x));
	PUT(e, "y", NUM(x + (int)ulen(surface)));
	PUT(e, "type", STR(type));
	return e;
}

static void leaves(Buf *b, const J *t)
{
	J *swaps = GET(t, "swaps");
	if (swaps) {
		EACH(child, GET(swaps, "tokens"))
			leaves(b, child);
	} else
		buf_put(b, S(GET(t, "text")));
}

static int known_fails, known_passing;

/*
 * A known case is one the issue above leaves failing: a mismatch counts as an
 * expected fail, and a match fails the run so the expectation gets updated.
 */
static void check_parse_known(mc_engine *e, const char *label,
			      const char *text, const J *expected, int known)
{
	parses++;
	J *q = OBJ();
	PUT(q, "op", STR("parse"));
	PUT(q, "text", STR(text));
	J *result = request(e, q), *actual = ARR();
	Buf original = { 0 };
	EACH(t, GET(result, "tokens")) {
		leaves(&original, t);
		J *swaps = GET(t, "swaps");
		if (!swaps)
			continue;
		J *item = OBJ();
		PUT(item, "canon", DUP(GET(t, "normal")));
		PUT(item, "text", DUP(GET(t, "text")));
		PUT(item, "x", DUP(GET(t, "x")));
		PUT(item, "y", DUP(GET(t, "y")));
		PUT(item, "type", DUP(GET(swaps, "type")));
		ADD(actual, item);
	}
	int same = result && cJSON_Compare(actual, expected, 1);
	if (known) {
		if (same && known_passing++ < 5)
			fprintf(stderr, "known fail now passing: %s | %s\n",
				label, text);
		known_fails += !same;
		same = !same;
	} else if (!same && failures < 40) {
		Buf b = { 0 };
		buf_put(&b, label);
		buf_put(&b, " | ");
		buf_put(&b, text);
		test_print_diff(b.p, expected, actual);
		free(b.p);
	}
	CHECK(same);
	CHECK(!strcmp(original.p ? original.p : "", text));
	if ((!same || strcmp(original.p ? original.p : "", text)) && current >= 0)
		family_failed[current]++;
	free(original.p);
	DEL(actual);
	DEL(result);
}

static void check_parse(mc_engine *e, const char *label, const char *text,
			const J *expected)
{
	check_parse_known(e, label, text, expected, 0);
}

static void check_terms(const Family *f, const char *text, const J *expected)
{
	terms_checked++;
	J *q = OBJ(), *list = ARR();
	PUT(q, "op", STR("match_terms"));
	PUT(q, "text", STR(text));
	for (int i = 0; i < 4 && f->classes[i].id; i++)
		ADD(list, STR(f->classes[i].label));
	PUT(q, "terms", list);
	mc_engine *e = mc_create();
	J *actual = request(e, q);
	mc_destroy(e);
	int same = actual && cJSON_Compare(actual, expected, 1);
	if (!same && failures < 40) {
		Buf b = { 0 };
		buf_put(&b, "match_terms ");
		buf_put(&b, f->name);
		buf_put(&b, " | ");
		buf_put(&b, text);
		test_print_diff(b.p, expected, actual);
		free(b.p);
	}
	CHECK(same);
	if (!same && current >= 0)
		family_failed[current]++;
	DEL(actual);
}

/* One authored text inside a frame, and what it must produce. */
typedef struct {
	char *text;
	J *entities; /* parse expectation */
	J *terms; /* match_terms expectation */
	int known; /* a known_underscore_span case on underscored IDs */
} Case;

static void add_entity(Case *c, const Family *f, const char *id,
		       const char *surface, int x, const char *type)
{
	ADD(c->entities, entity(id, surface, x, type));
	J *t = OBJ();
	PUT(t, "term", NUM(class_index(f, id)));
	PUT(t, "x", NUM(x));
	PUT(t, "y", NUM(x + (int)ulen(surface)));
	ADD(c->terms, t);
}

static Case framed(const char *prefix, const char *inner, const char *suffix)
{
	Buf b = { 0 };
	buf_put(&b, prefix);
	buf_put(&b, inner);
	buf_put(&b, suffix);
	Case c = { buf_take(&b), ARR(), ARR(), 0 };
	return c;
}

static void case_free(Case *c)
{
	free(c->text);
	DEL(c->entities);
	DEL(c->terms);
}

/* Every case of a family: positives and negatives in every frame and casing,
 * and scenarios as written. */
static int family_cases(const Family *f, Case **out)
{
	int n = 0, cap = 64;
	Case *cases = malloc((size_t)cap * sizeof(*cases));
	for (int frame = 0; frame < FRAME_COUNT; frame++)
		for (int casing = 0; casing < 3; casing++) {
			const char *prefix = frames[frame][0],
				   *suffix = frames[frame][1];
			int x = (int)ulen(prefix);
			for (int i = 0; i < 8 && f->positives[i].text; i++) {
				if (n + 2 > cap)
					cases = realloc(cases,
							(size_t)(cap *= 2) *
								sizeof(*cases));
				char *inner = cased(f->positives[i].text, casing);
				Case c = framed(prefix, inner, suffix);
				add_entity(&c, f, f->positives[i].id, inner, x,
					   f->positives[i].type);
				c.known = underscored && f->known_underscore_span &&
					  !strcmp(f->positives[i].type, "spans");
				cases[n++] = c;
				free(inner);
			}
			for (int i = 0; i < 8 && f->negatives[i]; i++) {
				if (n + 2 > cap)
					cases = realloc(cases,
							(size_t)(cap *= 2) *
								sizeof(*cases));
				char *inner = cased(f->negatives[i], casing);
				cases[n++] = framed(prefix, inner, suffix);
				free(inner);
			}
		}
	for (int i = 0; i < 4 && f->scenarios[i].text; i++) {
		const Scenario *s = &f->scenarios[i];
		if (!s->entities[0].id)
			continue;
		if (n + 2 > cap)
			cases = realloc(cases, (size_t)(cap *= 2) * sizeof(*cases));
		Case c = framed("", s->text, "");
		for (int j = 0; j < 4 && s->entities[j].id; j++)
			add_entity(&c, f, s->entities[j].id,
				   s->entities[j].surface, s->entities[j].x,
				   s->entities[j].type);
		cases[n++] = c;
	}
	*out = cases;
	return n;
}

static void run_cases(mc_engine *e, const char *label, Case *cases, int n)
{
	for (int i = 0; i < n; i++)
		check_parse_known(e, label, cases[i].text, cases[i].entities,
				  cases[i].known);
}

static void test_loads(void)
{
	for (underscored = 0; underscored < 2; underscored++)
	for (int fi = 0; fi < FAMILY_COUNT; fi++) {
		const Family *f = &families[fi];
		current = fi;
		Case *cases;
		int n = family_cases(f, &cases);
		for (int source = 0; source < SOURCE_COUNT; source++)
			for (int mode = 0; mode < MODE_COUNT; mode++) {
				mc_engine *e = load(f, source, mode);
				CHECK(e != NULL);
				if (!e)
					continue;
				Buf label = { 0 };
				buf_put(&label, f->name);
				buf_put(&label, ", ");
				buf_put(&label, source_names[source]);
				buf_put(&label, ", ");
				buf_put(&label, mode_names[mode]);
				if (underscored)
					buf_put(&label, ", underscored IDs");
				run_cases(e, label.p, cases, n);
				free(label.p);
				mc_destroy(e);
			}
		/* Terms have no class IDs, so they run once. */
		if (!f->skip_terms && !underscored)
			for (int i = 0; i < n; i++)
				check_terms(f, cases[i].text, cases[i].terms);
		for (int i = 0; i < n; i++)
			case_free(&cases[i]);
		free(cases);
	}
	underscored = 0;
}

/* Turtle fragments for update use full IRIs. */
static char *fragment(const char *id, const char *label)
{
	Buf b = { 0 };
	char *written = class_id(id);
	buf_put(&b, "<" NS);
	buf_put(&b, written);
	free(written);
	buf_put(&b, "> <http://www.w3.org/1999/02/22-rdf-syntax-ns#type> "
		    "<http://www.w3.org/2002/07/owl#Class> ; "
		    "<http://www.w3.org/2000/01/rdf-schema#label> ");
	quoted(&b, label);
	buf_put(&b, " .\n");
	return buf_take(&b);
}

static J *update_request(const char *id, const char *text)
{
	J *q = OBJ();
	PUT(q, "op", STR("update"));
	Buf iri = { 0 };
	char *written = class_id(id);
	buf_put(&iri, NS);
	buf_put(&iri, written);
	free(written);
	if (text) {
		J *replace = OBJ();
		PUT(replace, iri.p, STR(text));
		PUT(q, "replace", replace);
	} else {
		J *remove = ARR();
		ADD(remove, STR(iri.p));
		PUT(q, "remove", remove);
	}
	free(iri.p);
	return q;
}

static mc_engine *load_turtle(const char *text, int live)
{
	J *q = OBJ();
	PUT(q, "op", STR("load"));
	PUT(q, "turtle", STR(text));
	PUT(q, "name", STR("plus-update"));
	PUT(q, "class_based", BOOL(1));
	PUT(q, "interface", STR(live ? "data" : "json"));
	mc_engine *e = mc_create();
	J *r = request(e, q);
	DEL(r);
	return e;
}

static J *snapshot_of(mc_engine *e)
{
	J *q = OBJ();
	PUT(q, "op", STR("snapshot"));
	return request(e, q);
}

/*
 * Each family's classes arrive one update at a time on an engine that holds
 * one seed class. The result matches a fresh load of the same document and
 * gives every authored match.
 */
static void test_updates(void)
{
	static const char seed_id[] = "Seed";
	for (underscored = 0; underscored < 2; underscored++)
	for (int fi = 0; fi < FAMILY_COUNT; fi++) {
		const Family *f = &families[fi];
		if (f->comma_lists)
			continue;
		current = fi;
		Case *cases;
		int n = family_cases(f, &cases);
		for (int live = 0; live < 2; live++) {
			char *seed = fragment(seed_id, "seed");
			Buf document = { 0 };
			buf_put(&document, seed);
			mc_engine *e = load_turtle(seed, live);
			free(seed);
			for (int i = 0; i < 4 && f->classes[i].id; i++) {
				char *text = fragment(f->classes[i].id,
						      f->classes[i].label);
				J *r = request(e, update_request(f->classes[i].id,
								 text));
				CHECK(r != NULL);
				DEL(r);
				buf_put(&document, text);
				free(text);
			}
			mc_engine *fresh = load_turtle(document.p, live);
			J *a = snapshot_of(e), *b = snapshot_of(fresh);
			int same = a && b && cJSON_Compare(a, b, 1);
			if (!same && failures < 40)
				fprintf(stderr, "update snapshot differs: %s live=%d\n",
					f->name, live);
			CHECK(same);
			DEL(a);
			DEL(b);
			Buf label = { 0 };
			buf_put(&label, f->name);
			buf_put(&label, live ? ", update live" : ", update");
			if (underscored)
				buf_put(&label, ", underscored IDs");
			run_cases(e, label.p, cases, n);
			run_cases(fresh, label.p, cases, n);
			free(label.p);
			free(document.p);
			mc_destroy(fresh);
			mc_destroy(e);
		}
		for (int i = 0; i < n; i++)
			case_free(&cases[i]);
		free(cases);
	}
	underscored = 0;
}

static J *expect_one(const char *id, const char *surface, int x,
		     const char *type)
{
	J *a = ARR();
	if (id)
		ADD(a, entity(id, surface, x, type));
	return a;
}

/* A label rewritten by update changes which texts match, in both directions.
 * A second class keeps the ontology from going empty when Lang is removed. */
static void test_update_sequence(void)
{
	for (int live = 0; live < 2; live++) {
		char *lang = fragment("Lang", "C Programming"),
		     *other = fragment("Seed", "seed");
		Buf start = { 0 };
		buf_put(&start, lang);
		buf_put(&start, other);
		mc_engine *e = load_turtle(start.p, live);
		free(start.p);
		free(lang);
		free(other);
		J *none = ARR(), *c_prog = expect_one("Lang", "C Programming", 0,
						    "exact"),
		  *cpp_prog = expect_one("Lang", "C++ Programming", 0, "exact");
		check_parse(e, "sequence start", "C Programming", c_prog);
		check_parse(e, "sequence start", "C++ Programming", none);
		static const char *const labels[] = { "C++ Programming",
						      "C Programming",
						      "C++ Programming" };
		for (int step = 0; step < 3; step++) {
			char *text = fragment("Lang", labels[step]);
			J *r = request(e, update_request("Lang", text));
			free(text);
			CHECK(r != NULL);
			DEL(r);
			int plus = step != 1;
			check_parse(e, "sequence", "C++ Programming",
				    plus ? cpp_prog : none);
			check_parse(e, "sequence", "C Programming",
				    plus ? none : c_prog);
		}
		J *r = request(e, update_request("Lang", NULL));
		CHECK(r != NULL);
		DEL(r);
		check_parse(e, "sequence removed", "C++ Programming", none);
		check_parse(e, "sequence removed", "C Programming", none);
		DEL(none);
		DEL(c_prog);
		DEL(cpp_prog);
		mc_destroy(e);
	}
}

static void test_plus_syntax(void)
{
	static const struct {
		const char *text;
		int syntax;
	} cases[] = {
		{ "alpha+beta", 1 },
		{ "alpha + beta", 1 },
		{ " alpha+beta ", 1 },
		{ "alpha+beta+gamma", 1 },
		{ "ALPHA+BETA", 1 },
		{ "alpha +beta", 1 },
		{ "alpha+ beta", 1 },
		{ "abdominal_wound+penetrating_wound", 1 },
		{ "Human Behavior + Social Environment", 1 },
		{ "u.s+history+to+1877", 1 },
		{ "Decrease+ brain function", 1 },
		{ "A+ Certification", 1 },
		{ "decrease+, Decreases", 1 },
		{ "a+b", 1 },
		{ "1+2", 1 },
		{ "x+.", 1 },
		{ "alpha+\xc2\xad", 1 }, /* a soft hyphen is not whitespace */
		{ "C++", 0 },
		{ "C+", 0 },
		{ "C++ Programming", 0 },
		{ "Notepad++", 0 },
		{ "+", 0 },
		{ "++", 0 },
		{ "+++", 0 },
		{ " + ", 0 },
		{ "+beta", 0 },
		{ " +beta", 0 },
		{ "alpha+", 0 },
		{ "alpha+ ", 0 },
		{ "alpha++beta", 0 },
		{ "alpha+ +beta", 0 },
		{ "alpha+\t+beta", 0 },
		{ "alpha+\n+beta", 0 },
		{ "alpha+\xe3\x80\x80+beta", 0 }, /* ideographic space */
		{ "+alpha+beta", 0 },
		{ "alpha+beta+", 0 },
		{ "Hypomenorrhea+", 0 },
		{ "decrease+", 0 },
		{ "plain text", 0 },
		{ "", 0 },
		{ "alpha_beta", 0 },
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
		int got = plus_syntax(cases[i].text);
		if (got != cases[i].syntax && failures < 40)
			fprintf(stderr, "plus_syntax(\"%s\") = %d, expected %d\n",
				cases[i].text, got, cases[i].syntax);
		CHECK(got == cases[i].syntax);
	}
}

static int has_empty_word(const J *spans)
{
	int found = 0;
	EACH(key, spans) {
		found |= !*key->string;
		EACH(r, key)
			EACH(w, GET(r, "content"))
				found |= !*S(w);
	}
	return found;
}

/* The views a literal produces: its plus signs stay in the synonym, and
 * only span syntax makes a span rule. */
static void test_views(void)
{
	mc_engine *e = mc_create();
	for (int fi = 0; fi < FAMILY_COUNT; fi++) {
		const Family *f = &families[fi];
		if (f->comma_lists)
			continue;
		for (int i = 0; i < 4 && f->classes[i].id; i++) {
			const char *label = f->classes[i].label;
			if (!strchr(label, '+'))
				continue;
			int syntax = plus_syntax(label);
			for (int plus_only = 0; plus_only < 2; plus_only++) {
				J *q = OBJ(), *data = OBJ(), *values = ARR();
				ADD(values, STR(label));
				PUT(data, "Key", values);
				PUT(q, "op", STR("generate_spans"));
				PUT(q, "data", data);
				PUT(q, "plus_only", BOOL(plus_only));
				J *spans = request(e, q);
				CHECK(spans != NULL);
				CHECK(!has_empty_word(spans));
				if (plus_only)
					CHECK(syntax ? SIZE(spans) > 0 :
						       SIZE(spans) == 0);
				DEL(spans);
			}
			J *q = OBJ(), *data = OBJ(), *values = ARR();
			ADD(values, STR(label));
			PUT(data, "Key", values);
			PUT(q, "op", STR("generate_synonyms"));
			PUT(q, "data", data);
			J *fwd = request(e, q);
			J *forms = GET(fwd, "key");
			char *written = lowered(label);
			char *flat_source = replace(written, "+", " ");
			char *flat = norm(flat_source, 1, 1);
			if (syntax) {
				CHECK(contains(forms, flat));
				CHECK(!contains(forms, written));
			} else {
				char *trimmed = norm(written, 1, 1);
				CHECK(contains(forms, trimmed));
				if (*flat)
					CHECK(!contains(forms, flat));
				free(trimmed);
			}
			free(flat);
			free(flat_source);
			free(written);
			DEL(fwd);
		}
	}
	mc_destroy(e);
}

/* Terms that differ only in the whitespace around literal plus signs are one
 * term, and the first one reports the match. */
static void test_term_identity(void)
{
	static const char *const variants[] = { "C++ Programming",
						"C ++ Programming",
						"c+ +  programming",
						"C++Programming" };
	J *q = OBJ(), *list = ARR();
	for (int i = 0; i < 4; i++)
		ADD(list, STR(variants[i]));
	PUT(q, "op", STR("match_terms"));
	PUT(q, "text", STR("C++ Programming. C + + Programming."));
	PUT(q, "terms", list);
	mc_engine *e = mc_create();
	J *actual = request(e, q);
	mc_destroy(e);
	J *expected = cJSON_Parse("[{\"term\":0,\"x\":0,\"y\":15},"
				  "{\"term\":0,\"x\":17,\"y\":34}]");
	int same = actual && cJSON_Compare(actual, expected, 1);
	if (!same)
		test_print_diff("term identity", expected, actual);
	CHECK(same);
	DEL(actual);
	DEL(expected);
}

/* The fixture ontology that holds Hypomenorrhea+ as a seeAlso. */
static void test_fixture(void)
{
	char *path = test_path(root,
			       "tests/fixtures/ontologies/medic-copilot-20230818.owl");
	J *q = OBJ();
	PUT(q, "op", STR("load"));
	PUT(q, "path", STR(path));
	PUT(q, "format", STR("turtle"));
	free(path);
	mc_engine *e = mc_create();
	J *r = request(e, q);
	CHECK(r != NULL);
	DEL(r);
	/* The ontology names many common words, so the texts hold only the
	 * literal. */
	J *plus = expect_one("Menorrhagia", "Hypomenorrhea+", 0, "exact"),
	  *lower_plus = expect_one("Menorrhagia", "hypomenorrhea +", 0, "exact"),
	  *none = ARR();
	check_parse(e, "fixture", "Hypomenorrhea+", plus);
	check_parse(e, "fixture", "hypomenorrhea +", lower_plus);
	check_parse(e, "fixture", "Hypomenorrhea", none);
	DEL(lower_plus);
	J *s = snapshot_of(e);
	CHECK(s && !has_empty_word(GET(s, "spans")));
	DEL(s);
	DEL(plus);
	DEL(none);
	mc_destroy(e);
}

int main(int argc, char **argv)
{
	root = argc > 1 ? argv[1] : ".";
	test_plus_syntax();
	test_views();
	test_term_identity();
	test_update_sequence();
	test_loads();
	test_updates();
	current = -1;
	test_fixture();
	for (int fi = 0; fi < FAMILY_COUNT; fi++)
		if (family_failed[fi])
			fprintf(stderr, "family %s: %d failed checks\n",
				families[fi].name, family_failed[fi]);
	printf("plus_literals: %d families, %d parses, %d match_terms calls, "
	       "%d assertions, %d failures, %d expected fails, %d expected "
	       "fails now passing\n",
	       FAMILY_COUNT, parses, terms_checked, assertions, failures,
	       known_fails, known_passing);
	return failures ? 1 : 0;
}
