/*
 * tokenize.c - Native raw-text tokenizer.
 *
 * Splits plain text into the tokens the matcher reads: id, text, x, y and
 * normal. Punctuation becomes its own token, except periods and commas inside
 * numbers, apostrophes inside words and ampersands between letters. A period
 * or comma that ends a number, and underscores that open or close a word, are
 * split off. Whitespace other than single spaces between tokens is kept as
 * its own zero-width token so offsets and phrase windows stay aligned.
 * craigtrim/mutatoc#1
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

static int ends_with(const char *s, const char *tail)
{
	size_t a = strlen(s), b = strlen(tail);
	return a >= b && !strcmp(s + a - b, tail);
}

static const Contraction *contraction(const char *word)
{
	for (size_t i = 0; i < sizeof(contractions) / sizeof(*contractions);
	     i++)
		if (!strcmp(word, contractions[i].word))
			return &contractions[i];
	return NULL;
}

static const char *abbreviation(const char *word)
{
	for (size_t i = 0; i < sizeof(abbreviations) / sizeof(*abbreviations);
	     i++)
		if (!strcmp(word, abbreviations[i].word))
			return abbreviations[i].expansion;
	return NULL;
}

/* Words keep their trailing space; every other character is weighed alone. */
static J *pre_split(const char *text)
{
	J *split_tokens = ARR();
	const char *start = text, *p = text;
	while (*p) {
		if (*p++ == ' ') {
			char *s = slice(start, (size_t)(p - start));
			ADD(split_tokens, STR(s));
			free(s);
			start = p;
		}
	}
	if (*start)
		ADD(split_tokens, STR(start));
	J *expanded = ARR();
	EACH(t, split_tokens) {
		const Contraction *c = strchr(S(t), '\'') ? contraction(S(t)) :
							    NULL;
		if (c) {
			ADD(expanded, STR(c->first));
			if (c->second)
				ADD(expanded, STR(c->second));
		} else
			ADD(expanded, DUP(t));
	}
	DEL(split_tokens);
	J *punct = ARR();
	EACH(t, expanded) {
		int dots = 0;
		for (const char *a = S(t); *a; a++)
			dots += *a == '.';
		/* Multi-period words such as initialisms keep every period. */
		const char *abbr = dots < 2 ? abbreviation(S(t)) : NULL;
		char *word = copy(abbr ? abbr : S(t));
		Buf b = { 0 };
		uint32_t prev = 0;
		const char *a = word;
		while (*a) {
			uint32_t ch = uread(&a);
			const char *peek = a;
			uint32_t next = *peek ? uread(&peek) : 0;
			if (ualpha(ch) || numeric(ch) || ch == ' ' ||
			    ch == '_' ||
			    ((ch == '.' || ch == ',') && numeric(prev)) ||
			    (ch == '\'' && ualpha(prev)) ||
			    (ch == '&' && ualpha(prev) && ualpha(next)))
				uwrite(&b, ch);
			else {
				if (b.n) {
					ADD(punct, STR(b.p));
					free(buf_take(&b));
				}
				Buf single = { 0 };
				uwrite(&single, ch);
				ADD(punct, STR(single.p));
				free(single.p);
			}
			prev = ch;
		}
		if (b.n)
			ADD(punct, STR(b.p));
		free(b.p);
		free(word);
	}
	DEL(expanded);
	J *spaces = ARR();
	for (int i = 0; i < SIZE(punct); i++) {
		char *word = copy(S(AT(punct, i)));
		if (i + 1 < SIZE(punct) && !strcmp(S(AT(punct, i + 1)), " ")) {
			Buf b = { 0 };
			buf_put(&b, word);
			buf_put(&b, " ");
			free(word);
			word = buf_take(&b);
			i++;
		}
		ADD(spaces, STR(word));
		free(word);
	}
	DEL(punct);
	/* A closing quote after a word is split off, except after a plural s. */
	J *quotes = ARR();
	EACH(t, spaces) {
		const char *word = S(t);
		if (strchr(word, '\'') && !ends_with(word, "s' ") &&
		    ends_with(word, "' ")) {
			char *prefix = slice(word, strlen(word) - 2);
			Buf b = { 0 };
			buf_put(&b, prefix);
			buf_put(&b, " ");
			ADD(quotes, STR(b.p));
			ADD(quotes, STR("'"));
			free(prefix);
			free(b.p);
		} else
			ADD(quotes, DUP(t));
	}
	DEL(spaces);
	int singles = 0, last_single = -1, first_suffix = -1, i = 0;
	EACH(t, quotes) {
		const char *s = S(t);
		if (!strcmp(s, "'")) {
			singles++;
			last_single = i;
		}
		char *trim = norm(s, 0, 0);
		if (ulen(s) > 1 && ends_with(trim, "'") && first_suffix < 0)
			first_suffix = i;
		free(trim);
		i++;
	}
	if (first_suffix >= 0 && singles % 2 == 1 &&
	    last_single <= first_suffix) {
		J *out = ARR();
		i = 0;
		EACH(t, quotes) {
			if (i++ == first_suffix) {
				char *prefix = slice(S(t), strlen(S(t)) - 1);
				Buf b = { 0 };
				buf_put(&b, prefix);
				buf_put(&b, " ");
				ADD(out, STR(b.p));
				ADD(out, STR("'"));
				free(prefix);
				free(b.p);
			} else
				ADD(out, DUP(t));
		}
		DEL(quotes);
		quotes = out;
	}
	return quotes;
}

