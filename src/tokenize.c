/*
 * tokenize.c - Native raw-text tokenizer.
 *
 * Splits plain text into the tokens the matcher reads: id, text, x, y and
 * normal. A token's text is always a slice of the input, so the tokens
 * concatenate back to it, and x and y are code point offsets into it. Only
 * normal is folded, for matching. Punctuation becomes its own token, except
 * periods and commas inside numbers, apostrophes inside words and ampersands
 * between letters. A period or comma that ends a number, and underscores that
 * open or close a word, are split off. A word keeps the one space that follows
 * it; any other run of whitespace is its own token with an empty normal.
 * craigtrim/mutatoc#1, craigtrim/mutatoc#7
 */

#include "mc.h"
#include "tokenize_data.h"
#include <inttypes.h>

static int numeric(uint32_t c)
{
	size_t l = 0, n = sizeof(numeric_ranges) / sizeof(*numeric_ranges);
	while (l < n) {
		size_t m = l + (n - l) / 2;
		if (c < numeric_ranges[m][0])
			n = m;
		else if (c > numeric_ranges[m][1])
			l = m + 1;
		else
			return 1;
	}
	return 0;
}

/*
 * Every hyphen and dash folds to -, every apostrophe and single quote to ',
 * and every double quote to ", so look-alike input matches alike.
 */
static const uint32_t dashes[] = { 0x058a, 0x1806, 0x2010, 0x2011, 0x2012,
				   0x2013, 0x2014, 0x2015, 0x2053, 0x207b,
				   0x208b, 0x2212, 0x2e3a, 0x2e3b, 0x301c,
				   0x3030, 0xfe58, 0xfe63, 0xff0d };
static const uint32_t squotes[] = { '\'',   '`',    0x00b4, 0x02bc,
				    0x2018, 0x2019, 0x201a, 0x201b,
				    0x2032, 0x2039, 0x203a, 0xff07 };
static const uint32_t dquotes[] = { '"',    0x00ab, 0x00bb, 0x201c, 0x201d,
				    0x201e, 0x201f, 0x2033, 0xff02 };

static int in_set(uint32_t c, const uint32_t *set, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (c == set[i])
			return 1;
	return 0;
}

static int apostrophe(uint32_t c)
{
	return in_set(c, squotes, sizeof(squotes) / sizeof(*squotes));
}

/* An apostrophe is never a letter, so every apostrophe splits words alike. */
static int letter(uint32_t c)
{
	return ualpha(c) && !apostrophe(c);
}

static uint32_t char_at(const char *s, size_t at)
{
	const char *p = s + at;
	return uread(&p);
}

/* The start of the code point that ends s[lo..end). */
static size_t char_start(const char *s, size_t lo, size_t end)
{
	size_t at = end > lo ? end - 1 : lo;
	while (at > lo && ((unsigned char)s[at] & 0xc0) == 0x80)
		at--;
	return at;
}

/* A byte span of the input; space marks a run of whitespace. */
typedef struct {
	size_t start, end;
	int space;
} Piece;
typedef struct {
	Piece *p;
	int n, cap;
} Pieces;

static int push(Pieces *ps, size_t start, size_t end, int space)
{
	if (ps->n == ps->cap) {
		int cap = ps->cap ? ps->cap * 2 : 64;
		Piece *grown = realloc(ps->p, (size_t)cap * sizeof(*grown));
		if (!grown)
			return 0;
		ps->p = grown;
		ps->cap = cap;
	}
	ps->p[ps->n++] = (Piece){ start, end, space };
	return 1;
}

/*
 * Underscores that open or close a piece, and a period or comma that ends a
 * number, become their own pieces.
 */
