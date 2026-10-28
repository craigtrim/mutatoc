/*
 * test_normalization.c - Accents and invisible characters fold out of every
 * match key.
 *
 * Runs Unicode's NormalizationTest.txt against the vendored utf8proc, checks
 * the fold of every code point against an oracle read from UnicodeData.txt
 * and DerivedCoreProperties.txt that shares no code with utf8proc, and checks
 * that ASCII never changes. Matching families then cross case, accent form
 * and invisible characters against tests/fixtures/ontologies/accents-test.json
 * through parse, parse_tokens, the exact stage, the live interface, an update
 * and a snapshot that mutatoc 0.7.0 prepared. Expected entities follow from
 * where the words were put, never from an engine run. Every case is a match,
 * a no match or an expected fail; an expected fail runs every time, and one
 * that starts passing fails the suite. craigtrim/mutatoc#25
 */

#include "testlib.h"
#include "utf8proc.h"
#include <time.h>

#define FIXTURE "tests/fixtures/ontologies/accents-test.json"
#define SNAPSHOT "tests/fixtures/ontologies/accents-test.0.7.0.snapshot.json"
#define LEGACY "tests/fixtures/ontologies/accents-test.0.7.0.tokens.json"
#define UNICODE_DIR "tests/fixtures/unicode/"
#define CODE_POINTS 0x110000

/* ---------------------------------------------------------------------- */
/* Bookkeeping.                                                           */

