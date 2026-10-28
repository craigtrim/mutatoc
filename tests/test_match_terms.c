/*
 * test_match_terms.c - match_terms finds each term in a text with no ontology.
 *
 * Every case sends a match_terms request through mc_request to three real
 * engines: a new one, one with an ontology loaded and one after a failed
 * load. All three must give the same result, and the whole result is checked:
 * each match's term, x and y, and the text those offsets cover. Texts are
 * built piece by piece, so the expected offsets follow from where the words
 * were put, and a hand-worked table pins literal rows. A cross-check loads one
 * class per term and requires parse to report the same matches. Every case is
 * a match, a no match or an expected fail; an expected fail runs every time,
 * and one that starts passing fails the suite. craigtrim/mutatoc#23
 */

#include "testlib.h"
#include <ctype.h>
#include <time.h>

/* ---------------------------------------------------------------------- */
/* Bookkeeping.                                                           */

typedef struct {
	const char *name;
	int minimum, cases, failed, xfail;
} Category;
static Category categories[] = {
	{ "table", 57 },	{ "case", 288 },      { "whitespace", 150 },
	{ "punctuation", 581 }, { "unicode", 74 },    { "position", 176 },
	{ "spans", 615 },	{ "lists", 72 },      { "engines", 6 },
	{ "errors", 65 },	{ "isolation", 112 }, { "cross-check", 2097 },
};
#define CATEGORY_COUNT ((int)(sizeof(categories) / sizeof(*categories)))
static int failures, printed, xpasses;

static Category *category(const char *name)
{
	for (int i = 0; i < CATEGORY_COUNT; i++)
		if (!strcmp(categories[i].name, name))
			return &categories[i];
	fprintf(stderr, "unknown category %s\n", name);
	exit(2);
}

static void fail_case(Category *c, const char *what, const char *input,
		      const J *expected, const J *actual)
{
	failures++;
	c->failed++;
	if (++printed > 40)
		return;
	J *shown = STR(input ? input : "");
	char *in = cJSON_PrintUnformatted(shown);
	fprintf(stderr, "%s | %.200s | %s\n", c->name, in, what);
	if (expected || actual)
		test_print_diff("", expected, actual);
	free(in);
	DEL(shown);
}

/* ---------------------------------------------------------------------- */
/* Texts built piece by piece, with offsets counted from UTF-8 lead bytes. */

typedef struct {
	Buf b;
	int cps;
} Text;
typedef struct {
	size_t byte;
	int cp;
} Mark;

static int code_points(const char *s)
{
	int n = 0;
	for (; *s; s++)
		n += ((unsigned char)*s & 0xc0) != 0x80;
	return n;
}

static void put(Text *t, const char *s)
{
	buf_put(&t->b, s);
	t->cps += code_points(s);
}

static Mark mark(const Text *t)
{
	Mark m = { t->b.n, t->cps };
	return m;
}

static const char *text_of(const Text *t)
{
	return t->b.p ? t->b.p : "";
}

static void text_free(Text *t)
{
	free(t->b.p);
	memset(t, 0, sizeof(*t));
}

/* Expects term over the text from m to the end of t so far. */
static void expect(J *list, int term, const Text *t, Mark m)
{
	J *o = OBJ();
	PUT(o, "term", NUM(term));
	PUT(o, "x", NUM(m.cp));
	PUT(o, "y", NUM(t->cps));
	char *s = slice(text_of(t) + m.byte, t->b.n - m.byte);
	PUT(o, "text", STR(s));
	free(s);
	ADD(list, o);
}

/* The text from code point x to y, found by counting lead bytes. */
static char *cp_slice(const char *s, int x, int y)
{
	const char *from = NULL, *to = NULL;
	int i = 0;
	for (const char *p = s;; p++) {
		if (((unsigned char)*p & 0xc0) == 0x80)
			continue;
		if (i == x)
			from = p;
		if (i == y) {
			to = p;
			break;
		}
		if (!*p)
			break;
		i++;
	}
	if (!from || !to || to < from)
		return copy("<offsets outside the text>");
	return slice(from, (size_t)(to - from));
}

/* Adds the covered text to each match of a match_terms result. */
static void annotate(J *matches, const char *text)
{
	EACH(m, matches) {
		char *s = cp_slice(text, GET(m, "x")->valueint,
				   GET(m, "y")->valueint);
		PUT(m, "text", STR(s));
		free(s);
	}
}

/* A hand-worked match: term over code points x to y of text. */
static void expect_at(J *list, int term, const char *text, int x, int y)
{
	J *o = OBJ();
	PUT(o, "term", NUM(term));
	PUT(o, "x", NUM(x));
	PUT(o, "y", NUM(y));
	char *s = cp_slice(text, x, y);
	PUT(o, "text", STR(s));
	free(s);
	ADD(list, o);
}

static J *terms_of(const char *const *terms, int n)
{
	J *a = ARR();
	for (int i = 0; i < n; i++)
		ADD(a, STR(terms[i]));
	return a;
}

/* ---------------------------------------------------------------------- */
/* Engines and requests.                                                  */

enum { NEW, LOADED, FAILED, ENGINES };
static mc_engine *engines[ENGINES];
static const char *const engine_names[ENGINES] = {
	"new engine", "loaded engine", "engine after a failed load"
};

/* An ontology whose labels share words with the terms under test. */
static const char *const loaded_ontology =
	"[{\"format\":\"mutatoc/1\",\"namespace\":\"http://example.org/l#\"},"
	"{\"id\":\"Topic\",\"type\":\"owl:Class\",\"label\":\"data science\","
	"\"synonyms\":[\"alpha+beta\",\"science fiction\",\"intro\"]},"
	"{\"id\":\"Drink\",\"type\":\"owl:Class\",\"label\":\"Caf\xc3\xa9 "
	"Cr\xc3\xa8me\",\"synonyms\":[\"stra\xc3\x9f"
	"e\"]}]";

static J *load_request(const char *content)
{
	J *q = OBJ();
	PUT(q, "op", STR("load"));
	PUT(q, "content", STR(content));
	PUT(q, "format", STR("json"));
	return q;
}

static int setup_engines(void)
{
	for (int i = 0; i < ENGINES; i++)
		engines[i] = mc_create();
	J *q = load_request(loaded_ontology);
	J *r = test_request(engines[LOADED], q);
	int ok = cJSON_IsTrue(GET(r, "ok"));
	DEL(r);
	DEL(q);
	q = load_request("[{\"format\":\"mutatoc/1\"");
	r = test_request(engines[FAILED], q);
	ok = ok && !cJSON_IsTrue(GET(r, "ok"));
	DEL(r);
	DEL(q);
	return ok;
}

static J *request(const J *terms, const char *text, const char *line_breaks)
{
	J *q = OBJ();
	PUT(q, "op", STR("match_terms"));
	PUT(q, "text", STR(text));
	PUT(q, "terms", DUP(terms));
	if (line_breaks)
		PUT(q, "line_breaks", STR(line_breaks));
	return q;
}

/* The matches match_terms returns, with their covered text, or NULL. */
static J *run(mc_engine *e, const J *terms, const char *text,
	      const char *line_breaks)
{
	J *q = request(terms, text, line_breaks);
	J *r = test_request(e, q);
	DEL(q);
	J *out = cJSON_IsTrue(GET(r, "ok")) ?
			 cJSON_DetachItemFromObjectCaseSensitive(r, "result") :
			 NULL;
	DEL(r);
	if (out)
		annotate(out, text);
	return out;
}

enum { MATCH, NO_MATCH, XFAIL };

/*
 * Runs one request on every engine. Each must give a new engine's result,
 * which must equal the expected matches, or for an expected fail must not.
 */
