/*
 * test_token_fidelity.c - Tokens and entities that keep the text the
 * consumer sent.
 *
 * Every case checks the token contract: the tokens concatenate back to the
 * input, each token's text sits at its x, y leaves out trailing whitespace,
 * no token is empty, a whitespace token is zero-width with an empty normal,
 * normal is the text lowercased with every hyphen, dash and quote folded,
 * and an entity's text is the input from its x to its y. Each family of
 * generated inputs also asserts its token split or its matches against
 * tests/fixtures/ontologies/apostrophe-synonyms-test.ttl. Expected results
 * come from the rules in the issue, never from an earlier engine run, and
 * the suite fails when a family falls below its minimum case count.
 * craigtrim/mutatoc#7
 */

#include "testlib.h"

#define FIXTURE "tests/fixtures/ontologies/apostrophe-synonyms-test.ttl"

typedef struct {
	const char *name;
	int minimum, cases;
} Family;
static Family families[] = {
	{ "glyph placement", 420 }, { "whitespace", 180 },
	{ "entities", 480 },	    { "letter case", 160 },
	{ "tables", 1068 },	    { "ascii sweep", 475 },
	{ "unicode", 80 },	    { "drift", 30 },
	{ "degenerate", 40 },	    { "invalid utf-8", 10 },
	{ "fold table", 160 },	    { "stored glyphs", 141 },
};
#define FAMILY_COUNT ((int)(sizeof(families) / sizeof(*families)))
static int assertions, failures;
static Family *current;

static void begin(const char *family)
{
	for (int i = 0; i < FAMILY_COUNT; i++)
		if (!strcmp(families[i].name, family)) {
			current = &families[i];
			current->cases++;
			return;
		}
	fprintf(stderr, "unknown family %s\n", family);
	exit(2);
}

/* Records one assertion; prints the first failures with their input. */
static int verify(int ok, const char *input, const char *what,
		  const J *expected, const J *actual)
{
	assertions++;
	if (ok)
		return 1;
	if (++failures <= 40) {
		char *a = expected ? cJSON_PrintUnformatted(expected) : NULL,
		     *b = actual ? cJSON_PrintUnformatted(actual) : NULL;
		J *shown = STR(input);
		char *in = cJSON_PrintUnformatted(shown);
		fprintf(stderr, "%s | %.200s | %s\n", current->name, in, what);
		if (a || b)
			fprintf(stderr,
				"  expected: %.600s\n  actual:   %.600s\n",
				a ? a : "null", b ? b : "null");
		free(a);
		free(b);
		free(in);
		DEL(shown);
	}
	return 0;
}

/* ---------------------------------------------------------------------- */
/* The fold table, written out from the issue rather than read from src.  */

static const uint32_t dashes[] = { 0x2010, 0x2011, 0x2012, 0x2013, 0x2014,
				   0x2015, 0x2e3a, 0x2e3b, 0xfe58, 0xfe63,
				   0xff0d, 0x058a, 0x1806, 0x2212, 0x207b,
				   0x208b, 0x301c, 0x3030, 0x2053 };
static const uint32_t squotes[] = { 0x2019, 0x2018, 0x201b, 0x201a,
				    0x02bc, 0x2039, 0x203a, 0xff07,
				    0x0060, 0x00b4, 0x2032 };
static const uint32_t dquotes[] = { 0x201c, 0x201d, 0x201e, 0x201f,
				    0x00ab, 0x00bb, 0xff02, 0x2033 };
#define COUNT(a) (sizeof(a) / sizeof(*(a)))

static int has(const uint32_t *set, size_t n, uint32_t c)
{
	for (size_t i = 0; i < n; i++)
		if (set[i] == c)
			return 1;
	return 0;
}

static char *expected_normal(const char *text)
{
	Buf b = { 0 };
	const char *p = text;
	while (*p) {
		const char *at = p;
		uint32_t c = uread(&p);
		const char *peek = p;
		uint32_t next = *p ? uread(&peek) : 0;
		if ((c == 0x60 || c == 0xb4) && next == c) {
			buf_put(&b, "\"");
			p = peek;
		} else if (has(dashes, COUNT(dashes), c))
			buf_put(&b, "-");
		else if (has(squotes, COUNT(squotes), c))
			buf_put(&b, "'");
		else if (has(dquotes, COUNT(dquotes), c))
			buf_put(&b, "\"");
		else
			buf_add(&b, at, (size_t)(p - at));
	}
	char *out = norm(b.p ? b.p : "", 1, 0);
	free(b.p);
	return out;
}

static char *utf8(uint32_t c)
{
	Buf b = { 0 };
	uwrite(&b, c);
	return buf_take(&b);
}

/* ---------------------------------------------------------------------- */
/* The token contract, checked on every case.                             */

/* Code points of trailing whitespace, and whether the text is all of it. */
static size_t trailing_space(const char *s, int *all)
{
	size_t n = 0, total = 0;
	const char *p = s;
	while (*p) {
		n = uspace(uread(&p)) ? n + 1 : 0;
		total++;
	}
	*all = n == total;
	return n;
}

static void leaves(const J *tokens, J *out)
{
	EACH(t, tokens) {
		J *swaps = GET(t, "swaps");
		if (swaps)
			leaves(GET(swaps, "tokens"), out);
		else
			ADD(out, DUP(t));
	}
}