/* The code point that ends s[0..end), where end is a character boundary. */
static uint32_t last_char(const char *s, size_t end)
{
	size_t start = end;
	while (start && ((unsigned char)s[start - 1] & 0xc0) == 0x80)
		start--;
	if (start)
		start--;
	while (start && ((unsigned char)s[start] & 0xc0) == 0x80)
		start--;
	const char *p = s + start;
	return end > start ? uread(&p) : 0;
}

static void add_piece(J *out, const char *s, size_t n)
{
	char *piece = slice(s, n);
	ADD(out, STR(piece));
	free(piece);
}

/*
 * A lone single quote reads as a double quote. Underscores that open or close
 * a word, and a period or comma that ends a number, become their own tokens;
 * the word's trailing space moves to its last piece.
 */
static J *split_edges(J *pre)
{
	J *out = ARR();
	EACH(t, pre) {
		const char *s = S(t);
		if (!strcmp(s, "'")) {
			ADD(out, STR("\""));
			continue;
		}
		size_t end = strlen(s), a = 0;
		while (end && s[end - 1] == ' ')
			end--;
		size_t b = end, trailing = 0;
		int punct = 0;
		if (!end) {
			ADD(out, DUP(t));
			continue;
		}
		size_t lead = 0;
		while (b - a > 1 && s[a] == '_') {
			a++;
			lead++;
		}
		while (b - a > 1 && s[b - 1] == '_') {
			b--;
			trailing++;
		}
		if (b - a > 1 && (s[b - 1] == '.' || s[b - 1] == ',') &&
		    numeric(last_char(s + a, b - a - 1)))
			punct = s[--b];
		J *pieces = ARR();
		for (size_t i = 0; i < lead; i++)
			ADD(pieces, STR("_"));
		add_piece(pieces, s + a, b - a);
		if (punct) {
			char p[2] = { (char)punct, 0 };
			ADD(pieces, STR(p));
		}
		for (size_t i = 0; i < trailing; i++)
			ADD(pieces, STR("_"));
		int count = SIZE(pieces), index = 0;
		EACH(piece, pieces) {
			if (++index < count || !s[end])
				ADD(out, DUP(piece));
			else {
				Buf last = { 0 };
				buf_put(&last, S(piece));
				buf_put(&last, s + end);
				ADD(out, STR(last.p));
				free(last.p);
			}
		}
		DEL(pieces);
	}
	return out;
}

/*
 * Tokens are joined with single spaces. The first space after a token
 * separates it from the next; any other run of whitespace becomes its own
 * token.
 */
