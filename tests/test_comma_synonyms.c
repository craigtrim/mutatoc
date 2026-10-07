/*
 * test_comma_synonyms.c - Whole comma literals and opt-in synonym lists.
 *
 * Authored expectations cover four predicates, six literal shapes, twelve
 * sentence frames, four casings and five comma spacings across Turtle,
 * JSON, JSONL and reloaded snapshots. No expected match comes from the
 * engine. Numeric commas stay intact in list mode. craigtrim/mutatoc#15
 */

#include "testlib.h"

static int assertions, failures, cases, positives, negatives;
static const char *root;

#define CHECK(value)                                                  \
	do {                                                          \
		assertions++;                                         \
		if (!(value)) {                                       \
			if (failures++ < 40)                          \
				fprintf(stderr, "FAIL line %d: %s\n", \
					__LINE__, #value);            \
		}                                                     \
	} while (0)

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

static J *load_request(const char *extension, int lists, int live, int deferred)
{
	Buf relative = { 0 };
	buf_put(&relative, "tests/fixtures/ontologies/comma-synonyms-test.");
	buf_put(&relative, extension);
	char *path = test_path(root, relative.p);
	J *q = OBJ();
	PUT(q, "op", STR("load"));
	PUT(q, "path", STR(path));
	PUT(q, "name", STR("comma-test"));
	if (lists >= 0)
		PUT(q, "comma_lists", BOOL(lists));
	if (live) {
		PUT(q, "interface", STR("data"));
		PUT(q, "class_based", BOOL(1));
	}
	if (deferred)
		PUT(q, "graph_only", BOOL(1));
	free(path);
	free(relative.p);
	return q;
}

static mc_engine *load(const char *extension, int lists, int live, int deferred)
{
	mc_engine *e = mc_create();
	J *r = request(e, load_request(extension, lists, live, deferred));
	if (!r) {
		mc_destroy(e);
		return NULL;
	}
	DEL(r);
	return e;
}

static J *snapshot(mc_engine *e)
{
	J *q = OBJ();
	PUT(q, "op", STR("snapshot"));
	return request(e, q);
}

static mc_engine *reload(mc_engine *e, int lists)
{
	J *q = OBJ();
	PUT(q, "op", STR("load"));
	PUT(q, "snapshot", snapshot(e));
	PUT(q, "comma_lists", BOOL(lists));
	mc_engine *other = mc_create();
	J *r = request(other, q);
	DEL(r);
	return other;
}

static J *entity(const char *canon, const char *text, int x)
{
	J *e = OBJ();
	PUT(e, "canon", STR(canon));
	PUT(e, "text", STR(text));
	PUT(e, "x", NUM(x));
	PUT(e, "y", NUM(x + (int)ulen(text)));
	PUT(e, "type", STR("exact"));
	PUT(e, "confidence", NUM(100));
	return e;
}

static J *one(const char *canon, const char *text, int x)
{
	J *a = ARR();
	if (canon)
		ADD(a, entity(canon, text, x));
	return a;
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

static void check_parse(mc_engine *e, const char *text, const J *expected)
{
	cases++;
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
		PUT(item, "confidence", DUP(GET(swaps, "confidence")));
		ADD(actual, item);
	}
	int same = result && cJSON_Compare(actual, expected, 1);
	if (!same && failures < 40)
		test_print_diff(text, expected, actual);
	CHECK(same);
	CHECK(!strcmp(original.p ? original.p : "", text));
	free(original.p);
	DEL(actual);
	DEL(result);
}

static void check_one(mc_engine *e, const char *text, const char *canon)
{
	J *expected = one(canon, text, 0);
	check_parse(e, text, expected);
	DEL(expected);
}

typedef struct {
	const char *phrase, *canon;
} Literal;

static const Literal literals[] = {
	{ "Cobalt, Meridian", "label_one" },
	{ "Equality, Crime, And Justice", "equalitycrimeandjustice" },
	{ "Amber, Birch, Cedar, Dahlia", "label_three" },
	{ "Power, Privilege & Diversity", "label_marks" },
	{ "Economic analysis, comparative", "label_inverted" },
	{ "Top 1,000 Words", "label_number" },
	{ "Dune, Estuary", "see_one" },
	{ "Falcon, Grove, Harbor", "see_two" },
	{ "Indigo, Juniper, Kestrel, Lagoon", "see_three" },
	{ "Optics, Lens/Prism", "see_marks" },
	{ "Data analysis, graphical", "see_inverted" },
	{ "First 2,000 Terms", "see_number" },
	{ "Marble, Nebula", "alt_one" },
	{ "Ochre, Pebble, Quartz", "alt_two" },
	{ "Raven, Saffron, Thistle, Umber", "alt_three" },
	{ "Signal, Wave & Frequency", "alt_marks" },
	{ "Reasoning, quantitative", "alt_inverted" },
	{ "Beyond 3,000 Entries", "alt_number" },
	{ "Violet, Willow", "inflect_one" },
	{ "Xenon, Yarrow, Zephyr", "inflect_two" },
	{ "Acorn, Badger, Coral, Driftwood", "inflect_three" },
	{ "Minerals, Ore/Rock", "inflect_marks" },
	{ "Research methods, experimental", "inflect_inverted" },
	{ "Next 4,000 Samples", "inflect_number" },
};

/* Same twelve frames as test_punctuated_synonyms.c. */
static const char *frames[][2] = {
	{ "", "" },
	{ "I took ", " last fall." },
	{ "(", ")" },
	{ "\"", "\"" },
	{ "[", "]" },
	{ "Notes: ", "" },
	{ "", ", then lunch" },
	{ "Before ", "; after" },
	{ "- ", " -" },
	{ "Enrolled in ", "!" },
	{ "Q: ", "?" },
	{ "Two courses: ", " and lunch." },
};

static char *recase(const char *text, int mode)
{
	char *out = copy(text);
	int start = 1;
	for (char *p = out; *p; p++) {
		if (mode == 1 || mode == 3)
			*p = (char)tolower((unsigned char)*p);
		if (mode == 2 || (mode == 3 && start))
			*p = (char)toupper((unsigned char)*p);
		start = !isalpha((unsigned char)*p);
	}
	return out;
}

static char *spaced(const char *text, int mode)
{
	static const char *separators[] = { ", ", ",", " ,", " , ", ",  " };
	Buf b = { 0 };
	for (const char *p = text; *p; p++) {
		if (*p == ',') {
			buf_put(&b, separators[mode]);
			while (p[1] == ' ')
				p++;
		} else
			buf_add(&b, p, 1);
	}
	return buf_take(&b);
}

static void matrix(mc_engine **engines)
{
	for (size_t s = 0; s < sizeof(literals) / sizeof(*literals); s++) {
		int number = s % 6 == 5;
		for (int c = 0; c < 4; c++)
			for (int space = 0; space < (number ? 1 : 5); space++) {
				char *cased = recase(literals[s].phrase, c);
				char *variant = number ? copy(cased) :
							 spaced(cased, space);
				for (size_t f = 0;
				     f < sizeof(frames) / sizeof(*frames);
				     f++) {
					Buf b = { 0 };
					buf_put(&b, frames[f][0]);
					buf_put(&b, variant);
					buf_put(&b, frames[f][1]);
					J *expected =
						one(literals[s].canon, variant,
						    (int)ulen(frames[f][0]));
					for (int l = 0; l < 4; l++) {
						check_parse(engines[l], b.p,
							    expected);
						positives++;
					}
					DEL(expected);
					free(b.p);
				}
				free(variant);
				free(cased);
			}
		/* Deliberately split numeric commas too: their pieces must not match. */
		J *pieces = split(literals[s].phrase, ",");
		EACH(piece, pieces) {
			char *trimmed = norm(S(piece), 0, 0);
			for (int c = 0; c < 2; c++) {
				char *variant = recase(trimmed, c);
				for (size_t f = 0;
				     f < sizeof(frames) / sizeof(*frames);
				     f++) {
					Buf b = { 0 };
					buf_put(&b, frames[f][0]);
					buf_put(&b, variant);
					buf_put(&b, frames[f][1]);
					J *expected = ARR();
					for (int l = 0; l < 4; l++) {
						check_parse(engines[l], b.p,
							    expected);
						negatives++;
					}
					DEL(expected);
					free(b.p);
				}
				free(variant);
			}
			free(trimmed);
		}
		DEL(pieces);
	}
	CHECK(positives == 19968);
	CHECK(negatives == 5760);
}

static void authored(mc_engine *e)
{
	check_one(e, "Equality, Crime, and Justice", "equalitycrimeandjustice");
	check_one(e, "Crime And Punishment", "crimeandpunishment");
	check_one(e, "Crime", NULL);
	check_one(e, "Equality", NULL);
	check_one(e, "And Justice", NULL);
	check_one(e, "Juvenile Crime", NULL);
	check_one(e, "Equality Crime and Justice", NULL);
	check_one(e, "Top 1,000 Words", "label_number");
	check_one(e, "Top 1", NULL);
	check_one(e, "000 Words", NULL);
	check_one(e, "Equality\t,\tCrime  ,  and Justice",
		  "equalitycrimeandjustice");
	check_one(e, "Equality  ,\t Crime,\tand Justice",
		  "equalitycrimeandjustice");
	check_one(e, "PRBC, PRBCs", "packed_blood");
	check_one(e, "PRBC", NULL);
	check_one(e, "PRBCs", NULL);
	check_one(e, "stab injury", NULL);
	check_one(e, "penetrate injury", NULL);

	static const char *surfaces[] = { "Equality, Crime, and Justice",
					  "Top 1,000 Words",
					  "Economic analysis, comparative" };
	static const char *canons[] = { "equalitycrimeandjustice",
					"label_number", "label_inverted" };
	for (int lines = 0; lines < 2; lines++) {
		Buf b = { 0 };
		J *expected = ARR();
		for (int i = 0; i < 3; i++) {
			if (i)
				buf_put(&b, lines ? "\r\n" : "; then ");
			ADD(expected, entity(canons[i], surfaces[i],
					     (int)ulen(b.p ? b.p : "")));
			buf_put(&b, surfaces[i]);
		}
		check_parse(e, b.p, expected);
		DEL(expected);
		free(b.p);
	}
	/* Code point offsets remain correct after a multibyte prefix. */
	J *expected = one("equalitycrimeandjustice", surfaces[0], 5);
	check_parse(e, "café Equality, Crime, and Justice", expected);
	DEL(expected);
}

static void views(mc_engine *e, int lists)
{
	J *s = snapshot(e), *fwd = GET(GET(s, "synonyms"), "fwd");
	for (size_t i = 0; i < sizeof(literals) / sizeof(*literals); i++) {
		char *phrase = lower(literals[i].phrase);
		J *values = GET(fwd, literals[i].canon);
		int number = i % 6 == 5;
		CHECK(contains(values, phrase) == (!lists || number));
		J *parts = split(phrase, ",");
		EACH(part, parts) {
			char *piece = norm(S(part), 0, 0);
			CHECK(contains(values, piece) == (lists && !number));
			free(piece);
		}
		DEL(parts);
		free(phrase);
	}
	J *spans = GET(s, "spans");
	if (!lists) {
		/* Ordinary span rules may use the whole name, never either heading piece. */
		EACH(r, GET(spans, "economic")) {
			if (!strcmp(S(GET(r, "canon")), "label_inverted"))
				CHECK(contains(GET(r, "content"),
					       "analysis,") &&
				      contains(GET(r, "content"),
					       "comparative"));
		}
		EACH(r, GET(spans, "research")) {
			if (!strcmp(S(GET(r, "canon")), "inflect_inverted"))
				CHECK(contains(GET(r, "content"), "methods,") &&
				      contains(GET(r, "content"),
					       "experimental"));
		}
		CHECK(!contains(GET(fwd, "packed_injury"), "stab injury"));
		CHECK(contains(GET(fwd, "packed_injury"),
			       "stab injury, penetrate injury"));
	} else {
		CHECK(contains(GET(fwd, "packed_blood"), "prbc"));
		CHECK(contains(GET(fwd, "packed_blood"), "prbcs"));
		CHECK(SIZE(GET(spans, "stab")) == 1);
		CHECK(SIZE(GET(spans, "penetrate")) == 1);
		CHECK(contains(GET(AT(GET(spans, "stab"), 0), "content"),
			       "injury"));
		CHECK(contains(GET(AT(GET(spans, "penetrate"), 0), "content"),
			       "injury"));
		CHECK(SIZE(GET(AT(GET(spans, "stab"), 0), "content")) == 1);
	}
	DEL(s);
}

static void list_mode(mc_engine *e)
{
	check_one(e, "PRBC", "packed_blood");
	check_one(e, "PRBCs", "packed_blood");
	check_one(e, "stab injury", "packed_injury");
	check_one(e, "penetrate injury", "packed_injury");
	check_one(e, "Top 1,000 Words", "label_number");
	check_one(e, "Top 1", NULL);
	check_one(e, "000 Words", NULL);
	views(e, 1);
}

static void inline_sources(void)
{
	static const char *formats[] = { "ttl", "json", "jsonl" };
	for (int f = 0; f < 3; f++) {
		J *file = load_request(formats[f], -1, 0, 0);
		mc_error error = { 0 };
		char *text = read_file(S(GET(file, "path")), &error);
		DEL(file);
		CHECK(text && !error.code);
		if (!text)
			continue;
		for (int lists = 0; lists < 2; lists++) {
			mc_engine *e = mc_create();
			J *q = OBJ();
			PUT(q, "op", STR("load"));
			PUT(q, "content", STR(text));
			PUT(q, "format", STR(formats[f]));
			PUT(q, "comma_lists", BOOL(lists));
			J *r = request(e, q);
			DEL(r);
			if (lists)
				list_mode(e);
			else {
				authored(e);
				views(e, 0);
			}
			mc_destroy(e);
		}
		free(text);
	}
}

static void collections(void)
{
	for (int live = 0; live < 2; live++) {
		mc_engine *e = mc_create();
		for (int lists = 0; lists < 2; lists++) {
			J *q = OBJ(), *sources = ARR(), *a = OBJ(), *b = OBJ();
			PUT(a, "format", STR("json"));
			PUT(a, "content",
			    STR("[{\"id\":\"http://example.org/#First\","
				"\"label\":\"Alpine, Boreal\"}]"));
			PUT(b, "turtle",
			    STR("@prefix rdfs: <" RDFS "> . "
				"<http://example.org/#Second> rdfs:label \"Cinder, Delta\" ."));
			PUT(b, "comma_lists", BOOL(!lists));
			ADD(sources, a);
			ADD(sources, b);
			PUT(q, "op", STR("load"));
			PUT(q, "sources", sources);
			PUT(q, "comma_lists", BOOL(lists));
			if (live)
				PUT(q, "interface", STR("data"));
			J *r = request(e, q);
			DEL(r);
			check_one(e, "Alpine", lists ? "first" : NULL);
			check_one(e, "Cinder", lists ? NULL : "second");
			check_one(e, lists ? "Cinder, Delta" : "Alpine, Boreal",
				  lists ? "second" : "first");
		}
		J *single = load_request("jsonl", -1, 0, 0);
		J *q = OBJ(), *paths = ARR();
		ADD(paths, DUP(GET(single, "path")));
		PUT(q, "op", STR("load"));
		PUT(q, "paths", paths);
		PUT(q, "comma_lists", BOOL(1));
		if (live)
			PUT(q, "interface", STR("data"));
		J *r = request(e, q);
		DEL(r);
		DEL(single);
		list_mode(e);
		mc_destroy(e);
	}
}

static void reload_options(void)
{
	mc_engine *e = load("ttl", 1, 1, 0);
	if (!e)
		return;
	list_mode(e);
	J *before = snapshot(e);
	static const char *invalid[] = { "null", "1", "\"true\"", "[]", "{}" };
	for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
		J *q = load_request("json", -1, 0, 0);
		PUT(q, "comma_lists", cJSON_Parse(invalid[i]));
		J *r = test_request(e, q);
		CHECK(cJSON_IsFalse(GET(r, "ok")));
		CHECK(GET(GET(r, "error"), "code") &&
		      GET(GET(r, "error"), "code")->valueint == 2);
		DEL(r);
		DEL(q);
	}
	/* A failed collection must not replace the old options or live index. */
	J *q = OBJ(), *sources = ARR();
	ADD(sources, load_request("json", 0, 0, 0));
	J *bad = OBJ();
	PUT(bad, "turtle", STR("invalid turtle"));
	ADD(sources, bad);
	PUT(q, "op", STR("load"));
	PUT(q, "sources", sources);
	J *r = test_request(e, q);
	CHECK(cJSON_IsFalse(GET(r, "ok")));
	DEL(r);
	DEL(q);
	J *after = snapshot(e);
	CHECK(cJSON_Compare(before, after, 1));
	DEL(before);
	DEL(after);
	check_one(e, "PRBC", "packed_blood");
	/* An omitted option on a later load resets to whole literals. */
	r = request(e, load_request("json", -1, 1, 1));
	DEL(r);
	check_one(e, "PRBC", NULL);
	check_one(e, "PRBC, PRBCs", "packed_blood");
	mc_destroy(e);
}