/* Byte offsets of every code point in text, plus one past the end. */
static size_t *offsets(const char *text, size_t *n)
{
	*n = ulen(text);
	size_t *at = malloc((*n + 1) * sizeof(*at));
	const char *p = text;
	for (size_t i = 0; i <= *n; i++) {
		at[i] = (size_t)(p - text);
		if (*p)
			uread(&p);
	}
	return at;
}

static int contract_tokens(const char *text, const J *tokens)
{
	size_t n, *at = offsets(text, &n), pos = 0, len = strlen(text);
	int ok = cJSON_IsArray(tokens);
	EACH(t, tokens) {
		const char *s = S(GET(t, "text")),
			   *normal = S(GET(t, "normal"));
		size_t size = strlen(s), count = ulen(s);
		int all = 0;
		size_t space = trailing_space(s, &all);
		double x = GET(t, "x")->valuedouble,
		       y = GET(t, "y")->valuedouble;
		char *expected = expected_normal(s);
		int good = *s && x == (double)pos && pos + count <= n &&
			   at[pos] + size <= len &&
			   !memcmp(text + at[pos], s, size) &&
			   y == (double)(pos + count - space) &&
			   (!all || !*normal) && !strcmp(normal, expected);
		ok = verify(good, text, "token contract", t, NULL) && ok;
		free(expected);
		pos += count;
		if (!good)
			break;
	}
	ok = verify(!ok || pos == n, text, "tokens cover the input", NULL,
		    tokens) &&
	     ok;
	free(at);
	return ok;
}

static int contract_entities(const char *text, const J *tokens,
			     const size_t *at, size_t n)
{
	int ok = 1;
	EACH(t, tokens) {
		J *swaps = GET(t, "swaps");
		if (!swaps)
			continue;
		J *inner = ARR();
		leaves(GET(swaps, "tokens"), inner);
		double x = GET(t, "x")->valuedouble,
		       y = GET(t, "y")->valuedouble;
		int good =
			SIZE(inner) > 0 && x >= 0 && x <= y && y <= (double)n &&
			x == GET(AT(inner, 0), "x")->valuedouble &&
			y == GET(AT(inner, SIZE(inner) - 1), "y")->valuedouble;
		if (good) {
			size_t a = at[(size_t)x], b = at[(size_t)y];
			const char *s = S(GET(t, "text"));
			good = strlen(s) == b - a &&
			       !memcmp(text + a, s, b - a);
		}
		ok = verify(good, text, "entity text is the input from x to y",
			    NULL, t) &&
		     ok;
		DEL(inner);
		ok = contract_entities(text, GET(swaps, "tokens"), at, n) && ok;
	}
	return ok;
}

static int contract_parse(const char *text, const J *result)
{
	J *flat = ARR();
	leaves(GET(result, "tokens"), flat);
	int ok = contract_tokens(text, flat);
	DEL(flat);
	size_t n, *at = offsets(text, &n);
	ok = contract_entities(text, GET(result, "tokens"), at, n) && ok;
	free(at);
	return ok;
}

/* ---------------------------------------------------------------------- */
/* Requests.                                                              */

static J *run(mc_engine *e, const char *op, const char *text)
{
	J *q = OBJ();
	PUT(q, "op", STR(op));
	PUT(q, "text", STR(text));
	J *r = test_request(e, q);
	DEL(q);
	J *result =
		cJSON_IsTrue(GET(r, "ok")) ?
			cJSON_DetachItemFromObjectCaseSensitive(r, "result") :
			NULL;
	verify(result != NULL, text, op, NULL, r);
	DEL(r);
	return result;
}

/* Tokenizes text under the contract and returns the tokens. */
static J *tokens_of(mc_engine *e, const char *text)
{
	J *tokens = run(e, "tokenize", text);
	if (tokens)
		contract_tokens(text, tokens);
	return tokens;
}

/* Parses text under the contract and returns its entities in order. */
static J *entities_of(mc_engine *e, const char *text)
{
	J *result = run(e, "parse", text), *out = ARR();
	if (!result)
		return out;
	contract_parse(text, result);
	EACH(t, GET(result, "tokens")) {
		J *swaps = GET(t, "swaps");
		if (!swaps)
			continue;
		J *x = OBJ();
		PUT(x, "canon", DUP(GET(swaps, "canon")));
		PUT(x, "x", DUP(GET(t, "x")));
		PUT(x, "y", DUP(GET(t, "y")));
		PUT(x, "text", DUP(GET(t, "text")));
		ADD(out, x);
	}
	DEL(result);
	return out;
}

/* Trimmed texts of the tokens that are not whitespace. */
static J *words(const J *tokens)
{
	J *out = ARR();
	EACH(t, tokens) {
		char *s = norm(S(GET(t, "text")), 0, 0);
		if (*s)
			ADD(out, STR(s));
		free(s);
	}
	return out;
}

static void expect_words(mc_engine *e, const char *text, J *expected)
{
	J *tokens = tokens_of(e, text),
	  *actual = tokens ? words(tokens) : NIL();
	verify(cJSON_Compare(actual, expected, 1), text, "token split",
	       expected, actual);
	DEL(actual);
	DEL(tokens);
	DEL(expected);
}