static int edges(Pieces *ps, const char *s, size_t a, size_t b)
{
	size_t lead = a, trail = b;
	while (trail - lead > 1 && s[lead] == '_')
		lead++;
	while (trail - lead > 1 && s[trail - 1] == '_')
		trail--;
	size_t middle = trail;
	if (trail - lead > 1 && (s[trail - 1] == '.' || s[trail - 1] == ',') &&
	    numeric(char_at(s, char_start(s, lead, trail - 1))))
		middle = trail - 1;
	for (size_t i = a; i < lead; i++)
		if (!push(ps, i, i + 1, 0))
			return 0;
	if (!push(ps, lead, middle, 0) ||
	    (middle < trail && !push(ps, middle, trail, 0)))
		return 0;
	for (size_t i = trail; i < b; i++)
		if (!push(ps, i, i + 1, 0))
			return 0;
	return 1;
}

/* Splits one word, s[start..end) with no whitespace, at its punctuation. */
static int split_word(Pieces *ps, const char *s, size_t start, size_t end)
{
	const char *p = s + start;
	uint32_t prev = 0;
	size_t open = start;
	int building = 0;
	while ((size_t)(p - s) < end) {
		size_t at = (size_t)(p - s);
		uint32_t ch = uread(&p);
		size_t after = (size_t)(p - s);
		uint32_t next = after < end ? char_at(s, after) : 0;
		/* A pair of backticks or acute accents is one double quote. */
		if ((ch == '`' || ch == 0x00b4) && next == ch) {
			if (building && !edges(ps, s, open, at))
				return 0;
			building = 0;
			uread(&p);
			if (!push(ps, at, (size_t)(p - s), 0))
				return 0;
		} else if (letter(ch) || numeric(ch) || ch == '_' ||
			   ((ch == '.' || ch == ',') && numeric(prev)) ||
			   (apostrophe(ch) && letter(prev)) ||
			   (ch == '&' && letter(prev) && letter(next))) {
			if (!building)
				open = at;
			building = 1;
		} else {
			if (building && !edges(ps, s, open, at))
				return 0;
			building = 0;
			if (!push(ps, at, after, 0))
				return 0;
		}
		prev = ch;
	}
	return !building || edges(ps, s, open, end);
}

static int scan(Pieces *ps, const char *s)
{
	const char *p = s;
	while (*p) {
		size_t start = (size_t)(p - s);
		const char *q = p;
		int space = uspace(uread(&q));
		while (*q) {
			const char *r = q;
			if (uspace(uread(&r)) != space)
				break;
			q = r;
		}
		size_t end = (size_t)(q - s);
		if (space ? !push(ps, start, end, 1) :
			    !split_word(ps, s, start, end))
			return 0;
		p = q;
	}
	return 1;
}

/* Where the apostrophe ending a word starts, or 0 when no word ends in one. */
static size_t closing(const char *s, const Piece *pc)
{
	size_t at = char_start(s, pc->start, pc->end);
	return at > pc->start && apostrophe(char_at(s, at)) &&
			       letter(char_at(s,
					      char_start(s, pc->start, at))) ?
		       at :
		       0;
}

static int lone_apostrophe(const char *s, const Piece *pc)
{
	return !pc->space && char_start(s, pc->start, pc->end) == pc->start &&
	       apostrophe(char_at(s, pc->start));
}

static int split_at(Pieces *ps, int i, size_t at)
{
	if (!push(ps, 0, 0, 0))
		return 0;
	memmove(ps->p + i + 2, ps->p + i + 1,
		(size_t)(ps->n - i - 2) * sizeof(*ps->p));
	ps->p[i + 1] = (Piece){ at, ps->p[i].end, 0 };
	ps->p[i].end = at;
	return 1;
}

/*
 * A closing apostrophe after a word is split off, except after a plural s,
 * wherever the word ends. Then, when an odd number of lone apostrophes comes
 * before the first word that still ends in one, that apostrophe closes the
 * quote and is split off too.
 */