static void generated_rules(void)
{
	mc_engine *e = mc_create();
	for (int lists = 0; lists < 2; lists++)
		for (int plus = 0; plus < 2; plus++) {
			J *q = OBJ(), *data = OBJ(), *a = ARR(), *b = ARR();
			ADD(a, STR("stab+injury, penetrate+injury"));
			ADD(b, STR("civil justice, reform"));
			PUT(data, "wound_pattern", a);
			PUT(data, "civic_title", b);
			PUT(q, "op", STR("generate_spans"));
			PUT(q, "data", data);
			PUT(q, "comma_lists", BOOL(lists));
			PUT(q, "plus_only", BOOL(plus));
			J *rules = request(e, q);
			CHECK(SIZE(GET(rules, "stab")) == 1);
			J *content = GET(AT(GET(rules, "stab"), 0), "content");
			CHECK(SIZE(content) == (lists ? 1 : 2));
			CHECK(contains(content, "injury"));
			CHECK(contains(content, "injury, penetrate") == !lists);
			CHECK(SIZE(GET(rules, "penetrate")) == lists);
			CHECK(SIZE(GET(rules, "civil")) == !plus);
			if (!plus) {
				content = GET(AT(GET(rules, "civil"), 0),
					      "content");
				CHECK(SIZE(content) == (lists ? 1 : 2));
				CHECK(contains(content,
					       lists ? "justice" : "justice,"));
				CHECK(contains(content, "reform") == !lists);
			}
			DEL(rules);
		}
	mc_destroy(e);
}