static void check(const char *name, const J *terms, const char *text,
		  const char *line_breaks, const J *expected, int kind)
{
	Category *c = category(name);
	c->cases++;
	if (kind != XFAIL && (kind == NO_MATCH) != !SIZE(expected))
		fail_case(c, "label disagrees with its expected matches", text,
			  expected, NULL);
	J *got[ENGINES];
	for (int i = 0; i < ENGINES; i++)
		got[i] = run(engines[i], terms, text, line_breaks);
	for (int i = 1; i < ENGINES; i++)
		if (!got[0] || !got[i] || !cJSON_Compare(got[i], got[0], 1)) {
			char what[96];
			snprintf(what, sizeof(what),
				 "%s differs from a new engine",
				 engine_names[i]);
			fail_case(c, what, text, got[0], got[i]);
		}
	int same = got[0] && cJSON_Compare(got[0], expected, 1);
	if (kind == XFAIL) {
		if (same) {
			xpasses++;
			fail_case(c,
				  "expected fail now passes; update its label",
				  text, expected, got[0]);
		} else
			c->xfail++;
	} else if (!same)
		fail_case(c, kind == MATCH ? "match" : "no match", text,
			  expected, got[0]);
	for (int i = 0; i < ENGINES; i++)
		DEL(got[i]);
}

/*
 * Loads one class per term, the term as its only synonym and a single-word
 * id, and requires parse to report what match_terms reports. Two + terms can
 * rank differently there, so lists with more than one are left out.
 */
static void cross_check(const J *terms, const char *text,
			const char *line_breaks)
{
	int plus = 0;
	EACH(t, terms) {
		const char *s = S(t);
		plus += strchr(s, '+') != NULL;
		int visible = 0;
		for (const char *p = s; *p; p++)
			visible |= !isspace((unsigned char)*p) && *p != '+';
		if (!visible)
			return;
	}
	if (plus > 1 || !SIZE(terms))
		return;
	Category *c = category("cross-check");
	c->cases++;
	J *onto = ARR(), *head = OBJ();
	PUT(head, "format", STR("mutatoc/1"));
	PUT(head, "namespace", STR("http://example.org/cross#"));
	ADD(onto, head);
	int i = 0;
	EACH(t, terms) {
		char id[24];
		snprintf(id, sizeof(id), "E%d", i++);
		J *cls = OBJ(), *syn = ARR();
		PUT(cls, "id", STR(id));
		PUT(cls, "type", STR("owl:Class"));
		ADD(syn, STR(S(t)));
		PUT(cls, "synonyms", syn);
		ADD(onto, cls);
	}
	char *content = cJSON_PrintUnformatted(onto);
	DEL(onto);
	mc_engine *e = mc_create();
	J *q = load_request(content);
	free(content);
	J *r = test_request(e, q);
	int ok = cJSON_IsTrue(GET(r, "ok"));
	DEL(r);
	DEL(q);
	J *parsed = NULL;
	if (ok) {
		q = OBJ();
		PUT(q, "op", STR("parse"));
		PUT(q, "text", STR(text));
		if (line_breaks)
			PUT(q, "line_breaks", STR(line_breaks));
		r = test_request(e, q);
		DEL(q);
		if (cJSON_IsTrue(GET(r, "ok"))) {
			parsed = ARR();
			EACH(t, GET(GET(r, "result"), "tokens")) {
				const char *canon =
					S(GET(GET(t, "swaps"), "canon"));
				if (*canon != 'e')
					continue;
				J *o = OBJ();
				PUT(o, "term", NUM(atoi(canon + 1)));
				PUT(o, "x", DUP(GET(t, "x")));
				PUT(o, "y", DUP(GET(t, "y")));
				ADD(parsed, o);
			}
			annotate(parsed, text);
		}
		DEL(r);
	}
	J *mine = run(engines[NEW], terms, text, line_breaks);
	if (!parsed || !mine || !cJSON_Compare(parsed, mine, 1))
		fail_case(c, "parse with one class per term differs", text,
			  parsed, mine);
	DEL(parsed);
	DEL(mine);
	mc_destroy(e);
}

/* Checks one term list and text, then cross-checks it against parse. */
static void both(const char *name, const J *terms, const char *text,
		 const char *line_breaks, const J *expected, int kind)
{
	check(name, terms, text, line_breaks, expected, kind);
	cross_check(terms, text, line_breaks);
}

/* One term against one built text. */
static void one(const char *name, const char *term, const Text *t,
		const char *line_breaks, const J *expected, int kind)
{
	J *terms = ARR();
	ADD(terms, STR(term));
	both(name, terms, text_of(t), line_breaks, expected, kind);
	DEL(terms);
}

/* ---------------------------------------------------------------------- */
/* Case.                                                                  */

enum {
	CASE_LOWER,
	CASE_UPPER,
	CASE_TITLE,
	CASE_FIRST,
	CASE_ALTERNATE,
	CASE_SPLIT,
	CASE_STYLES
};

/* Changes the case of one character: ASCII, and the Latin-1 letters. */
static void put_case(Buf *b, const char **p, int up)
{
	unsigned char c = (unsigned char)**p;
	if (c < 0x80) {
		char ch[2] = { (char)(up ? toupper(c) : tolower(c)), 0 };
		buf_put(b, ch);
		(*p)++;
		return;
	}
	if (c == 0xc3 && (*p)[1]) {
		unsigned char d = (unsigned char)(*p)[1];
		/* C3 80 to 9E are capitals, A0 to BE small, except U+00D7, U+00F7 and U+00DF. */
		if (up && d >= 0xa0 && d <= 0xbe && d != 0xb7)
			d -= 0x20;
		else if (!up && d >= 0x80 && d <= 0x9e && d != 0x97)
			d += 0x20;
		char ch[3] = { (char)c, (char)d, 0 };
		buf_put(b, ch);
		*p += 2;
		return;
	}
	const char *start = (*p)++;
	while (((unsigned char)**p & 0xc0) == 0x80)
		(*p)++;
	buf_add(b, start, (size_t)(*p - start));
}

/* A phrase in one style; words break at spaces and +. */
static char *styled(const char *phrase, int style)
{
	Buf b = { 0 };
	int word = 0, letter = 0;
	for (const char *p = phrase; *p;) {
		if (*p == ' ' || *p == '+') {
			char ch[2] = { *p++, 0 };
			buf_put(&b, ch);
			word++;
			letter = 0;
			continue;
		}
		int up = style == CASE_UPPER ||
			 (style == CASE_TITLE && !letter) ||
			 (style == CASE_FIRST && !word && !letter) ||
			 (style == CASE_ALTERNATE && letter % 2) ||
			 (style == CASE_SPLIT && !word);
		put_case(&b, &p, up);
		letter++;
	}
	return buf_take(&b);
}

static char *replaced(const char *s, char from, char to)
{
	char *out = copy(s);
	for (char *p = out; *p; p++)
		if (*p == from)
			*p = to;
	return out;
}

static void case_family(void)
{
	static const char *const phrases[] = { "data science",
					       "caf\xc3\xa9 cr\xc3\xa8me",
					       "driver's ed",
					       "machine learning" };
	for (size_t k = 0; k < sizeof(phrases) / sizeof(*phrases); k++)
		for (int tm = 0; tm < CASE_STYLES; tm++)
			for (int xm = 0; xm < CASE_STYLES; xm++) {
				char *term = styled(phrases[k], tm);
				char *words = styled(phrases[k], xm);
				Text t = { 0 };
				J *want = ARR();
				put(&t, "Intro: ");
				Mark m = mark(&t);
				put(&t, words);
				expect(want, 0, &t, m);
				put(&t, " today.");
				one("case", term, &t, NULL, want, MATCH);
				DEL(want);
				text_free(&t);
				/* The + form, with a word between. */
				char *plus = replaced(term, ' ', '+');
				char *spread = styled(phrases[k], xm);
				char *gap = strchr(spread, ' ');
				*gap = 0;
				want = ARR();
				put(&t, "Intro: ");
				m = mark(&t);
				put(&t, spread);
				put(&t, " of ");
				put(&t, gap + 1);
				expect(want, 0, &t, m);
				put(&t, " today.");
				one("case", plus, &t, NULL, want, MATCH);
				DEL(want);
				text_free(&t);
				free(spread);
				free(plus);
				free(words);
				free(term);
			}
}

/* ---------------------------------------------------------------------- */
/* Whitespace.                                                            */

static const struct {
	const char *s;
	int breaks;
} spaces[] = {
	{ " ", 0 },
	{ "  ", 0 },
	{ "\t", 0 },
	{ "\n", 1 },
	{ "\r", 1 },
	{ "\r\n", 1 },
	{ "\n\n", 1 },
	{ "\xc2\xa0", 0 },
	{ "\xe2\x80\x83", 0 },
	{ "\xe3\x80\x80", 0 },
	{ "\v", 1 },
	{ "\f", 1 },
	{ "\xc2\x85", 1 },
	{ "\xe2\x80\xa8", 1 },
	{ "\xe2\x80\xa9", 1 },
};
#define SPACES ((int)(sizeof(spaces) / sizeof(*spaces)))
static const char *const modes[] = { NULL, "boundary" };

