/*
 * test_span_distance.c - Span rules hold every content word within distance.
 *
 * Loads tests/fixtures/ontologies/span-distance-test.ttl at distances 0 to 8
 * through an OWL load, the data interface and a reloaded snapshot, and loads
 * authored span rules through prepared snapshots. Most families build each
 * text token by token, so the expected entities follow from where the words
 * were put. Texts with repeated or shuffled words are checked against an
 * oracle that tries every choice of occurrences and shares no code with the
 * engine's search. A hand-worked table pins literal offsets, and two
 * metamorphic families check that a larger distance never loses a match and
 * that leading filler only shifts offsets. craigtrim/mutatoc#9
 */

#include "testlib.h"
#include <ctype.h>

#define FIXTURE "tests/fixtures/ontologies/span-distance-test.ttl"
#define MAX_DISTANCE 8
#define LOADS 3

/* ---------------------------------------------------------------------- */
/* Bookkeeping.                                                           */

typedef struct {
	const char *name;
	int cases, failed;
} Category;
static Category categories[32];
static int category_count, case_total, assertions, failures;

static Category *category_of(const char *name)
{
	for (int i = 0; i < category_count; i++)
		if (!strcmp(categories[i].name, name))
			return &categories[i];
	categories[category_count].name = name;
	return &categories[category_count++];
}

/* One distinct input: a text under one ontology and distance. */
static void count_case(const char *name)
{
	case_total++;
	category_of(name)->cases++;
}

/* Compares one result and, on a failure, prints the input as JSON. */
static void check(const char *category, const char *where, const char *text,
		  const J *actual, const J *expected)
{
	assertions++;
	if (cJSON_Compare(actual, expected, 1))
		return;
	category_of(category)->failed++;
	if (++failures > 40)
		return;
	J *quoted = STR(text);
	char *q = cJSON_PrintUnformatted(quoted);
	Buf b = { 0 };
	buf_put(&b, category);
	buf_put(&b, " | ");
	buf_put(&b, where);
	buf_put(&b, " | ");
	buf_add(&b, q, strlen(q) > 400 ? 400 : strlen(q));
	test_print_diff(b.p, expected, actual);
	free(b.p);
	free(q);
	DEL(quoted);
}

static uint32_t seed = 0x9e3779b9u;

/* A deterministic draw below n. */
static int rnd(int n)
{
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;
	return (int)(seed % (uint32_t)n);
}

/* ---------------------------------------------------------------------- */
/* Texts built token by token.                                            */

/* Every separator here is one token; separator_tokens() pins that. */
static const struct {
	const char *text, *normal;
	int space;
} separators[] = {
	{ ", ", ",", 0 },  { "; ", ";", 0 },  { ": ", ":", 0 },
	{ ". ", ".", 0 },  { " - ", "-", 0 }, { " / ", "/", 0 },
	{ "-", "-", 0 },   { "/", "/", 0 },   { "  ", "", 1 },
	{ "   ", "", 1 },  { "\t", "", 1 },   { "\t\t", "", 1 },
	{ " \t", "", 1 },  { "\t ", "", 1 },  { "\n", "", 1 },
	{ "\r\n", "", 1 }, { "\n\n", "", 1 }, { "\r\n\r\n", "", 1 },
};
#define SEPARATORS ((int)(sizeof(separators) / sizeof(*separators)))
enum { SEP_COMMA = 0, SEP_TAB = 10, SEP_LF = 14, SEP_CRLF = 15 };

static const char *fillers[] = {
	"amber",   "birch",  "cobalt", "dune",	 "ember",  "fjord",
	"garnet",  "hazel",  "iris",   "jade",	 "kelp",   "lilac",
	"maple",   "nectar", "onyx",   "quill",	 "raven",  "sage",
	"thistle", "umber",  "velvet", "willow", "yarrow", "zinnia",
};
#define FILLERS ((int)(sizeof(fillers) / sizeof(*fillers)))

/* A text with the token each piece becomes and its offsets. */
typedef struct {
	Buf text;
	int n, cap, word, filler;
	char **normal;
	int *x, *y, *space;
} Text;

static void text_push(Text *t, const char *piece, const char *normal, int space)
{
	if (t->n == t->cap) {
		t->cap = t->cap ? t->cap * 2 : 32;
		t->normal =
			realloc(t->normal, (size_t)t->cap * sizeof(*t->normal));
		t->x = realloc(t->x, (size_t)t->cap * sizeof(*t->x));
		t->y = realloc(t->y, (size_t)t->cap * sizeof(*t->y));
		t->space =
			realloc(t->space, (size_t)t->cap * sizeof(*t->space));
	}
	t->x[t->n] = (int)t->text.n;
	buf_put(&t->text, piece);
	t->y[t->n] = (int)t->text.n;
	t->normal[t->n] = copy(normal);
	t->space[t->n++] = space;
}

/* A word, joined by one space to a word before it. */
static void text_word(Text *t, const char *word)
{
	char normal[64];
	size_t i = 0;
	for (; word[i] && i + 1 < sizeof(normal); i++)
		normal[i] = (char)tolower((unsigned char)word[i]);
	normal[i] = 0;
	if (t->word)
		buf_put(&t->text, " ");
	text_push(t, word, normal, 0);
	t->word = 1;
}

static void text_sep(Text *t, int sep)
{
	text_push(t, separators[sep].text, separators[sep].normal,
		  separators[sep].space);
	t->word = 0;
}

/* The next filler word; a text never repeats one before it runs out. */
static void text_filler(Text *t)
{
	text_word(t, fillers[t->filler++ % FILLERS]);
}

static const char *text_of(const Text *t)
{
	return t->text.p ? t->text.p : "";
}

static void text_free(Text *t)
{
	for (int i = 0; i < t->n; i++)
		free(t->normal[i]);
	free(t->normal);
	free(t->x);
	free(t->y);
	free(t->space);
	free(t->text.p);
	memset(t, 0, sizeof(*t));
}

/* Writes word in one of four letter cases: lower, Title, UPPER, aLtErNaTe. */
static const char *styled(char *out, size_t size, const char *word, int style)
{
	size_t i = 0;
	for (; word[i] && i + 1 < size; i++) {
		int c = (unsigned char)word[i];
		int up = style == 2 || (style == 1 && !i) ||
			 (style == 3 && i % 2);
		out[i] = (char)(up ? toupper(c) : tolower(c));
	}
	out[i] = 0;
	return out;
}

static void text_styled(Text *t, const char *word, int style)
{
	char buf[64];
	text_word(t, styled(buf, sizeof(buf), word, style));
}

/* ---------------------------------------------------------------------- */
/* The fixture's labels and the rules an oracle derives from them.        */

typedef struct {
	const char *canon;
	const char *label[8]; /* the words an exact match spells, in order */
	const char *rule[8]; /* the words the span rule requires */
} Label;
enum { GLACIER, MARINE, ORBITAL, NORDIC, COPPER, OFFICE, CERAMICS };
static const Label labels[] = {
	{ "glacier_survey", { "glacier", "survey" }, { "glacier", "survey" } },
	{ "marine_pottery_workshop",
	  { "marine", "pottery", "workshop" },
	  { "marine", "pottery", "workshop" } },
	{ "orbital_weaving_canvas_studio",
	  { "orbital", "weaving", "canvas", "studio" },
	  { "orbital", "weaving", "canvas", "studio" } },
	{ "nordic_choral_ferry_garden_clinic",
	  { "nordic", "choral", "ferry", "garden", "clinic" },
	  { "nordic", "choral", "ferry", "garden", "clinic" } },
	{ "copper_harbor_violin_repair_guild_annex",
	  { "copper", "harbor", "violin", "repair", "guild", "annex" },
	  { "copper", "harbor", "violin", "repair", "guild", "annex" } },
	{ "office_of_tidal_records",
	  { "office", "of", "tidal", "records" },
	  { "office", "tidal", "records" } },
	{ "ceramics_and_kiln_safety",
	  { "ceramics", "and", "kiln", "safety" },
	  { "ceramics", "kiln", "safety" } },
};

static int count_words(const char *const *w)
{
	int n = 0;
	while (n < 8 && w[n])
		n++;
	return n;
}