static void numeric_lists(void)
{
	static const char *literals[] = { "Top 1,000,000 Words",
					  "Count ١,٠٠٠ Units",
					  "Count １,０００ Units",
					  "Ratio 3,14 Value" };
	for (size_t i = 0; i < sizeof(literals) / sizeof(*literals); i++)
		for (int lists = 0; lists < 2; lists++) {
			J *records = ARR(), *record = OBJ();
			PUT(record, "id", STR("http://example.org/#Number"));
			PUT(record, "label", STR(literals[i]));
			ADD(records, record);
			char *wire = cJSON_PrintUnformatted(records);
			J *q = OBJ();
			PUT(q, "op", STR("load"));
			PUT(q, "content", STR(wire));
			PUT(q, "format", STR("json"));
			PUT(q, "comma_lists", BOOL(lists));
			mc_engine *e = mc_create();
			J *r = request(e, q);
			DEL(r);
			check_one(e, literals[i], "number");
			J *s = snapshot(e);
			CHECK(SIZE(GET(GET(GET(s, "synonyms"), "fwd"),
				       "number")) == 1);
			DEL(s);
			mc_destroy(e);
			free(wire);
			DEL(records);
		}
}

static void econ(void)
{
	mc_engine *e = mc_create();
	char *path =
		test_path(root, "tests/fixtures/ontologies/econ-20160218.owl");
	J *q = OBJ();
	PUT(q, "op", STR("load"));
	PUT(q, "path", STR(path));
	J *r = request(e, q);
	DEL(r);
	free(path);
	check_one(e, "Economic analysis, comparative",
		  "comparative_economic_analysis");
	check_one(e, "Reasoning, quantitative", "quantitative_reasoning");
	check_one(e, "comparative", NULL);
	check_one(e, "quantitative", NULL);
	mc_destroy(e);
}