static void add_entity(J *list, const char *text, const char *canon,
		       const char *surface, const char **from)
{
	const char *at = strstr(*from, surface);
	if (!at) {
		fprintf(stderr, "test bug: %s not in %s\n", surface, text);
		exit(2);
	}
	char *prefix = slice(text, (size_t)(at - text));
	size_t x = ulen(prefix);
	free(prefix);
	J *e = OBJ();
	PUT(e, "canon", STR(canon));
	PUT(e, "x", NUM((double)x));
	PUT(e, "y", NUM((double)(x + ulen(surface))));
	PUT(e, "text", STR(surface));
	ADD(list, e);
	*from = at + strlen(surface);
}

/* One expected entity, or none when canon is NULL. */
static J *one_entity(const char *text, const char *canon, const char *surface)
{
	J *list = ARR();
	const char *from = text;
	if (canon)
		add_entity(list, text, canon, surface, &from);
	return list;
}

static void expect_entities(mc_engine *e, const char *text, const J *expected)
{
	J *actual = entities_of(e, text);
	verify(cJSON_Compare(actual, expected, 1), text, "entities", expected,
	       actual);
	DEL(actual);
}

static char *format(const char *frame, const char *a)
{
	size_t n = strlen(frame) + strlen(a) + 1;
	char *s = malloc(n);
	snprintf(s, n, frame, a);
	return s;
}

static char *concat(const char *a, const char *b, const char *c)
{
	Buf x = { 0 };
	buf_put(&x, a);
	buf_put(&x, b);
	buf_put(&x, c);
	return x.p ? buf_take(&x) : copy("");
}

/* Every apostrophe in the ASCII template replaced by g. */
static char *with_glyph(const char *template, const char *g)
{
	Buf b = { 0 };
	for (const char *p = template; *p; p++)
		if (*p == '\'')
			buf_put(&b, g);
		else
			buf_add(&b, p, 1);
	return buf_take(&b);
}

/* J array from a list of strings ending in NULL. */
static J *list(const char *first, ...)
{
	J *out = ARR();
	va_list ap;
	va_start(ap, first);
	for (const char *s = first; s; s = va_arg(ap, const char *))
		ADD(out, STR(s));
	va_end(ap);
	return out;
}

/* ---------------------------------------------------------------------- */
/* Glyph by placement.                                                    */

static const char *glyphs[] = { "'", "’", "‘", "ʼ", "‛",
				"`", "´", "′", "″", "\"" };
#define APOSTROPHES 8 /* The first eight fold to '. */

static void glyph_placement(mc_engine *e)
{
	for (int gi = 0; gi < (int)COUNT(glyphs); gi++) {
		const char *g = glyphs[gi];
		int apos = gi < APOSTROPHES,
		    pair = !strcmp(g, "`") || !strcmp(g, "´");
		char *gg = concat(g, g, ""), *ggg = concat(g, g, g),
		     *dg = concat("Driver", g, ""),
		     *dgs = concat("Driver", g, "s"),
		     *tga = concat("T", g, "ai"), *dogs = concat("dogs", g, ""),
		     *gh = concat(g, "hi", g);
		for (int k = 0; k < 14; k++) {
			char *snippet = NULL;
			J *pieces = NULL;
			switch (k) {
			case 0:
				snippet = copy(dgs);
				pieces = apos ? list(dgs, NULL) :
						list("Driver", g, "s", NULL);
				break;
			case 1:
				snippet = copy(tga);
				pieces = apos ? list(tga, NULL) :
						list("T", g, "ai", NULL);
				break;
			case 2:
				snippet = copy(dogs);
				pieces = apos ? list(dogs, NULL) :
						list("dogs", g, NULL);
				break;
			case 3:
				snippet = concat("rock", g, "");
				pieces = list("rock", g, NULL);
				break;
			case 4:
				snippet = concat(g, "tis", "");
				pieces = list(g, "tis", NULL);
				break;
			case 5:
				snippet = concat(g, "90s", "");
				pieces = list(g, "90s", NULL);
				break;
			case 6:
				snippet = concat("5", g, "11");
				pieces = list("5", g, "11", NULL);
				break;
			case 7:
				snippet = concat("Driver ", g, " s");
				pieces = list("Driver", g, "s", NULL);
				break;
			case 8:
				snippet = concat("Driver ", g, "s");
				pieces = list("Driver", g, "s", NULL);
				break;
			case 9:
				snippet = concat("Driver", g, " s");
				pieces = list("Driver", g, "s", NULL);
				break;
			case 10:
				snippet = concat("Driver", gg, "s");
				pieces = pair ? list("Driver", gg, "s", NULL) :
					 apos ? list(dg, g, "s", NULL) :
						list("Driver", g, g, "s", NULL);
				break;
			case 11:
				snippet = concat("Driver", ggg, "s");
				pieces = pair ? list("Driver", gg, g, "s",
						     NULL) :
					 apos ? list(dg, g, g, "s", NULL) :
						list("Driver", g, g, g, "s",
						     NULL);
				break;
			case 12:
				snippet = copy(g);
				pieces = list(g, NULL);
				break;
			default:
				snippet = copy(gh);
				pieces = list(g, "hi", g, NULL);
				break;
			}
			for (int where = 0; where < 3; where++) {
				begin("glyph placement");
				J *expected = ARR();
				char *text;
				if (where == 0) {
					text = concat(snippet, " sat here", "");
				} else if (where == 1) {
					text = concat("we saw ", snippet,
						      " today");
					ADD(expected, STR("we"));
					ADD(expected, STR("saw"));
				} else {
					text = concat("it was ", snippet, "");
					ADD(expected, STR("it"));
					ADD(expected, STR("was"));
				}
				EACH(p, pieces)
					ADD(expected, DUP(p));
				if (where == 0) {
					ADD(expected, STR("sat"));
					ADD(expected, STR("here"));
				} else if (where == 1)
					ADD(expected, STR("today"));
				expect_words(e, text, expected);
				free(text);
			}
			DEL(pieces);
			free(snippet);
		}
		free(gg);
		free(ggg);
		free(dg);
		free(dgs);
		free(tga);
		free(dogs);
		free(gh);
	}
}