static int quotes(Pieces *ps, const char *s)
{
	/* One pass into a new list, so many closing quotes stay linear. */
	Pieces out = { 0 };
	for (int i = 0; i < ps->n; i++) {
		Piece pc = ps->p[i];
		int last = i + 1 == ps->n || ps->p[i + 1].space;
		size_t at = pc.space || !last ? 0 : closing(s, &pc);
		uint32_t before = at ? char_at(s, char_start(s, pc.start, at)) :
				       's';
		if (before != 's' && before != 'S' ?
			    !push(&out, pc.start, at, 0) ||
				    !push(&out, at, pc.end, 0) :
			    !push(&out, pc.start, pc.end, pc.space)) {
			free(out.p);
			return 0;
		}
	}
	free(ps->p);
	*ps = out;
	int singles = 0, last_single = -1, first_suffix = -1;
	for (int i = 0; i < ps->n; i++) {
		if (lone_apostrophe(s, &ps->p[i])) {
			singles++;
			last_single = i;
		} else if (first_suffix < 0 && !ps->p[i].space &&
			   closing(s, &ps->p[i]))
			first_suffix = i;
	}
	if (first_suffix >= 0 && singles % 2 == 1 && last_single < first_suffix)
		return split_at(ps, first_suffix,
				closing(s, &ps->p[first_suffix]));
	return 1;
}

static uint64_t murmur64a(const char *key, size_t len, uint64_t seed)
{
	const uint64_t m = 0xc6a4a7935bd1e995ull;
	const int r = 47;
	uint64_t h = seed ^ (len * m);
	const unsigned char *data = (const unsigned char *)key;
	size_t blocks = len / 8;
	for (size_t i = 0; i < blocks; i++) {
		uint64_t k = 0;
		for (int j = 7; j >= 0; j--)
			k = (k << 8) | data[i * 8 + (size_t)j];
		k *= m;
		k ^= k >> r;
		k *= m;
		h ^= k;
		h *= m;
	}
	const unsigned char *tail = data + blocks * 8;
	switch (len & 7) {
	case 7:
		h ^= (uint64_t)tail[6] << 48; /* fall through */
	case 6:
		h ^= (uint64_t)tail[5] << 40; /* fall through */
	case 5:
		h ^= (uint64_t)tail[4] << 32; /* fall through */
	case 4:
		h ^= (uint64_t)tail[3] << 24; /* fall through */
	case 3:
		h ^= (uint64_t)tail[2] << 16; /* fall through */
	case 2:
		h ^= (uint64_t)tail[1] << 8; /* fall through */
	case 1:
		h ^= (uint64_t)tail[0];
		h *= m;
	}
	h ^= h >> r;
	h *= m;
	h ^= h >> r;
	return h;
}

/* Hashes the UTF-8 text; strings hold an embedded NUL as C0 80 internally. */
static uint64_t text_hash(const char *s)
{
	size_t n = strlen(s), out = 0;
	char *bytes = malloc(n + 1);
	if (!bytes)
		return 0;
	for (size_t i = 0; i < n; i++) {
		if ((unsigned char)s[i] == 0xc0 &&
		    (unsigned char)s[i + 1] == 0x80) {
			bytes[out++] = 0;
			i++;
		} else
			bytes[out++] = s[i];
	}
	uint64_t h = murmur64a(bytes, out, 1);
	free(bytes);
	return h;
}

/* Lowercase, with every hyphen, dash and quote folded to its ASCII form. */
static char *normal_form(const char *text)
{
	Buf b = { 0 };
	const char *p = text;
	while (*p) {
		const char *at = p;
		uint32_t c = uread(&p);
		const char *peek = p;
		uint32_t next = *p ? uread(&peek) : 0;
		if ((c == '`' || c == 0x00b4) && next == c) {
			buf_put(&b, "\"");
			p = peek;
		} else if (in_set(c, dashes, sizeof(dashes) / sizeof(*dashes)))
			buf_put(&b, "-");
		else if (in_set(c, dquotes, sizeof(dquotes) / sizeof(*dquotes)))
			buf_put(&b, "\"");
		else if (apostrophe(c))
			buf_put(&b, "'");
		else
			buf_add(&b, at, (size_t)(p - at));
	}
	char *out = norm(b.p ? b.p : "", 1, 0);
	free(b.p);
	return out;
}