static void whitespace_family(void)
{
	static const char *const forms[] = { "data science", "data+science" };
	for (int s = 0; s < SPACES; s++)
		for (int mode = 0; mode < 2; mode++)
			for (int f = 0; f < 2; f++)
				for (int framed = 0; framed < 2; framed++) {
					Text t = { 0 };
					J *want = ARR();
					if (framed)
						put(&t, "see ");
					Mark m = mark(&t);
					put(&t, "data");
					put(&t, spaces[s].s);
					put(&t, "science");
					int blocked = mode && spaces[s].breaks;
					if (!blocked)
						expect(want, 0, &t, m);
					if (framed)
						put(&t, " now");
					one("whitespace", forms[f], &t,
					    modes[mode], want,
					    blocked ? NO_MATCH : MATCH);
					DEL(want);
					text_free(&t);
				}
	/* Whitespace inside a term matches a single space in the text. */
	for (int s = 0; s < SPACES; s++)
		for (int mode = 0; mode < 2; mode++) {
			Buf b = { 0 };
			buf_put(&b, "data");
			buf_put(&b, spaces[s].s);
			buf_put(&b, "science");
			char *term = buf_take(&b);
			Text t = { 0 };
			J *want = ARR();
			Mark m = mark(&t);
			put(&t, "data science");
			expect(want, 0, &t, m);
			one("whitespace", term, &t, modes[mode], want, MATCH);
			DEL(want);
			text_free(&t);
			free(term);
		}
}

/* ---------------------------------------------------------------------- */
/* Punctuation.                                                           */

static const char ascii_punct[] = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";

/*
 * Marks between two words. A joiner stays inside the word before it
 * (docs/protocol.md, Tokenization); every other mark is its own token.
 */
static const struct {
	const char *s;
	int joins;
} unicode_punct[] = {
	{ "\xe2\x80\x90", 0 }, { "\xe2\x80\x91", 0 }, { "\xe2\x80\x92", 0 },
	{ "\xe2\x80\x93", 0 }, { "\xe2\x80\x94", 0 }, { "\xe2\x80\x95", 0 },
	{ "\xe2\x88\x92", 0 }, { "\xe2\x80\x9c", 0 }, { "\xe2\x80\x9d", 0 },
	{ "\xe2\x80\xa6", 0 }, { "``", 0 },	      { "\xc2\xb4\xc2\xb4", 0 },
	{ "\xe2\x80\x98", 1 }, { "\xe2\x80\x99", 1 }, { "\xca\xbc", 1 },
	{ "\xc2\xb4", 1 },
};

/* One mark between, before, after and around data science. */
static void punctuation_mark(const char *p, int joins)
{
	static const char *const forms[] = { "data science", "data+science" };
	for (int f = 0; f < 2; f++) {
		/* Between the words, without and with spaces. */
		for (int spaced = 0; spaced < 2; spaced++) {
			Text t = { 0 };
			J *want = ARR();
			Mark m = mark(&t);
			put(&t, "data");
			put(&t, spaced ? " " : "");
			put(&t, p);
			put(&t, spaced ? " " : "");
			put(&t, "science");
			int kind;
			if (joins && !spaced)
				kind = NO_MATCH; /* one token */
			else {
				expect(want, 0, &t, m);
				/* A plain phrase stops at the mark (#16). */
				kind = f ? MATCH : XFAIL;
			}
			one("punctuation", forms[f], &t, NULL, want, kind);
			DEL(want);
			text_free(&t);
		}
		/* Before, after and around the phrase. */
		for (int where = 0; where < 3; where++) {
			Text t = { 0 };
			J *want = ARR();
			if (where != 1)
				put(&t, p);
			Mark m = mark(&t);
			put(&t, "data science");
			expect(want, 0, &t, m);
			if (where)
				put(&t, p);
			one("punctuation", forms[f], &t, NULL, want, MATCH);
			DEL(want);
			text_free(&t);
		}
	}
	/* The same mark inside the term, against the text written alike. */
	if (strcmp(p, "+")) {
		Buf b = { 0 };
		buf_put(&b, "data");
		buf_put(&b, p);
		buf_put(&b, "science");
		char *term = buf_take(&b);
		for (int spaced = 0; spaced < 2; spaced++) {
			Text t = { 0 };
			J *want = ARR();
			Mark m = mark(&t);
			put(&t, "data");
			put(&t, spaced ? " " : "");
			put(&t, p);
			put(&t, spaced ? " " : "");
			put(&t, "science");
			/*
			 * A joined term is one word, which spaces split. An
			 * underscore inside a term is a word break, as in a
			 * label, so data_science matches data science only.
			 */
			int kind = (joins && spaced) || !strcmp(p, "_") ?
					   NO_MATCH :
					   MATCH;
			if (kind == MATCH)
				expect(want, 0, &t, m);
			one("punctuation", term, &t, NULL, want, kind);
			DEL(want);
			text_free(&t);
		}
		free(term);
	}
}

static void punctuation_family(void)
{
	for (const char *p = ascii_punct; *p; p++) {
		char mark_s[2] = { *p, 0 };
		punctuation_mark(mark_s, strchr("&'_`", *p) != NULL);
	}
	for (size_t i = 0; i < sizeof(unicode_punct) / sizeof(*unicode_punct);
	     i++)
		punctuation_mark(unicode_punct[i].s, unicode_punct[i].joins);
	/* Look-alike marks in a term match the ASCII mark in the text. */
	static const struct {
		const char *term, *text;
	} alike[] = {
		{ "data\xe2\x80\x93science", "data-science" },
		{ "data-science", "data\xe2\x80\x94science" },
		{ "driver\xe2\x80\x99s ed", "driver's ed" },
		{ "driver's ed", "DRIVER\xca\xbcS ED" },
		{ "data \xe2\x80\x9cscience\xe2\x80\x9d", "data \"science\"" },
		{ "data \"science\"", "data \xe2\x80\x9cscience\xe2\x80\x9d" },
		{ "wait\xe2\x80\xa6 data", "wait\xe2\x80\xa6 data" },
	};
	for (size_t i = 0; i < sizeof(alike) / sizeof(*alike); i++) {
		Text t = { 0 };
		J *want = ARR();
		Mark m = mark(&t);
		put(&t, alike[i].text);
		expect(want, 0, &t, m);
		one("punctuation", alike[i].term, &t, NULL, want, MATCH);
		DEL(want);
		text_free(&t);
	}
}

/* ---------------------------------------------------------------------- */
/* Unicode.                                                               */