/* ---------------------------------------------------------------------- */
/* Whitespace around quotes.                                              */

static const char *separators[] = {
	" ",
	"  ",
	"   ",
	"\t",
	"\n",
	"\r\n",
	"\r",
	"\v",
	"\f",
	"\xc2\xa0",
	"\xe2\x80\x89",
	"\xe2\x80\xaf",
	"\xe3\x80\x80",
	"\xe2\x80\xa8",
	"\xc2\x85",
};

static void whitespace(mc_engine *e)
{
	/* Two halves joined by a separator, and the words each split into. */
	static const char *phrases[][3] = {
		{ "Driver's", "Ed", "drivers_education" },
		{ "Driver’s", "Ed", "drivers_education" },
		{ "say 'hi'", "now", NULL },
		{ "Driver '", "s Ed", NULL },
	};
	for (int s = 0; s < (int)COUNT(separators); s++)
		for (int p = 0; p < (int)COUNT(phrases); p++)
			for (int slot = 0; slot < 3; slot++) {
				begin("whitespace");
				const char *sep = separators[s],
					   *left = phrases[p][0],
					   *right = phrases[p][1],
					   *canon = phrases[p][2];
				char *inner = concat(left,
						     slot == 1 ? sep : " ",
						     right),
				     *before = concat("Sign up",
						      slot == 0 ? sep : " ",
						      inner),
				     *text = concat(before,
						    slot == 2 ? sep : " ",
						    "today");
				J *expected = one_entity(text, canon, inner);
				expect_entities(e, text, expected);
				DEL(expected);
				J *split =
					p == 0 ? list("Sign", "up", "Driver's",
						      "Ed", "today", NULL) :
					p == 1 ? list("Sign", "up", "Driver’s",
						      "Ed", "today", NULL) :
					p == 2 ? list("Sign", "up", "say", "'",
						      "hi", "'", "now", "today",
						      NULL) :
						 list("Sign", "up", "Driver",
						      "'", "s", "Ed", "today",
						      NULL);
				expect_words(e, text, split);
				free(inner);
				free(before);
				free(text);
			}
}

/* ---------------------------------------------------------------------- */
/* Entity matching and letter case.                                       */

static const char *synonyms[][2] = {
	{ "Driver's Ed", "drivers_education" },
	{ "Women's Studies", "womens_studies" },
	{ "Men's Choir", "mens_choir" },
	{ "T'ai Chi Ch'uan", "taijiquan" },
	{ "Children's Literature", "childrens_literature" },
	{ "Today's Health Care", "contemporary_health" },
	{ "O'Brien Hall", "obrien_hall" },
	{ "Rock 'n' Roll", "rock_music" },
};

static const char *frames[] = {
	"%s",
	"%s is offered this fall.",
	"We offer %s on Mondays.",
	"Sign up for %s.",
	"Electives (such as %s) fill fast.",
	"She called it \"%s\" twice.",
};

static void entities(const mc_engine *const *loads, int load_count)
{
	for (int s = 0; s < (int)COUNT(synonyms); s++)
		for (int g = 0; g < (int)COUNT(glyphs); g++)
			for (int f = 0; f < (int)COUNT(frames); f++) {
				begin("entities");
				char *phrase = with_glyph(synonyms[s][0],
							  glyphs[g]),
				     *text = format(frames[f], phrase);
				J *expected = one_entity(
					text,
					g < APOSTROPHES ? synonyms[s][1] : NULL,
					phrase);
				for (int l = 0; l < load_count; l++)
					expect_entities((mc_engine *)loads[l],
							text, expected);
				DEL(expected);
				free(phrase);
				free(text);
			}
}

static char *recase(const char *s, int mode)
{
	char *out = copy(s);
	int start = 1, letters = 0;
	for (char *p = out; *p; p++) {
		unsigned char c = (unsigned char)*p;
		if (c < 128 && isalpha(c)) {
			int up = mode == 1 || (mode == 2 && start) ||
				 (mode == 3 && letters % 2 == 0);
			*p = (char)(up ? toupper(c) : tolower(c));
			letters++;
		}
		start = c == ' ';
	}
	return out;
}

