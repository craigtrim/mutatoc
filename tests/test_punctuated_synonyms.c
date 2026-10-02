/*
 * test_punctuated_synonyms.c - Synonyms written with punctuation.
 *
 * Loads tests/fixtures/ontologies/punctuated-synonyms-test.ttl, whose
 * synonyms carry slashes, colons, parentheses, brackets, hyphens,
 * apostrophes, ampersands and a number sign. A synonym the text spells out
 * must match as one exact entity in any sentence frame, letter case or
 * spacing around its punctuation, through an OWL load, the data interface
 * and a reloaded snapshot. Expected matches come from the authored synonyms,
 * not from an earlier run of the engine. craigtrim/mutatoc#5
 */

#include "testlib.h"

#define FIXTURE "tests/fixtures/ontologies/punctuated-synonyms-test.ttl"

typedef struct {
	const char *name;
	int cases;
} Category;
static Category categories[16];
static int category_count, assertions, failures;

static void count_case(const char *name)
{
	for (int i = 0; i < category_count; i++)
		if (!strcmp(categories[i].name, name)) {
			categories[i].cases++;
			return;
		}
	categories[category_count].name = name;
	categories[category_count++].cases = 1;
}

static void check(const char *category, const char *label, const J *actual,
		  const J *expected)
{
	count_case(category);
	assertions++;
	if (!cJSON_Compare(actual, expected, 1) && ++failures <= 40) {
		Buf b = { 0 };
		buf_put(&b, category);
		buf_put(&b, " | ");
		buf_put(&b, label);
		test_print_diff(b.p, expected, actual);
		free(b.p);
	}
}

static void add_leaves(Buf *b, const J *token)
{
	J *swaps = GET(token, "swaps");
	if (!swaps) {
		buf_put(b, S(GET(token, "text")));
		return;
	}
	EACH(child, GET(swaps, "tokens"))
		add_leaves(b, child);
}

static J *entity(const char *canon, const char *surface, int x, int y)
{
	J *e = OBJ();
	PUT(e, "canon", STR(canon));
	PUT(e, "type", STR("exact"));
	PUT(e, "ner", NIL());
	PUT(e, "x", NUM(x));
	PUT(e, "y", NUM(y));
	PUT(e, "surface", STR(surface));
	return e;
}

/* Every matched token of a parse result, with the text of its leaf tokens. */
static J *found(const J *result)
{
	J *out = ARR();
	EACH(t, GET(result, "tokens")) {
		J *swaps = GET(t, "swaps");
		if (!swaps)
			continue;
		Buf b = { 0 };
		add_leaves(&b, t);
		char *surface = norm(b.p ? b.p : "", 0, 0);
		J *e = entity(S(GET(swaps, "canon")), surface,
			      GET(t, "x")->valueint, GET(t, "y")->valueint);
		set(e, "type", DUP(GET(swaps, "type")));
		set(e, "ner", GET(t, "ner") ? DUP(GET(t, "ner")) : NIL());
		ADD(out, e);
		free(surface);
		free(b.p);
	}
	return out;
}

static J *request(mc_engine *e, J *q)
{
	J *r = test_request(e, q);
	DEL(q);
	if (!cJSON_IsTrue(GET(r, "ok"))) {
		fprintf(stderr, "request failed: %s\n",
			S(GET(GET(r, "error"), "message")));
		DEL(r);
		return NULL;
	}
	J *result = cJSON_DetachItemFromObjectCaseSensitive(r, "result");
	DEL(r);
	return result;
}

static J *parse(mc_engine *e, const char *text)
{
	J *q = OBJ();
	PUT(q, "op", STR("parse"));
	PUT(q, "text", STR(text));
	J *result = request(e, q), *out = result ? found(result) : NIL();
	DEL(result);
	return out;
}

/* Code points before the n-th occurrence of surface, searching from *from. */
static int offset(const char *text, const char *surface, const char **from)
{
	const char *at = strstr(*from, surface);
	if (!at)
		return -1;
	char *prefix = slice(text, (size_t)(at - text));
	int x = (int)ulen(prefix);
	free(prefix);
	*from = at + strlen(surface);
	return x;
}