static void unicode_family(void)
{
	static const struct {
		const char *term, *text;
		int kind;
	} pairs[] = {
		/* Accents fold both ways, precomposed or decomposed (#25). */
		{ "caf\xc3\xa9 cr\xc3\xa8me", "CAF\xc3\x89 CR\xc3\x88ME",
		  MATCH },
		{ "caf\xc3\xa9 cr\xc3\xa8me", "cafe creme", MATCH },
		{ "cafe creme", "caf\xc3\xa9 cr\xc3\xa8me", MATCH },
		{ "caf\xc3\xa9 cr\xc3\xa8me", "cafe\xcc\x81 cre\xcc\x80me",
		  MATCH },
		{ "cafe\xcc\x81 cre\xcc\x80me", "Caf\xc3\xa9 Cr\xc3\xa8me",
		  MATCH },
		{ "CAFE\xcc\x81", "caf\xc3\xa9", MATCH },
		{ "na\xc3\xafve r\xc3\xa9sum\xc3\xa9", "NAIVE RESUME", MATCH },
		{ "naive resume", "Na\xc3\xafve R\xc3\xa9sum\xc3\xa9", MATCH },
		/* Sharp s is not ss. */
		{ "stra\xc3\x9f"
		  "e",
		  "STRA\xe1\xba\x9e"
		  "E",
		  MATCH },
		{ "stra\xc3\x9f"
		  "e",
		  "Stra\xc3\x9f"
		  "e",
		  MATCH },
		{ "stra\xc3\x9f"
		  "e",
		  "STRASSE", NO_MATCH },
		{ "stra\xc3\x9f"
		  "e",
		  "strasse", NO_MATCH },
		{ "STRASSE",
		  "stra\xc3\x9f"
		  "e",
		  NO_MATCH },
		{ "strasse", "STRASSE", MATCH },
		/* Turkish dotted capital I folds; dotless i is another letter. */
		{ "istanbul", "\xc4\xb0stanbul", MATCH },
		{ "\xc4\xb0stanbul", "istanbul", MATCH },
		{ "ISTANBUL", "\xc4\xb0STANBUL", MATCH },
		{ "istanbul", "I\xcc\x87STANBUL", MATCH },
		{ "istanbul", "\xc4\xb1stanbul", NO_MATCH },
		{ "\xc4\xb1stanbul", "istanbul", NO_MATCH },
		/* Invisible characters inside a word fold away (#25). */
		{ "data science", "da\xc2\xadta science", MATCH },
		{ "data science",
		  "data scien\xc2\xad"
		  "ce",
		  MATCH },
		{ "da\xc2\xadta science", "data science", MATCH },
		{ "data science", "data\xe2\x80\x8b science", MATCH },
		{ "datascience", "data\xe2\x80\x8bscience", MATCH },
		{ "data science", "data\xe2\x80\x8bscience", NO_MATCH },
		/* Fullwidth letters are not folded (#25). */
		{ "data science",
		  "\xef\xbd\x84\xef\xbd\x81\xef\xbd\x94\xef\xbd\x81 "
		  "\xef\xbd\x93\xef\xbd\x83\xef\xbd\x89\xef\xbd\x85\xef\xbd\x8e"
		  "\xef\xbd\x83\xef\xbd\x85",
		  XFAIL },
		{ "\xef\xbd\x84\xef\xbd\x81\xef\xbd\x94\xef\xbd\x81", "data",
		  XFAIL },
	};
	for (size_t i = 0; i < sizeof(pairs) / sizeof(*pairs); i++)
		for (int framed = 0; framed < 2; framed++) {
			Text t = { 0 };
			J *want = ARR();
			if (framed)
				put(&t, "On ");
			Mark m = mark(&t);
			put(&t, pairs[i].text);
			if (pairs[i].kind != NO_MATCH)
				expect(want, 0, &t, m);
			if (framed)
				put(&t, " today");
			one("unicode", pairs[i].term, &t, NULL, want,
			    pairs[i].kind);
			DEL(want);
			text_free(&t);
		}
	/*
	 * Astral characters are one code point each: an emoji before, between
	 * and after the words, and mathematical letters as their own word.
	 */
	static const char *const astral[] = {
		"\xf0\x9f\x98\x80", "\xf0\x9d\x90\xb1",
		"\xf0\x9f\x87\xba\xf0\x9f\x87\xb8"
	};
	for (size_t a = 0; a < sizeof(astral) / sizeof(*astral); a++)
		for (int f = 0; f < 2; f++) {
			const char *term = f ? "alpha+beta" : "alpha beta";
			for (int where = 0; where < 3; where++) {
				Text t = { 0 };
				J *want = ARR();
				if (!where) {
					put(&t, astral[a]);
					put(&t, " ");
				}
				Mark m = mark(&t);
				put(&t, "alpha ");
				if (where == 1) {
					put(&t, astral[a]);
					put(&t, " ");
				}
				put(&t, "beta");
				/* Between the words, only the span still fits. */
				int kind = where == 1 && !f ? NO_MATCH : MATCH;
				if (kind == MATCH)
					expect(want, 0, &t, m);
				if (where == 2) {
					put(&t, " ");
					put(&t, astral[a]);
				}
				one("unicode", term, &t, NULL, want, kind);
				DEL(want);
				text_free(&t);
			}
		}
}

/* ---------------------------------------------------------------------- */
/* Position.                                                              */

static void position_family(void)
{
	static const char *const before[] = {
		"", "we love ", "(", "1. ", "\xf0\x9f\x98\x80 ", "\n"
	};
	static const char *const after[] = {
		"", " a lot", ")", ".", " \xf0\x9f\x98\x80", "\n"
	};
	static const char *const forms[] = { "data science", "data+science" };
	for (int f = 0; f < 2; f++)
		for (size_t b = 0; b < 6; b++)
			for (size_t a = 0; a < 6; a++)
				for (int mode = 0; mode < 2; mode++) {
					Text t = { 0 };
					J *want = ARR();
					put(&t, before[b]);
					Mark m = mark(&t);
					put(&t, "data science");
					expect(want, 0, &t, m);
					put(&t, after[a]);
					one("position", forms[f], &t,
					    modes[mode], want, MATCH);
					DEL(want);
					text_free(&t);
				}
	/* Repeated and adjacent occurrences, two to four of them. */
	static const char *const joins[] = { " ", " and ", ", ", "\n" };
	for (int f = 0; f < 2; f++)
		for (size_t j = 0; j < 4; j++)
			for (int times = 2; times <= 4; times++) {
				Text t = { 0 };
				J *want = ARR();
				for (int k = 0; k < times; k++) {
					if (k)
						put(&t, joins[j]);
					Mark m = mark(&t);
					put(&t, "data science");
					expect(want, 0, &t, m);
				}
				one("position", forms[f], &t, NULL, want,
				    MATCH);
				DEL(want);
				text_free(&t);
			}
	/* Another term's match next to this one, on either side. */
	for (int side = 0; side < 2; side++) {
		static const char *const pair[] = { "data science",
						    "machine learning" };
		J *terms = terms_of(pair, 2);
		Text t = { 0 };
		J *want = ARR();
		Mark m = mark(&t);
		put(&t, side ? "machine learning" : "data science");
		expect(want, side, &t, m);
		put(&t, " ");
		m = mark(&t);
		put(&t, side ? "data science" : "machine learning");
		expect(want, !side, &t, m);
		both("position", terms, text_of(&t), NULL, want, MATCH);
		DEL(want);
		DEL(terms);
		text_free(&t);
	}
	/*
	 * Overlapping terms: the longest phrase wins, then the leftmost, and
	 * the other term's words are taken.
	 */
	static const struct {
		const char *terms[2], *text;
		int term, x, y;
	} overlaps[] = {
		{ { "data science", "science fiction" },
		  "data science fiction",
		  0,
		  0,
		  12 },
		{ { "science fiction", "data science" },
		  "data science fiction",
		  1,
		  0,
		  12 },
		{ { "data science fiction", "data science" },
		  "data science fiction",
		  0,
		  0,
		  20 },
		{ { "data science", "data science fiction" },
		  "data science fiction",
		  1,
		  0,
		  20 },
		{ { "science fiction", "data science fiction" },
		  "data science fiction",
		  1,
		  0,
		  20 },
		{ { "big data", "data science" }, "big data science", 0, 0, 8 },
	};
	for (size_t i = 0; i < sizeof(overlaps) / sizeof(*overlaps); i++) {
		J *terms = terms_of(overlaps[i].terms, 2), *want = ARR();
		expect_at(want, overlaps[i].term, overlaps[i].text,
			  overlaps[i].x, overlaps[i].y);
		both("position", terms, overlaps[i].text, NULL, want, MATCH);
		DEL(want);
		DEL(terms);
	}
}

/* ---------------------------------------------------------------------- */
/* + terms.                                                               */

/*
 * alpha+beta in both orders with tokens between: words, punctuation and
 * stopwords take a position each and whitespace runs none. The first and last
 * words may be four positions apart, so three tokens between still match and
 * four do not.
 */