static J *segments(const char *s)
{
	J *out = ARR();
	int in_space = 0;
	const char *start = s, *p = s;
	while (*p) {
		const char *at = p;
		uint32_t c = uread(&p);
		if (uspace(c) != in_space) {
			if (start < at)
				add_piece(out, start, (size_t)(at - start));
			start = c == ' ' ? p : at;
			in_space = !in_space;
		}
	}
	if (*start)
		ADD(out, STR(start));
	return out;
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

static char *collapse(char *s)
{
	while (strstr(s, "  ")) {
		char *next = replace(s, "  ", " ");
		free(s);
		s = next;
	}
	return s;
}

/* Lowercase, with typographic hyphens and quotes folded to ASCII. */
static char *normal_form(const char *text)
{
	static const uint32_t hyphens[] = { 0x058a, 0x1806, 0x2010, 0x2011,
					    0x2012, 0x2013, 0x2014, 0x2015,
					    0x2053, 0x207b, 0x208b, 0x2212,
					    0x2e3a, 0x2e3b, 0x301c, 0x3030,
					    0xfe58, 0xfe63, 0xff0d };
	static const char *dquotes[] = { "\xe2\x80\x9c",    "\xe2\x80\x9d",
					 "\xc2\xab",	    "\xc2\xbb",
					 "\xe2\x80\x9e",    "``",
					 "\xc2\xb4\xc2\xb4" };
	static const char *squotes[] = { "\xe2\x80\x99", "\xe2\x80\x98",
					 "\xe2\x80\x9b", "`" };
	char *s = copy(text), *next;
	for (size_t i = 0; i < sizeof(hyphens) / sizeof(*hyphens); i++) {
		Buf b = { 0 };
		uwrite(&b, hyphens[i]);
		next = replace(s, b.p, "-");
		free(s);
		free(b.p);
		s = next;
	}
	for (size_t i = 0; i < sizeof(dquotes) / sizeof(*dquotes); i++) {
		next = replace(s, dquotes[i], "\"");
		free(s);
		s = next;
	}
	for (size_t i = 0; i < sizeof(squotes) / sizeof(*squotes); i++) {
		next = replace(s, squotes[i], "'");
		free(s);
		s = next;
	}
	char *out = norm(s, 1, 0);
	free(s);
	return out;
}

J *tokenize_text(const char *text)
{
	J *pre = pre_split(text), *edges = split_edges(pre);
	char *joined = join(edges, " ");
	J *raw = segments(joined), *result = ARR();
	free(joined);
	DEL(pre);
	DEL(edges);
	for (int i = 0; i < SIZE(raw); i++) {
		const char *source = S(AT(raw, i));
		int next = i + 1 < SIZE(raw) && !strcmp(S(AT(raw, i + 1)), " ");
		char *spaced = replace(source, "\n", " "),
		     *t = collapse(spaced);
		if (next && !ends_with(t, " ")) {
			Buf b = { 0 };
			buf_put(&b, t);
			buf_put(&b, " ");
			free(t);
			t = buf_take(&b);
		}
		char id[48];
		snprintf(id, sizeof(id), "%" PRIu64 "#%d", text_hash(source),
			 SIZE(result));
		J *token = OBJ();
		PUT(token, "id", STR(id));
		PUT(token, "text", STR(t));
		ADD(result, token);
		free(t);
		if (next)
			i++;
	}
	DEL(raw);
	size_t pos = 0;
	for (int i = 0; i < SIZE(result); i++) {
		J *t = AT(result, i);
		const char *next = i + 1 < SIZE(result) ?
					   S(GET(AT(result, i + 1), "text")) :
					   "";
		/* Closing punctuation sits directly against the preceding word. */
		if (!strcmp(next, ")") || !strcmp(next, "\"") ||
		    !strcmp(next, "!") || !strcmp(next, "?")) {
			char *trim = norm(S(GET(t, "text")), 0, 0);
			set(t, "text", STR(trim));
			free(trim);
		}
		const char *surface = S(GET(t, "text"));
		char *trim = norm(surface, 0, 0),
		     *normal = normal_form(surface);
		PUT(t, "x", NUM((double)pos));
		PUT(t, "y", NUM((double)(pos + ulen(trim))));
		PUT(t, "normal", STR(normal));
		pos += ulen(surface);
		free(trim);
		free(normal);
	}
	return result;
}