/*
 * Expected entities for text, each given as a canon and the surface it spans,
 * in text order. Offsets come from where each surface sits in the text.
 */
static J *expect(const char *text, const char *const (*matches)[2])
{
	J *out = ARR();
	const char *from = text;
	for (int i = 0; matches[i][0]; i++) {
		int x = offset(text, matches[i][1], &from);
		ADD(out, entity(matches[i][0], matches[i][1], x,
				x + (int)ulen(matches[i][1])));
	}
	return out;
}

/* ---------------------------------------------------------------------- */
/* tokenize_key: the window text the exact matcher builds for a synonym.   */

static void keys(void)
{
	static const char *cases[][2] = {
		/* Plain words need no alias. */
		{ "physical education", NULL },
		{ "abc", NULL },
		{ "ABC", NULL },
		{ "a_b", NULL },
		{ "café au lait", NULL },
		{ "", NULL },
		/* The tokenizer keeps these characters inside the word. */
		{ "a&p", NULL },
		{ "human growth & development", NULL },
		{ "driver's education", NULL },
		{ "1,000", NULL },
		{ "3.14", NULL },
		/* Slashes. */
		{ "well/health/physical education",
		  "well / health / physical education" },
		{ "Well/Health/Physical Education",
		  "well / health / physical education" },
		{ "well / health / physical education", NULL },
		{ "well /health/ physical education",
		  "well / health / physical education" },
		{ "anat/phys", "anat / phys" },
		{ "x/y/z", "x / y / z" },
		{ "café/thé", "café / thé" },
		{ "spanish i/ii", "spanish i / ii" },
		{ "driver's ed/safety", "driver's ed / safety" },
		/* Colons. */
		{ "pe:pe", "pe : pe" },
		{ "intro to logic: part 1", "intro to logic : part 1" },
		/* Brackets and parentheses. */
		{ "calc (honors)", "calc ( honors )" },
		{ "consumer price index (cpi)",
		  "consumer price index ( cpi )" },
		{ "math lab [remedial]", "math lab [ remedial ]" },
		/* Hyphens, including typographic dashes folded to ASCII. */
		{ "pre-calculus", "pre - calculus" },
		{ "pre–calculus", "pre - calculus" },
		{ "computer-aided manufacturing",
		  "computer - aided manufacturing" },
		/* Other punctuation. */
		{ "ap calc!", "ap calc !" },
		{ "studio art #2", "studio art # 2" },
		/* A curly apostrophe folds into its word (craigtrim/mutatoc#7). */
		{ "women’s studies", "women's studies" },
		{ "u.s. history", "u . s . history" },
		/* Underscores at a word edge and irregular spacing. */
		{ "a_", "a _" },
		{ "_a", "_ a" },
		{ "a  b", "a b" },
		{ " a", "a" },
		{ "a ", "a" },
		{ " ", NULL },
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
		char *key = tokenize_key(cases[i][0]);
		J *actual = key ? STR(key) : NIL(),
		  *expected = cases[i][1] ? STR(cases[i][1]) : NIL();
		check("tokenize_key", cases[i][0], actual, expected);
		DEL(actual);
		DEL(expected);
		free(key);
	}
}

/* ---------------------------------------------------------------------- */
/* Load paths: every case runs through each of them.                       */

typedef struct {
	const char *name;
	mc_engine *engine;
} Load;

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
	J *r = request(e, q);
	if (!r) {
		mc_destroy(e);
		return NULL;
	}
	DEL(r);
	return e;
}

/* A snapshot of the OWL load, reloaded as JSON views in a fresh engine. */
static mc_engine *load_snapshot(mc_engine *from)
{
	J *q = OBJ();
	PUT(q, "op", STR("snapshot"));
	J *snapshot = request(from, q);
	if (!snapshot)
		return NULL;
	mc_engine *e = mc_create();
	q = OBJ();
	PUT(q, "op", STR("load"));
	PUT(q, "name", STR("punctuated-synonyms-test"));
	PUT(q, "snapshot", snapshot);
	J *r = request(e, q);
	if (!r) {
		mc_destroy(e);
		return NULL;
	}
	DEL(r);
	return e;
}