static void spans_family(void)
{
	static const char *const takes[] = { "x", ",", "the", "42", "-" };
	static const char *const runs[] = { " ", "  \t ", " \n ", "\r\n" };
	for (int order = 0; order < 2; order++)
		for (int n = 0; n <= 5; n++)
			for (size_t kind = 0; kind < 5; kind++)
				for (size_t run = 0; run < 4; run++)
					for (int mode = 0; mode < 2; mode++) {
						Text t = { 0 };
						J *want = ARR();
						Mark m = mark(&t);
						put(&t,
						    order ? "beta" : "alpha");
						for (int k = 0; k < n; k++) {
							put(&t, runs[run]);
							put(&t,
							    takes[(kind +
								   (size_t)k) %
								  5]);
						}
						put(&t, runs[run]);
						put(&t,
						    order ? "alpha" : "beta");
						int crosses = mode && run >= 2;
						int fits = n <= 3 && !crosses;
						if (fits)
							expect(want, 0, &t, m);
						one("spans", "alpha+beta", &t,
						    modes[mode], want,
						    fits ? MATCH : NO_MATCH);
						DEL(want);
						text_free(&t);
					}
	/* Whitespace runs alone never take a position. */
	for (int count = 1; count <= 8; count++) {
		Text t = { 0 };
		J *want = ARR();
		Mark m = mark(&t);
		put(&t, "alpha");
		for (int k = 0; k < count; k++)
			put(&t, k % 2 ? "\t" : "  ");
		put(&t, "beta");
		expect(want, 0, &t, m);
		one("spans", "alpha+beta", &t, NULL, want, MATCH);
		DEL(want);
		text_free(&t);
	}
	/*
	 * Three parts in every order with g1 and g2 tokens in the two gaps: the
	 * first and last words are g1 + g2 + 2 positions apart.
	 */
	static const char *const three[] = { "alpha", "beta", "gamma" };
	static const int orders[6][3] = {
		{ 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 },
		{ 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 }
	};
	for (int o = 0; o < 6; o++)
		for (int g1 = 0; g1 <= 3; g1++)
			for (int g2 = 0; g2 <= 3; g2++) {
				Text t = { 0 };
				J *want = ARR();
				Mark m = mark(&t);
				put(&t, three[orders[o][0]]);
				for (int k = 0; k < g1; k++)
					put(&t, " x");
				put(&t, " ");
				put(&t, three[orders[o][1]]);
				for (int k = 0; k < g2; k++)
					put(&t, " y");
				put(&t, " ");
				put(&t, three[orders[o][2]]);
				int fits = g1 + g2 <= 2;
				if (fits)
					expect(want, 0, &t, m);
				one("spans", "alpha+beta+gamma", &t, NULL, want,
				    fits ? MATCH : NO_MATCH);
				DEL(want);
				text_free(&t);
			}
	/* Spaces around +, and case, change nothing. */
	static const char *const written[] = { "alpha + beta", " alpha+beta ",
					       "ALPHA+BETA",   "Alpha+Beta",
					       "alpha +beta",  "alpha+ beta",
					       "aLpHa+BeTa" };
	static const char *const texts[] = { "alpha x beta", "BETA, ALPHA",
					     "Alpha the of beta" };
	for (size_t w = 0; w < sizeof(written) / sizeof(*written); w++)
		for (size_t x = 0; x < 3; x++) {
			Text t = { 0 };
			J *want = ARR();
			Mark m = mark(&t);
			put(&t, texts[x]);
			expect(want, 0, &t, m);
			one("spans", written[w], &t, NULL, want, MATCH);
			DEL(want);
			text_free(&t);
		}
	/*
	 * Malformed patterns: an empty part never matches, and the parts that
	 * are left still match as a phrase.
	 */
	static const struct {
		const char *term, *text;
		int x, y;
	} malformed[] = {
		{ "alpha+", "alpha beta", 0, 5 },
		{ "+beta", "alpha beta", 6, 10 },
		{ "alpha++beta", "alpha beta", 0, 10 },
		{ "alpha++beta", "alpha x beta", -1, -1 },
		{ "+alpha+beta", "alpha x beta", -1, -1 },
		{ "+alpha+beta", "alpha beta", 0, 10 },
		{ "alpha+beta+", "beta x alpha", -1, -1 },
		{ "alpha+beta+", "alpha beta", 0, 10 },
		{ "+", "alpha + beta", -1, -1 },
		{ "++", "alpha beta", -1, -1 },
	};
	for (size_t i = 0; i < sizeof(malformed) / sizeof(*malformed); i++) {
		J *want = ARR();
		if (malformed[i].x >= 0)
			expect_at(want, 0, malformed[i].text, malformed[i].x,
				  malformed[i].y);
		J *terms = ARR();
		ADD(terms, STR(malformed[i].term));
		both("spans", terms, malformed[i].text, NULL, want,
		     SIZE(want) ? MATCH : NO_MATCH);
		DEL(terms);
		DEL(want);
	}
}

/* ---------------------------------------------------------------------- */
/* Term lists.                                                            */

/*
 * Terms topicN area for N below count, matched where text names them. Returns
 * the seconds the check took.
 */
static double long_list(int count)
{
	J *terms = ARR();
	for (int i = 0; i < count; i++) {
		char term[40];
		snprintf(term, sizeof(term), "topic%d area", i);
		ADD(terms, STR(term));
	}
	int picks[] = { 0, count / 2, count - 1 };
	Text t = { 0 };
	J *want = ARR();
	put(&t, "Intro");
	for (int k = 0; k < 3; k++) {
		char word[40];
		snprintf(word, sizeof(word), "topic%d area", picks[k]);
		put(&t, k ? " and " : ": ");
		Mark m = mark(&t);
		put(&t, word);
		expect(want, picks[k], &t, m);
	}
	put(&t, ", not topic area.");
	clock_t started = clock();
	check("lists", terms, text_of(&t), NULL, want, MATCH);
	double seconds = (double)(clock() - started) / CLOCKS_PER_SEC;
	DEL(want);
	DEL(terms);
	text_free(&t);
	return seconds;
}