/* Span rules authored straight into a prepared snapshot. */
typedef struct {
	const char *canon, *key, *content[4], *context;
} Authored;
static const Authored authored[] = {
	{ "north_ox", "north", { "ox" }, NULL },
	{ "beacon_fig_pier", "beacon", { "fig", "pier" }, NULL },
	{ "quartz_elk_mesa_tundra",
	  "quartz",
	  { "elk", "mesa", "tundra" },
	  NULL },
	{ "bay_cat_ant", "bay", { "cat", "ant" }, NULL },
	{ "violet_slip_dock", "violet", { "dock", "slip" }, "permit" },
};
#define AUTHORED ((int)(sizeof(authored) / sizeof(*authored)))
enum { VIOLET = 4 };

typedef struct {
	const char *canon;
	const char *label[8]; /* the exact phrase; words is 0 when none */
	int words;
	const char *content[8]; /* sorted by length, then bytes */
	int k, distance, forward, reverse;
	const char *context;
} Rule;

static void sort_content(Rule *r)
{
	for (int i = 1; i < r->k; i++)
		for (int j = i; j > 0; j--) {
			const char *a = r->content[j - 1], *b = r->content[j];
			size_t la = strlen(a), lb = strlen(b);
			if (la < lb || (la == lb && strcmp(a, b) <= 0))
				break;
			r->content[j - 1] = b;
			r->content[j] = a;
		}
}

static Rule label_rule(int label, int distance)
{
	const Label *l = &labels[label];
	Rule r = { 0 };
	r.canon = l->canon;
	r.words = count_words(l->label);
	for (int i = 0; i < r.words; i++)
		r.label[i] = l->label[i];
	r.k = count_words(l->rule);
	for (int i = 0; i < r.k; i++)
		r.content[i] = l->rule[i];
	sort_content(&r);
	r.distance = distance;
	r.forward = r.reverse = 1;
	return r;
}

static Rule authored_rule(int a, int forward, int reverse, int distance)
{
	Rule r = { 0 };
	r.canon = authored[a].canon;
	r.content[r.k++] = authored[a].key;
	for (int i = 0; i < 4 && authored[a].content[i]; i++)
		r.content[r.k++] = authored[a].content[i];
	sort_content(&r);
	r.distance = distance;
	r.forward = forward;
	r.reverse = reverse;
	r.context = authored[a].context;
	return r;
}

/* The words of an authored rule, key first. */
static int authored_words(int a, const char **out)
{
	int k = 0;
	out[k++] = authored[a].key;
	for (int i = 0; i < 4 && authored[a].content[i]; i++)
		out[k++] = authored[a].content[i];
	return k;
}

/* ---------------------------------------------------------------------- */
/* The oracle.                                                            */

static J *entity(const char *canon, const char *type, int x, int y)
{
	J *e = OBJ();
	PUT(e, "canon", STR(canon));
	PUT(e, "type", STR(type));
	PUT(e, "x", NUM(x));
	PUT(e, "y", NUM(y));
	return e;
}

typedef struct {
	const char *normal, *canon, *type;
	int x, y, space;
} Piece;

typedef struct {
	Piece *s;
	int n;
} Stream;

static void collapse_slots(Stream *st, int lo, int hi, const char *canon,
			   const char *type)
{
	Piece merged = { canon, canon, type, st->s[lo].x, st->s[hi].y, 0 };
	st->s[lo] = merged;
	memmove(st->s + lo + 1, st->s + hi + 1,
		(size_t)(st->n - hi - 1) * sizeof(*st->s));
	st->n -= hi - lo;
}

/* Collapses each spelling of the label; whitespace-only tokens inside it do not interrupt it. */
static void oracle_exact(Stream *st, const Rule *r)
{
	if (!r->words)
		return;
	for (int i = 0; i < st->n; i++) {
		if (st->s[i].canon || strcmp(st->s[i].normal, r->label[0]))
			continue;
		int j = i, w = 1;
		for (; w < r->words; w++) {
			j++;
			while (j < st->n && st->s[j].space)
				j++;
			if (j >= st->n || st->s[j].canon ||
			    strcmp(st->s[j].normal, r->label[w]))
				break;
		}
		if (w == r->words)
			collapse_slots(st, i, j, r->canon, "exact");
	}
}

typedef struct {
	const Rule *r;
	int *at[8], count[8], chosen[8];
	int found, lo, hi;
} Search;

/* Tries every choice of one occurrence per content word. */
static void search(Search *q, int c)
{
	const Rule *r = q->r;
	if (c < r->k) {
		for (int i = 0; i < q->count[c]; i++) {
			q->chosen[c] = q->at[c][i];
			search(q, c + 1);
		}
		return;
	}
	int lo = q->chosen[0], hi = q->chosen[0];
	for (int i = 1; i < r->k; i++) {
		if (q->chosen[i] < lo)
			lo = q->chosen[i];
		if (q->chosen[i] > hi)
			hi = q->chosen[i];
	}
	if (r->k > 1 && strcmp(r->content[0], r->content[r->k - 1])) {
		int delta = q->chosen[0] - q->chosen[r->k - 1];
		if ((delta < 0 && !r->reverse) || (delta > 0 && !r->forward))
			return;
	}
	if (hi - lo > r->distance)
		return;
	if (!q->found || hi - lo < q->hi - q->lo ||
	    (hi - lo == q->hi - q->lo && lo < q->lo)) {
		q->found = 1;
		q->lo = lo;
		q->hi = hi;
	}
}

static void oracle_spans(Stream *st, const Rule *r)
{
	Search q = { 0 };
	q.r = r;
	long combinations = 1;
	int ok = 1, context = !r->context;
	for (int i = 0; i < st->n; i++)
		if (r->context && !strcmp(st->s[i].normal, r->context))
			context = 1;
	for (int c = 0; c < r->k; c++) {
		q.at[c] = malloc((size_t)(st->n ? st->n : 1) * sizeof(int));
		for (int i = 0; i < st->n; i++)
			if (!st->s[i].canon &&
			    !strcmp(st->s[i].normal, r->content[c]))
				q.at[c][q.count[c]++] = i;
		combinations *= q.count[c];
		ok = ok && q.count[c];
	}
	if (combinations > 2000000) {
		fprintf(stderr, "oracle: %ld combinations\n", combinations);
		exit(2);
	}
	if (ok && context)
		search(&q, 0);
	for (int c = 0; c < r->k; c++)
		free(q.at[c]);
	if (q.found)
		collapse_slots(st, q.lo, q.hi, r->canon, "spans");
}

enum { PARSE, SPANS_ONLY };

/*
 * The entities a parse returns: up to three sweeps of exact matching and
 * then one span each. The spans stage alone runs one span search.
 */
static J *oracle(const Text *t, const Rule *r, int mode)
{
	Stream st = { calloc((size_t)t->n + 1, sizeof(Piece)), t->n };
	for (int i = 0; i < t->n; i++)
		st.s[i] = (Piece){ t->normal[i], NULL,	  NULL,
				   t->x[i],	 t->y[i], t->space[i] };
	for (int sweep = 0; sweep < (mode == PARSE ? 3 : 1); sweep++) {
		if (mode == PARSE)
			oracle_exact(&st, r);
		oracle_spans(&st, r);
	}
	J *out = ARR();
	for (int i = 0; i < st.n; i++)
		if (st.s[i].canon)
			ADD(out, entity(st.s[i].canon, st.s[i].type, st.s[i].x,
					st.s[i].y));
	free(st.s);
	return out;
}

/* ---------------------------------------------------------------------- */
/* Engines.                                                               */

static mc_engine *engines[MAX_DISTANCE + 1][LOADS];
static const char *load_names[LOADS] = { "owl", "data", "snapshot" };
/* Authored rules by flags (both, forward only, reverse only, neither) and distance. */
static mc_engine *directed[4][3];
static const int flag_forward[4] = { 1, 1, 0, 0 },
		 flag_reverse[4] = { 1, 0, 1, 0 },
		 directed_distance[3] = { 2, 4, 6 };

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

static mc_engine *load_with(J *q)
{
	mc_engine *e = mc_create();
	J *r = request(e, q);
	if (!r) {
		mc_destroy(e);
		return NULL;
	}
	DEL(r);
	return e;
}