static void letter_case(mc_engine *e)
{
	static const char *five[] = { "'", "’", "‘", "ʼ", "＇" };
	for (int mode = 0; mode < 4; mode++)
		for (int s = 0; s < (int)COUNT(synonyms); s++)
			for (int g = 0; g < (int)COUNT(five); g++) {
				begin("letter case");
				char *phrase = with_glyph(synonyms[s][0],
							  five[g]),
				     *cased = recase(phrase, mode),
				     *text = format("We offer %s on Mondays.",
						    cased);
				J *expected =
					one_entity(text, synonyms[s][1], cased);
				expect_entities(e, text, expected);
				DEL(expected);
				free(phrase);
				free(cased);
				free(text);
			}
}

/* ---------------------------------------------------------------------- */
/* The contraction and abbreviation words the tokenizer once expanded.    */

static const char *contractions[] = {
	"can't",  "could've", "everyone's", "he'd",    "he'll",	   "he's",
	"here's", "how've",   "i'll",	    "i'm",     "it's",	   "let's",
	"she'd",  "she's",    "should've",  "that's",  "the're",   "there's",
	"they'd", "they'll",  "they're",    "they've", "wander'd", "we'd",
	"we'll",  "we're",    "what're",    "what's",  "where's",  "who's",
	"why're", "won't",    "would've",   "y'all",   "you'd",	   "you're",
	"you've",
};
static const char *abbreviations[] = {
	"abbr.", "abr.",  "acad.",  "adj.",  "adm.",	 "aka.",    "approx.",
	"appt.", "apt.",  "assoc.", "ave.",  "bibliog.", "biol.",   "blvd.",
	"bot.",	 "cap.",  "chap.",  "chem.", "co.",	 "colloq.", "com.",
	"conf.", "cont.", "cp.",    "cr.",   "crit.",	 "cyn.",    "def.",
	"dept.", "diff.", "dr.",    "e.g.",  "ea.",	 "est.",    "etc.",
	"gen.",	 "impt.", "ln.",    "min.",  "misc.",	 "mr.",	    "mrs.",
	"nec.",	 "no.",	  "rd.",    "re.",   "sim.",	 "st.",	    "tel.",
	"temp.", "vet.",  "vs.",
};

/* A word's pieces: whole, or split at every period. */
static void add_pieces(J *out, const char *word, int periods)
{
	if (!periods) {
		ADD(out, STR(word));
		return;
	}
	const char *start = word;
	for (const char *p = word;; p++) {
		if (*p == '.' || !*p) {
			if (p > start) {
				char *s = slice(start, (size_t)(p - start));
				ADD(out, STR(s));
				free(s);
			}
			if (!*p)
				break;
			ADD(out, STR("."));
			start = p + 1;
		}
	}
}

static void table_words(mc_engine *e, const char **table, int n, int periods)
{
	static const char *positions[] = { "%s we said", "and %s we said",
					   "we said %s", "we said %s, then" };
	for (int i = 0; i < n; i++)
		for (int mode = 0; mode < 3; mode++)
			for (int at = 0; at < 4; at++) {
				begin("tables");
				char *word = mode == 0 ? copy(table[i]) :
					     mode == 1 ? recase(table[i], 1) :
							 copy(table[i]);
				if (mode == 2)
					word[0] = (char)toupper(
						(unsigned char)word[0]);
				char *text = format(positions[at], word);
				J *expected = ARR();
				if (at == 1)
					ADD(expected, STR("and"));
				if (at >= 2) {
					ADD(expected, STR("we"));
					ADD(expected, STR("said"));
				}
				add_pieces(expected, word, periods);
				if (at <= 1) {
					ADD(expected, STR("we"));
					ADD(expected, STR("said"));
				}
				if (at == 3) {
					ADD(expected, STR(","));
					ADD(expected, STR("then"));
				}
				expect_words(e, text, expected);
				free(word);
				free(text);
			}
}

/* ---------------------------------------------------------------------- */
/* Every printable ASCII character in five settings.                      */

static void ascii_sweep(mc_engine *e)
{
	for (int c = 0x20; c <= 0x7e; c++) {
		char ch[2] = { (char)c, 0 };
		int word = isalnum(c), under = c == '_',
		    apos = c == '\'' || c == '`', amp = c == '&',
		    space = c == ' ';
		char *a_b = concat("a", ch, "b"), *ca = concat(ch, "a", ""),
		     *ac = concat("a", ch, "");
		for (int form = 0; form < 5; form++) {
			begin("ascii sweep");
			char *text = NULL;
			J *expected = NULL;
			switch (form) {
			case 0:
				text = copy(ch);
				expected = space ? ARR() : list(ch, NULL);
				break;
			case 1:
				text = copy(a_b);
				expected = word || under || apos || amp ?
						   list(a_b, NULL) :
					   space ? list("a", "b", NULL) :
						   list("a", ch, "b", NULL);
				break;
			case 2:
				text = concat("a ", ch, " b");
				expected = space ? list("a", "b", NULL) :
						   list("a", ch, "b", NULL);
				break;
			case 3:
				text = copy(ca);
				expected = word	 ? list(ca, NULL) :
					   space ? list("a", NULL) :
						   list(ch, "a", NULL);
				break;
			default:
				text = copy(ac);
				expected = word	 ? list(ac, NULL) :
					   space ? list("a", NULL) :
						   list("a", ch, NULL);
				break;
			}
			expect_words(e, text, expected);
			free(text);
		}
		free(a_b);
		free(ca);
		free(ac);
	}
}