static void lists_family(void)
{
	/*
	 * Work grows with the list: 100 times the terms may take at most 300
	 * times as long, in any build, sanitized or not.
	 */
	long_list(1);
	double hundred = long_list(100), ten_thousand = long_list(10000);
	if (ten_thousand > 300 * (hundred > 0.001 ? hundred : 0.001)) {
		char what[96];
		snprintf(what, sizeof(what),
			 "10,000 terms took %.3f s against %.3f s for 100",
			 ten_thousand, hundred);
		fail_case(category("lists"), what, NULL, NULL, NULL);
	}
	static const struct {
		const char *terms[6], *text;
		int m[3][3];
	} rows[] = {
		/*
		 * The same term in other case or spacing reports the first.
		 * Terms that differ in accents are two terms, and the one
		 * written as the text folds wins, as for labels.
		 */
		{ { "data science", "Data  Science", "DATA SCIENCE",
		    "data\tscience" },
		  "Data Science",
		  { { 0, 0, 12 }, { -1 } } },
		{ { "DATA SCIENCE", "data science" },
		  "data science",
		  { { 0, 0, 12 }, { -1 } } },
		{ { "alpha+beta", "ALPHA + BETA", "alpha+beta" },
		  "alpha x beta",
		  { { 0, 0, 12 }, { -1 } } },
		{ { "caf\xc3\xa9", "cafe", "CAF\xc3\x89" },
		  "Cafe",
		  { { 1, 0, 4 }, { -1 } } },
		{ { "caf\xc3\xa9", "CAF\xc3\x89" },
		  "Cafe",
		  { { 0, 0, 4 }, { -1 } } },
		/* An underscore inside a term is a word break. */
		{ { "data_science" }, "data science", { { 0, 0, 12 }, { -1 } } },
		{ { "data_science" }, "data_science", { { -1 } } },
		/* A term inside another. */
		{ { "data", "data science" },
		  "data science",
		  { { 1, 0, 12 }, { -1 } } },
		{ { "data", "data science" }, "data", { { 0, 0, 4 }, { -1 } } },
		{ { "data", "data science" },
		  "data science data",
		  { { 1, 0, 12 }, { 0, 13, 17 }, { -1 } } },
		/* A plain term and a + term over the same words. */
		{ { "alpha beta", "alpha+beta" },
		  "alpha beta",
		  { { 0, 0, 10 }, { -1 } } },
		{ { "alpha beta", "alpha+beta" },
		  "alpha x beta",
		  { { 1, 0, 12 }, { -1 } } },
		{ { "alpha+beta", "alpha beta" },
		  "alpha beta",
		  { { 0, 0, 10 }, { -1 } } },
		{ { "alpha+beta", "alpha beta" },
		  "alpha x beta",
		  { { 0, 0, 12 }, { -1 } } },
		/* The + term with more words wins, wherever it is listed. */
		{ { "alpha+beta", "alpha+beta+gamma" },
		  "gamma x alpha y beta",
		  { { 1, 0, 20 }, { -1 } } },
		{ { "alpha+beta+gamma", "alpha+beta" },
		  "gamma x alpha y beta",
		  { { 0, 0, 20 }, { -1 } } },
		/*
		 * Two + terms of one rank: the one whose first word sorts
		 * first takes the window.
		 */
		{ { "alpha+beta", "beta+alpha" },
		  "alpha x beta",
		  { { 0, 0, 12 }, { -1 } } },
		{ { "beta+alpha", "alpha+beta" },
		  "alpha x beta",
		  { { 1, 0, 12 }, { -1 } } },
		/* Empty and whitespace-only terms never match. */
		{ { "", " ", "\t", "\n", "+", "data science" },
		  "data science",
		  { { 5, 0, 12 }, { -1 } } },
		{ { "", " ", "\t\n" }, "data science", { { -1 } } },
		/* A term's match is never another term's word. */
		{ { "x", "x 0+y" }, "x y", { { 0, 0, 1 }, { -1 } } },
		{ { "alpha+beta", "alpha_beta" },
		  "alpha beta",
		  { { 0, 0, 10 }, { -1 } } },
		{ { "alpha_beta", "alpha+beta" },
		  "alpha x beta",
		  { { 1, 0, 12 }, { -1 } } },
	};
	for (size_t i = 0; i < sizeof(rows) / sizeof(*rows); i++) {
		int n = 0;
		while (n < 6 && rows[i].terms[n])
			n++;
		J *terms = terms_of(rows[i].terms, n), *want = ARR();
		for (int k = 0; k < 3 && rows[i].m[k][0] >= 0; k++)
			expect_at(want, rows[i].m[k][0], rows[i].text,
				  rows[i].m[k][1], rows[i].m[k][2]);
		both("lists", terms, rows[i].text, NULL, want,
		     SIZE(want) ? MATCH : NO_MATCH);
		DEL(want);
		DEL(terms);
	}
	/*
	 * Spellings a label derives: an underscore is a word break and a final
	 * ! ? or . may drop, together too.
	 */
	static const char *const derived[] = {
		"data_science!", "data_science?", "data_science.",
		"data science!", "Data_Science!", "DATA_SCIENCE."
	};
	for (size_t i = 0; i < sizeof(derived) / sizeof(*derived); i++)
		for (int mode = 0; mode < 2; mode++) {
			J *terms = ARR(), *want = ARR();
			ADD(terms, STR(derived[i]));
			expect_at(want, 0, "data science", 0, 12);
			both("lists", terms, "data science", modes[mode], want,
			     MATCH);
			DEL(terms);
			DEL(want);
		}
	/*
	 * Two spellings that fold to cafe, the second with an invisible
	 * character after a space: the first listed wins wherever an
	 * unrelated term stands in the list.
	 */
	static const char *const invisible[] = { "\xe2\x80\x8b", "\xc2\xad",
						 "\xcc\x81", "\xe2\x81\xa0",
						 "\xef\xb8\x8f" };
	for (size_t v = 0; v < 5; v++)
		for (int place = 0; place < 3; place++)
			for (int mode = 0; mode < 2; mode++) {
				char second[24];
				snprintf(second, sizeof(second),
					 "caf\xc3\xa9 %s", invisible[v]);
				const char *list[3];
				int k = 0, first = -1;
				for (int slot = 0; slot < 3; slot++) {
					if (slot == place)
						list[k++] = "alpha beta";
					else if (first < 0) {
						first = k;
						list[k++] = "caf\xc3\xa9";
					} else
						list[k++] = second;
				}
				J *terms = terms_of(list, 3), *want = ARR();
				expect_at(want, first, "cafe", 0, 4);
				both("lists", terms, "cafe", modes[mode], want,
				     MATCH);
				DEL(terms);
				DEL(want);
			}
	/* An empty list matches nothing in any text. */
	static const char *const any[] = { "", "data science", "alpha x beta",
					   "\xf0\x9f\x98\x80" };
	for (size_t i = 0; i < 4; i++) {
		J *terms = ARR(), *want = ARR();
		check("lists", terms, any[i], NULL, want, NO_MATCH);
		DEL(terms);
		DEL(want);
	}
}

/* ---------------------------------------------------------------------- */
/* Isolation: one term's match is never another term's input.             */

static void isolation_family(void)
{
	/*
	 * A term's list index after its words in the text, which a canonical
	 * form built from the index must not match.
	 */
	static const int places[] = { 0, 1, 9, 10, 99 };
	static const struct {
		const char *term, *words;
	} forms[] = {
		{ "alpha", "alpha" },
		{ "alpha beta", "alpha beta" },
		{ "alpha+beta", "alpha x beta" },
		{ "alpha_beta", "alpha beta" },
		{ "caf\xc3\xa9", "cafe" },
	};
	static const char *const seps[] = { " ", "  ", "\t", "\n" };
	for (size_t i = 0; i < sizeof(places) / sizeof(*places); i++)
		for (size_t f = 0; f < sizeof(forms) / sizeof(*forms); f++)
			for (size_t s = 0; s < 4; s++) {
				J *terms = ARR();
				for (int k = 0; k < places[i]; k++) {
					char pad[24];
					snprintf(pad, sizeof(pad), "pad%d", k);
					ADD(terms, STR(pad));
				}
				ADD(terms, STR(forms[f].term));
				char index[16];
				snprintf(index, sizeof(index), "%d", places[i]);
				Text t = { 0 };
				J *want = ARR();
				Mark m = mark(&t);
				put(&t, forms[f].words);
				expect(want, places[i], &t, m);
				put(&t, seps[s]);
				put(&t, index);
				both("isolation", terms, text_of(&t), NULL,
				     want, MATCH);
				DEL(want);
				DEL(terms);
				text_free(&t);
			}
	/* The lookup's internal key names never match text. */
	static const char *const placed[] = { "alpha", "beta" };
	J *pair = terms_of(placed, 2), *none = ARR();
	both("isolation", pair, "p0 P1 p1 P0", NULL, none, NO_MATCH);
	DEL(pair);
	DEL(none);
	/* An index joined to the word by an underscore is one other word. */
	J *terms = ARR(), *want = ARR();
	ADD(terms, STR("alpha"));
	both("isolation", terms, "alpha_0", NULL, want, NO_MATCH);
	DEL(terms);
	DEL(want);
	/*
	 * A + term whose first part folds to another term's canonical form
	 * once an invisible character drops away.
	 */
	static const char *const invisible[] = { "\xe2\x80\x8b", "\xc2\xad",
						 "\xcc\x81", "\xe2\x81\xa0",
						 "\xef\xb8\x8f" };
	for (size_t v = 0; v < 5; v++)
		for (int mode = 0; mode < 2; mode++) {
			char second[32];
			snprintf(second, sizeof(second), "x 0 %s+y",
				 invisible[v]);
			const char *const list[] = { "x", second };
			J *pair = terms_of(list, 2), *one_match = ARR();
			expect_at(one_match, 0, "x y", 0, 1);
			check("isolation", pair, "x y", modes[mode], one_match,
			      MATCH);
			DEL(pair);
			DEL(one_match);
		}
}

/* ---------------------------------------------------------------------- */
/* Engine state.                                                          */

static J *parse_result(mc_engine *e, const char *text)
{
	J *q = OBJ();
	PUT(q, "op", STR("parse"));
	PUT(q, "text", STR(text));
	J *r = test_request(e, q);
	DEL(q);
	return r;
}