static mc_engine *load_fixture(const char *path, int distance, int data)
{
	J *q = OBJ();
	PUT(q, "op", STR("load"));
	PUT(q, "path", STR(path));
	PUT(q, "distance", NUM(distance));
	if (data) {
		PUT(q, "interface", STR("data"));
		PUT(q, "class_based", BOOL(1));
	}
	return load_with(q);
}

static mc_engine *load_snapshot(J *snapshot)
{
	J *q = OBJ();
	PUT(q, "op", STR("load"));
	PUT(q, "name", STR("span-distance-test"));
	PUT(q, "snapshot", snapshot);
	return load_with(q);
}

static J *snapshot_of(mc_engine *e)
{
	J *q = OBJ();
	PUT(q, "op", STR("snapshot"));
	return request(e, q);
}

/* A copy of base whose span rules are the authored ones, all with these flags. */
static J *authored_snapshot(const J *base, int forward, int reverse,
			    int distance)
{
	J *s = DUP(base), *spans = OBJ();
	for (int a = 0; a < AUTHORED; a++) {
		J *rule = OBJ(), *content = ARR(), *list = ARR();
		for (int i = 0; i < 4 && authored[a].content[i]; i++)
			ADD(content, STR(authored[a].content[i]));
		PUT(rule, "content", content);
		PUT(rule, "distance", NUM(distance));
		PUT(rule, "forward", BOOL(forward));
		PUT(rule, "reverse", BOOL(reverse));
		PUT(rule, "canon", STR(authored[a].canon));
		if (authored[a].context) {
			J *context = ARR();
			ADD(context, STR(authored[a].context));
			PUT(rule, "context", context);
		}
		ADD(list, rule);
		PUT(spans, authored[a].key, list);
	}
	cJSON_ReplaceItemInObjectCaseSensitive(s, "spans", spans);
	return s;
}

/* Every matched token: canon, match type and offsets. */
static J *found(const J *tokens)
{
	J *out = ARR();
	EACH(t, tokens) {
		J *swaps = GET(t, "swaps");
		if (swaps)
			ADD(out,
			    entity(S(GET(swaps, "canon")),
				   S(GET(swaps, "type")), GET(t, "x")->valueint,
				   GET(t, "y")->valueint));
	}
	return out;
}

static J *parse(mc_engine *e, const char *text)
{
	J *q = OBJ();
	PUT(q, "op", STR("parse"));
	PUT(q, "text", STR(text));
	J *r = request(e, q), *out = r ? found(GET(r, "tokens")) : NIL();
	DEL(r);
	return out;
}

/* One case: the same text at one distance on every load path. */
static void expect_text(const char *category, int distance, const char *text,
			const J *expected)
{
	count_case(category);
	for (int l = 0; l < LOADS; l++) {
		char where[32];
		snprintf(where, sizeof(where), "%s d=%d", load_names[l],
			 distance);
		J *actual = parse(engines[distance][l], text);
		check(category, where, text, actual, expected);
		DEL(actual);
	}
}

static void expect_oracle(const char *category, int label, int distance,
			  const Text *t)
{
	Rule r = label_rule(label, distance);
	J *expected = oracle(t, &r, PARSE);
	expect_text(category, distance, text_of(t), expected);
	DEL(expected);
}

/* One entity over tokens first to last. */
static J *covering(const Text *t, int first, int last, const char *canon,
		   const char *type)
{
	J *a = ARR();
	ADD(a, entity(canon, type, t->x[first], t->y[last]));
	return a;
}

/* Steps p to the next permutation in lexical order; 0 after the last. */
static int next_permutation(int *p, int k)
{
	int i = k - 2;
	while (i >= 0 && p[i] >= p[i + 1])
		i--;
	if (i < 0)
		return 0;
	int j = k - 1;
	while (p[j] <= p[i])
		j--;
	int s = p[i];
	p[i] = p[j];
	p[j] = s;
	for (int a = i + 1, b = k - 1; a < b; a++, b--) {
		s = p[a];
		p[a] = p[b];
		p[b] = s;
	}
	return 1;
}

static void identity(int *p, int k)
{
	for (int i = 0; i < k; i++)
		p[i] = i;
}

static int is_identity(const int *p, int k)
{
	for (int i = 0; i < k; i++)
		if (p[i] != i)
			return 0;
	return 1;
}

/* ---------------------------------------------------------------------- */
/* Hand-worked cases with literal offsets.                                */

typedef struct {
	int distance;
	const char *text;
	struct {
		const char *canon, *type;
		int x, y;
	} want[3];
} Literal;

static const Literal accepted[] = {
	{ 4,
	  "Marine Pottery Workshop",
	  { { "marine_pottery_workshop", "exact", 0, 23 } } },
	{ 4,
	  "marine amber pottery birch workshop",
	  { { "marine_pottery_workshop", "spans", 0, 35 } } },
	/* The distance is inclusive: a spread of 4 matches, 5 does not. */
	{ 4,
	  "marine amber birch pottery workshop",
	  { { "marine_pottery_workshop", "spans", 0, 35 } } },
	{ 4, "marine amber birch cobalt pottery workshop", { { NULL } } },
	/* The word between the shortest and longest is bounded too. */
	{ 4,
	  "pottery amber birch cobalt dune ember fjord marine workshop",
	  { { NULL } } },
	{ 8,
	  "pottery amber birch cobalt dune ember fjord marine workshop",
	  { { "marine_pottery_workshop", "spans", 0, 59 } } },
	/* A far repeat of a word does not hide the near cluster. */
	{ 4,
	  "workshop marine pottery amber birch cobalt dune ember fjord workshop",
	  { { "marine_pottery_workshop", "spans", 0, 23 } } },
	/* Two windows of spread 2 share "workshop"; the leftmost wins. */
	{ 4,
	  "pottery marine workshop pottery",
	  { { "marine_pottery_workshop", "spans", 0, 23 } } },
	/* A tighter window later beats a looser one sharing its words. */
	{ 4,
	  "marine pottery amber workshop marine pottery",
	  { { "marine_pottery_workshop", "spans", 21, 44 } } },
	/* Punctuation and extra whitespace each take a position. */
	{ 4,
	  "marine, pottery, workshop",
	  { { "marine_pottery_workshop", "spans", 0, 25 } } },
	{ 4, "marine, amber, pottery, workshop", { { NULL } } },
	{ 4,
	  "marine amber pottery workshop",
	  { { "marine_pottery_workshop", "spans", 0, 29 } } },
	{ 4, "marine  amber  pottery workshop", { { NULL } } },
	{ 4, "marine\tamber\tpottery workshop", { { NULL } } },
	/* Whitespace never interrupts an exact phrase. */
	{ 4,
	  "marine\r\npottery\r\nworkshop",
	  { { "marine_pottery_workshop", "exact", 0, 25 } } },
	{ 4,
	  "Marine Pottery\r\nWorkshop",
	  { { "marine_pottery_workshop", "exact", 0, 24 } } },
	/* Items of a pasted list sit a tab, a count and a break apart. */
	{ 4, "Marine Biology\t12\r\nPottery Workshop Basics\t30", { { NULL } } },
	{ 4, "Marine Pottery\t12\r\nWorkshop Basics\t30", { { NULL } } },
	{ 8,
	  "Marine Pottery\t12\r\nWorkshop Basics\t30",
	  { { "marine_pottery_workshop", "spans", 0, 27 } } },
	/* Six words cannot fit a distance of 4 unless spelled exactly. */
	{ 4, "annex guild repair violin harbor copper", { { NULL } } },
	{ 5,
	  "annex guild repair violin harbor copper",
	  { { "copper_harbor_violin_repair_guild_annex", "spans", 0, 39 } } },
	{ 4,
	  "clinic garden ferry choral nordic",
	  { { "nordic_choral_ferry_garden_clinic", "spans", 0, 33 } } },
	{ 4,
	  "Nordic Choral Ferry Garden Clinic",
	  { { "nordic_choral_ferry_garden_clinic", "exact", 0, 33 } } },
	{ 4, "nordic choral ferry garden amber clinic", { { NULL } } },
	{ 4,
	  "orbital studio canvas weaving",
	  { { "orbital_weaving_canvas_studio", "spans", 0, 29 } } },
	{ 4,
	  "weaving amber orbital studio canvas",
	  { { "orbital_weaving_canvas_studio", "spans", 0, 35 } } },
	/* A stopword is not required, but takes a position when present. */
	{ 4,
	  "office tidal records",
	  { { "office_of_tidal_records", "spans", 0, 20 } } },
	{ 4,
	  "Office of Tidal Records",
	  { { "office_of_tidal_records", "exact", 0, 23 } } },
	{ 4,
	  "records of the tidal office",
	  { { "office_of_tidal_records", "spans", 0, 27 } } },
	{ 4, "records of the old tidal office", { { NULL } } },
	{ 4,
	  "ceramics, kiln and safety",
	  { { "ceramics_and_kiln_safety", "spans", 0, 25 } } },
	{ 4,
	  "Ceramics and Kiln Safety",
	  { { "ceramics_and_kiln_safety", "exact", 0, 24 } } },
	/* The three-word rule outranks the two-word rule inside it. */
	{ 4,
	  "applied amber harmonic birch analysis",
	  { { "applied_harmonic_analysis", "spans", 0, 37 } } },
	{ 4,
	  "analysis amber harmonic birch cobalt applied",
	  { { "harmonic_analysis", "spans", 0, 23 } } },
	{ 4,
	  "applied harmonic amber birch cobalt dune ember analysis",
	  { { NULL } } },
	/* An exact entity in the gap is one position. */
	{ 4,
	  "marine tea ceremony pottery workshop",
	  { { "marine_pottery_workshop", "spans", 0, 36 } } },
	{ 4,
	  "marine tea ceremony pottery tea ceremony workshop",
	  { { "marine_pottery_workshop", "spans", 0, 49 } } },
	{ 4,
	  "marine tea ceremony amber pottery tea ceremony workshop",
	  { { "tea_ceremony", "exact", 7, 19 },
	    { "tea_ceremony", "exact", 34, 46 } } },
	{ 4,
	  "MARINE amber Pottery WORKSHOP",
	  { { "marine_pottery_workshop", "spans", 0, 29 } } },
	/* The old last-occurrence lookup hid this two-word match. */
	{ 4,
	  "survey glacier amber birch cobalt dune ember survey",
	  { { "glacier_survey", "spans", 0, 14 } } },
	{ 0, "glacier amber survey", { { NULL } } },
	{ 0, "glacier survey", { { "glacier_survey", "exact", 0, 14 } } },
	{ 1, "survey glacier", { { "glacier_survey", "spans", 0, 14 } } },
	{ 1, "glacier amber survey", { { NULL } } },
	{ 4, "Glacier-Survey", { { "glacier_survey", "spans", 0, 14 } } },
	{ 4, "glacier / survey", { { "glacier_survey", "spans", 0, 16 } } },
	{ 4,
	  "glacier survey glacier survey",
	  { { "glacier_survey", "exact", 0, 14 },
	    { "glacier_survey", "exact", 15, 29 } } },
	/* The exact match goes first, then the span among what is left. */
	{ 4,
	  "survey amber glacier, birch glacier survey",
	  { { "glacier_survey", "spans", 0, 20 },
	    { "glacier_survey", "exact", 28, 42 } } },
};