/* ---------------------------------------------------------------------- */
/* Every authored punctuated synonym, in frames, cases and spacings.       */

typedef struct {
	const char *phrase; /* As written in the fixture. */
	const char *canon;
} Synonym;

static const Synonym synonyms[] = {
	{ "Well/Health/Physical Education", "physical_education" },
	{ "PE:PE", "physical_education" },
	{ "ANAT/PHYS", "anatomy_physiology" },
	{ "A&P", "anatomy_physiology" },
	{ "Career Exploration/Planning", "career_planning" },
	{ "Calc (Honors)", "calculus" },
	{ "AP Calc!", "calculus" },
	{ "Pre-Calculus", "precalculus" },
	{ "Pre-Calc/Trig", "precalculus" },
	{ "Computer-Aided Manufacturing", "computer_aided_manufacturing" },
	{ "Spanish I/II", "spanish" },
	{ "Women’s Studies", "womens_studies" },
	{ "Driver's Ed/Safety", "drivers_education" },
	{ "Consumer Price Index (CPI)", "consumer_price_index" },
	{ "Intro to Logic: Part 1", "logic" },
	{ "Math Lab [Remedial]", "math_lab" },
	{ "Studio Art #2", "studio_art" },
	{ "Health/Wellness/Fitness/Nutrition/Recreation/Sport Science",
	  "health_science" },
	{ "Human Growth & Development", "human_growth" },
	{ "U.S. History", "us_history" },
};

/* Words that are not synonyms, around the phrase. */
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

static char *recase(const char *s, int mode)
{
	char *out = copy(s);
	for (char *p = out; *p; p++)
		if (mode == 1 && *p >= 'A' && *p <= 'Z')
			*p = (char)(*p - 'A' + 'a');
		else if (mode == 2 && *p >= 'a' && *p <= 'z')
			*p = (char)(*p - 'a' + 'A');
	return out;
}

/*
 * The phrase with a space on both sides of each separator. Closing ")" and
 * "!" get a space after only: the tokenizer trims a word that sits against
 * them, which shifts the offsets of the closing mark at the end of a text,
 * independent of synonyms.
 */
static char *spaced(const char *s)
{
	Buf b = { 0 };
	int pending = 0;
	for (const char *p = s; *p; p++) {
		int both = strchr("/:-#[](", *p) != NULL,
		    after = strchr(")!", *p) != NULL;
		if (*p == ' ') {
			pending = 1;
			continue;
		}
		if ((pending || both) && b.n)
			buf_put(&b, " ");
		pending = both || after;
		buf_add(&b, p, 1);
	}
	return buf_take(&b);
}

static void generated(const Load *loads, int load_count)
{
	for (size_t s = 0; s < sizeof(synonyms) / sizeof(*synonyms); s++)
		for (int mode = 0; mode < 3; mode++)
			for (int space = 0; space < 2; space++) {
				char *cased = recase(synonyms[s].phrase, mode),
				     *variant = space ? spaced(cased) :
							copy(cased);
				if (space && !strcmp(variant, cased)) {
					free(cased);
					free(variant);
					continue;
				}
				for (size_t f = 0;
				     f < sizeof(frames) / sizeof(*frames);
				     f++) {
					Buf text = { 0 };
					buf_put(&text, frames[f][0]);
					buf_put(&text, variant);
					buf_put(&text, frames[f][1]);
					int x = (int)ulen(frames[f][0]);
					J *expected = ARR();
					ADD(expected,
					    entity(synonyms[s].canon, variant,
						   x, x + (int)ulen(variant)));
					for (int l = 0; l < load_count; l++) {
						J *actual =
							parse(loads[l].engine,
							      text.p);
						Buf label = { 0 };
						buf_put(&label, loads[l].name);
						buf_put(&label, ": ");
						buf_put(&label, text.p);
						check("generated", label.p,
						      actual, expected);
						free(label.p);
						DEL(actual);
					}
					DEL(expected);
					free(text.p);
				}
				free(cased);
				free(variant);
			}
}