static void mixed_numeric_lists(void)
{
	static const char *formats[] = { "ttl", "json", "jsonl" };
	static const char *sources[] = {
		"<http://example.org/#Mixed> <" RDFS "label> "
		"\" , Top 1,000 Words, Count ١,٠٠٠ Units, PRBC, , \" .",
		"[{\"id\":\"http://example.org/#Mixed\","
		"\"label\":\" , Top 1,000 Words, Count ١,٠٠٠ Units, PRBC, , \"}]",
		"{\"id\":\"http://example.org/#Mixed\","
		"\"label\":\" , Top 1,000 Words, Count ١,٠٠٠ Units, PRBC, , \"}\n"
	};
	for (int f = 0; f < 3; f++) {
		mc_engine *e = mc_create();
		J *q = OBJ();
		PUT(q, "op", STR("load"));
		PUT(q, "content", STR(sources[f]));
		PUT(q, "format", STR(formats[f]));
		PUT(q, "comma_lists", BOOL(1));
		J *r = request(e, q);
		DEL(r);
		check_one(e, "Top 1,000 Words", "mixed");
		check_one(e, "Count ١,٠٠٠ Units", "mixed");
		check_one(e, "PRBC", "mixed");
		check_one(e, "Top 1", NULL);
		check_one(e, "000 Words", NULL);
		check_one(e, "Count ١", NULL);
		check_one(e, "٠٠٠ Units", NULL);
		J *s = snapshot(e);
		CHECK(SIZE(GET(GET(GET(s, "synonyms"), "fwd"), "mixed")) == 3);
		DEL(s);
		mc_destroy(e);
	}
}