static void acceptance(void)
{
	for (size_t i = 0; i < sizeof(accepted) / sizeof(*accepted); i++) {
		const Literal *c = &accepted[i];
		J *expected = ARR();
		for (int w = 0; w < 3 && c->want[w].canon; w++)
			ADD(expected, entity(c->want[w].canon, c->want[w].type,
					     c->want[w].x, c->want[w].y));
		expect_text("acceptance", c->distance, c->text, expected);
		DEL(expected);
	}
}

/* ---------------------------------------------------------------------- */
/* The issue's own ontologies and inputs.                                 */

static mc_engine *issue_engine(const char *local, const char *label, int data)
{
	char turtle[512];
	snprintf(turtle, sizeof(turtle),
		 "@prefix : <http://example.org/c#> .\n"
		 "@prefix owl: <http://www.w3.org/2002/07/owl#> .\n"
		 "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n"
		 ":%s a owl:Class ; rdfs:label \"%s\" .\n",
		 local, label);
	J *q = OBJ();
	PUT(q, "op", STR("load"));
	PUT(q, "turtle", STR(turtle));
	PUT(q, "name", STR("c"));
	if (data) {
		PUT(q, "interface", STR("data"));
		PUT(q, "class_based", BOOL(1));
	}
	return load_with(q);
}

/* "Circuit Theory", lines of filler, then "Digital Design Basics". */
static char *issue_list(int lines, const char *brk, int reversed)
{
	Buf b = { 0 };
	buf_put(&b,
		reversed ? "Digital Design Basics\t12" : "Circuit Theory\t12");
	for (int i = 1; i <= lines; i++) {
		char line[64];
		snprintf(line, sizeof(line), "%sPottery Studio %d\t12", brk, i);
		buf_put(&b, line);
	}
	buf_put(&b, brk);
	buf_put(&b,
		reversed ? "Circuit Theory\t12" : "Digital Design Basics\t12");
	return buf_take(&b);
}

static void issue(void)
{
	static const struct {
		const char *local, *label;
	} classes[] = {
		{ "Circuit_Design", "Circuit Design" },
		{ "Digital_Circuit_Design", "Digital Circuit Design" },
		{ "Advanced_Digital_Circuit_Design",
		  "Advanced Digital Circuit Design" },
	};
	mc_engine *e[3][2];
	for (int c = 0; c < 3; c++)
		for (int d = 0; d < 2; d++)
			e[c][d] = issue_engine(classes[c].local,
					       classes[c].label, d);
	/* Each %s is the issue's ten filler words. */
	const char *w = "one two three four five six seven eight nine ten";
	static const struct {
		int engine;
		const char *format, *canon, *type;
		int x, y;
	} rows[] = {
		{ 0, "Circuit %s Design", NULL, NULL, 0, 0 },
		{ 0, "Circuit one two Design", "circuit_design", "spans", 0,
		  22 },
		{ 1, "Digital Circuit Design", "digital_circuit_design",
		  "exact", 0, 22 },
		{ 1, "digital one circuit two design", "digital_circuit_design",
		  "spans", 0, 30 },
		{ 1, "Circuit %s digital design", NULL, NULL, 0, 0 },
		{ 1, "Circuit %s design digital", NULL, NULL, 0, 0 },
		{ 1, "digital design %s circuit", NULL, NULL, 0, 0 },
		{ 1, "digital %s circuit design", NULL, NULL, 0, 0 },
		{ 1, "design %s digital circuit", NULL, NULL, 0, 0 },
		{ 1, "circuit digital design %s circuit",
		  "digital_circuit_design", "spans", 0, 22 },
		{ 2, "advanced digital circuit design",
		  "advanced_digital_circuit_design", "exact", 0, 31 },
		{ 2, "circuit %s digital %s advanced design", NULL, NULL, 0,
		  0 },
	};
	for (size_t i = 0; i < sizeof(rows) / sizeof(*rows); i++) {
		char text[200];
		snprintf(text, sizeof(text), rows[i].format, w, w);
		J *expected = ARR();
		if (rows[i].canon)
			ADD(expected, entity(rows[i].canon, rows[i].type,
					     rows[i].x, rows[i].y));
		count_case("issue");
		for (int d = 0; d < 2; d++) {
			J *actual = parse(e[rows[i].engine][d], text);
			check("issue", d ? "data" : "owl", text, actual,
			      expected);
			DEL(actual);
		}
		DEL(expected);
	}
	/* The original report: one span across a pasted list. */
	static const struct {
		int lines, reversed;
		const char *brk;
	} lists[] = {
		{ 20, 0, "\r\n" }, { 20, 1, "\r\n" },  { 50, 0, "\n" },
		{ 20, 0, "\n" },   { 587, 0, "\r\n" },
	};
	for (size_t i = 0; i < sizeof(lists) / sizeof(*lists); i++) {
		char *text = issue_list(lists[i].lines, lists[i].brk,
					lists[i].reversed);
		J *expected = ARR();
		count_case("issue");
		for (int d = 0; d < 2; d++) {
			J *actual = parse(e[1][d], text);
			check("issue", d ? "data" : "owl", text, actual,
			      expected);
			DEL(actual);
		}
		DEL(expected);
		free(text);
	}
	const char *split = "Circuit Theory\nAmber Birch\nDigital Logic\n"
			    "Cobalt Dune\nDesign Basics";
	J *none = ARR();
	count_case("issue");
	for (int d = 0; d < 2; d++) {
		J *actual = parse(e[1][d], split);
		check("issue", d ? "data" : "owl", split, actual, none);
		DEL(actual);
	}
	DEL(none);
	for (int c = 0; c < 3; c++)
		for (int d = 0; d < 2; d++)
			mc_destroy(e[c][d]);
}