static void engines_family(void)
{
	Category *c = category("engines");
	static const char *const texts[] = { "data science and alpha x beta",
					     "Caf\xc3\xa9 Cr\xc3\xa8me",
					     "science fiction intro" };
	static const char *const list[] = { "data science", "alpha+beta",
					    "caf\xc3\xa9", "fiction" };
	J *terms = terms_of(list, 4);
	/*
	 * The loaded engine keeps its match index and parses the same before
	 * and after, and a request for many terms changes neither.
	 */
	for (size_t i = 0; i < 3; i++) {
		c->cases++;
		J *before = parse_result(engines[LOADED], texts[i]);
		MatchIndex *index = engines[LOADED]->index;
		J *got = run(engines[LOADED], terms, texts[i], NULL);
		J *after = parse_result(engines[LOADED], texts[i]);
		if (!cJSON_IsTrue(GET(before, "ok")) || !got ||
		    !cJSON_Compare(before, after, 1))
			fail_case(c, "parse differs after match_terms",
				  texts[i], before, after);
		if (engines[LOADED]->index != index || !index)
			fail_case(c, "match_terms replaced the match index",
				  texts[i], NULL, NULL);
		DEL(got);
		DEL(before);
		DEL(after);
	}
	/* Engines without an ontology still have none afterwards. */
	for (int e = 0; e < ENGINES; e++) {
		if (e == LOADED)
			continue;
		c->cases++;
		J *got = run(engines[e], terms, texts[0], NULL);
		J *r = parse_result(engines[e], texts[0]);
		if (!got || cJSON_IsTrue(GET(r, "ok")) ||
		    GET(GET(r, "error"), "code")->valueint != 4 ||
		    engines[e]->snapshot || engines[e]->index)
			fail_case(c, "match_terms gave the engine an ontology",
				  engine_names[e], NULL, r);
		DEL(got);
		DEL(r);
	}
	/* A load after match_terms works as on a new engine. */
	c->cases++;
	mc_engine *later = mc_create();
	J *got = run(later, terms, texts[0], NULL);
	J *q = load_request(loaded_ontology);
	J *r = test_request(later, q);
	J *fresh = parse_result(later, texts[0]),
	  *loaded = parse_result(engines[LOADED], texts[0]);
	if (!got || !cJSON_IsTrue(GET(r, "ok")) ||
	    !cJSON_Compare(GET(fresh, "result"), GET(loaded, "result"), 1))
		fail_case(c, "load after match_terms differs", texts[0], loaded,
			  fresh);
	DEL(got);
	DEL(q);
	DEL(r);
	DEL(fresh);
	DEL(loaded);
	mc_destroy(later);
	DEL(terms);
}

/* ---------------------------------------------------------------------- */
/* Input errors.                                                          */

static void errors_family(void)
{
	static const char *const bad[] = {
		"{\"op\":\"match_terms\",\"terms\":[\"x\"]}",
		"{\"op\":\"match_terms\",\"text\":1,\"terms\":[\"x\"]}",
		"{\"op\":\"match_terms\",\"text\":null,\"terms\":[\"x\"]}",
		"{\"op\":\"match_terms\",\"text\":[\"x\"],\"terms\":[\"x\"]}",
		"{\"op\":\"match_terms\",\"text\":{},\"terms\":[\"x\"]}",
		"{\"op\":\"match_terms\",\"text\":true,\"terms\":[\"x\"]}",
		"{\"op\":\"match_terms\",\"text\":\"x\"}",
		"{\"op\":\"match_terms\",\"text\":\"x\",\"terms\":\"x\"}",
		"{\"op\":\"match_terms\",\"text\":\"x\",\"terms\":{}}",
		"{\"op\":\"match_terms\",\"text\":\"x\",\"terms\":1}",
		"{\"op\":\"match_terms\",\"text\":\"x\",\"terms\":null}",
		"{\"op\":\"match_terms\",\"text\":\"x\",\"terms\":[1]}",
		"{\"op\":\"match_terms\",\"text\":\"x\",\"terms\":[null]}",
		"{\"op\":\"match_terms\",\"text\":\"x\",\"terms\":[[\"x\"]]}",
		"{\"op\":\"match_terms\",\"text\":\"x\",\"terms\":[{}]}",
		"{\"op\":\"match_terms\",\"text\":\"x\",\"terms\":[\"x\",false]}",
		"{\"op\":\"match_terms\",\"text\":\"x\",\"terms\":[\"x\"],"
		"\"line_breaks\":\"Boundary\"}",
		"{\"op\":\"match_terms\",\"text\":\"x\",\"terms\":[\"x\"],"
		"\"line_breaks\":\"none\"}",
		"{\"op\":\"match_terms\",\"text\":\"x\",\"terms\":[\"x\"],"
		"\"line_breaks\":1}",
		"{\"op\":\"match_terms\",\"text\":\"x\",\"terms\":[\"x\"],"
		"\"line_breaks\":null}",
		"{\"op\":\"match_terms\",\"text\":\"x\",\"terms\":[\"x\"],"
		"\"line_breaks\":\"\"}",
	};
	Category *c = category("errors");
	for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++)
		for (int e = 0; e < ENGINES; e++) {
			c->cases++;
			mc_error err;
			char *r = mc_request(engines[e], bad[i], &err);
			J *out = r ? cJSON_Parse(r) : NULL;
			mc_free(r);
			J *code = GET(GET(out, "error"), "code");
			if (cJSON_IsTrue(GET(out, "ok")) || !code ||
			    code->valueint != 2)
				fail_case(c, "expected input error 2", bad[i],
					  NULL, out);
			DEL(out);
		}
	/* Both line_breaks modes are accepted. */
	static const char *const good[] = { "ignore", "boundary" };
	for (int i = 0; i < 2; i++) {
		J *terms = ARR(), *want = ARR();
		ADD(terms, STR("x"));
		expect_at(want, 0, "x", 0, 1);
		check("errors", terms, "x", good[i], want, MATCH);
		DEL(terms);
		DEL(want);
	}
}

/* ---------------------------------------------------------------------- */
/* Hand-worked table.                                                     */