static void prepared(mc_engine *e)
{
	static const char *texts[] = { "Equality", ", ",   "Crime",
				       ", ",	   "and ", "Justice" };
	static const char *forms[] = { "equality", ",",	  "crime",
				       ",",	   "and", "justice" };
	J *tokens = ARR();
	int x = 0;
	for (int i = 0; i < 6; i++) {
		J *t = OBJ();
		PUT(t, "id", NUM(i));
		PUT(t, "x", NUM(x));
		PUT(t, "y", NUM(x + strlen(forms[i])));
		PUT(t, "text", STR(texts[i]));
		PUT(t, "normal", STR(forms[i]));
		PUT(t, "caller", STR("retain this field"));
		ADD(tokens, t);
		x += (int)strlen(texts[i]);
	}
	J *q = OBJ();
	PUT(q, "op", STR("parse_tokens"));
	PUT(q, "tokens", DUP(tokens));
	J *r = request(e, q);
	J *t = AT(GET(r, "tokens"), 0), *swaps = GET(t, "swaps");
	cases++;
	CHECK(SIZE(GET(r, "tokens")) == 1);
	CHECK(!strcmp(S(GET(t, "normal")), "equalitycrimeandjustice"));
	CHECK(!strcmp(S(GET(swaps, "type")), "exact"));
	CHECK(GET(t, "x") && GET(t, "x")->valueint == 0);
	CHECK(GET(t, "y") && GET(t, "y")->valueint == 28);
	CHECK(cJSON_Compare(GET(swaps, "tokens"), tokens, 1));
	DEL(r);
	DEL(tokens);
}