/* ---------------------------------------------------------------------- */
/* Unicode beside apostrophe words and ahead of entities.                 */

static void unicode(mc_engine *e)
{
	static const char *items[] = {
		"🐶",
		"👩‍👩‍👧",
		"🇺🇸",
		"👍🏽",
		"e\xcc\x81",
		"é",
		"中文",
		"한국어",
		"مرحبا",
		"שלום",
		"\xe2\x80\x8e",
		"\xe2\x80\x8f",
		"\xe2\x80\x8d",
		"\xe2\x80\x8c",
		"\xef\xbb\xbf",
		"co\xc2\xad"
		"op",
		"\xef\xbf\xbd",
		"\xc0\x80", /* An embedded NUL, as strings hold it. */
		"𝔘𝔫𝔦",
		"Ω≈ç√",
	};
	for (int i = 0; i < (int)COUNT(items); i++)
		for (int at = 0; at < 4; at++) {
			begin("unicode");
			const char *u = items[i];
			char *text = at == 0 ? concat(u, " Driver's Ed", "") :
				     at == 1 ? concat("Driver's Ed ", u, "") :
				     at == 2 ? concat("Driv", u, "er's Ed") :
					       concat(u, " then Biology", "");
			J *tokens = tokens_of(e, text);
			DEL(tokens);
			if (at == 2) {
				J *found = entities_of(e, text);
				DEL(found);
			} else {
				J *expected = one_entity(
					text,
					at == 3 ? "biology" :
						  "drivers_education",
					at == 3 ? "Biology" : "Driver's Ed");
				expect_entities(e, text, expected);
				DEL(expected);
			}
			free(text);
		}
}

/* ---------------------------------------------------------------------- */
/* Drift: one bad stretch early must not move anything after it.          */

static void repeated(Buf *b, const char *s, int n)
{
	for (int i = 0; i < n; i++)
		buf_put(b, s);
}

static void drift(mc_engine *e)
{
	begin("drift");
	const char *reported =
		"I took Driver ' s Ed last year. Then I took Biology and "
		"Chemistry.";
	J *expected = ARR();
	const char *from = reported;
	add_entity(expected, reported, "biology", "Biology", &from);
	add_entity(expected, reported, "chemistry", "Chemistry", &from);
	expect_entities(e, reported, expected);
	DEL(expected);

	static const char *lone[] = { "' ", "’ ", "\" " };
	static const int counts[] = { 1, 10, 100, 1000 };
	for (int g = 0; g < 3; g++)
		for (int c = 0; c < 4; c++) {
			begin("drift");
			Buf b = { 0 };
			repeated(&b, lone[g], counts[c]);
			buf_put(&b, "Biology");
			J *one = one_entity(b.p, "biology", "Biology");
			expect_entities(e, b.p, one);
			DEL(one);
			free(b.p);
		}

	/* Each template holds four entities after its stray quotes. */
	static const char *templates[][5] = {
		{ "Speaker ' s note:\tWe ' re in Driver’s Ed\nand Biology, "
		  "then 'Art History' and Chemistry.  ",
		  "Driver’s Ed", "Biology", "Art History", "Chemistry" },
		{ "“Quote” ‘ s aside:\r\nthey ‘re in Driver‘s  Ed, Biology "
		  "\"and\" then Art History; Chemistry! ",
		  "Driver‘s  Ed", "Biology", "Art History", "Chemistry" },
		{ "Note\xc2\xa0` s gap — we ` ll do Driver`s\tEd, Biology, "
		  "(Art\nHistory) and Chemistry? ",
		  "Driver`s\tEd", "Biology", "Art\nHistory", "Chemistry" },
	};
	static const char *canons[] = { "drivers_education", "biology",
					"art_history", "chemistry" };
	for (int t = 0; t < 3; t++)
		for (int c = 0; c < 4; c++) {
			begin("drift");
			Buf b = { 0 };
			repeated(&b, templates[t][0], counts[c]);
			J *all = ARR();
			const char *at = b.p;
			for (int r = 0; r < counts[c]; r++)
				for (int k = 0; k < 4; k++)
					add_entity(all, b.p, canons[k],
						   templates[t][k + 1], &at);
			expect_entities(e, b.p, all);
			DEL(all);
			free(b.p);
		}

	static const char *runs[] = { " ", "\t", "\n", "\r\n", "\xc2\xa0" };
	for (int r = 0; r < 5; r++) {
		begin("drift");
		Buf b = { 0 };
		buf_put(&b, "Start");
		repeated(&b, runs[r], 100);
		buf_put(&b, "Biology");
		J *one = one_entity(b.p, "biology", "Biology");
		expect_entities(e, b.p, one);
		DEL(one);
		free(b.p);
	}
}

/* ---------------------------------------------------------------------- */
/* Degenerate input.                                                      */

static void degenerate_case(mc_engine *e, const char *text)
{
	begin("degenerate");
	J *tokens = tokens_of(e, text), *none = ARR();
	DEL(tokens);
	expect_entities(e, text, none);
	DEL(none);
}