typedef struct {
	const char *name;
	int minimum, cases, failed, xfail;
} Category;
static Category categories[] = {
	{ "conformance", 19000 },   { "code points", 1112064 },
	{ "ascii", 128 },	    { "words", 21600 },
	{ "joined", 2304 },	    { "line breaks", 1152 },
	{ "spans", 36000 },	    { "invisible fillers", 252 },
	{ "letters", 40 },	    { "normals", 240 },
	{ "collisions", 20 },	    { "updates", 240 },
	{ "mixed runs", 1584 },	    { "punctuation edges", 58 },
	{ "invisible words", 240 }, { "offsets", 516 },
	{ "self spans", 30 },	    { "legacy tokens", 70 },
	{ "folded repeats", 71 },   { "span cycles", 115 },
	{ "span bounds", 14 },	    { "expected fails", 12 },
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
/* UTF-8 helpers that share no code with the fold.                        */

static void put_cp(Buf *b, uint32_t c)
{
	uwrite(b, c);
}

static char *encode(const uint32_t *cps, size_t n)
{
	Buf b = { 0 };
	for (size_t i = 0; i < n; i++)
		put_cp(&b, cps[i]);
	return buf_take(&b);
}

static char *read_text(const char *root, const char *relative)
{
	char *path = test_path(root, relative);
	mc_error err = { 0 };
	char *text = read_file(path, &err);
	if (!text)
		fprintf(stderr, "%s: %s\n", path, err.message);
	free(path);
	return text;
}

/* Splits text into lines in place, dropping carriage returns. */
static char **lines_of(char *text, int *count)
{
	int cap = 1024, n = 0;
	char **out = malloc((size_t)cap * sizeof(*out));
	for (char *p = text; p && *p;) {
		char *end = strchr(p, '\n');
		if (end)
			*end = 0;
		size_t len = strlen(p);
		if (len && p[len - 1] == '\r')
			p[len - 1] = 0;
		if (n == cap) {
			cap *= 2;
			out = realloc(out, (size_t)cap * sizeof(*out));
		}
		out[n++] = p;
		p = end ? end + 1 : NULL;
	}
	*count = n;
	return out;
}

/* Parses space-separated hex code points; returns the count. */
static size_t hex_points(const char *s, uint32_t *out, size_t cap)
{
	size_t n = 0;
	while (*s && n < cap) {
		while (*s == ' ')
			s++;
		if (!isxdigit((unsigned char)*s))
			break;
		char *end;
		out[n++] = (uint32_t)strtoul(s, &end, 16);
		s = end;
	}
	return n;
}

/* ---------------------------------------------------------------------- */
/* NormalizationTest.txt against the vendored utf8proc.                   */

static void conformance(const char *root)
{
	Category *c = category("conformance");
	char *text = read_text(root, UNICODE_DIR "NormalizationTest.txt");
	if (!text) {
		fail_case(c, "NormalizationTest.txt missing", NULL, NULL, NULL);
		return;
	}
	int count;
	char **lines = lines_of(text, &count);
	for (int i = 0; i < count; i++) {
		char *line = lines[i];
		if (!*line || *line == '#' || *line == '@')
			continue;
		char *col[5] = { 0 };
		char *p = line;
		for (int k = 0; k < 5 && p; k++) {
			col[k] = p;
			p = strchr(p, ';');
			if (p)
				*p++ = 0;
		}
		if (!col[4])
			continue;
		char *s[5];
		for (int k = 0; k < 5; k++) {
			uint32_t cps[64];
			size_t n = hex_points(col[k], cps, 64);
			s[k] = encode(cps, n);
		}
		c->cases++;
		/* UAX #15 conformance: c2 = NFC(c1..c3), c4 = NFC(c4..c5), and so on. */
		int ok = 1;
		for (int k = 0; k < 5 && ok; k++) {
			const utf8proc_uint8_t *in =
				(const utf8proc_uint8_t *)s[k];
			char *nfc = (char *)utf8proc_NFC(in),
			     *nfd = (char *)utf8proc_NFD(in),
			     *nfkc = (char *)utf8proc_NFKC(in),
			     *nfkd = (char *)utf8proc_NFKD(in);
			ok = nfc && nfd && nfkc && nfkd &&
			     !strcmp(nfc, s[k < 3 ? 1 : 3]) &&
			     !strcmp(nfd, s[k < 3 ? 2 : 4]) &&
			     !strcmp(nfkc, s[3]) && !strcmp(nfkd, s[4]);
			utf8proc_free(nfc);
			utf8proc_free(nfd);
			utf8proc_free(nfkc);
			utf8proc_free(nfkd);
		}
		if (!ok)
			fail_case(c, "normalization form", s[0], NULL, NULL);
		for (int k = 0; k < 5; k++)
			free(s[k]);
	}
	free(lines);
	free(text);
}

/* ---------------------------------------------------------------------- */
/* Every code point against an oracle read from the Unicode data files.   */

typedef struct {
	unsigned char mark, ignorable;
	uint32_t *decomposition; /* canonical only, terminated by 0 */
} Point;
static Point *points;

static int load_oracle(const char *root)
{
	char *data = read_text(root, UNICODE_DIR "UnicodeData.txt"),
	     *derived =
		     read_text(root, UNICODE_DIR "DerivedCoreProperties.txt");
	if (!data || !derived) {
		free(data);
		free(derived);
		return 0;
	}
	points = calloc(CODE_POINTS, sizeof(*points));
	int count;
	char **lines = lines_of(data, &count);
	uint32_t first = 0;
	for (int i = 0; i < count; i++) {
		char *f[6] = { 0 }, *p = lines[i];
		for (int k = 0; k < 6 && p; k++) {
			f[k] = p;
			p = strchr(p, ';');
			if (p)
				*p++ = 0;
		}
		if (!f[5])
			continue;
		uint32_t cp = (uint32_t)strtoul(f[0], NULL, 16);
		int mark = f[2][0] == 'M';
		/* Ranges come as a First line and a Last line. */
		if (strstr(f[1], ", First>")) {
			first = cp;
			continue;
		}
		uint32_t lo = strstr(f[1], ", Last>") ? first : cp;
		for (uint32_t c = lo; c <= cp; c++)
			points[c].mark = (unsigned char)mark;
		if (*f[5] && *f[5] != '<') {
			uint32_t cps[32];
			size_t n = hex_points(f[5], cps, 31);
			points[cp].decomposition = calloc(
				n + 1, sizeof(*points[cp].decomposition));
			memcpy(points[cp].decomposition, cps, n * sizeof(*cps));
		}
	}
	free(lines);
	lines = lines_of(derived, &count);
	for (int i = 0; i < count; i++) {
		char *line = lines[i];
		if (!*line || *line == '#' ||
		    !strstr(line, "; Default_Ignorable_Code_Point"))
			continue;
		char *end;
		uint32_t lo = (uint32_t)strtoul(line, &end, 16), hi = lo;
		if (end[0] == '.' && end[1] == '.')
			hi = (uint32_t)strtoul(end + 2, NULL, 16);
		for (uint32_t c = lo; c <= hi; c++)
			points[c].ignorable = 1;
	}
	free(lines);
	free(data);
	free(derived);
	return 1;
}

/* The fold the issue defines: decompose, then drop marks and ignorables. */
static void oracle_fold(uint32_t cp, Buf *b)
{
	const Point *pt = &points[cp];
	int hangul = cp >= 0xac00 && cp <= 0xd7a3;
	if (pt->decomposition && !hangul) {
		for (const uint32_t *d = pt->decomposition; *d; d++)
			oracle_fold(*d, b);
		return;
	}
	if ((pt->mark || pt->ignorable) && cp >= 0x80)
		return;
	put_cp(b, cp);
}

static void code_points(void)
{
	Category *c = category("code points");
	for (uint32_t cp = 0; cp < CODE_POINTS; cp++) {
		if (cp >= 0xd800 && cp <= 0xdfff)
			continue;
		c->cases++;
		Buf in = { 0 }, want = { 0 };
		put_cp(&in, cp);
		oracle_fold(cp, &want);
		char *s = buf_take(&in), *expected = buf_take(&want),
		     *actual = ufold(s);
		int joiner = cp >= 0x80 &&
			     (points[cp].mark || points[cp].ignorable);
		if (strcmp(actual, expected) || ujoiner(cp) != joiner ||
		    ufolds(cp) != !!strcmp(s, expected)) {
			char label[48];
			snprintf(label, sizeof(label), "U+%04X fold", cp);
			J *e = STR(expected), *a = STR(actual);
			fail_case(c, label, s, e, a);
			DEL(e);
			DEL(a);
		}
		free(s);
		free(expected);
		free(actual);
	}
}

/* ASCII never changes, on the fast path or inside non-ASCII text. */
static void ascii(void)
{
	Category *c = category("ascii");
	for (uint32_t cp = 0; cp < 0x80; cp++) {
		c->cases++;
		Buf b = { 0 };
		put_cp(&b, cp);
		char *alone = buf_take(&b);
		buf_put(&b, alone);
		buf_put(&b, "\xc3\x9f"); /* ß, which the fold keeps */
		buf_put(&b, alone);
		char *mixed = buf_take(&b), *fast = ufold(alone),
		     *slow = ufold(mixed);
		utf8proc_int32_t out[4], bc = 0;
		utf8proc_ssize_t n = utf8proc_decompose_char(
			(utf8proc_int32_t)cp, out, 4,
			UTF8PROC_DECOMPOSE | UTF8PROC_STRIPMARK |
				UTF8PROC_IGNORE,
			&bc);
		if (strcmp(fast, alone) || strcmp(slow, mixed) || n != 1 ||
		    out[0] != (utf8proc_int32_t)cp || ujoiner(cp))
			fail_case(c, "ASCII changed", mixed, NULL, NULL);
		free(alone);
		free(mixed);
		free(fast);
		free(slow);
	}
}

/* ---------------------------------------------------------------------- */
/* Word variants: accent form, case and invisible characters.              */

/* Precomposed, decomposed and unaccented forms of each word. */
typedef struct {
	const char *forms[3];
} Word;
static const Word cafe = { { "caf\xc3\xa9", "cafe\xcc\x81", "cafe" } },
		  creme = { { "cr\xc3\xa8me", "cre\xcc\x80me", "creme" } },
		  naive = { { "na\xc3\xafve", "nai\xcc\x88ve", "naive" } },
		  resume = { { "r\xc3\xa9sum\xc3\xa9", "re\xcc\x81sume\xcc\x81",
			       "resume" } };

/*
 * Cases: lower, upper, title, alternating. Invisibles: none, a soft hyphen
 * inside, a zero width joiner inside, a left-to-right mark before, a zero
 * width space after.
 */
enum { CASES = 4, INVISIBLES = 5, VARIANTS = 3 * CASES * INVISIBLES };

static char *recase(const char *s, int mode)
{
	Buf b = { 0 };
	const char *p = s;
	int letters = 0;
	while (*p) {
		uint32_t c = uread(&p);
		Buf one = { 0 };
		put_cp(&one, c);
		char *ch = buf_take(&one);
		int up = mode == 1 || (mode == 2 && letters == 0) ||
			 (mode == 3 && letters % 2 == 1);
		if (ualpha(c))
			letters++;
		char *out = up ? upper(ch) : lower(ch);
		buf_put(&b, out);
		free(out);
		free(ch);
	}
	return buf_take(&b);
}

/* Inserts an invisible character; returns a new string. */
static char *invisible(const char *s, int mode)
{
	Buf b = { 0 };
	if (mode == 3)
		buf_put(&b, "\xe2\x80\x8e"); /* U+200E */
	const char *p = s;
	int at = 0;
	while (*p) {
		const char *start = p;
		uread(&p);
		buf_add(&b, start, (size_t)(p - start));
		at++;
		if (mode == 1 && at == 2)
			buf_put(&b, "\xc2\xad"); /* U+00AD */
		if (mode == 2 && at == 1)
			buf_put(&b, "\xe2\x80\x8d"); /* U+200D */
	}
	if (mode == 4)
		buf_put(&b, "\xe2\x80\x8b"); /* U+200B */
	return buf_take(&b);
}

/* Variant v of w: accent form v / 20, case (v / 5) % 4, invisible v % 5. */
static char *variant(const Word *w, int v)
{
	char *cased = recase(w->forms[v / (CASES * INVISIBLES)],
			     (v / INVISIBLES) % CASES),
	     *out = invisible(cased, v % INVISIBLES);
	free(cased);
	return out;
}

static int leads_with_mark(int v)
{
	return v % INVISIBLES == 3;
}

/* ---------------------------------------------------------------------- */
/* Engines and paths.                                                     */

enum { FRESH, SNAPSHOT_070, LIVE, UPDATED, ENGINES };
static mc_engine *engines[ENGINES];
static const char *const engine_names[ENGINES] = { "fresh load",
						   "0.7.0 snapshot", "live",
						   "update" };

static int load_engines(const char *root)
{
	char *content = read_text(root, FIXTURE);
	J *snapshot = test_read_json(root, SNAPSHOT);
	if (!content || !snapshot) {
		free(content);
		DEL(snapshot);
		return 0;
	}
	int ok = 1;
	for (int i = 0; i < ENGINES; i++) {
		engines[i] = mc_create();
		J *q = OBJ();
		PUT(q, "op", STR("load"));
		PUT(q, "name", STR("accents-test"));
		if (i == SNAPSHOT_070) {
			PUT(q, "snapshot", DUP(snapshot));
		} else {
			PUT(q, "content", STR(content));
			PUT(q, "format", STR("json"));
		}
		if (i == LIVE)
			PUT(q, "interface", STR("data"));
		J *r = test_request(engines[i], q);
		ok = ok && cJSON_IsTrue(GET(r, "ok"));
		DEL(r);
		DEL(q);
	}
	/* Replace the accented class with an ASCII label, then restore it. */
	const char *iri = "http://example.org/accents#Ecafecreme";
	const char *labels[] = { "placeholder words",
				 "Caf\xc3\xa9 Cr\xc3\xa8me" };
	for (int i = 0; i < 2 && ok; i++) {
		Buf b = { 0 };
		buf_put(&b, "<");
		buf_put(&b, iri);
		buf_put(&b, "> a <http://www.w3.org/2002/07/owl#Class> ; "
			    "<http://www.w3.org/2000/01/rdf-schema#label> \"");
		buf_put(&b, labels[i]);
		buf_put(&b, "\" .");
		char *turtle = buf_take(&b);
		J *q = OBJ(), *replace = OBJ();
		PUT(q, "op", STR("update"));
		PUT(replace, iri, STR(turtle));
		PUT(q, "replace", replace);
		J *r = test_request(engines[UPDATED], q);
		ok = cJSON_IsTrue(GET(r, "ok"));
		if (!ok)
			test_print_diff("update failed", q, r);
		DEL(r);
		DEL(q);
		free(turtle);
	}
	free(content);
	DEL(snapshot);
	return ok;
}

/* canon, x and y of every entity, plus text when with_text. */
static J *entities_of(const J *tokens, int with_text)
{
	J *out = ARR();
	EACH(t, tokens) {
		J *swaps = GET(t, "swaps");
		if (!swaps)
			continue;
		J *e = OBJ();
		PUT(e, "canon", DUP(GET(swaps, "canon")));
		PUT(e, "x", DUP(GET(t, "x")));
		PUT(e, "y", DUP(GET(t, "y")));
		if (with_text)
			PUT(e, "text", DUP(GET(t, "text")));
		ADD(out, e);
	}
	return out;
}

static J *request_tokens(mc_engine *e, const char *op, const char *text,
			 J *tokens, const char *line_breaks)
{
	J *q = OBJ();
	PUT(q, "op", STR(op));
	if (text)
		PUT(q, "text", STR(text));
	if (tokens)
		PUT(q, "tokens", tokens);
	if (!strcmp(op, "transform_tokens"))
		PUT(q, "stage", STR("exact"));
	if (line_breaks)
		PUT(q, "line_breaks", STR(line_breaks));
	J *r = test_request(e, q);
	DEL(q);
	J *result =
		cJSON_IsTrue(GET(r, "ok")) ?
			cJSON_DetachItemFromObjectCaseSensitive(r, "result") :
			NULL;
	DEL(r);
	return result;
}

/* Strips each expected entity down to canon, x and y. */
static J *without_text(const J *expected)
{
	J *out = DUP(expected);
	EACH(e, out)
		cJSON_DeleteItemFromObjectCaseSensitive(e, "text");
	return out;
}

enum { MATCH, NO_MATCH, XFAIL };
enum { ALL_PATHS = 1, EXACT_ONLY = 2 };

/*
 * Runs one text through every engine and path and compares the entities.
 * An expected fail runs on the fresh engine only and must still fail.
 */
static void check(const char *name, const char *text, const J *expected,
		  int kind, int flags, const char *line_breaks)
{
	Category *c = category(name);
	c->cases++;
	if (kind == XFAIL) {
		J *r = request_tokens(engines[FRESH], "parse", text, NULL,
				      line_breaks);
		J *actual = r ? entities_of(GET(r, "tokens"), 1) : NULL;
		if (actual && cJSON_Compare(actual, expected, 1)) {
			xpasses++;
			fail_case(c,
				  "expected fail now passes; update its label",
				  text, expected, actual);
		} else
			c->xfail++;
		DEL(actual);
		DEL(r);
		return;
	}
	J *bare = without_text(expected);
	for (int i = 0; i < ENGINES; i++) {
		J *r = request_tokens(engines[i], "parse", text, NULL,
				      line_breaks);
		J *actual = r ? entities_of(GET(r, "tokens"), 1) : NULL;
		if (!actual || !cJSON_Compare(actual, expected, 1)) {
			char what[64];
			snprintf(what, sizeof(what), "parse, %s",
				 engine_names[i]);
			fail_case(c, what, text, expected, actual);
		}
		DEL(actual);
		DEL(r);
	}
	if (flags & ALL_PATHS) {
		J *tokens = request_tokens(engines[FRESH], "tokenize", text,
					   NULL, NULL);
		const char *ops[] = { "parse_tokens", "transform_tokens" };
		for (int k = 0; k < 2; k++) {
			if (k == 1 && !(flags & EXACT_ONLY))
				continue;
			J *r = request_tokens(engines[FRESH], ops[k], NULL,
					      DUP(tokens), line_breaks);
			J *list = k ? r : GET(r, "tokens");
			J *actual = r ? entities_of(list, 0) : NULL;
			if (!actual || !cJSON_Compare(actual, bare, 1))
				fail_case(c, ops[k], text, bare, actual);
			DEL(actual);
			DEL(r);
		}
		/*
		 * Tokens prepared before folding keep accented normals. 0.7.0
		 * split invisible characters off, so only texts without them
		 * have the same tokens then and now.
		 */
		int visible = 1;
		for (const char *p = text; *p && visible;)
			visible = !ujoiner(uread(&p));
		if (visible) {
			J *old = DUP(tokens);
			EACH(t, old) {
				char *trim = norm(S(GET(t, "text")), 1, 0);
				set(t, "normal", STR(trim));
				free(trim);
			}
			J *r = request_tokens(engines[FRESH], "parse_tokens",
					      NULL, old, line_breaks);
			J *actual = r ? entities_of(GET(r, "tokens"), 0) : NULL;
			if (!actual || !cJSON_Compare(actual, bare, 1))
				fail_case(c,
					  "parse_tokens with unfolded normals",
					  text, bare, actual);
			DEL(actual);
			DEL(r);
		}
		DEL(tokens);
	}
	DEL(bare);
	(void)kind;
}

static J *one_entity(const char *canon, const char *text, size_t x, size_t y)
{
	J *list = ARR(), *e = OBJ();
	PUT(e, "canon", STR(canon));
	PUT(e, "x", NUM((double)x));
	PUT(e, "y", NUM((double)y));
	/* The entity's text is the input from x to y. */
	const char *p = text;
	for (size_t i = 0; i < x; i++)
		uread(&p);
	const char *start = p;
	for (size_t i = x; i < y; i++)
		uread(&p);
	char *s = slice(start, (size_t)(p - start));
	PUT(e, "text", STR(s));
	free(s);
	ADD(list, e);
	return list;
}

/* ---------------------------------------------------------------------- */
/* Families.                                                              */

static const char *const contexts[][2] = { { "", "" },
					   { "I went to ", " today" } };
#define CONTEXTS 2

/*
 * Where a phrase starts: a mark before the first word joins the whitespace
 * in front of it, so the word starts after the mark unless the text does.
 */
static size_t phrase_x(const char *prefix, int v)
{
	size_t x = ulen(prefix);
	return x + (leads_with_mark(v) && *prefix ? 1 : 0);
}

static void words(void)
{
	static const char *const seps[] = { " ", "  ", "\t" };
	for (int a = 0; a < VARIANTS; a++) {
		char *w1 = variant(&cafe, a);
		for (int b = 0; b < VARIANTS; b++) {
			char *w2 = variant(&creme, b);
			for (int s = 0; s < 3; s++)
				for (int k = 0; k < CONTEXTS; k++) {
					Buf t = { 0 };
					buf_put(&t, contexts[k][0]);
					buf_put(&t, w1);
					buf_put(&t, seps[s]);
					buf_put(&t, w2);
					size_t y = ulen(t.p);
					buf_put(&t, contexts[k][1]);
					char *text = buf_take(&t);
					J *expected = one_entity(
						"ecafecreme", text,
						phrase_x(contexts[k][0], a), y);
					/* Prepared tokens and the exact stage on a sample. */
					int flags = (a + b + s + k) % 7 == 0 ?
							    ALL_PATHS |
								    EXACT_ONLY :
							    0;
					check("words", text, expected, MATCH,
					      flags, NULL);
					DEL(expected);
					free(text);
				}
			free(w2);
		}
		free(w1);
	}
}

/* An invisible character with no space after it joins two words into one. */
static void joined(void)
{
	static const char *const joiners[] = { "\xe2\x80\x8b", "\xc2\xad",
					       "\xe2\x80\x8d", "\xe2\x81\xa0" };
	for (int a = 0; a < VARIANTS; a += INVISIBLES)
		for (int b = 0; b < VARIANTS; b += INVISIBLES)
			for (int j = 0; j < 4; j++)
				for (int spaced = 0; spaced < 2; spaced++)
					for (int k = 0; k < CONTEXTS; k++) {
						char *w1 = variant(&cafe, a),
						     *w2 = variant(&creme, b);
						Buf t = { 0 };
						buf_put(&t, contexts[k][0]);
						buf_put(&t, w1);
						buf_put(&t, joiners[j]);
						if (spaced)
							buf_put(&t, " ");
						buf_put(&t, w2);
						size_t y = ulen(t.p);
						buf_put(&t, contexts[k][1]);
						char *text = buf_take(&t);
						J *expected = one_entity(
							spaced ?
								"ecafecreme" :
								"ecafecremejoined",
							text,
							ulen(contexts[k][0]),
							y);
						check("joined", text, expected,
						      MATCH,
						      ALL_PATHS | EXACT_ONLY,
						      NULL);
						DEL(expected);
						free(text);
						free(w1);
						free(w2);
					}
}

static void line_breaks(void)
{
	static const char *const breaks[] = { "\n", "\r\n", "\xe2\x80\xa8",
					      " \xe2\x80\x8b\n" };
	for (int a = 0; a < VARIANTS; a += INVISIBLES)
		for (int b = 0; b < VARIANTS; b += INVISIBLES)
			for (int s = 0; s < 4; s++)
				for (int mode = 0; mode < 2; mode++) {
					char *w1 = variant(&cafe, a),
					     *w2 = variant(&creme, b);
					Buf t = { 0 };
					buf_put(&t, w1);
					buf_put(&t, breaks[s]);
					buf_put(&t, w2);
					char *text = buf_take(&t);
					J *expected =
						mode ? ARR() :
						       one_entity("ecafecreme",
								  text, 0,
								  ulen(text));
					check("line breaks", text, expected,
					      mode ? NO_MATCH : MATCH,
					      ALL_PATHS | EXACT_ONLY,
					      mode ? "boundary" : "ignore");
					DEL(expected);
					free(text);
					free(w1);
					free(w2);
				}
}

/* naïve+résumé in both orders, 0 to 4 words apart. */
static void spans(void)
{
	for (int a = 0; a < VARIANTS; a++)
		for (int b = 0; b < VARIANTS; b++)
			for (int order = 0; order < 2; order++)
				for (int gap = 0; gap <= 4; gap++) {
					char *w1 = variant(order ? &resume :
								   &naive,
							   a),
					     *w2 = variant(order ? &naive :
								   &resume,
							   b);
					Buf t = { 0 };
					buf_put(&t, w1);
					buf_put(&t, " ");
					for (int g = 0; g < gap; g++)
						buf_put(&t, "x ");
					buf_put(&t, w2);
					char *text = buf_take(&t);
					/* Four words apart is past the distance of 4. */
					J *expected =
						gap < 4 ?
							one_entity(
								"enaiveresume",
								text, 0,
								ulen(text)) :
							ARR();
					check("spans", text, expected,
					      gap < 4 ? MATCH : NO_MATCH, 0,
					      NULL);
					DEL(expected);
					free(text);
					free(w1);
					free(w2);
				}
}

/* Tokens made only of invisible characters take no position. */
static void invisible_fillers(void)
{
	static const char *const fillers[] = { "\xc2\xad", "\xe2\x80\x8b",
					       "\xe2\x80\x8e" };
	for (int a = 0; a < VARIANTS; a += 5)
		for (int f = 0; f < 3; f++)
			for (int n = 0; n <= 6; n++) {
				char *w1 = variant(&resume, a),
				     *w2 = variant(&naive, a);
				Buf t = { 0 };
				buf_put(&t, w1);
				buf_put(&t, " ");
				for (int g = 0; g < n; g++) {
					buf_put(&t, fillers[f]);
					buf_put(&t, " ");
				}
				buf_put(&t, w2);
				char *text = buf_take(&t);
				J *expected = one_entity("enaiveresume", text,
							 0, ulen(text));
				check("invisible fillers", text, expected,
				      MATCH, 0, NULL);
				DEL(expected);
				free(text);
				free(w1);
				free(w2);
			}
}

static void letters(void)
{
	static const struct {
		const char *text, *canon;
	} table[] = {
		{ "\xc4\xb0stanbul", "eistanbul" },
		{ "\xc4\xb0STANBUL", "eistanbul" },
		{ "istanbul", "eistanbul" },
		{ "Istanbul", "eistanbul" },
		{ "ISTANBUL", "eistanbul" },
		{ "i\xcc\x87stanbul", "eistanbul" },
		{ "I\xcc\x87STANBUL", "eistanbul" },
		{ "\xc4\xb1stanbul", NULL }, /* dotless ı is another letter */
		{ "stra\xc3\x9f"
		  "e",
		  "estrasse" },
		{ "STRA\xe1\xba\x9e"
		  "E",
		  "estrasse" },
		{ "Stra\xc3\x9f"
		  "e",
		  "estrasse" },
		{ "STRASSE", NULL }, /* ß is not ss */
		{ "strasse", NULL },
		{ "Strasse", NULL },
		{ "coffee", "ecoffee" },
		{ "COFFEE", "ecoffee" },
		{ "Coffee", "ecoffee" },
		{ "cOfFeE", "ecoffee" },
		{ "co\xc2\xad"
		  "ffee",
		  "ecoffee" },
		{ "coffee\xcc\x81", "ecoffee" },
	};
	for (size_t i = 0; i < sizeof(table) / sizeof(*table); i++)
		for (int k = 0; k < CONTEXTS; k++) {
			Buf t = { 0 };
			buf_put(&t, contexts[k][0]);
			size_t x = ulen(t.p ? t.p : "");
			buf_put(&t, table[i].text);
			size_t y = ulen(t.p);
			buf_put(&t, contexts[k][1]);
			char *text = buf_take(&t);
			J *expected =
				table[i].canon ?
					one_entity(table[i].canon, text, x, y) :
					ARR();
			check("letters", text, expected,
			      table[i].canon ? MATCH : NO_MATCH,
			      ALL_PATHS | EXACT_ONLY, NULL);
			DEL(expected);
			free(text);
		}
}

/* Every variant's normal is the unaccented lowercase word. */
static void normals(void)
{
	const Word *ws[] = { &cafe, &creme, &naive, &resume };
	for (int w = 0; w < 4; w++)
		for (int v = 0; v < VARIANTS; v++) {
			Category *c = category("normals");
			c->cases++;
			char *text = variant(ws[w], v);
			J *tokens = request_tokens(engines[FRESH], "tokenize",
						   text, NULL, NULL);
			const char *want = ws[w]->forms[2];
			int ok = SIZE(tokens) == 1 &&
				 !strcmp(S(GET(tokens->child, "normal")),
					 want) &&
				 !strcmp(S(GET(tokens->child, "text")), text);
			if (!ok) {
				J *e = STR(want);
				fail_case(c, "normal", text, e, tokens);
				DEL(e);
			}
			DEL(tokens);
			free(text);
		}
}

/*
 * When cafe and café are both labels, text in either form folds to cafe, and
 * the label stored in exactly that form wins whichever comes first.
 */
static void collisions(void)
{
	static const char *const orders[] = {
		"[{\"format\":\"mutatoc/1\",\"namespace\":\"http://example.org/c#\"},"
		"{\"id\":\"Eplain\",\"type\":\"owl:Class\",\"label\":\"cafe\"},"
		"{\"id\":\"Eaccent\",\"type\":\"owl:Class\",\"label\":\"caf\xc3\xa9\"}]",
		"[{\"format\":\"mutatoc/1\",\"namespace\":\"http://example.org/c#\"},"
		"{\"id\":\"Eaccent\",\"type\":\"owl:Class\",\"label\":\"caf\xc3\xa9\"},"
		"{\"id\":\"Eplain\",\"type\":\"owl:Class\",\"label\":\"cafe\"}]",
	};
	static const char *const texts[] = { "cafe", "caf\xc3\xa9",
					     "CAF\xc3\x89", "cafe\xcc\x81",
					     "Cafe\xcc\x81" };
	Category *c = category("collisions");
	for (int o = 0; o < 2; o++) {
		mc_engine *e = mc_create();
		J *q = OBJ();
		PUT(q, "op", STR("load"));
		PUT(q, "content", STR(orders[o]));
		PUT(q, "format", STR("json"));
		J *r = test_request(e, q);
		DEL(q);
		if (!cJSON_IsTrue(GET(r, "ok")))
			fail_case(c, "load", orders[o], NULL, r);
		DEL(r);
		for (size_t t = 0; t < sizeof(texts) / sizeof(*texts); t++)
			for (int k = 0; k < CONTEXTS; k++) {
				c->cases++;
				Buf b = { 0 };
				buf_put(&b, contexts[k][0]);
				size_t x = ulen(b.p ? b.p : "");
				buf_put(&b, texts[t]);
				size_t y = ulen(b.p);
				buf_put(&b, contexts[k][1]);
				char *text = buf_take(&b);
				J *expected = one_entity("eplain", text, x, y);
				J *result = request_tokens(e, "parse", text,
							   NULL, NULL);
				J *actual = result ? entities_of(GET(result,
								     "tokens"),
								 1) :
						     NULL;
				if (!actual ||
				    !cJSON_Compare(actual, expected, 1))
					fail_case(c, "folded tie", text,
						  expected, actual);
				DEL(actual);
				DEL(result);
				DEL(expected);
				free(text);
			}
		mc_destroy(e);
	}
}

/* ---------------------------------------------------------------------- */
/* Incremental updates against a fresh load of the same graph.            */

#define UPDATE_PREFIXES                                    \
	"@prefix : <http://example.org/u#> . "             \
	"@prefix owl: <http://www.w3.org/2002/07/owl#> . " \
	"@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> . "

static char *turtle_class(const char *id, const char *label)
{
	Buf b = { 0 };
	buf_put(&b, ":");
	buf_put(&b, id);
	buf_put(&b, " a owl:Class ; rdfs:label \"");
	buf_put(&b, label);
	buf_put(&b, "\" . ");
	return buf_take(&b);
}

static mc_engine *turtle_engine(const char *const (*classes)[2], int n,
				const char *interface)
{
	Buf b = { 0 };
	buf_put(&b, UPDATE_PREFIXES);
	for (int i = 0; i < n; i++) {
		if (!classes[i][1])
			continue;
		char *c = turtle_class(classes[i][0], classes[i][1]);
		buf_put(&b, c);
		free(c);
	}
	char *ttl = buf_take(&b);
	mc_engine *e = mc_create();
	J *q = OBJ();
	PUT(q, "op", STR("load"));
	PUT(q, "turtle", STR(ttl));
	if (*interface)
		PUT(q, "interface", STR(interface));
	J *r = test_request(e, q);
	int ok = cJSON_IsTrue(GET(r, "ok"));
	DEL(r);
	DEL(q);
	free(ttl);
	if (!ok) {
		mc_destroy(e);
		return NULL;
	}
	return e;
}

/* Applies one replacement, or a removal when label is NULL. */
static int update_class(mc_engine *e, const char *id, const char *label)
{
	char iri[96];
	snprintf(iri, sizeof(iri), "http://example.org/u#%s", id);
	J *q = OBJ();
	PUT(q, "op", STR("update"));
	if (label) {
		Buf b = { 0 };
		buf_put(&b, "<");
		buf_put(&b, iri);
		buf_put(&b, "> a <http://www.w3.org/2002/07/owl#Class> ; "
			    "<http://www.w3.org/2000/01/rdf-schema#label> \"");
		buf_put(&b, label);
		buf_put(&b, "\" .");
		char *ttl = buf_take(&b);
		J *replace = OBJ();
		PUT(replace, iri, STR(ttl));
		PUT(q, "replace", replace);
		free(ttl);
	} else {
		J *remove = ARR();
		ADD(remove, STR(iri));
		PUT(q, "remove", remove);
	}
	J *r = test_request(e, q);
	int ok = cJSON_IsTrue(GET(r, "ok"));
	DEL(r);
	DEL(q);
	return ok;
}

/* Texts for a two-word label: both orders, a gap, unaccented and upper. */
static void label_texts(const char *label, J *out)
{
	const char *space = strchr(label, ' ');
	if (!space)
		return;
	char *first = slice(label, (size_t)(space - label)),
	     *second = copy(space + 1);
	const char *const pairs[][2] = { { first, second }, { second, first } };
	for (int o = 0; o < 2; o++)
		for (int gap = 0; gap < 2; gap++)
			for (int form = 0; form < 3; form++) {
				Buf b = { 0 };
				buf_put(&b, pairs[o][0]);
				buf_put(&b, gap ? " x " : " ");
				buf_put(&b, pairs[o][1]);
				char *text = buf_take(&b), *shown;
				if (form == 1) {
					/* The unaccented form, folded by hand. */
					shown = ufold(text);
				} else if (form == 2)
					shown = upper(text);
				else
					shown = copy(text);
				unique(out, shown);
				free(shown);
				free(text);
			}
	free(first);
	free(second);
}

/*
 * Each scenario starts from one set of labels and applies its updates. The
 * updated engine must match a fresh load of the final labels on every text.
 */
static void updates(void)
{
	static const struct {
		const char *before[2][2], *after[2][2];
	} scenarios[] = {
		{ { { "e_test", "caf\xc3\xa9 cream" }, { "e_other", NULL } },
		  { { "e_test", "coffee cream" }, { "e_other", NULL } } },
		{ { { "e_test", "coffee cream" }, { "e_other", NULL } },
		  { { "e_test", "caf\xc3\xa9 cream" }, { "e_other", NULL } } },
		{ { { "e_a", "caf\xc3\xa9 au" }, { "e_b", "cafe noir" } },
		  { { "e_a", "th\xc3\xa9 au" }, { "e_b", "cafe noir" } } },
		{ { { "e_a", "r\xc3\xa9sum\xc3\xa9 writing" },
		    { "e_b", "resume editing" } },
		  { { "e_a", "r\xc3\xa9sum\xc3\xa9 writing" },
		    { "e_b", "resume formatting" } } },
		{ { { "e_a", "caf\xc3\xa9 au" }, { "e_b", "cafe noir" } },
		  { { "e_a", NULL }, { "e_b", "cafe noir" } } },
	};
	static const char *const interfaces[] = { "", "data" };
	Category *c = category("updates");
	for (size_t s = 0; s < sizeof(scenarios) / sizeof(*scenarios); s++)
		for (int i = 0; i < 2; i++) {
			mc_engine *updated = turtle_engine(scenarios[s].before,
							   2, interfaces[i]),
				  *fresh = turtle_engine(scenarios[s].after, 2,
							 interfaces[i]);
			int ok = updated && fresh;
			for (int k = 0; k < 2 && ok; k++) {
				const char *const *was = scenarios[s].before[k];
				const char *const *now = scenarios[s].after[k];
				if (was[1] != now[1])
					ok = update_class(updated, now[0],
							  now[1]);
			}
			if (!ok) {
				fail_case(c, "load or update failed", NULL,
					  NULL, NULL);
				mc_destroy(updated);
				mc_destroy(fresh);
				continue;
			}
			J *texts = ARR();
			for (int k = 0; k < 2; k++) {
				if (scenarios[s].before[k][1])
					label_texts(scenarios[s].before[k][1],
						    texts);
				if (scenarios[s].after[k][1])
					label_texts(scenarios[s].after[k][1],
						    texts);
			}
			int matched = 0;
			EACH(t, texts) {
				c->cases++;
				J *a = request_tokens(updated, "parse", S(t),
						      NULL, NULL),
				  *b = request_tokens(fresh, "parse", S(t),
						      NULL, NULL);
				J *ea = a ? entities_of(GET(a, "tokens"), 1) :
					    NULL,
				  *eb = b ? entities_of(GET(b, "tokens"), 1) :
					    NULL;
				matched += eb && SIZE(eb) > 0;
				if (!ea || !eb || !cJSON_Compare(ea, eb, 1))
					fail_case(
						c,
						"update differs from a fresh load",
						S(t), eb, ea);
				DEL(ea);
				DEL(eb);
				DEL(a);
				DEL(b);
			}
			/* A scenario that matches nothing would prove nothing. */
			if (!matched)
				fail_case(c, "scenario matched no text", NULL,
					  NULL, NULL);
			DEL(texts);
			mc_destroy(updated);
			mc_destroy(fresh);
		}
}

/* Runs of whitespace and invisible characters between two words. */
static void mixed_runs(void)
{
	static const char *const runs[] = { " \xc2\xad ", "\t\xe2\x80\x8b\t",
					    " \xe2\x81\xa0 \xe2\x80\x8b ",
					    "\xc2\xad \xc2\xad ",
					    " \xe2\x80\x8e\xe2\x80\x8f " };
	static const char *const breaks[] = { " \xc2\xad\n\xc2\xad ",
					      "\xe2\x80\x8b\n\xe2\x80\x8b",
					      "\xc2\xad\r\n" };
	for (int a = 0; a < VARIANTS; a += INVISIBLES)
		for (int b = 0; b < VARIANTS; b += INVISIBLES) {
			char *w1 = variant(&cafe, a), *w2 = variant(&creme, b);
			for (int r = 0; r < 5; r++) {
				Buf t = { 0 };
				buf_put(&t, w1);
				buf_put(&t, runs[r]);
				buf_put(&t, w2);
				char *text = buf_take(&t);
				J *expected = one_entity("ecafecreme", text, 0,
							 ulen(text));
				check("mixed runs", text, expected, MATCH,
				      ALL_PATHS | EXACT_ONLY, NULL);
				DEL(expected);
				free(text);
			}
			for (int r = 0; r < 3; r++)
				for (int mode = 0; mode < 2; mode++) {
					Buf t = { 0 };
					buf_put(&t, w1);
					buf_put(&t, breaks[r]);
					buf_put(&t, w2);
					char *text = buf_take(&t);
					J *expected =
						mode ? ARR() :
						       one_entity("ecafecreme",
								  text, 0,
								  ulen(text));
					check("mixed runs", text, expected,
					      mode ? NO_MATCH : MATCH,
					      ALL_PATHS | EXACT_ONLY,
					      mode ? "boundary" : "ignore");
					DEL(expected);
					free(text);
				}
			free(w1);
			free(w2);
		}
}

static void compare_engines(Category *c, mc_engine *a, mc_engine *b,
			    const char *const *texts, size_t count,
			    const char *what)
{
	int matched = 0;
	for (size_t t = 0; t < count; t++) {
		c->cases++;
		J *ra = request_tokens(a, "parse", texts[t], NULL, NULL),
		  *rb = request_tokens(b, "parse", texts[t], NULL, NULL);
		J *ea = ra ? entities_of(GET(ra, "tokens"), 1) : NULL,
		  *eb = rb ? entities_of(GET(rb, "tokens"), 1) : NULL;
		matched += eb && SIZE(eb) > 0;
		if (!ea || !eb || !cJSON_Compare(ea, eb, 1))
			fail_case(c, what, texts[t], eb, ea);
		DEL(ea);
		DEL(eb);
		DEL(ra);
		DEL(rb);
	}
	if (!matched)
		fail_case(c, "no text matched", NULL, NULL, NULL);
}

/*
 * A label word made only of invisible characters is no span requirement, so
 * the label matches as the same label without it does: after a fresh load,
 * and after updates to and from it, in both interfaces.
 */
static void invisible_words(void)
{
	static const char *const labels[] = {
		"alpha \xc2\xad beta",
		"\xc2\xad alpha beta",
		"alpha beta \xc2\xad",
		"alpha \xe2\x80\x8b beta",
		"\xe2\x81\xa0 alpha \xe2\x81\xa0 beta",
	};
	static const char *const texts[] = {
		"alpha beta",
		"beta alpha",
		"alpha x beta",
		"beta x y alpha",
		"beta \xc2\xad alpha",
		"alpha x y z w beta",
		"alpha",
		"beta",
	};
	static const char *const interfaces[] = { "", "data" };
	const size_t count = sizeof(texts) / sizeof(*texts);
	Category *c = category("invisible words");
	for (int i = 0; i < 2; i++)
		for (size_t l = 0; l < sizeof(labels) / sizeof(*labels); l++) {
			const char *const with[1][2] = { { "e_x", labels[l] } };
			const char *const clean[1][2] = { { "e_x",
							    "alpha beta" } };
			mc_engine *dirty =
					  turtle_engine(with, 1, interfaces[i]),
				  *plain = turtle_engine(clean, 1,
							 interfaces[i]),
				  *changed = turtle_engine(clean, 1,
							   interfaces[i]);
			if (!dirty || !plain || !changed) {
				fail_case(c, "load failed", labels[l], NULL,
					  NULL);
			} else {
				compare_engines(
					c, dirty, plain, texts, count,
					"differs from the label without it");
				if (!update_class(changed, "e_x", labels[l]))
					fail_case(c, "update failed", labels[l],
						  NULL, NULL);
				compare_engines(
					c, changed, dirty, texts, count,
					"update to it differs from a fresh load");
				if (!update_class(changed, "e_x", "alpha beta"))
					fail_case(c, "update failed", labels[l],
						  NULL, NULL);
				compare_engines(
					c, changed, plain, texts, count,
					"update from it differs from a fresh load");
			}
			mc_destroy(dirty);
			mc_destroy(plain);
			mc_destroy(changed);
		}
}

/*
 * Words that fold alike are the same word. A rule left with one word on a
 * class of that name finishes and matches as the plain label does, café cafe
 * matches as cafe cafe does, an authored rule repeating an ASCII word keeps
 * its match, and two rules that would turn a token back and forth finish.
 */
static void folded_repeats(void)
{
	static const struct {
		const char *id, *label, *plain;
	} pairs[] = {
		/* A plus label is also its words as an exact phrase. */
		{ "cafe", "caf\xc3\xa9+caf\xc3\xa9",
		  "caf\xc3\xa9 caf\xc3\xa9" },
		{ "caf\xc3\xa9", "cafe+cafe", "cafe cafe" },
		{ "e_test", "caf\xc3\xa9 cafe", "cafe cafe" },
	};
	static const char *const texts[] = {
		"cafe",
		"caf\xc3\xa9",
		"cafe x caf\xc3\xa9",
		"caf\xc3\xa9 cafe",
		"x cafe y",
		"caf\xc3\xa9 x y z cafe",
	};
	static const char *const interfaces[] = { "", "data" };
	Category *c = category("folded repeats");
	for (int i = 0; i < 2; i++) {
		for (size_t p = 0; p < sizeof(pairs) / sizeof(*pairs); p++) {
			const char *const with[1][2] = { { pairs[p].id,
							   pairs[p].label } };
			const char *const clean[1][2] = { { pairs[p].id,
							    pairs[p].plain } };
			mc_engine *dirty =
					  turtle_engine(with, 1, interfaces[i]),
				  *plain = turtle_engine(clean, 1,
							 interfaces[i]);
			if (!dirty || !plain)
				fail_case(c, "load failed", pairs[p].label,
					  NULL, NULL);
			else
				compare_engines(
					c, dirty, plain, texts,
					sizeof(texts) / sizeof(*texts),
					"differs from the label it folds to");
			mc_destroy(dirty);
			mc_destroy(plain);
		}
		/* Each rule's one word is the other class's name. */
		const char *const cycle[2][2] = { { "y", "x+\xc2\xad" },
						  { "x", "y+\xc2\xad" } };
		mc_engine *e = turtle_engine(cycle, 2, interfaces[i]);
		static const char *const cycle_texts[] = { "x", "y", "x y",
							   "y x y" };
		for (size_t t = 0; e && t < 4; t++) {
			c->cases++;
			J *r = request_tokens(e, "parse", cycle_texts[t], NULL,
					      NULL);
			J *found = r ? entities_of(GET(r, "tokens"), 1) : NULL;
			if (!found || !SIZE(found))
				fail_case(c,
					  "cycle did not finish with a match",
					  cycle_texts[t], NULL, r);
			DEL(found);
			DEL(r);
		}
		if (!e)
			fail_case(c, "load failed", "cycle", NULL, NULL);
		mc_destroy(e);

		/*
		 * Visible words that fold alike close a cycle 0.7.0 never had.
		 * A token's forms never repeat, so tea ends on cafe as in
		 * 0.7.0, and cafe and café end on tea, which the folded café
		 * rule makes before the tea rule would bring cafe back.
		 */
		const char *const visible[2][2] = {
			{ "cafe", "tea+tea" },
			{ "tea", "caf\xc3\xa9+caf\xc3\xa9" }
		};
		e = turtle_engine(visible, 2, interfaces[i]);
		static const struct {
			const char *text, *canon;
			int x, y;
			const char *then;
			int then_x, then_y;
		} tea[] = {
			{ "tea", "cafe", 0, 3, NULL, 0, 0 },
			{ "tea x cafe", "cafe", 0, 3, "cafe", 6, 10 },
			{ "cafe", "tea", 0, 4, NULL, 0, 0 },
			{ "caf\xc3\xa9", "tea", 0, 4, NULL, 0, 0 },
		};
		for (size_t t = 0; e && t < sizeof(tea) / sizeof(*tea); t++) {
			c->cases++;
			J *r = request_tokens(e, "parse", tea[t].text, NULL,
					      NULL);
			J *found = r ? entities_of(GET(r, "tokens"), 1) : NULL;
			J *want = one_entity(tea[t].canon, tea[t].text,
					     (size_t)tea[t].x,
					     (size_t)tea[t].y);
			if (tea[t].then) {
				J *then = one_entity(tea[t].then, tea[t].text,
						     (size_t)tea[t].then_x,
						     (size_t)tea[t].then_y);
				ADD(want, DUP(then->child));
				DEL(then);
			}
			if (!found || !cJSON_Compare(found, want, 1))
				fail_case(c, "folded cycle", tea[t].text, want,
					  found);
			DEL(want);
			DEL(found);
			DEL(r);
		}
		/*
		 * The same cycle at 200 places costs a few changes per token,
		 * not a search over every combination of them.
		 */
		Buf many = { 0 };
		for (int k = 0; k < 200; k++)
			buf_put(&many, k ? " x tea" : "tea");
		char *text = buf_take(&many);
		c->cases++;
		clock_t started = clock();
		J *r = e ? request_tokens(e, "parse", text, NULL, NULL) : NULL;
		double seconds = (double)(clock() - started) / CLOCKS_PER_SEC;
		J *found = r ? entities_of(GET(r, "tokens"), 1) : NULL;
		if (!found || SIZE(found) != 200 || seconds > 5.0)
			fail_case(c, "folded cycle at 200 places", NULL, NULL,
				  NULL);
		DEL(found);
		DEL(r);
		free(text);
		if (!e)
			fail_case(c, "load failed", "tea and cafe", NULL, NULL);
		mc_destroy(e);
	}

	/*
	 * Entity sequences a ga and c ea once shared a sequence hash; an
	 * acyclic chain through them ends as in 0.7.0.
	 */
	mc_engine *acyclic = mc_create();
	J *chain = test_call(
		acyclic,
		"{\"op\":\"load\",\"name\":\"collision\",\"snapshot\":{"
		"\"synonyms\":{\"fwd\":{\"a\":[\"a\"],\"ga\":[\"ga\"]},"
		"\"rev\":{\"a\":[\"a\"],\"ga\":[\"ga\"]},\"lookup\":{\"1\":"
		"[\"a\",\"ga\"]}},\"spans\":{\"a\":[{\"canon\":\"c\","
		"\"content\":[\"a\"],\"distance\":4,\"forward\":true,"
		"\"reverse\":true}],\"c\":[{\"canon\":\"end\",\"content\":"
		"[\"ea\"],\"distance\":4,\"forward\":true,\"reverse\":true}],"
		"\"ga\":[{\"canon\":\"ea\",\"content\":[\"ga\"],\"distance\":4,"
		"\"forward\":true,\"reverse\":true},{\"canon\":\"wrong\","
		"\"content\":[\"c\"],\"distance\":4,\"forward\":true,"
		"\"reverse\":true}]}}}");
	if (!chain)
		fail_case(c, "collision snapshot failed to load", NULL, NULL,
			  NULL);
	c->cases++;
	J *want = one_entity("end", "a ga", 0, 4);
	J *r = request_tokens(acyclic, "parse", "a ga", NULL, NULL);
	J *found = r ? entities_of(GET(r, "tokens"), 1) : NULL;
	if (!found || !cJSON_Compare(found, want, 1))
		fail_case(c, "acyclic chain through a ga", "a ga", want, found);
	DEL(found);
	DEL(r);
	DEL(want);
	DEL(chain);
	mc_destroy(acyclic);

	/*
	 * ASCII chains that come back to a form a span made, once the context
	 * changed, as 0.7.0 runs them: q p to a p, b p, b z, a z, then e, and
	 * the same entered through a two-word span.
	 */
	static const struct {
		const char *q_rule, *lookup, *text;
		int y;
	} entries[] = {
		{ "{\"canon\":\"a\",\"content\":[\"q\"]", "[\"q\"]", "q p", 3 },
		{ "{\"canon\":\"a\",\"content\":[\"r\"]", "[\"q\",\"r\"]",
		  "q r p", 5 },
	};
	for (size_t k = 0; k < 2; k++) {
		Buf b = { 0 };
		buf_put(&b,
			"{\"op\":\"load\",\"name\":\"context\",\"snapshot\":{"
			"\"synonyms\":{\"fwd\":{\"q\":[\"q\"],\"r\":[\"r\"]},"
			"\"rev\":{\"q\":[\"q\"],\"r\":[\"r\"]},\"lookup\":{\"1\":");
		buf_put(&b, entries[k].lookup);
		buf_put(&b, "}},\"spans\":{\"q\":[");
		buf_put(&b, entries[k].q_rule);
		buf_put(&b,
			",\"distance\":4,\"forward\":true,\"reverse\":true}],"
			"\"a\":[{\"canon\":\"b\",\"content\":[\"a\"],\"context\":"
			"[\"p\"],\"distance\":4,\"forward\":true,\"reverse\":"
			"true},{\"canon\":\"e\",\"content\":[\"z\"],\"distance\":"
			"4,\"forward\":true,\"reverse\":true}],\"p\":[{\"canon\":"
			"\"z\",\"content\":[\"p\"],\"context\":[\"b\"],"
			"\"distance\":4,\"forward\":true,\"reverse\":true}],"
			"\"b\":[{\"canon\":\"a\",\"content\":[\"b\"],\"context\":"
			"[\"z\"],\"distance\":4,\"forward\":true,\"reverse\":"
			"true}]}}}");
		char *wire = buf_take(&b);
		mc_engine *ctx = mc_create();
		J *loaded_ctx = test_call(ctx, wire);
		if (!loaded_ctx)
			fail_case(c, "context snapshot failed to load", NULL,
				  NULL, NULL);
		for (int ctr = 0; ctr < 3; ctr += 2) {
			c->cases++;
			J *expected = one_entity("e", entries[k].text, 0,
						 (size_t)entries[k].y);
			J *rq = OBJ();
			PUT(rq, "op", STR("parse"));
			PUT(rq, "text", STR(entries[k].text));
			PUT(rq, "ctr", NUM(ctr));
			J *rs = test_request(ctx, rq);
			J *got = cJSON_IsTrue(GET(rs, "ok")) ?
					 entities_of(GET(GET(rs, "result"),
							 "tokens"),
						     1) :
					 NULL;
			if (!got || !cJSON_Compare(got, expected, 1))
				fail_case(c, "context chain through spans",
					  entries[k].text, expected, got);
			DEL(got);
			DEL(rs);
			DEL(rq);
			DEL(expected);
		}
		DEL(loaded_ctx);
		mc_destroy(ctx);
		free(wire);
	}

	/*
	 * ASCII rules whose canonical form has an accent: folding lets the
	 * second rule match the first one's entity, and the parse finishes.
	 */
	for (int i = 0; i < 2; i++) {
		const char *const accented[2][2] = {
			{ "caf\xc3\xa9", "tea+tea" }, { "tea", "cafe+cafe" }
		};
		mc_engine *ae = turtle_engine(accented, 2, interfaces[i]);
		static const char *const ae_texts[] = { "tea", "cafe",
							"tea x cafe x tea" };
		for (size_t t = 0; ae && t < 3; t++) {
			c->cases++;
			J *rs = request_tokens(ae, "parse", ae_texts[t], NULL,
					       NULL);
			J *got = rs ? entities_of(GET(rs, "tokens"), 1) : NULL;
			if (!got || !SIZE(got))
				fail_case(c, "accented canonical cycle",
					  ae_texts[t], NULL, got);
			DEL(got);
			DEL(rs);
		}
		if (!ae)
			fail_case(c, "load failed", "accented cycle", NULL,
				  NULL);
		mc_destroy(ae);
	}
	/* An authored rule repeating one ASCII word, as 0.7.0 matched it. */
	J *snapshot = cJSON_Parse(
		"{\"synonyms\":{\"fwd\":{\"e_test\":[\"zzz\"]},"
		"\"rev\":{\"zzz\":[\"e_test\"]},\"lookup\":{\"1\":[\"zzz\"]}},"
		"\"spans\":{\"alpha\":[{\"content\":[\"alpha\",\"alpha\"],"
		"\"canon\":\"e_test\",\"distance\":4,\"forward\":true,"
		"\"reverse\":true}]}}");
	mc_engine *e = mc_create();
	J *q = OBJ();
	PUT(q, "op", STR("load"));
	PUT(q, "name", STR("authored"));
	PUT(q, "snapshot", snapshot);
	J *loaded = test_request(e, q);
	if (!cJSON_IsTrue(GET(loaded, "ok")))
		fail_case(c, "authored snapshot failed to load", NULL, NULL,
			  loaded);
	static const char *const authored[] = { "alpha", "x alpha y" };
	for (int t = 0; t < 2; t++) {
		c->cases++;
		J *expected = one_entity("e_test", authored[t],
					 (size_t)(t ? 2 : 0),
					 (size_t)(t ? 7 : 5));
		J *r = request_tokens(e, "parse", authored[t], NULL, NULL);
		J *found = r ? entities_of(GET(r, "tokens"), 1) : NULL;
		if (!found || !cJSON_Compare(found, expected, 1))
			fail_case(c, "authored repeated-word rule", authored[t],
				  expected, found);
		DEL(found);
		DEL(r);
		DEL(expected);
	}
	DEL(loaded);
	DEL(q);
	mc_destroy(e);

	/*
	 * A chain that comes back to an earlier canonical form after its
	 * context changed, as 0.7.0 runs it: a p, b p, b z, a z, then e.
	 */
	e = mc_create();
	loaded = test_call(
		e,
		"{\"op\":\"load\",\"name\":\"chain\",\"snapshot\":{"
		"\"synonyms\":{\"fwd\":{\"a\":[\"a\"]},\"rev\":{\"a\":[\"a\"]},"
		"\"lookup\":{\"1\":[\"a\"]}},\"spans\":{\"a\":[{\"content\":"
		"[\"a\"],\"canon\":\"b\",\"context\":[\"p\"],\"distance\":4,"
		"\"forward\":true,\"reverse\":true},{\"content\":[\"z\"],"
		"\"canon\":\"e\",\"distance\":4,\"forward\":true,\"reverse\":"
		"true}],\"p\":[{\"content\":[\"p\"],\"canon\":\"z\",\"distance"
		"\":4,\"forward\":true,\"reverse\":true}],\"b\":[{\"content\":"
		"[\"b\"],\"canon\":\"a\",\"context\":[\"z\"],\"distance\":4,"
		"\"forward\":true,\"reverse\":true}]}}}");
	if (!loaded)
		fail_case(c, "chain snapshot failed to load", NULL, NULL, NULL);
	for (int ctr = 0; ctr < 3; ctr += 2) {
		c->cases++;
		J *expected = one_entity("e", "a p", 0, 3);
		J *rq = OBJ();
		PUT(rq, "op", STR("parse"));
		PUT(rq, "text", STR("a p"));
		PUT(rq, "ctr", NUM(ctr));
		J *r = test_request(e, rq);
		J *found = cJSON_IsTrue(GET(r, "ok")) ?
				   entities_of(GET(GET(r, "result"), "tokens"),
					       1) :
				   NULL;
		if (!found || !cJSON_Compare(found, expected, 1))
			fail_case(c, "chain that revisits a canonical form",
				  "a p", expected, found);
		DEL(found);
		DEL(r);
		DEL(rq);
		DEL(expected);
	}
	DEL(loaded);
	mc_destroy(e);

	/*
	 * The spans stage on its own: a match that changes nothing at the
	 * first cafe must not hide the second one, as in 0.7.0.
	 */
	for (int i = 0; i < 2; i++) {
		const char *const label[1][2] = { { "caf\xc3\xa9",
						    "cafe+cafe" } };
		e = turtle_engine(label, 1, interfaces[i]);
		c->cases++;
		J *tokens = e ? request_tokens(e, "tokenize", "cafe x cafe",
					       NULL, NULL) :
				NULL;
		J *q2 = OBJ();
		PUT(q2, "op", STR("transform_tokens"));
		PUT(q2, "stage", STR("spans"));
		PUT(q2, "tokens", tokens ? tokens : ARR());
		J *r = e ? test_request(e, q2) : NULL;
		J *found = cJSON_IsTrue(GET(r, "ok")) ?
				   entities_of(GET(r, "result"), 1) :
				   NULL;
		J *expected = ARR();
		J *a = one_entity("caf\xc3\xa9", "cafe x cafe", 0, 4),
		  *b = one_entity("caf\xc3\xa9", "cafe x cafe", 7, 11);
		ADD(expected, DUP(a->child));
		ADD(expected, DUP(b->child));
		if (!found || !cJSON_Compare(found, expected, 1))
			fail_case(c, "spans stage stopped after one cafe",
				  "cafe x cafe", expected, found);
		DEL(a);
		DEL(b);
		DEL(expected);
		DEL(found);
		DEL(r);
		DEL(q2);
		mc_destroy(e);
	}
}

typedef struct {
	const char *canon;
	int x, y;
} Placed;

/* The entities a list of places names, as entities_of lists them. */
static J *places(const Placed *p)
{
	J *out = ARR();
	for (; p->canon; p++) {
		J *o = OBJ();
		PUT(o, "canon", STR(p->canon));
		PUT(o, "x", NUM(p->x));
		PUT(o, "y", NUM(p->y));
		ADD(out, o);
	}
	return out;
}

/* An engine whose only synonym is a and whose span rules are spans. */
static mc_engine *span_engine(const char *spans)
{
	Buf b = { 0 };
	buf_put(&b, "{\"op\":\"load\",\"name\":\"cycle\",\"snapshot\":{"
		    "\"synonyms\":{\"fwd\":{\"a\":[\"a\"]},\"rev\":{\"a\":"
		    "[\"a\"]},\"lookup\":{\"1\":[\"a\"]}},\"spans\":");
	buf_put(&b, spans);
	buf_put(&b, "}}");
	char *wire = buf_take(&b);
	mc_engine *e = mc_create();
	J *loaded = test_call(e, wire);
	free(wire);
	if (!loaded) {
		mc_destroy(e);
		return NULL;
	}
	DEL(loaded);
	return e;
}

/*
 * Single-word span rules that turn one token back and forth: a to b, which
 * may need a context word, and b to a. Folding opens the cycle through the
 * rule's context, a context word in the text or the exact match that starts
 * it, and ASCII rules open it on their own; 0.7.0 never finished any of these
 * texts that reach the cycle. A token stops where its forms would repeat:
 * one that no span covers takes its first span, and a covered token does not
 * go back to a sequence of forms the stage already had. The result is the
 * same for every sweep count and for the spans stage on its own. An authored
 * empty word still never matches, as in 0.7.0. craigtrim/mutatoc#25
 */
static void span_cycles(void)
{
#define RULE "\"distance\":4,\"forward\":true,\"reverse\":true}]"
	static const struct {
		const char *spans, *text;
		Placed parse[4], stage[4];
	} cases[] = {
		/* The context café, met by folding cafe or as written. */
		{ "{\"a\":[{\"canon\":\"b\",\"content\":[\"a\"],\"context\":"
		  "[\"caf\xc3\xa9\"]," RULE ",\"b\":[{\"canon\":\"a\","
		  "\"content\":[\"b\"]," RULE "}",
		  "a cafe",
		  { { "b", 0, 1 } },
		  { { "b", 0, 1 } } },
		{ "{\"a\":[{\"canon\":\"b\",\"content\":[\"a\"],\"context\":"
		  "[\"caf\xc3\xa9\"]," RULE ",\"b\":[{\"canon\":\"a\","
		  "\"content\":[\"b\"]," RULE "}",
		  "a caf\xc3\xa9",
		  { { "b", 0, 1 } },
		  { { "b", 0, 1 } } },
		{ "{\"a\":[{\"canon\":\"b\",\"content\":[\"a\"],\"context\":"
		  "[\"caf\xc3\xa9\"]," RULE ",\"b\":[{\"canon\":\"a\","
		  "\"content\":[\"b\"]," RULE "}",
		  "cafe a",
		  { { "b", 5, 6 } },
		  { { "b", 5, 6 } } },
		{ "{\"a\":[{\"canon\":\"b\",\"content\":[\"a\"],\"context\":"
		  "[\"caf\xc3\xa9\"]," RULE ",\"b\":[{\"canon\":\"a\","
		  "\"content\":[\"b\"]," RULE "}",
		  "a",
		  { { "a", 0, 1 } },
		  { { NULL } } },
		/* The context cafe, met by folding café or as written. */
		{ "{\"a\":[{\"canon\":\"b\",\"content\":[\"a\"],\"context\":"
		  "[\"cafe\"]," RULE ",\"b\":[{\"canon\":\"a\","
		  "\"content\":[\"b\"]," RULE "}",
		  "a caf\xc3\xa9",
		  { { "b", 0, 1 } },
		  { { "b", 0, 1 } } },
		{ "{\"a\":[{\"canon\":\"b\",\"content\":[\"a\"],\"context\":"
		  "[\"cafe\"]," RULE ",\"b\":[{\"canon\":\"a\","
		  "\"content\":[\"b\"]," RULE "}",
		  "a cafe",
		  { { "b", 0, 1 } },
		  { { "b", 0, 1 } } },
		{ "{\"a\":[{\"canon\":\"b\",\"content\":[\"a\"],\"context\":"
		  "[\"cafe\"]," RULE ",\"b\":[{\"canon\":\"a\","
		  "\"content\":[\"b\"]," RULE "}",
		  "a",
		  { { "a", 0, 1 } },
		  { { NULL } } },
		/* A context of one soft hyphen folds away. */
		{ "{\"a\":[{\"canon\":\"b\",\"content\":[\"a\"],\"context\":"
		  "[\"\xc2\xad\"]," RULE ",\"b\":[{\"canon\":\"a\","
		  "\"content\":[\"b\"]," RULE "}",
		  "a",
		  { { "b", 0, 1 } },
		  { { "b", 0, 1 } } },
		{ "{\"a\":[{\"canon\":\"b\",\"content\":[\"a\"],\"context\":"
		  "[\"\xc2\xad\"]," RULE ",\"b\":[{\"canon\":\"a\","
		  "\"content\":[\"b\"]," RULE "}",
		  "a b",
		  { { "b", 0, 1 }, { "a", 2, 3 } },
		  { { "b", 0, 1 }, { "a", 2, 3 } } },
		{ "{\"a\":[{\"canon\":\"b\",\"content\":[\"a\"],\"context\":"
		  "[\"\xc2\xad\"]," RULE ",\"b\":[{\"canon\":\"a\","
		  "\"content\":[\"b\"]," RULE "}",
		  "a cafe",
		  { { "b", 0, 1 } },
		  { { "b", 0, 1 } } },
		/* No context: folding makes the exact match, or ASCII does. */
		{ "{\"a\":[{\"canon\":\"b\",\"content\":[\"a\"]," RULE
		  ",\"b\":[{\"canon\":\"a\",\"content\":[\"b\"]," RULE "}",
		  "\xc3\xa1",
		  { { "b", 0, 1 } },
		  { { "b", 0, 1 } } },
		{ "{\"a\":[{\"canon\":\"b\",\"content\":[\"a\"]," RULE
		  ",\"b\":[{\"canon\":\"a\",\"content\":[\"b\"]," RULE "}",
		  "a\xcc\x81",
		  { { "b", 0, 2 } },
		  { { "b", 0, 2 } } },
		{ "{\"a\":[{\"canon\":\"b\",\"content\":[\"a\"]," RULE
		  ",\"b\":[{\"canon\":\"a\",\"content\":[\"b\"]," RULE "}",
		  "a\xc2\xad",
		  { { "b", 0, 2 } },
		  { { "b", 0, 2 } } },
		{ "{\"a\":[{\"canon\":\"b\",\"content\":[\"a\"]," RULE
		  ",\"b\":[{\"canon\":\"a\",\"content\":[\"b\"]," RULE "}",
		  "\xe2\x80\x8b"
		  "a",
		  { { "b", 0, 2 } },
		  { { "b", 0, 2 } } },
		{ "{\"a\":[{\"canon\":\"b\",\"content\":[\"a\"]," RULE
		  ",\"b\":[{\"canon\":\"a\",\"content\":[\"b\"]," RULE "}",
		  "a",
		  { { "b", 0, 1 } },
		  { { "b", 0, 1 } } },
		{ "{\"a\":[{\"canon\":\"b\",\"content\":[\"a\"]," RULE
		  ",\"b\":[{\"canon\":\"a\",\"content\":[\"b\"]," RULE "}",
		  "a b",
		  { { "b", 0, 1 }, { "a", 2, 3 } },
		  { { "b", 0, 1 }, { "a", 2, 3 } } },
		{ "{\"a\":[{\"canon\":\"b\",\"content\":[\"a\"]," RULE
		  ",\"b\":[{\"canon\":\"a\",\"content\":[\"b\"]," RULE "}",
		  "a x a",
		  { { "a", 0, 1 }, { "b", 4, 5 } },
		  { { "a", 0, 1 }, { "b", 4, 5 } } },
		/*
		 * A span over several tokens starts the record again: d_d_d
		 * may turn back into c, which a sequence from before the span
		 * held, and c then stops before d_d_d comes back.
		 */
		{ "{\"c\":[{\"canon\":\"d_d_d\",\"content\":[\"c\"]," RULE
		  ",\"q\":[{\"canon\":\"m_m\",\"content\":[\"q\",\"r\"]," RULE
		  ",\"d_d_d\":[{\"canon\":\"c\",\"content\":"
		  "[\"d_d_d\"]," RULE "}",
		  "c q r",
		  { { "c", 0, 1 }, { "m_m", 2, 5 } },
		  { { "c", 0, 1 }, { "m_m", 2, 5 } } },
		/* A frozen token stays frozen when a span before it collapses. */
		{ "{\"a\":[{\"canon\":\"b\",\"content\":[\"a\"]," RULE
		  ",\"b\":[{\"canon\":\"a\",\"content\":[\"b\"]," RULE
		  ",\"q\":[{\"canon\":\"m\",\"content\":[\"q\",\"r\"]," RULE
		  "}",
		  "q r a",
		  { { "m", 0, 3 }, { "b", 4, 5 } },
		  { { "m", 0, 3 }, { "b", 4, 5 } } },
		/* Authored empty words never match, as in 0.7.0. */
		{ "{\"a\":[{\"canon\":\"b\",\"content\":[\"a\"],\"context\":"
		  "[\"\"]," RULE "}",
		  "a",
		  { { "a", 0, 1 } },
		  { { NULL } } },
		{ "{\"a\":[{\"canon\":\"b\",\"content\":[\"a\"],\"context\":"
		  "[\"\"]," RULE "}",
		  "a x",
		  { { "a", 0, 1 } },
		  { { NULL } } },
		{ "{\"a\":[{\"canon\":\"b\",\"content\":[\"a\",\"\",\"x\"]," RULE
		  "}",
		  "a x",
		  { { "a", 0, 1 } },
		  { { NULL } } },
		{ "{\"a\":[{\"canon\":\"b\",\"content\":[\"a\",\"\",\"x\"]," RULE
		  "}",
		  "x a",
		  { { "a", 2, 3 } },
		  { { NULL } } },
	};
#undef RULE
	static const int sweeps[] = { 0, 1, 2, -1000 };
	Category *c = category("span cycles");
	for (size_t k = 0; k < sizeof(cases) / sizeof(*cases); k++) {
		mc_engine *e = span_engine(cases[k].spans);
		if (!e) {
			fail_case(c, "cycle snapshot failed to load",
				  cases[k].text, NULL, NULL);
			continue;
		}
		J *parsed = places(cases[k].parse),
		  *staged = places(cases[k].stage);
		for (int s = 0; s < 4; s++) {
			c->cases++;
			J *q = OBJ();
			PUT(q, "op", STR("parse"));
			PUT(q, "text", STR(cases[k].text));
			PUT(q, "ctr", NUM(sweeps[s]));
			J *r = test_request(e, q);
			J *found = cJSON_IsTrue(GET(r, "ok")) ?
					   entities_of(GET(GET(r, "result"),
							   "tokens"),
						       0) :
					   NULL;
			if (!found || !cJSON_Compare(found, parsed, 1))
				fail_case(c, "parse cycle", cases[k].text,
					  parsed, found);
			DEL(found);
			DEL(r);
			DEL(q);
		}
		c->cases++;
		J *tokens = request_tokens(e, "tokenize", cases[k].text, NULL,
					   NULL);
		J *q = OBJ();
		PUT(q, "op", STR("transform_tokens"));
		PUT(q, "stage", STR("spans"));
		PUT(q, "tokens", tokens ? tokens : ARR());
		J *r = test_request(e, q);
		J *found = cJSON_IsTrue(GET(r, "ok")) ?
				   entities_of(GET(r, "result"), 0) :
				   NULL;
		if (!found || !cJSON_Compare(found, staged, 1))
			fail_case(c, "spans stage cycle", cases[k].text, staged,
				  found);
		DEL(found);
		DEL(r);
		DEL(q);
		DEL(parsed);
		DEL(staged);
		mc_destroy(e);
	}
}

/*
 * A binary counter of n bits in span rules: six single-word rules per bit,
 * each needing a context word, step a pointer token c through the bits while
 * the bit tokens flip between z and o. start becomes c0 when the text holds
 * the entry word, and the pointer then counts to 2^n: at the top it stops at
 * done, or wraps to c0, so the sequence repeats only after 6(2^n - 1) spans.
 */
static char *counter_load(int bits, const char *entry, int wraps)
{
	Buf b = { 0 };
	char rule[256];
	buf_put(&b, "{\"op\":\"load\",\"name\":\"counter\",\"snapshot\":{"
		    "\"synonyms\":{\"fwd\":{\"seed\":[\"seed\"]},\"rev\":"
		    "{\"seed\":[\"seed\"]},\"lookup\":{\"1\":[\"seed\"]}},"
		    "\"spans\":{\"start\":[{\"canon\":\"c0\",\"content\":"
		    "[\"start\"],\"context\":[\"");
	buf_put(&b, entry);
	buf_put(&b, "\"],\"distance\":4,\"forward\":true,\"reverse\":true}]");
	for (int i = 0; i < bits; i++) {
		char next[16];
		if (i + 1 < bits || wraps)
			snprintf(next, sizeof(next), "c%d", (i + 1) % bits);
		else
			snprintf(next, sizeof(next), "done");
		const char *const shape =
			",\"%s%d\":[{\"canon\":\"%s\",\"content\":[\"%s%d\"],"
			"\"context\":[\"%s%d\"],\"distance\":4,\"forward\":true,"
			"\"reverse\":true}";
		char canon[16];
		/* c to s when the bit is zero, c to r when it is one. */
		snprintf(canon, sizeof(canon), "s%d", i);
		snprintf(rule, sizeof(rule), shape, "c", i, canon, "c", i, "z",
			 i);
		buf_put(&b, rule);
		snprintf(canon, sizeof(canon), "r%d", i);
		snprintf(rule, sizeof(rule),
			 ",{\"canon\":\"%s\",\"content\":[\"c%d\"],\"context\":"
			 "[\"o%d\"],\"distance\":4,\"forward\":true,"
			 "\"reverse\":true}]",
			 canon, i, i);
		buf_put(&b, rule);
		/* The bit flips under s and r. */
		snprintf(canon, sizeof(canon), "o%d", i);
		snprintf(rule, sizeof(rule), shape, "z", i, canon, "z", i, "s",
			 i);
		buf_put(&b, rule);
		buf_put(&b, "]");
		snprintf(canon, sizeof(canon), "z%d", i);
		snprintf(rule, sizeof(rule), shape, "o", i, canon, "o", i, "r",
			 i);
		buf_put(&b, rule);
		buf_put(&b, "]");
		/* s returns to the lowest bit; r carries to the next one. */
		snprintf(rule, sizeof(rule), shape, "s", i, "c0", "s", i, "o",
			 i);
		buf_put(&b, rule);
		buf_put(&b, "]");
		snprintf(rule, sizeof(rule), shape, "r", i, next, "r", i, "z",
			 i);
		buf_put(&b, rule);
		buf_put(&b, "]");
	}
	buf_put(&b, "}}}");
	return buf_take(&b);
}

/*
 * A chain of single-word rules w0 to w1 to w300, where w256 and tail in
 * either order become m0, and m0 becomes m1.
 */
static char *chain_load(void)
{
	Buf b = { 0 };
	char rule[200];
	buf_put(&b, "{\"op\":\"load\",\"name\":\"chain\",\"snapshot\":{"
		    "\"synonyms\":{\"fwd\":{\"seed\":[\"seed\"]},\"rev\":"
		    "{\"seed\":[\"seed\"]},\"lookup\":{\"1\":[\"seed\"]}},"
		    "\"spans\":{\"m0\":[{\"canon\":\"m1\",\"content\":[\"m0\"],"
		    "\"distance\":4,\"forward\":true,\"reverse\":true}]");
	for (int k = 0; k < 300; k++) {
		snprintf(rule, sizeof(rule),
			 ",\"w%d\":[{\"canon\":\"w%d\",\"content\":[\"w%d\"],"
			 "\"distance\":4,\"forward\":true,\"reverse\":true}",
			 k, k + 1, k);
		buf_put(&b, rule);
		if (k == 256)
			buf_put(&b, ",{\"canon\":\"m0\",\"content\":[\"w256\","
				    "\"tail\"],\"distance\":4,\"forward\":true,"
				    "\"reverse\":true}");
		buf_put(&b, "]");
	}
	buf_put(&b, "}}}");
	return buf_take(&b);
}

/* The most spans over one token alone that any token in tokens carries. */
static int deepest_history(const J *tokens, const char **canon)
{
	int deepest = 0;
	EACH(t, tokens) {
		int depth = 0;
		const J *u = t;
		while (GET(u, "swaps")) {
			const J *inner = GET(GET(u, "swaps"), "tokens");
			depth++;
			if (SIZE(inner) != 1)
				break;
			u = inner->child;
		}
		if (depth > deepest) {
			deepest = depth;
			*canon = S(GET(GET(t, "swaps"), "canon"));
		}
	}
	return deepest;
}

/*
 * Rules that keep a token changing without bringing a sequence back: the
 * counter. 0.7.0 finishes a counter that stops, with 2^(n+2) - 3 spans over
 * the pointer token, and never finishes one that wraps. A token takes at most
 * 256 spans over it alone in one parse, so every counter returns at once and
 * its result goes back through parse_tokens. A counter that stops within 256
 * spans ends on done as in 0.7.0, and one that wraps within them stops one
 * span short of its first sequence, at 2^(n+2) - 4. craigtrim/mutatoc#25
 */
static void span_bounds(void)
{
	static const struct {
		int bits, wraps, ctr;
		const char *entry;
		int deepest;
		const char *canon;
	} cases[] = {
		{ 5, 0, 0, "cafe", 125, "done" },
		{ 6, 0, 0, "cafe", 253, "done" },
		{ 7, 0, 0, "cafe", 256, NULL },
		{ 6, 1, 0, "caf\xc3\xa9", 252, NULL },
		{ 8, 1, 0, "caf\xc3\xa9", 256, NULL },
		{ 12, 1, 0, "caf\xc3\xa9", 256, NULL },
		{ 12, 1, 2, "caf\xc3\xa9", 256, NULL },
		{ 12, 1, -1000, "caf\xc3\xa9", 256, NULL },
		{ 24, 1, 0, "caf\xc3\xa9", 256, NULL },
		{ 64, 1, 0, "caf\xc3\xa9", 256, NULL },
		{ 12, 1, 0, "cafe", 256, NULL },
	};
	Category *c = category("span bounds");
	for (size_t k = 0; k < sizeof(cases) / sizeof(*cases); k++) {
		char *load = counter_load(cases[k].bits, cases[k].entry,
					  cases[k].wraps);
		mc_engine *e = mc_create();
		J *loaded = test_call(e, load);
		free(load);
		c->cases++;
		Buf b = { 0 };
		buf_put(&b, "start cafe");
		for (int i = 0; i < cases[k].bits; i++) {
			char word[16];
			snprintf(word, sizeof(word), " z%d", i);
			buf_put(&b, word);
		}
		char *text = buf_take(&b);
		J *q = OBJ();
		PUT(q, "op", STR("parse"));
		PUT(q, "text", STR(text));
		PUT(q, "ctr", NUM(cases[k].ctr));
		clock_t started = clock();
		J *r = loaded ? test_request(e, q) : NULL;
		double seconds = (double)(clock() - started) / CLOCKS_PER_SEC;
		J *tokens = cJSON_IsTrue(GET(r, "ok")) ?
				    GET(GET(r, "result"), "tokens") :
				    NULL;
		const char *canon = "";
		int deepest = tokens ? deepest_history(tokens, &canon) : -1;
		int ok = tokens && seconds < 2.0 &&
			 deepest == cases[k].deepest &&
			 (!cases[k].canon || !strcmp(canon, cases[k].canon));
		if (ok) {
			/* The result goes back in as tokens. */
			J *again = OBJ();
			PUT(again, "op", STR("parse_tokens"));
			PUT(again, "tokens", DUP(tokens));
			J *back = test_request(e, again);
			ok = cJSON_IsTrue(GET(back, "ok"));
			DEL(back);
			DEL(again);
		}
		if (!ok) {
			char what[160];
			snprintf(what, sizeof(what),
				 "counter of %d bits: deepest %d, %.2f s, %s",
				 cases[k].bits, deepest, seconds, canon);
			fail_case(c, what, cases[k].entry, NULL, NULL);
		}
		DEL(r);
		DEL(q);
		DEL(loaded);
		free(text);
		mc_destroy(e);
	}
	/*
	 * A chain stops at w256, where 0.7.0 went on to w300. A span over
	 * several tokens keeps the deepest history it covers, so m0, one span
	 * over tail and w256, stays m0.
	 */
	static const struct {
		const char *text, *canon;
		int deepest;
	} chained[] = { { "w0", "w256", 256 }, { "tail w0", "m0", 1 } };
	char *chain = chain_load();
	mc_engine *ce = mc_create();
	J *chain_loaded = test_call(ce, chain);
	free(chain);
	for (int k = 0; k < 2; k++) {
		c->cases++;
		J *r = chain_loaded ?
			       request_tokens(ce, "parse", chained[k].text,
					      NULL, NULL) :
			       NULL;
		const char *canon = "";
		int deepest = r ? deepest_history(GET(r, "tokens"), &canon) :
				  -1;
		if (deepest != chained[k].deepest ||
		    strcmp(canon, chained[k].canon))
			fail_case(c, "chain past the history limit",
				  chained[k].text, NULL, r);
		DEL(r);
	}
	DEL(chain_loaded);
	mc_destroy(ce);

	/* The spans stage on its own is bounded the same way. */
	char *load = counter_load(12, "caf\xc3\xa9", 1);
	mc_engine *e = mc_create();
	J *loaded = test_call(e, load);
	free(load);
	c->cases++;
	const char *text = "start cafe z0 z1 z2 z3 z4 z5 z6 z7 z8 z9 z10 z11";
	J *tokens = loaded ? request_tokens(e, "tokenize", text, NULL, NULL) :
			     NULL;
	J *q = OBJ();
	PUT(q, "op", STR("transform_tokens"));
	PUT(q, "stage", STR("spans"));
	PUT(q, "tokens", tokens ? tokens : ARR());
	clock_t started = clock();
	J *r = test_request(e, q);
	double seconds = (double)(clock() - started) / CLOCKS_PER_SEC;
	const char *canon = "";
	int deepest = cJSON_IsTrue(GET(r, "ok")) ?
			      deepest_history(GET(r, "result"), &canon) :
			      -1;
	if (deepest != 256 || seconds >= 2.0)
		fail_case(c, "spans stage counter of 12 bits", text, NULL,
			  NULL);
	DEL(r);
	DEL(q);
	DEL(loaded);
	mc_destroy(e);
}

/*
 * A span rule that folding leaves with one word, on a class of that name,
 * must not match its own result again; it matches as the plain label does.
 */
static void self_spans(void)
{
	static const char *const labels[] = {
		"alpha+\xc2\xad",
		"\xc2\xad+alpha",
		"alpha+\xe2\x80\x8b+\xc2\xad",
	};
	static const char *const texts[] = { "alpha", "alpha alpha",
					     "x alpha y", "alpha \xc2\xad",
					     "\xc2\xad alpha" };
	static const char *const interfaces[] = { "", "data" };
	Category *c = category("self spans");
	for (int i = 0; i < 2; i++)
		for (size_t l = 0; l < sizeof(labels) / sizeof(*labels); l++) {
			const char *const with[1][2] = { { "alpha",
							   labels[l] } };
			const char *const clean[1][2] = { { "alpha",
							    "alpha" } };
			mc_engine *dirty =
					  turtle_engine(with, 1, interfaces[i]),
				  *plain = turtle_engine(clean, 1,
							 interfaces[i]);
			if (!dirty || !plain)
				fail_case(c, "load failed", labels[l], NULL,
					  NULL);
			else
				compare_engines(c, dirty, plain, texts,
						sizeof(texts) / sizeof(*texts),
						"differs from the plain label");
			mc_destroy(dirty);
			mc_destroy(plain);
		}
}

/* canon, x and y of the entities in a parse or parse_tokens result. */
static J *placed(const J *result)
{
	return entities_of(
		cJSON_IsArray(result) ? result : GET(result, "tokens"), 0);
}

/*
 * Tokens that mutatoc 0.7.0 produced, which split invisible characters into
 * tokens of their own, match as new tokens of the same text do, and every
 * token that matched nothing comes back with the normal it was sent with.
 */
static void legacy_tokens(const char *root)
{
	Category *c = category("legacy tokens");
	J *doc = test_read_json(root, LEGACY);
	if (!doc || strcmp(S(GET(doc, "version")), "0.7.0")) {
		fail_case(c, "0.7.0 token fixture missing", LEGACY, NULL, NULL);
		DEL(doc);
		return;
	}
	int matched = 0;
	EACH(k, GET(doc, "cases")) {
		const char *text = S(GET(k, "text"));
		J *legacy = GET(k, "tokens");
		for (int i = 0; i < ENGINES; i++) {
			c->cases++;
			J *now = request_tokens(engines[i], "parse", text, NULL,
						NULL),
			  *old = request_tokens(engines[i], "parse_tokens",
						NULL, DUP(legacy), NULL);
			J *want = now ? placed(now) : NULL,
			  *got = old ? placed(old) : NULL;
			matched += want && SIZE(want) > 0;
			if (!want || !got || !cJSON_Compare(want, got, 1)) {
				char what[64];
				snprintf(what, sizeof(what),
					 "0.7.0 tokens, parse_tokens, %s",
					 engine_names[i]);
				fail_case(c, what, text, want, got);
			}
			if (old && i == FRESH)
				EACH(t, GET(old, "tokens")) {
					if (GET(t, "swaps"))
						continue;
					const char *id = S(GET(t, "id"));
					EACH(s, legacy)
						if (!strcmp(S(GET(s, "id")),
							    id) &&
						    strcmp(S(GET(s, "normal")),
							   S(GET(t, "normal"))))
							fail_case(
								c,
								"normal not returned as sent",
								text, s, t);
				}
			DEL(want);
			DEL(got);
			DEL(now);
			DEL(old);
		}
		c->cases++;
		J *fresh = request_tokens(engines[FRESH], "tokenize", text,
					  NULL, NULL);
		J *a = request_tokens(engines[FRESH], "transform_tokens", NULL,
				      fresh, NULL),
		  *b = request_tokens(engines[FRESH], "transform_tokens", NULL,
				      DUP(legacy), NULL);
		J *want = a ? placed(a) : NULL, *got = b ? placed(b) : NULL;
		if (!want || !got || !cJSON_Compare(want, got, 1))
			fail_case(c, "0.7.0 tokens, exact stage", text, want,
				  got);
		DEL(want);
		DEL(got);
		DEL(a);
		DEL(b);
	}
	if (!matched)
		fail_case(c, "no legacy text matched", NULL, NULL, NULL);
	DEL(doc);
}

/* The punctuation rules look past invisible characters to their edges. */
static void punctuation_edges(void)
{
	static const struct {
		const char *text, *canon;
		int x, y;
	} table[] = {
		{ "2020.", "eyear", 0, 4 },
		{ "2020.\xc2\xad", "eyear", 0, 4 },
		{ "2020\xcc\x81.", "eyear", 0, 5 },
		{ "2020\xcc\x81,", "eyear", 0, 5 },
		{ "2020\xe2\x80\x8b.", "eyear", 0, 5 },
		{ "_name_", "ename", 1, 5 },
		{ "_name_\xc2\xad", "ename", 1, 5 },
		{ "\xc2\xad_name_", "ename", 2, 6 },
		/* A mark belongs to the underscore before it. */
		{ "_\xcc\x81name_", "ename", 2, 6 },
		{ "_name\xe2\x80\x8b_", "ename", 1, 6 },
		{ "__name__", "ename", 2, 6 },
		{ "_\xc2\xad_name__", "ename", 3, 7 },
		{ "__name_\xc2\xad_", "ename", 2, 6 },
		{ "_\xc2\xad_name_\xc2\xad_", "ename", 3, 7 },
		{ "2020,_", "eyear", 0, 4 },
		{ "2020,\xc2\xad_", "eyear", 0, 4 },
		{ "2020.\xc2\xad_\xc2\xad_", "eyear", 0, 4 },
		/* Hangul fillers are letters and default ignorables at once. */
		{ "2020.\xe3\x85\xa4", "eyear", 0, 4 },
		{ "2020.\xe1\x85\x9f", "eyear", 0, 4 },
		{ "2020.\xe1\x85\xa0", "eyear", 0, 4 },
		{ "2020.\xef\xbe\xa0", "eyear", 0, 4 },
		{ "_\xe3\x85\xa4_name__", "ename", 3, 7 },
		{ "A&\xe3\x85\xa4"
		  "B",
		  "eab", 0, 4 },
		{ "A&B", "eab", 0, 3 },
		{ "a&b", "eab", 0, 3 },
		{ "A&\xc2\xad"
		  "B",
		  "eab", 0, 4 },
		{ "A\xc2\xad&B", "eab", 0, 4 },
		{ "A\xcc\x81&B", "eab", 0, 4 },
		{ "A&\xe2\x80\x8d"
		  "B",
		  "eab", 0, 4 },
	};
	for (size_t i = 0; i < sizeof(table) / sizeof(*table); i++)
		for (int k = 0; k < CONTEXTS; k++) {
			Buf t = { 0 };
			buf_put(&t, contexts[k][0]);
			size_t x = ulen(t.p ? t.p : "");
			buf_put(&t, table[i].text);
			buf_put(&t, contexts[k][1]);
			char *text = buf_take(&t);
			J *expected = one_entity(table[i].canon, text,
						 x + (size_t)table[i].x,
						 x + (size_t)table[i].y);
			check("punctuation edges", text, expected, MATCH,
			      ALL_PATHS | EXACT_ONLY, NULL);
			DEL(expected);
			free(text);
		}
}

/*
 * Every run of up to three whitespace and invisible characters, alone and
 * between two letters, keeps the token contract: tokens cover the input, y
 * leaves out only trailing whitespace, and a token of whitespace and
 * invisible characters has an empty normal.
 */
static void offsets(void)
{
	static const char *const alphabet[] = {
		" ", "\t", "\n", "\xc2\xad", "\xe2\x80\x8b", "\xe2\x80\x8e"
	};
	Category *c = category("offsets");
	int idx[3];
	for (int len = 1; len <= 3; len++) {
		int total = 1;
		for (int i = 0; i < len; i++)
			total *= 6;
		for (int n = 0; n < total; n++) {
			int v = n;
			for (int i = 0; i < len; i++, v /= 6)
				idx[i] = v % 6;
			for (int wrap = 0; wrap < 2; wrap++) {
				c->cases++;
				Buf b = { 0 };
				if (wrap)
					buf_put(&b, "a");
				for (int i = 0; i < len; i++)
					buf_put(&b, alphabet[idx[i]]);
				if (wrap)
					buf_put(&b, "b");
				char *text = buf_take(&b);
				J *tokens = request_tokens(engines[FRESH],
							   "tokenize", text,
							   NULL, NULL);
				Buf joined = { 0 };
				size_t pos = 0;
				int ok = tokens != NULL;
				EACH(t, tokens) {
					const char *s = S(GET(t, "text")),
						   *normal =
							   S(GET(t, "normal"));
					size_t count = ulen(s), trailing = 0;
					int invisible = 1;
					for (const char *p = s; *p;) {
						uint32_t ch = uread(&p);
						trailing =
							uspace(ch) ?
								trailing + 1 :
								0;
						invisible = invisible &&
							    (uspace(ch) ||
							     ujoiner(ch));
					}
					ok = ok &&
					     GET(t, "x")->valuedouble ==
						     (double)pos &&
					     GET(t, "y")->valuedouble ==
						     (double)(pos + count -
							      trailing) &&
					     (!invisible || !*normal);
					buf_put(&joined, s);
					pos += count;
				}
				char *all = buf_take(&joined);
				ok = ok && !strcmp(all, text);
				if (!ok)
					fail_case(c, "token contract", text,
						  NULL, tokens);
				free(all);
				DEL(tokens);
				free(text);
			}
		}
	}
}

/* The right answer, which the engine does not give yet. */
static void expected_fails(void)
{
	static const struct {
		const char *text, *canon, *why;
	} table[] = {
		/* Fullwidth letters are not folded (craigtrim/mutatoc#25). */
		{ "\xef\xbd\x83\xef\xbd\x81\xef\xbd\x86\xc3\xa9 "
		  "\xef\xbd\x83\xef\xbd\x92\xc3\xa8\xef\xbd\x8d\xef\xbd\x85",
		  "ecafecreme", "fullwidth letters" },
		{ "\xef\xbd\x83\xef\xbd\x8f\xef\xbd\x86\xef\xbd\x86\xef\xbd\x85"
		  "\xef\xbd\x85",
		  "ecoffee", "fullwidth letters" },
		/* Punctuation between words (craigtrim/mutatoc#16). */
		{ "caf\xc3\xa9-cr\xc3\xa8me", "ecafecreme", "#16" },
		{ "caf\xc3\xa9/cr\xc3\xa8me", "ecafecreme", "#16" },
		{ "caf\xc3\xa9, cr\xc3\xa8me", "ecafecreme", "#16" },
		{ "cafe - creme", "ecafecreme", "#16" },
	};
	for (size_t i = 0; i < sizeof(table) / sizeof(*table); i++)
		for (int k = 0; k < CONTEXTS; k++) {
			Buf t = { 0 };
			buf_put(&t, contexts[k][0]);
			size_t x = ulen(t.p ? t.p : "");
			buf_put(&t, table[i].text);
			size_t y = ulen(t.p);
			buf_put(&t, contexts[k][1]);
			char *text = buf_take(&t);
			J *expected = one_entity(table[i].canon, text, x, y);
			check("expected fails", text, expected, XFAIL, 0, NULL);
			DEL(expected);
			free(text);
		}
}

int main(int argc, char **argv)
{
	const char *root = argc > 1 ? argv[1] : ".";
	conformance(root);
	if (!load_oracle(root))
		failures++;
	else {
		code_points();
		ascii();
	}
	if (!load_engines(root)) {
		fprintf(stderr, "FAIL loading the accent fixtures\n");
		return 1;
	}
	words();
	joined();
	line_breaks();
	spans();
	invisible_fillers();
	letters();
	normals();
	collisions();
	updates();
	mixed_runs();
	punctuation_edges();
	invisible_words();
	self_spans();
	folded_repeats();
	span_cycles();
	span_bounds();
	legacy_tokens(root);
	offsets();
	expected_fails();
	int total = 0, short_of = 0;
	for (int i = 0; i < CATEGORY_COUNT; i++) {
		Category *c = &categories[i];
		total += c->cases;
		printf("%-18s %8d cases, %d failed", c->name, c->cases,
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
	printf("Normalization: %d cases, %d failed, %d expected fails now "
	       "passing.\n",
	       total, failures, xpasses);
	return failures || short_of ? 1 : 0;
}