static void table_family(void)
{
	static const struct {
		const char *terms[3], *text, *line_breaks;
		int kind;
		int m[3][3];
	} rows[] = {
		{ { "data science", "intro" },
		  "Intro to Data Science",
		  NULL,
		  MATCH,
		  { { 1, 0, 5 }, { 0, 9, 21 }, { -1 } } },
		{ { "data science" },
		  "DATA SCIENCE",
		  NULL,
		  MATCH,
		  { { 0, 0, 12 }, { -1 } } },
		{ { "data science" },
		  "Data science",
		  NULL,
		  MATCH,
		  { { 0, 0, 12 }, { -1 } } },
		{ { "data science" },
		  "dAtA sCiEnCe",
		  NULL,
		  MATCH,
		  { { 0, 0, 12 }, { -1 } } },
		{ { "data science" },
		  "data\nscience",
		  "boundary",
		  NO_MATCH,
		  { { -1 } } },
		{ { "data science" },
		  "data\r\nscience",
		  "boundary",
		  NO_MATCH,
		  { { -1 } } },
		{ { "data science" },
		  "data\xe2\x80\xa8science",
		  "boundary",
		  NO_MATCH,
		  { { -1 } } },
		{ { "data science" },
		  "data\xe3\x80\x80science",
		  "boundary",
		  MATCH,
		  { { 0, 0, 12 }, { -1 } } },
		{ { "data science" },
		  "(data science)",
		  NULL,
		  MATCH,
		  { { 0, 1, 13 }, { -1 } } },
		{ { "data science" },
		  "big data science work",
		  NULL,
		  MATCH,
		  { { 0, 4, 16 }, { -1 } } },
		{ { "data science" },
		  "\xf0\x9f\x98\x80 data science",
		  NULL,
		  MATCH,
		  { { 0, 2, 14 }, { -1 } } },
		{ { "data science" },
		  "science data",
		  NULL,
		  NO_MATCH,
		  { { -1 } } },
		{ { "data science" },
		  "science of data",
		  NULL,
		  NO_MATCH,
		  { { -1 } } },
		{ { "data science" },
		  "data sciences",
		  NULL,
		  NO_MATCH,
		  { { -1 } } },
		{ { "data science" },
		  "datascience",
		  NULL,
		  NO_MATCH,
		  { { -1 } } },
		{ { "alpha+beta" },
		  "alpha x y z beta",
		  NULL,
		  MATCH,
		  { { 0, 0, 16 }, { -1 } } },
		{ { "alpha+beta" },
		  "beta x y z alpha",
		  NULL,
		  MATCH,
		  { { 0, 0, 16 }, { -1 } } },
		{ { "alpha+beta" },
		  "alpha w x y z beta",
		  NULL,
		  NO_MATCH,
		  { { -1 } } },
		{ { "alpha+beta" },
		  "beta w x y z alpha",
		  NULL,
		  NO_MATCH,
		  { { -1 } } },
		{ { "alpha+beta" },
		  "alpha, x, beta",
		  NULL,
		  MATCH,
		  { { 0, 0, 14 }, { -1 } } },
		{ { "alpha+beta" },
		  "alpha , x , y beta",
		  NULL,
		  NO_MATCH,
		  { { -1 } } },
		{ { "alpha+beta" },
		  "alpha the of a beta",
		  NULL,
		  MATCH,
		  { { 0, 0, 19 }, { -1 } } },
		{ { "alpha+beta" },
		  "alpha  \t  beta",
		  NULL,
		  MATCH,
		  { { 0, 0, 14 }, { -1 } } },
		{ { "alpha+beta" },
		  "alpha\n\n\nbeta",
		  NULL,
		  MATCH,
		  { { 0, 0, 12 }, { -1 } } },
		{ { "alpha+beta" },
		  "alpha\nbeta",
		  "boundary",
		  NO_MATCH,
		  { { -1 } } },
		{ { "alpha+beta" },
		  "alpha x\ny beta",
		  "boundary",
		  NO_MATCH,
		  { { -1 } } },
		{ { "alpha+beta" }, "alpha&beta", NULL, NO_MATCH, { { -1 } } },
		{ { "alpha+beta" }, "alpha'beta", NULL, NO_MATCH, { { -1 } } },
		{ { "alpha+beta" }, "alpha_beta", NULL, NO_MATCH, { { -1 } } },
		{ { "alpha+beta" }, "alpha`beta", NULL, NO_MATCH, { { -1 } } },
		{ { "alpha+beta" },
		  "alpha/beta",
		  NULL,
		  MATCH,
		  { { 0, 0, 10 }, { -1 } } },
		{ { "alpha++beta" },
		  "alpha beta",
		  NULL,
		  MATCH,
		  { { 0, 0, 10 }, { -1 } } },
		{ { "alpha++beta" },
		  "alpha x beta",
		  NULL,
		  NO_MATCH,
		  { { -1 } } },
		{ { "alpha+", "+beta" },
		  "alpha beta",
		  NULL,
		  MATCH,
		  { { 0, 0, 5 }, { 1, 6, 10 }, { -1 } } },
		{ { "alpha+beta+gamma" },
		  "alpha beta gamma",
		  NULL,
		  MATCH,
		  { { 0, 0, 16 }, { -1 } } },
		{ { "alpha+beta", "alpha+beta+gamma" },
		  "gamma x alpha y beta",
		  NULL,
		  MATCH,
		  { { 1, 0, 20 }, { -1 } } },
		{ { "data", "data science", "science" },
		  "Data Science",
		  NULL,
		  MATCH,
		  { { 1, 0, 12 }, { -1 } } },
		{ { "data", "data science", "science" },
		  "data data science science",
		  NULL,
		  MATCH,
		  { { 0, 0, 4 }, { 1, 5, 17 }, { 2, 18, 25 } } },
		{ { "data science", "DATA SCIENCE", "Data Science" },
		  "data science",
		  NULL,
		  MATCH,
		  { { 0, 0, 12 }, { -1 } } },
		{ { "caf\xc3\xa9 cr\xc3\xa8me" },
		  "CAF\xc3\x89 CR\xc3\x88ME",
		  NULL,
		  MATCH,
		  { { 0, 0, 10 }, { -1 } } },
		{ { "caf\xc3\xa9 cr\xc3\xa8me" },
		  "Caf\xc3\xa9 Cr\xc3\xa8me",
		  NULL,
		  MATCH,
		  { { 0, 0, 10 }, { -1 } } },
		{ { "caf\xc3\xa9 cr\xc3\xa8me" },
		  "cafe creme",
		  NULL,
		  MATCH,
		  { { 0, 0, 10 }, { -1 } } },
		{ { "na\xc3\xafve+r\xc3\xa9sum\xc3\xa9" },
		  "r\xc3\xa9sum\xc3\xa9 x na\xc3\xafve",
		  NULL,
		  MATCH,
		  { { 0, 0, 14 }, { -1 } } },
		{ { "stra\xc3\x9f"
		    "e" },
		  "STRA\xe1\xba\x9e"
		  "E",
		  NULL,
		  MATCH,
		  { { 0, 0, 6 }, { -1 } } },
		{ { "stra\xc3\x9f"
		    "e" },
		  "STRASSE",
		  NULL,
		  NO_MATCH,
		  { { -1 } } },
		{ { "driver's ed" },
		  "Driver\xe2\x80\x99s Ed",
		  NULL,
		  MATCH,
		  { { 0, 0, 11 }, { -1 } } },
		{ { "driver's ed" },
		  "Driver`s Ed",
		  NULL,
		  MATCH,
		  { { 0, 0, 11 }, { -1 } } },
		{ { "driver's ed" },
		  "DRIVER'S ED",
		  NULL,
		  MATCH,
		  { { 0, 0, 11 }, { -1 } } },
		/* Input repair is the consumer's job (docs/protocol.md). */
		{ { "driver's ed" },
		  "Driver ' s Ed",
		  NULL,
		  NO_MATCH,
		  { { -1 } } },
		{ { "data science" },
		  "data-science",
		  NULL,
		  XFAIL,
		  { { 0, 0, 12 }, { -1 } } },
		{ { "Aircraft Physics/Weight & Balance" },
		  "Aircraft Physics-Weight & Balance",
		  NULL,
		  XFAIL,
		  { { 0, 0, 33 }, { -1 } } },
		{ { "caf\xc3\xa9" },
		  "cafe\xcc\x81",
		  NULL,
		  MATCH,
		  { { 0, 0, 5 }, { -1 } } },
		{ { "istanbul" },
		  "\xc4\xb0stanbul",
		  NULL,
		  MATCH,
		  { { 0, 0, 8 }, { -1 } } },
		{ { "data science" },
		  "\xef\xbd\x84\xef\xbd\x81\xef\xbd\x94\xef\xbd\x81 \xef\xbd\x93\xef\xbd\x83\xef\xbd\x89\xef\xbd\x85\xef\xbd\x8e\xef\xbd\x83\xef\xbd\x85",
		  NULL,
		  XFAIL,
		  { { 0, 0, 12 }, { -1 } } },
		{ { "data science" },
		  "da\xc2\xadta science",
		  NULL,
		  MATCH,
		  { { 0, 0, 13 }, { -1 } } },
		{ { "data science" },
		  "data\xe2\x80\x8b science",
		  NULL,
		  MATCH,
		  { { 0, 0, 13 }, { -1 } } },
		{ { "data science" },
		  "data\xe2\x80\x8bscience",
		  NULL,
		  NO_MATCH,
		  { { -1 } } },
	};
	for (size_t i = 0; i < sizeof(rows) / sizeof(*rows); i++) {
		int n = 0;
		while (n < 3 && rows[i].terms[n])
			n++;
		J *terms = terms_of(rows[i].terms, n), *want = ARR();
		for (int k = 0; k < 3 && rows[i].m[k][0] >= 0; k++)
			expect_at(want, rows[i].m[k][0], rows[i].text,
				  rows[i].m[k][1], rows[i].m[k][2]);
		both("table", terms, rows[i].text, rows[i].line_breaks, want,
		     rows[i].kind);
		DEL(want);
		DEL(terms);
	}
}

int main(void)
{
	if (!setup_engines()) {
		fprintf(stderr, "FAIL setting up the engines\n");
		return 1;
	}
	table_family();
	case_family();
	whitespace_family();
	punctuation_family();
	unicode_family();
	position_family();
	spans_family();
	lists_family();
	isolation_family();
	engines_family();
	errors_family();
	int total = 0, short_of = 0;
	for (int i = 0; i < CATEGORY_COUNT; i++) {
		Category *c = &categories[i];
		total += c->cases;
		printf("%-12s %6d cases, %d failed", c->name, c->cases,
		       c->failed);
		if (c->xfail)
			printf(", %d expected fails", c->xfail);
		if (c->cases < c->minimum) {
			printf(" (below minimum %d)", c->minimum);
			short_of++;
		}
		printf("\n");
	}
	for (int i = 0; i < ENGINES; i++)
		mc_destroy(engines[i]);
	printf("match_terms: %d cases, %d failed, %d expected fails now "
	       "passing.\n",
	       total, failures, xpasses);
	return failures || short_of ? 1 : 0;
}