static void degenerate(mc_engine *e)
{
	degenerate_case(e, "");
	for (int s = 0; s < (int)COUNT(separators); s++)
		degenerate_case(e, separators[s]);
	Buf quotes = { 0 };
	for (int n = 1; n <= 6; n++) {
		buf_put(&quotes, "'");
		degenerate_case(e, quotes.p);
	}
	free(quotes.p);
	Buf b = { 0 };
	repeated(&b, "'", 10000);
	degenerate_case(e, b.p);
	free(b.p);
	degenerate_case(e, "' ' ' '");
	b = (Buf){ 0 };
	repeated(&b, "' ", 5000);
	degenerate_case(e, b.p);
	free(b.p);
	static const char *runs[] = { "...", "!!!",  "?!?!",   "---",
				      "—–-", "“”‘’", "()[]{}", "&&&",
				      "___", ",,,",  "'.'",    "\"'\"" };
	for (int i = 0; i < (int)COUNT(runs); i++)
		degenerate_case(e, runs[i]);
	static const char *spaces[] = { " ", "\t", "\n" };
	for (int i = 0; i < 3; i++) {
		b = (Buf){ 0 };
		repeated(&b, spaces[i], 100);
		degenerate_case(e, b.p);
		free(b.p);
	}
}

/* ---------------------------------------------------------------------- */
/* Invalid UTF-8 is rejected before anything is tokenized.                */

static void invalid_utf8(mc_engine *e)
{
	static const char *bad[] = {
		"\xe2\x80",
		"\xc0\xaf",
		"\x80",
		"\xed\xa0\x80",
		"\xff",
		"\xfe",
		"\xf5\x80\x80\x80",
		"\xe0\x80\xaf",
		"\xf0\x80\x80\xaf",
		"\xc3",
	};
	static const char *ops[] = { "tokenize", "parse" };
	for (int i = 0; i < (int)COUNT(bad); i++) {
		begin("invalid utf-8");
		for (int o = 0; o < 2; o++) {
			Buf wire = { 0 };
			buf_put(&wire, "{\"op\":\"");
			buf_put(&wire, ops[o]);
			buf_put(&wire, "\",\"text\":\"Driver");
			buf_put(&wire, bad[i]);
			buf_put(&wire, "s Ed\"}");
			mc_error err;
			char *r = mc_request(e, wire.p, &err);
			J *out = r ? cJSON_Parse(r) : NULL;
			J *code = GET(GET(out, "error"), "code");
			verify(cJSON_IsFalse(GET(out, "ok")) && code &&
				       code->valueint == 2,
			       ops[o], "invalid UTF-8 is an input error", NULL,
			       out);
			DEL(out);
			mc_free(r);
			free(wire.p);
		}
	}
}

/* ---------------------------------------------------------------------- */
/* Every entry of the fold table.                                         */

static void fold_entry(mc_engine *e, const char *c, int kind)
{
	/* kind: 0 dash, 1 apostrophe or single quote, 2 double quote. */
	for (int at = 0; at < 4; at++) {
		begin("fold table");
		if (at == 0) {
			char *text = concat("a", c, "b");
			expect_words(e, text,
				     kind == 1 ? list(text, NULL) :
						 list("a", c, "b", NULL));
			free(text);
		} else if (at == 1) {
			expect_words(e, c, list(c, NULL));
		} else if (at == 2) {
			char *text = concat("a ", c, " b");
			expect_words(e, text, list("a", c, "b", NULL));
			free(text);
		} else {
			char *text = kind == 0 ? concat("Computer", c,
							"Aided Manufacturing") :
				     kind == 1 ? concat("Driver", c, "s Ed") :
						 concat("Say ", c, "Cheese");
			if (kind == 2) {
				char *closed = concat(text, c, "");
				free(text);
				text = closed;
			}
			J *expected = one_entity(
				text,
				kind == 0 ? "computer_aided_manufacturing" :
				kind == 1 ? "drivers_education" :
					    "photography",
				text);
			expect_entities(e, text, expected);
			DEL(expected);
			free(text);
		}
	}
}

static void fold_table(mc_engine *e)
{
	for (size_t i = 0; i < COUNT(dashes); i++) {
		char *c = utf8(dashes[i]);
		fold_entry(e, c, 0);
		free(c);
	}
	for (size_t i = 0; i < COUNT(squotes); i++) {
		char *c = utf8(squotes[i]);
		fold_entry(e, c, 1);
		free(c);
	}
	for (size_t i = 0; i < COUNT(dquotes); i++) {
		char *c = utf8(dquotes[i]);
		fold_entry(e, c, 2);
		free(c);
	}
	fold_entry(e, "``", 2);
	fold_entry(e, "´´", 2);
}

/* ---------------------------------------------------------------------- */
/* Synonyms stored with each variant, matched by input with every variant. */