/* ---------------------------------------------------------------------- */
/* Every order of every label, over a grid of gaps and distances.         */

static void grid(void)
{
	static const struct {
		int label, max_gap, single, low, high, step;
	} plans[] = {
		{ GLACIER, 7, 0, 0, 8, 1 }, { MARINE, 4, 0, 0, 8, 1 },
		{ ORBITAL, 2, 0, 3, 5, 1 }, { NORDIC, 1, 1, 4, 5, 1 },
		{ COPPER, 1, 1, 4, 6, 12 },
	};
	for (size_t p = 0; p < sizeof(plans) / sizeof(*plans); p++) {
		const Label *l = &labels[plans[p].label];
		int k = count_words(l->label), perm[8], index = 0;
		identity(perm, k);
		do {
			if (index++ % plans[p].step)
				continue;
			int gaps[8] = { 0 }, vector = plans[p].single ? -1 : 0;
			for (;;) {
				if (plans[p].single) {
					memset(gaps, 0, sizeof(gaps));
					if (vector >= 0)
						gaps[vector] = 1;
				}
				Text t = { 0 };
				int sum = 0;
				for (int i = 0; i < k; i++) {
					for (int f = 0; i && f < gaps[i - 1];
					     f++)
						text_filler(&t);
					sum += i ? gaps[i - 1] : 0;
					text_styled(&t, l->label[perm[i]], 1);
				}
				int exact = is_identity(perm, k) && !sum,
				    spread = k - 1 + sum;
				for (int d = plans[p].low; d <= plans[p].high;
				     d++) {
					J *expected =
						exact ? covering(&t, 0, t.n - 1,
								 l->canon,
								 "exact") :
						spread <= d ?
							covering(&t, 0, t.n - 1,
								 l->canon,
								 "spans") :
							ARR();
					expect_text("grid", d, text_of(&t),
						    expected);
					DEL(expected);
				}
				text_free(&t);
				if (plans[p].single) {
					if (++vector == k - 1)
						break;
					continue;
				}
				int g = 0;
				while (g < k - 1 &&
				       ++gaps[g] > plans[p].max_gap)
					gaps[g++] = 0;
				if (g == k - 1)
					break;
			}
		} while (next_permutation(perm, k));
	}
}

/* ---------------------------------------------------------------------- */
/* One word far from a cluster of the rest.                               */

static void displaced(void)
{
	static const int which[] = { MARINE, ORBITAL, NORDIC };
	static const int distances[] = { 4, 8 };
	for (int w = 0; w < 3; w++) {
		const Label *l = &labels[which[w]];
		int k = count_words(l->label);
		for (int away = 0; away < k; away++)
			for (int side = 0; side < 2; side++)
				for (int reversed = 0; reversed < 2; reversed++)
					for (int f = 1; f <= 15; f++) {
						Text t = { 0 };
						if (!side) {
							text_styled(
								&t,
								l->label[away],
								1);
							for (int i = 0; i < f;
							     i++)
								text_filler(&t);
						}
						for (int i = 0; i < k; i++) {
							int at =
								reversed ?
									k - 1 - i :
									i;
							if (at != away)
								text_styled(
									&t,
									l->label[at],
									1);
						}
						if (side) {
							for (int i = 0; i < f;
							     i++)
								text_filler(&t);
							text_styled(
								&t,
								l->label[away],
								1);
						}
						for (int d = 0; d < 2; d++) {
							J *expected =
								k - 1 + f <= distances[d] ?
									covering(
										&t,
										0,
										t.n - 1,
										l->canon,
										"spans") :
									ARR();
							expect_text(
								"displaced",
								distances[d],
								text_of(&t),
								expected);
							DEL(expected);
						}
						text_free(&t);
					}
	}
}

/* ---------------------------------------------------------------------- */
/* Random texts checked against the oracle.                               */

/* Inserts value at position at of items, which holds n values. */
static void insert(int *items, int *n, int at, int value)
{
	memmove(items + at + 1, items + at, (size_t)(*n - at) * sizeof(*items));
	items[at] = value;
	(*n)++;
}

/*
 * Items are word indexes into words, or -1 for a filler. Words take a random
 * letter case.
 */
static void render_items(Text *t, const int *items, int n,
			 const char *const *words)
{
	for (int i = 0; i < n; i++)
		if (items[i] < 0)
			text_filler(t);
		else
			text_styled(t, words[items[i]], rnd(4));
}

/* A shuffled cluster plus stray copies of its words and runs of filler. */
static int repeated_items(int *items, int k)
{
	int n = 0, perm[8];
	identity(perm, k);
	for (int i = k - 1; i > 0; i--) {
		int j = rnd(i + 1), s = perm[i];
		perm[i] = perm[j];
		perm[j] = s;
	}
	for (int i = 0; i < k; i++) {
		for (int g = i ? rnd(3) : 0; g > 0; g--)
			items[n++] = -1;
		items[n++] = perm[i];
	}
	for (int s = 1 + rnd(3); s > 0; s--)
		insert(items, &n, rnd(n + 1), rnd(k));
	for (int p = rnd(4); p > 0; p--)
		for (int f = 1 + rnd(6), at = rnd(n + 1); f > 0; f--)
			insert(items, &n, at, -1);
	return n;
}

static void repeated(void)
{
	static const int which[] = { GLACIER, MARINE, ORBITAL, NORDIC, OFFICE };
	for (int c = 0; c < 1250; c++) {
		int label = which[c % 5], items[96];
		const Label *l = &labels[label];
		int n = repeated_items(items, count_words(l->rule));
		Text t = { 0 };
		t.filler = rnd(FILLERS);
		render_items(&t, items, n, l->rule);
		expect_oracle("repeated", label, 2 + rnd(5), &t);
		text_free(&t);
	}
}

/* Words of the label, fillers and separators in random order. */
static void shuffled_text(Text *t, int label, int length)
{
	const Label *l = &labels[label];
	int words = count_words(l->label);
	t->filler = rnd(FILLERS);
	for (int i = 0; i < length; i++) {
		int r = rnd(100);
		if (r < 45)
			text_styled(t, l->label[rnd(words)], rnd(4));
		else if (r < 57 && i && i < length - 1 && t->word)
			text_sep(t, rnd(SEPARATORS));
		else
			text_filler(t);
	}
}

static void shuffled(void)
{
	static const int which[] = { GLACIER, MARINE, ORBITAL,
				     NORDIC,  OFFICE, CERAMICS };
	for (int c = 0; c < 2000; c++) {
		int label = which[c % 6];
		Text t = { 0 };
		shuffled_text(&t, label, 3 + rnd(22));
		expect_oracle("shuffled", label, rnd(MAX_DISTANCE + 1), &t);
		text_free(&t);
	}
}

/* ---------------------------------------------------------------------- */
/* Punctuation and whitespace between the words.                          */

/* Each separator is one token, whatever the letter case around it. */
static void separator_tokens(void)
{
	for (int s = 0; s < SEPARATORS; s++)
		for (int style = 0; style < 2; style++) {
			Text t = { 0 };
			text_styled(&t, "amber", style);
			text_sep(&t, s);
			text_styled(&t, "birch", style);
			J *q = OBJ();
			PUT(q, "op", STR("tokenize"));
			PUT(q, "text", STR(text_of(&t)));
			J *tokens = request(engines[4][0], q);
			J *actual = ARR(), *expected = ARR();
			Buf joined = { 0 };
			EACH(token, tokens) {
				ADD(actual, STR(S(GET(token, "normal"))));
				buf_put(&joined, S(GET(token, "text")));
			}
			ADD(actual, STR(joined.p ? joined.p : ""));
			for (int i = 0; i < t.n; i++)
				ADD(expected, STR(t.normal[i]));
			ADD(expected, STR(text_of(&t)));
			count_case("separator tokens");
			check("separator tokens", "tokenize", text_of(&t),
			      actual, expected);
			free(joined.p);
			DEL(actual);
			DEL(expected);
			DEL(tokens);
			text_free(&t);
		}
}