/* ---------------------------------------------------------------------- */
/* Hand-written cases.                                                     */

typedef struct {
	const char *category;
	const char *text;
	const char *const matches[5][2]; /* canon, surface; NULL ends */
} Case;

static const Case cases[] = {
	/* The reported text and its neighbours. */
	{ "reported",
	  "Well/Health/Physical Education",
	  { { "physical_education", "Well/Health/Physical Education" } } },
	{ "reported",
	  "I took Well/Health/Physical Education last fall.",
	  { { "physical_education", "Well/Health/Physical Education" } } },
	{ "reported",
	  "Well / Health / Physical Education",
	  { { "physical_education", "Well / Health / Physical Education" } } },
	{ "reported",
	  "Well /Health/ Physical Education",
	  { { "physical_education", "Well /Health/ Physical Education" } } },
	{ "reported",
	  "Well/ Health /Physical Education",
	  { { "physical_education", "Well/ Health /Physical Education" } } },
	{ "reported",
	  "WELL/HEALTH/PHYSICAL EDUCATION",
	  { { "physical_education", "WELL/HEALTH/PHYSICAL EDUCATION" } } },
	{ "reported",
	  "wElL/hEaLtH/pHySiCaL eDuCaTiOn",
	  { { "physical_education", "wElL/hEaLtH/pHySiCaL eDuCaTiOn" } } },
	{ "reported", "PE:PE", { { "physical_education", "PE:PE" } } },
	{ "reported", "PE : PE", { { "physical_education", "PE : PE" } } },
	{ "reported", "pe:pe", { { "physical_education", "pe:pe" } } },
	{ "reported",
	  "Physical Education",
	  { { "physical_education", "Physical Education" } } },
	/* The punctuation is part of the synonym: other marks do not match. */
	{ "partial", "Well/Health", { { NULL } } },
	{ "partial", "Well/Health/Physical", { { NULL } } },
	{ "partial", "Well/Health/", { { NULL } } },
	{ "partial", "PE", { { NULL } } },
	{ "partial", "PE:", { { NULL } } },
	{ "partial", "PE/PE", { { NULL } } },
	{ "partial", "PE::PE", { { NULL } } },
	{ "partial", "PE PE", { { NULL } } },
	{ "partial",
	  "Health/Physical Education",
	  { { "physical_education", "Physical Education" } } },
	{ "partial",
	  "Well-Health-Physical Education",
	  { { "physical_education", "Physical Education" } } },
	{ "partial",
	  "Well:Health:Physical Education",
	  { { "physical_education", "Physical Education" } } },
	{ "partial",
	  "Well Health Physical Education",
	  { { "physical_education", "Physical Education" } } },
	{ "partial",
	  "Well//Health/Physical Education",
	  { { "physical_education", "Physical Education" } } },
	{ "partial",
	  "Well/Health/Physical Education/Recreation",
	  { { "physical_education", "Well/Health/Physical Education" } } },
	{ "partial", "Calc Honors", { { NULL } } },
	{ "partial", "Calc (Honors", { { NULL } } },
	{ "partial", "Calc [Honors]", { { NULL } } },
	{ "partial", "Pre Calculus", { { "calculus", "Calculus" } } },
	{ "partial", "Precalc/Trig", { { NULL } } },
	{ "partial", "Health/Wellness/Fitness", { { NULL } } },
	{ "partial",
	  "Health/Wellness/Fitness/Nutrition/Recreation/Sport",
	  { { NULL } } },
	{ "partial",
	  "Consumer Price Index (CPI",
	  { { "consumer_price_index", "Consumer Price Index" } } },
	{ "partial",
	  "Consumer Price Index CPI",
	  { { "consumer_price_index", "Consumer Price Index" } } },
	{ "partial", "Math Lab Remedial", { { "math_lab", "Math Lab" } } },
	{ "partial", "Intro to Logic Part 1", { { "logic", "Logic" } } },
	/* Typographic dashes fold to a hyphen on both sides. */
	{ "dashes", "Pre–Calculus", { { "precalculus", "Pre–Calculus" } } },
	{ "dashes", "Pre‐Calculus", { { "precalculus", "Pre‐Calculus" } } },
	{ "dashes",
	  "Computer–Aided Manufacturing",
	  { { "computer_aided_manufacturing",
	      "Computer–Aided Manufacturing" } } },
	/* More than one synonym in a text. */
	{ "several",
	  "Well/Health/Physical Education and PE:PE",
	  { { "physical_education", "Well/Health/Physical Education" },
	    { "physical_education", "PE:PE" } } },
	{ "several",
	  "PE:PE PE:PE",
	  { { "physical_education", "PE:PE" },
	    { "physical_education", "PE:PE" } } },
	{ "several",
	  "ANAT/PHYS, Calc (Honors) and Pre-Calc/Trig",
	  { { "anatomy_physiology", "ANAT/PHYS" },
	    { "calculus", "Calc (Honors)" },
	    { "precalculus", "Pre-Calc/Trig" } } },
	{ "several",
	  "Spanish I/II then Women’s Studies then Driver's Ed/Safety",
	  { { "spanish", "Spanish I/II" },
	    { "womens_studies", "Women’s Studies" },
	    { "drivers_education", "Driver's Ed/Safety" } } },
	{ "several",
	  "Math Lab [Remedial] (Studio Art #2)",
	  { { "math_lab", "Math Lab [Remedial]" },
	    { "studio_art", "Studio Art #2" } } },
	{ "several",
	  "U.S. History, A&P and Human Growth & Development",
	  { { "us_history", "U.S. History" },
	    { "anatomy_physiology", "A&P" },
	    { "human_growth", "Human Growth & Development" } } },
	/* A longer synonym wins over the label inside it. */
	{ "longest",
	  "Consumer Price Index (CPI)",
	  { { "consumer_price_index", "Consumer Price Index (CPI)" } } },
	{ "longest",
	  "Math Lab [Remedial]",
	  { { "math_lab", "Math Lab [Remedial]" } } },
	{ "longest",
	  "Intro to Logic: Part 1",
	  { { "logic", "Intro to Logic: Part 1" } } },
	{ "longest",
	  "Health/Wellness/Fitness/Nutrition/Recreation/Sport Science",
	  { { "health_science",
	      "Health/Wellness/Fitness/Nutrition/Recreation/Sport Science" } } },
	{ "longest",
	  "Health / Wellness / Fitness / Nutrition / Recreation / Sport Science",
	  { { "health_science",
	      "Health / Wellness / Fitness / Nutrition / Recreation / Sport Science" } } },
	/*
	 * "Arts/Crafts" and "Arts / Crafts" tokenize alike. The class that
	 * wrote the spaced form matched this text before the change and keeps
	 * it.
	 */
	{ "collision", "Arts/Crafts", { { "craft_workshop", "Arts/Crafts" } } },
	{ "collision",
	  "Arts / Crafts",
	  { { "craft_workshop", "Arts / Crafts" } } },
	/* Two classes list the same synonym: the first declared wins. */
	{ "collision",
	  "Music/Theater",
	  { { "music_theater", "Music/Theater" } } },
	{ "collision",
	  "Music / Theater",
	  { { "music_theater", "Music / Theater" } } },
	{ "collision", "Theater Arts", { { "theater_arts", "Theater Arts" } } },
};