int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	root = argv[1];
	mc_engine *engines[4] = { load("ttl", -1, 0, 0), load("json", -1, 0, 0),
				  load("jsonl", -1, 0, 0), NULL };
	for (int i = 0; i < 3; i++)
		if (!engines[i])
			return 2;
	engines[3] = reload(engines[0], 1);
	matrix(engines);
	for (int i = 0; i < 4; i++) {
		authored(engines[i]);
		prepared(engines[i]);
		views(engines[i], 0);
		mc_destroy(engines[i]);
	}
	static const char *formats[] = { "ttl", "json", "jsonl" };
	for (int f = 0; f < 3; f++)
		for (int live = 0; live < 2; live++)
			for (int deferred = 0; deferred < 2; deferred++)
				for (int lists = 0; lists < 2; lists++) {
					mc_engine *e = load(formats[f], lists,
							    live, deferred);
					if (!e)
						return 2;
					if (lists)
						list_mode(e);
					else {
						authored(e);
						views(e, 0);
					}
					mc_engine *from_snapshot =
						reload(e, !lists);
					if (lists)
						list_mode(from_snapshot);
					else
						authored(from_snapshot);
					mc_destroy(from_snapshot);
					mc_destroy(e);
				}
	inline_sources();
	collections();
	reload_options();
	generated_rules();
	numeric_lists();
	mixed_numeric_lists();
	econ();
	printf("Comma synonyms: %d positive, %d negative matrix cases; %d total cases, %d/%d assertions passed.\n",
	       positives, negatives, cases, assertions - failures, assertions);
	return failures ? 1 : 0;
}