static void separator_spans(void)
{
	static const int which[] = { GLACIER, MARINE };
	static const char *patterns[] = {
		"s", "sfs", "fsf", "sf", "fs", "sfsfs"
	};
	static const int distances[] = { 2, 4, 6 };
	for (int w = 0; w < 2; w++) {
		const Label *l = &labels[which[w]];
		int k = count_words(l->label), perm[8];
		identity(perm, k);
		do {
			for (int s = 0; s < SEPARATORS; s++)
				for (int p = 0; p < 6; p++)
					for (int d = 0; d < 3; d++) {
						Text t = { 0 };
						for (int i = 0; i < k; i++) {
							for (const char *c =
								     i ? patterns[p] :
									 "";
							     *c; c++)
								if (*c == 's')
									text_sep(
										&t,
										s);
								else
									text_filler(
										&t);
							text_styled(
								&t,
								l->label[perm[i]],
								1);
						}
						expect_oracle("separators",
							      which[w],
							      distances[d], &t);
						text_free(&t);
					}
		} while (next_permutation(perm, k));
	}
}

/* ---------------------------------------------------------------------- */
/* Pasted lists: one title per line, a tab and a count, then a break.     */

static void list_line(Text *t, const char *const *words, const int *order,
		      int n, int count, int brk, int last)
{
	for (int i = 0; i < n; i++)
		text_styled(t, words[order[i]], 1);
	if (count) {
		text_sep(t, SEP_TAB);
		text_word(t, n % 2 ? "12" : "30");
	}
	if (!last)
		text_sep(t, brk);
}

static void filler_lines(Text *t, int lines, int count, int brk)
{
	for (int i = 0; i < lines; i++) {
		text_filler(t);
		text_filler(t);
		if (count) {
			text_sep(t, SEP_TAB);
			text_word(t, "7");
		}
		text_sep(t, brk);
	}
}

static void lines(void)
{
	static const int gaps[] = { 0, 1, 2, 3, 5, 8, 13, 21 };
	static const int small[] = { 0, 1, 3 };
	static const int breaks[] = { SEP_LF, SEP_CRLF };
	static const int distances[] = { 4, 8 };
	static const int which[] = { GLACIER, MARINE };
	for (int w = 0; w < 2; w++) {
		const Label *l = &labels[which[w]];
		int k = count_words(l->label), perm[8];
		identity(perm, k);
		do {
			for (int b = 0; b < 2; b++)
				for (int count = 0; count < 2; count++)
					for (int d = 0; d < 2; d++) {
						for (int cut = 1; cut < k;
						     cut++)
							for (int g = 0; g < 8;
							     g++) {
								Text t = { 0 };
								list_line(
									&t,
									l->label,
									perm,
									cut,
									count,
									breaks[b],
									0);
								filler_lines(
									&t,
									gaps[g],
									count,
									breaks[b]);
								list_line(
									&t,
									l->label,
									perm + cut,
									k - cut,
									count,
									breaks[b],
									1);
								expect_oracle(
									"lines",
									which[w],
									distances[d],
									&t);
								text_free(&t);
							}
						for (int g1 = 0;
						     k == 3 && g1 < 3; g1++)
							for (int g2 = 0; g2 < 3;
							     g2++) {
								Text t = { 0 };
								list_line(
									&t,
									l->label,
									perm, 1,
									count,
									breaks[b],
									0);
								filler_lines(
									&t,
									small[g1],
									count,
									breaks[b]);
								list_line(
									&t,
									l->label,
									perm + 1,
									1,
									count,
									breaks[b],
									0);
								filler_lines(
									&t,
									small[g2],
									count,
									breaks[b]);
								list_line(
									&t,
									l->label,
									perm + 2,
									1,
									count,
									breaks[b],
									1);
								expect_oracle(
									"lines",
									which[w],
									distances[d],
									&t);
								text_free(&t);
							}
					}
		} while (next_permutation(perm, k));
	}
}

/* Lists as long as the reported one, with far words and a real cluster. */
static void long_lists(void)
{
	static const int sizes[] = { 50, 200, 600, 1442 };
	static const int breaks[] = { SEP_LF, SEP_CRLF };
	static const char *biology[] = { "Marine", "Biology" },
			  *basics[] = { "Pottery", "Workshop", "Basics" },
			  *cluster[] = { "Workshop", "for", "Marine",
					 "Pottery" };
	static const int order[] = { 0, 1, 2, 3 };
	for (int s = 0; s < 4; s++)
		for (int b = 0; b < 2; b++)
			for (int scenario = 0; scenario < 3; scenario++) {
				Text t = { 0 };
				int away = scenario != 1, middle = scenario > 0,
				    rows = sizes[s];
				for (int i = 0; i < rows; i++) {
					int last = i == rows - 1;
					if (away && !i)
						list_line(&t, biology, order, 2,
							  1, breaks[b], last);
					else if (away && i == rows * 2 / 3)
						list_line(&t, basics, order, 3,
							  1, breaks[b], last);
					else if (middle && i == rows / 2)
						list_line(&t, cluster, order, 4,
							  1, breaks[b], last);
					else {
						text_filler(&t);
						text_filler(&t);
						text_sep(&t, SEP_TAB);
						text_word(&t, "7");
						if (!last)
							text_sep(&t, breaks[b]);
					}
				}
				expect_oracle("long lists", MARINE, 4, &t);
				text_free(&t);
			}
}

/* ---------------------------------------------------------------------- */
/* Stopwords, competing rules and exact entities inside a gap.            */

static void stopwords(void)
{
	static const char *fills[][2] = { { NULL },    { "of" },
					  { "the" },   { "and" },
					  { "amber" }, { "of", "the" } };
	static const int which[] = { OFFICE, CERAMICS };
	for (int w = 0; w < 2; w++) {
		const Label *l = &labels[which[w]];
		int perm[3];
		identity(perm, 3);
		do {
			for (int g1 = 0; g1 < 6; g1++)
				for (int g2 = 0; g2 < 6; g2++)
					for (int d = 3; d <= 5; d++) {
						Text t = { 0 };
						const int *g[2] = { &g1, &g2 };
						for (int i = 0; i < 3; i++) {
							for (int f = 0;
							     i && f < 2 &&
							     fills[*g[i - 1]][f];
							     f++)
								text_word(
									&t,
									fills[*g[i -
										 1]]
									     [f]);
							text_styled(
								&t,
								l->rule[perm[i]],
								1);
						}
						expect_oracle("stopwords",
							      which[w], d, &t);
						text_free(&t);
					}
		} while (next_permutation(perm, 3));
	}
}

/* "Applied Harmonic Analysis" outranks "Harmonic Analysis" when it fits. */
static void rank(void)
{
	static const char *words[] = { "applied", "harmonic", "analysis" };
	static const int distances[] = { 2, 4, 6 };
	int perm[3];
	identity(perm, 3);
	do {
		for (int g1 = 0; g1 <= 5; g1++)
			for (int g2 = 0; g2 <= 5; g2++) {
				Text t = { 0 };
				int at[3];
				for (int i = 0; i < 3; i++) {
					for (int f = 0;
					     i && f < (i == 1 ? g1 : g2); f++)
						text_filler(&t);
					at[perm[i]] = t.n;
					text_styled(&t, words[perm[i]], 1);
				}
				int a = at[0], h = at[1], n = at[2];
				int lo = a < h ? (a < n ? a : n) :
						 (h < n ? h : n),
				    hi = a > h ? (a > n ? a : n) :
						 (h > n ? h : n);
				for (int d = 0; d < 3; d++) {
					J *expected;
					if (h == a + 1 && n == h + 1)
						expected = covering(
							&t, a, n,
							"applied_harmonic_analysis",
							"exact");
					else if (n == h + 1)
						expected = covering(
							&t, h, n,
							"harmonic_analysis",
							"exact");
					else if (hi - lo <= distances[d])
						expected = covering(
							&t, lo, hi,
							"applied_harmonic_analysis",
							"spans");
					else if (abs(h - n) <= distances[d])
						expected = covering(
							&t, h < n ? h : n,
							h < n ? n : h,
							"harmonic_analysis",
							"spans");
					else
						expected = ARR();
					expect_text("rank", distances[d],
						    text_of(&t), expected);
					DEL(expected);
				}
				text_free(&t);
			}
	} while (next_permutation(perm, 3));
}