J *tokenize_text(const char *text)
{
	Pieces ps = { 0 };
	if (!scan(&ps, text) || !quotes(&ps, text)) {
		free(ps.p);
		return NULL;
	}
	J *result = ARR();
	size_t pos = 0;
	int count = 0; /* SIZE walks the whole list, so count instead. */
	for (int i = 0; i < ps.n; i++) {
		Piece pc = ps.p[i];
		if (pc.start == pc.end)
			continue;
		/* A word keeps the one space after it; whitespace runs stay whole. */
		size_t end = pc.end, source = pc.end;
		Piece *next = i + 1 < ps.n ? &ps.p[i + 1] : NULL;
		if (!pc.space && next && next->space &&
		    text[next->start] == ' ')
			end = ++next->start;
		char *t = slice(text + pc.start, end - pc.start),
		     *s = slice(text + pc.start, source - pc.start),
		     *trim = norm(t, 0, 0), *normal = normal_form(t);
		char id[48];
		snprintf(id, sizeof(id), "%" PRIu64 "#%d", text_hash(s),
			 count++);
		J *token = OBJ();
		PUT(token, "id", STR(id));
		PUT(token, "text", STR(t));
		PUT(token, "x", NUM((double)pos));
		PUT(token, "y", NUM((double)(pos + ulen(trim))));
		PUT(token, "normal", STR(normal));
		ADD(result, token);
		pos += ulen(t);
		free(t);
		free(s);
		free(trim);
		free(normal);
	}
	free(ps.p);
	return result;
}

static void slice_entities(J *tokens, const char *text, const size_t *at,
			   size_t n)
{
	EACH(t, tokens) {
		J *swaps = GET(t, "swaps");
		if (!swaps)
			continue;
		double x = GET(t, "x")->valuedouble,
		       y = GET(t, "y")->valuedouble;
		if (x >= 0 && x <= y && y <= (double)n) {
			char *s = slice(text + at[(size_t)x],
					at[(size_t)y] - at[(size_t)x]);
			set(t, "text", STR(s));
			free(s);
		}
		slice_entities(GET(swaps, "tokens"), text, at, n);
	}
}

/*
 * A matched entity's text is the input from its x to its y, so it shows what
 * the consumer sent rather than its tokens joined by spaces.
 */
void source_entities(J *tokens, const char *text)
{
	size_t n = ulen(text), *at = malloc((n + 1) * sizeof(*at));
	if (!at)
		return;
	const char *p = text;
	for (size_t i = 0; i <= n; i++) {
		at[i] = (size_t)(p - text);
		if (*p)
			uread(&p);
	}
	slice_entities(tokens, text, at, n);
	free(at);
}

/*
 * Letters and digits separated by single spaces, with underscores only inside
 * words, come back from the tokenizer unchanged apart from case.
 */
static int plain(const char *s)
{
	uint32_t prev = ' ';
	const char *p = s;
	while (*p) {
		uint32_t c = uread(&p),
			 next = *p ? (uint32_t)(unsigned char)*p : ' ';
		if (c == ' ' ? prev == ' ' || next == ' ' :
		    c == '_' ? prev == ' ' || next == ' ' :
			       !ualpha(c) && !numeric(c))
			return 0;
		prev = c;
	}
	return 1;
}

/*
 * The window text the exact matcher builds when its window covers every token
 * of text, or NULL when text is plain or already equals it. Synonyms are
 * stored as written, so "well/health/physical education" must be looked up as
 * "well / health / physical education" (craigtrim/mutatoc#5).
 */
char *tokenize_key(const char *text)
{
	if (plain(text))
		return NULL;
	J *ts = tokenize_text(text);
	if (!ts)
		return NULL;
	Buf b = { 0 };
	int words = 0;
	EACH(t, ts) {
		const char *normal = S(GET(t, "normal"));
		if (!*normal)
			continue;
		if (words++)
			buf_put(&b, " ");
		buf_put(&b, normal);
	}
	DEL(ts);
	char *key = norm(b.p ? b.p : "", 1, 0);
	free(b.p);
	if (!*key || !strcmp(key, text)) {
		free(key);
		return NULL;
	}
	return key;
}