static void explicit(const Load *loads, int load_count)
{
	for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
		J *expected = expect(cases[i].text, cases[i].matches);
		for (int l = 0; l < load_count; l++) {
			J *actual = parse(loads[l].engine, cases[i].text);
			Buf label = { 0 };
			buf_put(&label, loads[l].name);
			buf_put(&label, ": ");
			buf_put(&label, cases[i].text);
			check(cases[i].category, label.p, actual, expected);
			free(label.p);
			DEL(actual);
		}
		DEL(expected);
	}
}

/* ---------------------------------------------------------------------- */
/* Caller-supplied tokens reach the same alias.                            */

static void prepared(const Load *loads, int load_count)
{
	static const char *parts[][8] = {
		{ "Well", "/", "Health", "/", "Physical ", "Education" },
		{ "Well ", "/ ", "Health ", "/ ", "Physical ", "Education" },
		{ "PE", ":", "PE" },
		{ "Calc ", "(", "Honors", ")" },
		{ "Pre", "-", "Calculus" },
	};
	static const char *canons[] = { "physical_education",
					"physical_education",
					"physical_education", "calculus",
					"precalculus" };
	for (size_t i = 0; i < sizeof(parts) / sizeof(*parts); i++) {
		J *tokens = ARR();
		Buf all = { 0 };
		int x = 0;
		for (int j = 0; j < 8 && parts[i][j]; j++) {
			J *t = OBJ();
			char id[16], *normal = norm(parts[i][j], 1, 0);
			int n = (int)ulen(parts[i][j]);
			snprintf(id, sizeof(id), "t%d", j);
			PUT(t, "id", STR(id));
			PUT(t, "x", NUM(x));
			PUT(t, "y", NUM(x + n));
			PUT(t, "text", STR(parts[i][j]));
			PUT(t, "normal", STR(normal));
			ADD(tokens, t);
			buf_put(&all, parts[i][j]);
			x += n;
			free(normal);
		}
		char *surface = norm(all.p, 0, 0);
		J *expected = ARR();
		ADD(expected, entity(canons[i], surface, 0, x));
		for (int l = 0; l < load_count; l++) {
			J *q = OBJ();
			PUT(q, "op", STR("parse_tokens"));
			PUT(q, "tokens", DUP(tokens));
			J *result = request(loads[l].engine, q),
			  *actual = result ? found(result) : NIL();
			Buf label = { 0 };
			buf_put(&label, loads[l].name);
			buf_put(&label, ": ");
			buf_put(&label, surface);
			check("prepared", label.p, actual, expected);
			free(label.p);
			DEL(actual);
			DEL(result);
		}
		DEL(expected);
		DEL(tokens);
		free(surface);
		free(all.p);
	}
}