/* "Tea Ceremony" matches exactly first and then takes one position. */
static void nested(void)
{
	/* Per gap: t is a tea ceremony, f a filler. */
	static const char *patterns[] = { "", "t", "f", "tt", "tf", "ft" };
	const Label *l = &labels[MARINE];
	int perm[3];
	identity(perm, 3);
	do {
		for (int g1 = 0; g1 < 6; g1++)
			for (int g2 = 0; g2 < 6; g2++) {
				Text t = { 0 };
				J *ceremonies = ARR();
				int position = 0, first = 0, last = 0, from = 0,
				    to = 0;
				for (int i = 0; i < 3; i++) {
					for (const char *c =
						     i ? patterns[i == 1 ? g1 :
									   g2] :
							 "";
					     *c; c++, position++)
						if (*c == 'f')
							text_filler(&t);
						else {
							text_word(&t, "Tea");
							text_word(&t,
								  "Ceremony");
							ADD(ceremonies,
							    entity("tea_ceremony",
								   "exact",
								   t.x[t.n - 2],
								   t.y[t.n -
								       1]));
						}
					if (!i) {
						first = t.n;
						from = position;
					}
					last = t.n;
					to = position++;
					text_styled(&t, l->label[perm[i]], 1);
				}
				for (int d = 3; d <= 5; d++) {
					J *expected;
					if (is_identity(perm, 3) && !g1 && !g2)
						expected = covering(&t, first,
								    last,
								    l->canon,
								    "exact");
					else if (to - from <= d)
						expected = covering(&t, first,
								    last,
								    l->canon,
								    "spans");
					else
						expected = DUP(ceremonies);
					expect_text("nested", d, text_of(&t),
						    expected);
					DEL(expected);
				}
				DEL(ceremonies);
				text_free(&t);
			}
	} while (next_permutation(perm, 3));
}

/* Letter case never changes a match. */
static void letter_case(void)
{
	static const int which[] = { MARINE, ORBITAL, NORDIC };
	for (int w = 0; w < 3; w++) {
		const Label *l = &labels[which[w]];
		int k = count_words(l->label), perm[8];
		/* No gap, or one gap of 1 or 4 fillers; five words take only 4. */
		int sizes[2] = { k == 5 ? 4 : 1, 4 },
		    size_count = k == 5 ? 1 : 2;
		identity(perm, k);
		do {
			for (int gap = -1; gap < k - 1; gap++)
				for (int z = 0; z < (gap < 0 ? 1 : size_count);
				     z++)
					for (int style = k == 5 ? 2 : 0;
					     style < 4; style++) {
						int size = gap < 0 ? 0 :
								     sizes[z];
						Text t = { 0 };
						for (int i = 0; i < k; i++) {
							for (int f = 0;
							     i &&
							     gap == i - 1 &&
							     f < size;
							     f++)
								text_filler(&t);
							text_styled(
								&t,
								l->label[perm[i]],
								style);
						}
						J *expected =
							is_identity(perm, k) &&
									gap < 0 ?
								covering(
									&t, 0,
									t.n - 1,
									l->canon,
									"exact") :
							k - 1 + size <= 4 ?
								covering(
									&t, 0,
									t.n - 1,
									l->canon,
									"spans") :
								ARR();
						expect_text("letter case", 4,
							    text_of(&t),
							    expected);
						DEL(expected);
						text_free(&t);
					}
		} while (next_permutation(perm, k));
	}
}

/* ---------------------------------------------------------------------- */
/* Caller-supplied tokens, through a full parse and the spans stage.       */

static J *tokens_of(const Text *t)
{
	J *a = ARR();
	for (int i = 0; i < t->n; i++) {
		J *token = OBJ();
		char id[16], *piece = slice(text_of(t) + t->x[i],
					    (size_t)(t->y[i] - t->x[i]));
		snprintf(id, sizeof(id), "t%d", i);
		PUT(token, "id", STR(id));
		PUT(token, "x", NUM(t->x[i]));
		PUT(token, "y", NUM(t->y[i]));
		PUT(token, "text", STR(piece));
		PUT(token, "normal", STR(t->normal[i]));
		ADD(a, token);
		free(piece);
	}
	return a;
}

static void prepared(void)
{
	static const int which[] = { GLACIER, MARINE, ORBITAL, NORDIC, OFFICE };
	for (int c = 0; c < 400; c++) {
		int label = which[c % 5], d = 1 + rnd(MAX_DISTANCE);
		Text t = { 0 };
		shuffled_text(&t, label, 4 + rnd(16));
		Rule r = label_rule(label, d);
		J *full = oracle(&t, &r, PARSE),
		  *stage = oracle(&t, &r, SPANS_ONLY);
		count_case("prepared parse");
		count_case("prepared spans stage");
		for (int l = 0; l < LOADS; l++) {
			char where[32];
			snprintf(where, sizeof(where), "%s d=%d", load_names[l],
				 d);
			J *q = OBJ();
			PUT(q, "op", STR("parse_tokens"));
			PUT(q, "tokens", tokens_of(&t));
			J *result = request(engines[d][l], q),
			  *actual = result ? found(GET(result, "tokens")) :
					     NIL();
			check("prepared parse", where, text_of(&t), actual,
			      full);
			DEL(actual);
			DEL(result);
			q = OBJ();
			PUT(q, "op", STR("transform_tokens"));
			PUT(q, "stage", STR("spans"));
			PUT(q, "tokens", tokens_of(&t));
			result = request(engines[d][l], q);
			actual = result ? found(result) : NIL();
			check("prepared spans stage", where, text_of(&t),
			      actual, stage);
			DEL(actual);
			DEL(result);
		}
		DEL(full);
		DEL(stage);
		text_free(&t);
	}
}

/* ---------------------------------------------------------------------- */
/* Authored rules: direction flags and context words.                      */

static void expect_directed(const char *category, int flags, int di, int a,
			    const Text *t)
{
	Rule r = authored_rule(a, flag_forward[flags], flag_reverse[flags],
			       directed_distance[di]);
	J *expected = oracle(t, &r, PARSE),
	  *actual = parse(directed[flags][di], text_of(t));
	char where[48];
	snprintf(where, sizeof(where), "forward=%d reverse=%d d=%d", r.forward,
		 r.reverse, r.distance);
	count_case(category);
	check(category, where, text_of(t), actual, expected);
	DEL(actual);
	DEL(expected);
}

static void direction(void)
{
	static const int max_gap[] = { 6, 2, 1, 2 };
	for (int a = 0; a < 4; a++) {
		const char *words[8];
		int k = authored_words(a, words), perm[8];
		identity(perm, k);
		do {
			int gaps[8] = { 0 };
			for (;;) {
				Text t = { 0 };
				for (int i = 0; i < k; i++) {
					for (int f = 0; i && f < gaps[i - 1];
					     f++)
						text_filler(&t);
					text_word(&t, words[perm[i]]);
				}
				for (int flags = 0; flags < 4; flags++)
					for (int di = 0; di < 3; di++)
						expect_directed("direction",
								flags, di, a,
								&t);
				text_free(&t);
				int g = 0;
				while (g < k - 1 && ++gaps[g] > max_gap[a])
					gaps[g++] = 0;
				if (g == k - 1)
					break;
			}
		} while (next_permutation(perm, k));
	}
	/* Repeated words: the tightest choice that keeps the order wins. */
	for (int c = 0; c < 600; c++) {
		int a = c % 4, items[96];
		const char *words[8];
		int k = authored_words(a, words);
		int n = repeated_items(items, k);
		Text t = { 0 };
		t.filler = rnd(FILLERS);
		render_items(&t, items, n, words);
		expect_directed("direction repeated", rnd(4), rnd(3), a, &t);
		text_free(&t);
	}
}