static void stored_glyphs(const mc_engine *const *loads, int load_count)
{
	/* The fixture stores each with a different apostrophe that folds to '. */
	static const char *stored[][2] = {
		{ "Pilot's Log", "pilots_log" },
		{ "Baker's Dozen", "bakers_dozen" },
		{ "Miner's Lamp", "miners_lamp" },
		{ "Sailor's Knot", "sailors_knot" },
		{ "Guild's Hall", "guilds_hall" },
		{ "Weaver's Loom", "weavers_loom" },
		{ "Potter's Wheel", "potters_wheel" },
		{ "Smith's Forge", "smiths_forge" },
		{ "Archer's Bow", "archers_bow" },
		{ "Tailor's Chalk", "tailors_chalk" },
		{ "Hunter's Moon", "hunters_moon" },
	};
	J *inputs = ARR(); /* Each entry: phrase, canon or null. */
	for (size_t i = 0; i < COUNT(stored); i++)
		for (int g = 0; g < (int)COUNT(glyphs); g++) {
			J *pair = ARR();
			char *phrase = with_glyph(stored[i][0], glyphs[g]);
			ADD(pair, STR(phrase));
			ADD(pair, g < APOSTROPHES ? STR(stored[i][1]) : NIL());
			ADD(inputs, pair);
			free(phrase);
		}
	/* Stored as Say “Smile”, sent with every double quote. */
	for (size_t i = 0; i < COUNT(dquotes) + 3; i++) {
		char *q = i < COUNT(dquotes)	  ? utf8(dquotes[i]) :
			  i == COUNT(dquotes)	  ? copy("\"") :
			  i == COUNT(dquotes) + 1 ? copy("``") :
						    copy("´´");
		char *open = concat("Say ", q, "Smile"),
		     *phrase = concat(open, q, "");
		J *pair = ARR();
		ADD(pair, STR(phrase));
		ADD(pair, STR("portraits"));
		ADD(inputs, pair);
		free(q);
		free(open);
		free(phrase);
	}
	/* Stored as Data–Driven Design, sent with every hyphen and dash. */
	for (size_t i = 0; i <= COUNT(dashes); i++) {
		char *d = i < COUNT(dashes) ? utf8(dashes[i]) : copy("-"),
		     *phrase = concat("Data", d, "Driven Design");
		J *pair = ARR();
		ADD(pair, STR(phrase));
		ADD(pair, STR("data_driven_design"));
		ADD(inputs, pair);
		free(d);
		free(phrase);
	}
	EACH(pair, inputs) {
		begin("stored glyphs");
		const char *phrase = S(AT(pair, 0));
		J *canon = AT(pair, 1);
		char *text = format("We offer %s on Mondays.", phrase);
		J *expected = one_entity(
			text, cJSON_IsString(canon) ? S(canon) : NULL, phrase);
		for (int l = 0; l < load_count; l++)
			expect_entities((mc_engine *)loads[l], text, expected);
		DEL(expected);
		free(text);
	}
	DEL(inputs);
}

/* ---------------------------------------------------------------------- */

static mc_engine *load_path(const char *path, const char *interface)
{
	mc_engine *e = mc_create();
	J *q = OBJ();
	PUT(q, "op", STR("load"));
	PUT(q, "path", STR(path));
	if (interface) {
		PUT(q, "interface", STR(interface));
		PUT(q, "class_based", BOOL(1));
	}
	J *r = test_request(e, q);
	DEL(q);
	int ok = cJSON_IsTrue(GET(r, "ok"));
	DEL(r);
	if (!ok) {
		mc_destroy(e);
		return NULL;
	}
	return e;
}

/* A snapshot of the OWL load, reloaded as JSON views in a fresh engine. */
static mc_engine *load_snapshot(mc_engine *from)
{
	J *snapshot = test_call(from, "{\"op\":\"snapshot\"}");
	if (!snapshot)
		return NULL;
	mc_engine *e = mc_create();
	J *q = OBJ();
	PUT(q, "op", STR("load"));
	PUT(q, "name", STR("apostrophe-synonyms-test"));
	PUT(q, "snapshot", snapshot);
	J *r = test_request(e, q);
	DEL(q);
	int ok = cJSON_IsTrue(GET(r, "ok"));
	DEL(r);
	if (!ok) {
		mc_destroy(e);
		return NULL;
	}
	return e;
}

int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	char *path = test_path(argv[1], FIXTURE);
	mc_engine *owl = load_path(path, NULL), *data = load_path(path, "data"),
		  *snapshot = owl ? load_snapshot(owl) : NULL;
	free(path);
	if (!owl || !data || !snapshot) {
		fprintf(stderr, "FAIL could not load %s\n", FIXTURE);
		return 2;
	}
	const mc_engine *loads[] = { owl, data, snapshot };
	glyph_placement(owl);
	whitespace(owl);
	entities(loads, 3);
	letter_case(owl);
	table_words(owl, contractions, (int)COUNT(contractions), 0);
	table_words(owl, abbreviations, (int)COUNT(abbreviations), 1);
	ascii_sweep(owl);
	unicode(owl);
	drift(owl);
	degenerate(owl);
	invalid_utf8(owl);
	fold_table(owl);
	stored_glyphs(loads, 3);
	int total = 0, short_family = 0;
	for (int i = 0; i < FAMILY_COUNT; i++) {
		printf("%-16s %5d (minimum %d)\n", families[i].name,
		       families[i].cases, families[i].minimum);
		total += families[i].cases;
		if (families[i].cases < families[i].minimum) {
			fprintf(stderr, "FAIL %s has %d cases, below %d\n",
				families[i].name, families[i].cases,
				families[i].minimum);
			short_family = 1;
		}
	}
	printf("Token fidelity: %d cases, %d/%d assertions passed.\n", total,
	       assertions - failures, assertions);
	mc_destroy(owl);
	mc_destroy(data);
	mc_destroy(snapshot);
	return failures || short_family ? 1 : 0;
}