/* ---------------------------------------------------------------------- */
/* The views still hold each synonym as written.                           */

static void views(mc_engine *e)
{
	J *lookup = test_call(
		e,
		"{\"op\":\"query\",\"method\":\"lookup\",\"args\":[],\"kwargs\":{}}");
	int written = 0, tokenized = 0;
	EACH(group, lookup) {
		written += contains(group, "well/health/physical education");
		tokenized +=
			contains(group, "well / health / physical education");
	}
	J *a = NUM(written), *b = NUM(1);
	check("views", "lookup holds the written synonym", a, b);
	DEL(a);
	DEL(b);
	a = NUM(tokenized);
	b = NUM(0);
	check("views", "lookup holds no tokenized synonym", a, b);
	DEL(a);
	DEL(b);
	DEL(lookup);
	J *variants = test_call(
		e,
		"{\"op\":\"query\",\"method\":\"find_variants\",\"args\":[\"physical_education\"],\"kwargs\":{}}");
	a = BOOL(contains(variants, "well/health/physical education") &&
		 contains(variants, "pe:pe") &&
		 !contains(variants, "well / health / physical education") &&
		 !contains(variants, "pe : pe"));
	b = BOOL(1);
	check("views", "find_variants holds the written synonyms only", a, b);
	DEL(a);
	DEL(b);
	DEL(variants);
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
	Load loads[] = { { "owl", owl },
			 { "data", data },
			 { "snapshot", snapshot } };
	int load_count = (int)(sizeof(loads) / sizeof(*loads));
	keys();
	generated(loads, load_count);
	explicit(loads, load_count);
	prepared(loads, load_count);
	views(owl);
	for (int i = 0; i < category_count; i++)
		printf("%-14s %5d\n", categories[i].name, categories[i].cases);
	printf("Punctuated synonyms: %d/%d passed.\n", assertions - failures,
	       assertions);
	mc_destroy(owl);
	mc_destroy(data);
	mc_destroy(snapshot);
	return failures ? 1 : 0;
}