/* g filler words, the first of them "permit" when asked. */
static void context_gap(Text *t, int g, int permit)
{
	for (int f = 0; f < g; f++)
		text_word(t, permit && !f ? "permit" : fillers[f]);
}

static void context(void)
{
	/* Where "permit" goes: nowhere, first, last, inside the gap, far ahead. */
	for (int reversed = 0; reversed < 2; reversed++)
		for (int left = 0; left < 2; left++)
			for (int g = 0; g <= 6; g++)
				for (int where = 0; where < 5; where++) {
					if (where == 3 && !g)
						continue;
					Text t = { 0 };
					if (where == 1)
						text_word(&t, "permit");
					if (where == 4) {
						text_word(&t, "permit");
						for (int f = 0; f < 20; f++)
							text_filler(&t);
					}
					int first = t.n;
					if (left) {
						text_word(&t, "slip");
						context_gap(&t, g, where == 3);
					}
					text_word(&t,
						  reversed ? "dock" : "violet");
					text_word(&t,
						  reversed ? "violet" : "dock");
					if (!left) {
						context_gap(&t, g, where == 3);
						text_word(&t, "slip");
					}
					int last = t.n - 1;
					if (where == 2)
						text_word(&t, "permit");
					J *expected =
						where && g + 2 <= 4 ?
							covering(
								&t, first, last,
								"violet_slip_dock",
								"spans") :
							ARR();
					J *actual = parse(directed[0][1],
							  text_of(&t));
					count_case("context");
					check("context", "authored",
					      text_of(&t), actual, expected);
					DEL(actual);
					DEL(expected);
					text_free(&t);
				}
}

/* ---------------------------------------------------------------------- */
/* Metamorphic families.                                                  */

/* A larger distance keeps every match a smaller one found. */
static void monotone(void)
{
	static const int which[] = { GLACIER, MARINE, ORBITAL,
				     NORDIC,  COPPER, OFFICE };
	for (int c = 0; c < 300; c++) {
		int label = which[c % 6], perm[8];
		const Label *l = &labels[label];
		int k = count_words(l->rule);
		identity(perm, k);
		for (int i = k - 1; i > 0; i--) {
			int j = rnd(i + 1), s = perm[i];
			perm[i] = perm[j];
			perm[j] = s;
		}
		Text t = { 0 };
		t.filler = rnd(FILLERS);
		for (int i = 0; i < k; i++) {
			for (int f = i ? rnd(4) : 0; f > 0; f--) {
				text_filler(&t);
				if (!rnd(4))
					text_sep(&t, rnd(SEPARATORS));
			}
			text_styled(&t, l->rule[perm[i]], rnd(4));
		}
		J *results[MAX_DISTANCE + 1];
		for (int d = 0; d <= MAX_DISTANCE; d++) {
			Rule r = label_rule(label, d);
			J *expected = oracle(&t, &r, PARSE);
			char where[32];
			snprintf(where, sizeof(where), "owl d=%d", d);
			results[d] = parse(engines[d][0], text_of(&t));
			count_case("monotone");
			check("monotone", where, text_of(&t), results[d],
			      expected);
			DEL(expected);
		}
		for (int d = 0; d < MAX_DISTANCE; d++)
			if (SIZE(results[d])) {
				char where[48];
				snprintf(where, sizeof(where),
					 "owl d=%d keeps d=%d", d + 1, d);
				check("monotone", where, text_of(&t),
				      results[d + 1], results[d]);
			}
		for (int d = 0; d <= MAX_DISTANCE; d++)
			DEL(results[d]);
		text_free(&t);
	}
}

static J *shifted(const J *entities, int by)
{
	J *out = DUP(entities);
	EACH(e, out) {
		cJSON_SetNumberValue(GET(e, "x"), GET(e, "x")->valueint + by);
		cJSON_SetNumberValue(GET(e, "y"), GET(e, "y")->valueint + by);
	}
	return out;
}

/* Leading filler shifts every offset by its length; trailing filler changes nothing. */
static void shift(void)
{
	static const int which[] = { GLACIER, MARINE, ORBITAL, OFFICE };
	static const int prefixes[] = { 1, 3, 8, -1, 0 };
	for (int c = 0; c < 400; c++) {
		int label = which[c % 4], items[32], n = 0, perm[8];
		const Label *l = &labels[label];
		int k = count_words(l->rule);
		identity(perm, k);
		for (int i = k - 1; i > 0; i--) {
			int j = rnd(i + 1), s = perm[i];
			perm[i] = perm[j];
			perm[j] = s;
		}
		for (int i = 0; i < k; i++) {
			for (int g = i ? rnd(3) : 0; g > 0; g--)
				items[n++] = -1;
			items[n++] = perm[i];
		}
		int start = rnd(FILLERS);
		uint32_t keep = seed;
		Text base = { 0 };
		base.filler = start;
		render_items(&base, items, n, l->rule);
		J *before = parse(engines[4][0], text_of(&base));
		Rule r = label_rule(label, 4);
		J *construction = oracle(&base, &r, PARSE);
		count_case("shift");
		check("shift", "owl base", text_of(&base), before,
		      construction);
		DEL(construction);
		for (int p = 0; p < 5; p++) {
			Text t = { 0 };
			t.filler = start + FILLERS / 2;
			int lead = 0;
			if (prefixes[p] > 0)
				for (int f = 0; f < prefixes[p]; f++, lead++)
					text_filler(&t);
			else if (prefixes[p] < 0) {
				text_word(&t, "Amber");
				text_sep(&t, SEP_COMMA);
				lead = 2;
			}
			int saved = t.filler;
			t.filler = start;
			/* Replays the base text's letter cases. */
			seed = keep;
			render_items(&t, items, n, l->rule);
			t.filler = saved;
			J *expected;
			if (prefixes[p]) {
				expected = shifted(before, t.x[lead]);
			} else {
				for (int f = 0; f < 5; f++)
					text_filler(&t);
				expected = DUP(before);
			}
			J *actual = parse(engines[4][0], text_of(&t));
			count_case("shift");
			check("shift",
			      prefixes[p] ? "owl prefix" : "owl suffix",
			      text_of(&t), actual, expected);
			DEL(actual);
			DEL(expected);
			text_free(&t);
		}
		DEL(before);
		text_free(&base);
	}
}

/* ---------------------------------------------------------------------- */

int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	char *path = test_path(argv[1], FIXTURE);
	J *base = NULL;
	for (int d = 0; d <= MAX_DISTANCE; d++) {
		engines[d][0] = load_fixture(path, d, 0);
		engines[d][1] = load_fixture(path, d, 1);
		J *snapshot = engines[d][0] ? snapshot_of(engines[d][0]) : NULL;
		if (d == 4 && snapshot)
			base = DUP(snapshot);
		engines[d][2] = snapshot ? load_snapshot(snapshot) : NULL;
		for (int l = 0; l < LOADS; l++)
			if (!engines[d][l]) {
				fprintf(stderr, "FAIL could not load %s\n",
					FIXTURE);
				return 2;
			}
	}
	free(path);
	for (int f = 0; f < 4; f++)
		for (int di = 0; di < 3; di++) {
			directed[f][di] = load_snapshot(authored_snapshot(
				base, flag_forward[f], flag_reverse[f],
				directed_distance[di]));
			if (!directed[f][di]) {
				fprintf(stderr,
					"FAIL could not load authored rules\n");
				return 2;
			}
		}
	DEL(base);
	acceptance();
	issue();
	grid();
	displaced();
	repeated();
	shuffled();
	separator_tokens();
	separator_spans();
	lines();
	long_lists();
	stopwords();
	rank();
	nested();
	letter_case();
	prepared();
	direction();
	context();
	monotone();
	shift();
	for (int i = 0; i < category_count; i++)
		printf("%-22s %6d cases %6d failed assertions\n",
		       categories[i].name, categories[i].cases,
		       categories[i].failed);
	printf("Span distance: %d cases, %d/%d assertions passed.\n",
	       case_total, assertions - failures, assertions);
	for (int d = 0; d <= MAX_DISTANCE; d++)
		for (int l = 0; l < LOADS; l++)
			mc_destroy(engines[d][l]);
	for (int f = 0; f < 4; f++)
		for (int di = 0; di < 3; di++)
			mc_destroy(directed[f][di]);
	return failures ? 1 : 0;
}
