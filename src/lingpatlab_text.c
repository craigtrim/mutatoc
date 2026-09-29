/*
 * lingpatlab_text.c - LingPatLab text utilities and word lookup.
 *
 * Provides Unicode helpers, stemming, and dictionary membership checks.
 */

#include "lingpatlab.h"
#include "lingpatlab_data.h"
#include "lingpatlab_unicode.h"
#include "wordnet_data.h"
#include <math.h>

J *lp_data(const char *name)
{
	for (size_t i = 0; i < sizeof(lp_constants) / sizeof(*lp_constants);
	     i++)
		if (!strcmp(name, lp_constants[i].name))
			return cJSON_Parse(lp_constants[i].json);
	return NULL;
}

static int in_ranges(uint32_t c, const uint32_t (*ranges)[2], size_t n)
{
	size_t l = 0;
	while (l < n) {
		size_t m = l + (n - l) / 2;
		if (c < ranges[m][0])
			n = m;
		else if (c > ranges[m][1])
			l = m + 1;
		else
			return 1;
	}
	return 0;
}

#define RANGE_FN(name)                                                 \
	int lp_##name(uint32_t c)                                      \
	{                                                              \
		return in_ranges(c, lp_##name##_ranges,                \
				 sizeof(lp_##name##_ranges) /          \
					 sizeof(*lp_##name##_ranges)); \
	}
RANGE_FN(numeric)
RANGE_FN(upper)
RANGE_FN(punct)
int lp_all_numeric(const char *s)
{
	if (!*s)
		return 0;
	while (*s)
		if (!lp_numeric(uread(&s)))
			return 0;
	return 1;
}

int lp_all_upper(const char *s)
{
	char *u = upper(s), *l = lower(s);
	int result = strcmp(u, l) && !strcmp(s, u);
	free(u);
	free(l);
	return result;
}

int lp_ends(const char *s, const char *tail)
{
	size_t a = strlen(s), b = strlen(tail);
	return a >= b && !strcmp(s + a - b, tail);
}

char *lp_replace(char *s, const char *a, const char *b)
{
	char *p;
	if (!*a) {
		Buf out = { 0 };
		buf_put(&out, b);
		const char *it = s;
		while (*it) {
			uint32_t c = uread(&it);
			uwrite(&out, c);
			buf_put(&out, b);
		}
		p = buf_take(&out);
	} else
		p = replace(s, a, b);
	free(s);
	return p;
}

char *lp_strip(char *s)
{
	char *p = norm(s, 0, 0);
	free(s);
	return p;
}

char *lp_collapse(char *s)
{
	while (strstr(s, "  "))
		s = lp_replace(s, "  ", " ");
	return s;
}

J *lp_words(const char *s)
{
	J *a = ARR();
	const char *start = s;
	while (*s) {
		const char *p = s;
		uint32_t c = uread(&s);
		if (uspace(c)) {
			if (p > start) {
				char *v = slice(start, (size_t)(p - start));
				ADD(a, STR(v));
				free(v);
			}
			start = s;
		}
	}
	if (s > start)
		ADD(a, STR(start));
	return a;
}

char *lp_capitalize(const char *s)
{
	if (!*s)
		return copy(s);
	const char *end = s;
	uread(&end);
	char *first = slice(s, (size_t)(end - s)), *u = upper(first);
	Buf b = { 0 };
	buf_put(&b, u);
	buf_put(&b, end);
	free(first);
	free(u);
	return buf_take(&b);
}

static int measure(const char *s)
{
	char *v = lower(s);
	int n = 0, prev = 0;
	for (char *p = v; *p; p++) {
		int vowel = strchr("aeiou", *p) != NULL;
		if (vowel && !prev)
			n++;
		prev = vowel;
	}
	free(v);
	return n;
}

char *lp_stem(const char *s)
{
	static const char *steps[][24] = {
		{ "sses", "ies", "ss", "s", NULL },
		{ "y", "eed", "ed", "ing", NULL },
		{ "at", "bl", "iz", NULL },
		{ "ational", "tional", "enci",	  "anci",    "izer",	"bli",
		  "alli",    "entli",  "eli",	  "ousli",   "ization", "ation",
		  "ator",    "alism",  "iveness", "fulness", "ousness", "aliti",
		  "iviti",   "biliti", "logi",	  NULL },
		{ "icate", "ative", "alize", "iciti", "ical", "ful", "ness",
		  NULL },
		{ "al", "ance", "ence", "er", "ic", "able", "ible", "ant",
		  "ement", "ment", "ent", "ou", "ism", "ate", "iti", "ous",
		  "ive", "ize", NULL }
	};
	if (ulen(s) <= 2)
		return copy(s);
	for (int i = 0; i < 6; i++)
		for (int j = 0; steps[i][j]; j++)
			if (lp_ends(s, steps[i][j])) {
				char *stem = slice(
					s, strlen(s) - strlen(steps[i][j]));
				if (measure(stem) > (i == 5 ? 1 : 0)) {
					Buf b = { 0 };
					buf_put(&b, stem);
					if (i == 1 && j == 0)
						buf_put(&b, "i");
					if (i == 2)
						buf_put(&b, "e");
					free(stem);
					return buf_take(&b);
				}
				free(stem);
				return copy(s);
			}
	return copy(s);
}

static uint32_t rotl(uint32_t x, unsigned n)
{
	return (x << n) | (x >> (32 - n));
}

/* MD5 is used only to reproduce the pinned dictionary's membership keys. */
static void term_md5(const char *s, char hex[33])
{
	static const unsigned shifts[64] = {
		7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
		5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
		4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
		6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
	};
	static const uint32_t k[64] = {
		0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf,
		0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af,
		0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e,
		0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
		0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6,
		0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
		0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122,
		0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
		0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039,
		0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97,
		0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d,
		0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
		0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
	};
	size_t len = strlen(s), n = 0;
	unsigned char *data = calloc((len + 72) / 64 * 64, 1);
	for (size_t i = 0; i < len; i++) {
		if ((unsigned char)s[i] == 0xc0 &&
		    (unsigned char)s[i + 1] == 0x80) {
			data[n++] = 0;
			i++;
		} else
			data[n++] = (unsigned char)s[i];
	}
	size_t total = (n + 72) / 64 * 64;
	data[n] = 0x80;
	uint64_t bits = (uint64_t)n * 8;
	for (int j = 0; j < 8; j++)
		data[total - 8 + (size_t)j] = (unsigned char)(bits >> (j * 8));
	uint32_t h[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
	for (size_t offset = 0; offset < total; offset += 64) {
		uint32_t m[16];
		for (int j = 0; j < 16; j++) {
			const unsigned char *p = data + offset + j * 4;
			m[j] = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
			       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
		}
		uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
		for (unsigned j = 0; j < 64; j++) {
			uint32_t f;
			unsigned g;
			if (j < 16) {
				f = (b & c) | (~b & d);
				g = j;
			} else if (j < 32) {
				f = (d & b) | (~d & c);
				g = (5 * j + 1) % 16;
			} else if (j < 48) {
				f = b ^ c ^ d;
				g = (3 * j + 5) % 16;
			} else {
				f = c ^ (b | ~d);
				g = (7 * j) % 16;
			}
			uint32_t old = d;
			d = c;
			c = b;
			b += rotl(a + f + k[j] + m[g], shifts[j]);
			a = old;
		}
		h[0] += a;
		h[1] += b;
		h[2] += c;
		h[3] += d;
	}
	free(data);
	static const char digits[] = "0123456789abcdef";
	for (int i = 0; i < 16; i++) {
		unsigned v = (h[i / 4] >> (8 * (i % 4))) & 255;
		hex[2 * i] = digits[v >> 4];
		hex[2 * i + 1] = digits[v & 15];
	}
	hex[32] = 0;
}

static int word_hash(const char *s)
{
	if (!*s)
		return 0;
	char hash[33];
	char *normalized = norm(s, 1, 0);
	term_md5(normalized, hash);
	free(normalized);
	size_t l = 0,
	       n = sizeof(lp_wordnet_hashes) / sizeof(*lp_wordnet_hashes);
	while (l < n) {
		size_t m = l + (n - l) / 2;
		int c = strcmp(hash, lp_wordnet_hashes[m]);
		if (c < 0)
			n = m;
		else if (c > 0)
			l = m + 1;
		else
			return 1;
	}
	return 0;
}

int lp_wordnet(const char *s)
{
	char *text = norm(s, 1, 0);
	if (word_hash(text)) {
		free(text);
		return 1;
	}
	Buf b = { 0 };
	const char *p = text;
	while (*p) {
		uint32_t c = uread(&p);
		if (c < 128)
			uwrite(&b, c);
		else {
			size_t l = 0,
			       n = sizeof(lp_ascii_map) / sizeof(*lp_ascii_map);
			while (l < n) {
				size_t m = l + (n - l) / 2;
				if (c < lp_ascii_map[m].code)
					n = m;
				else if (c > lp_ascii_map[m].code)
					l = m + 1;
				else {
					buf_put(&b, lp_ascii_map[m].ascii);
					break;
				}
			}
		}
	}
	char *ascii = buf_take(&b);
	int exists = strcmp(text, ascii) && word_hash(ascii);
	size_t n = strlen(ascii);
	if (!exists && ulen(ascii) > 3 && lp_ends(ascii, "s")) {
		ascii[n - 1] = 0;
		exists = word_hash(ascii);
	}
	free(ascii);
	free(text);
	return exists;
}

static int text_punct(uint32_t c)
{
	return (c >= 33 && c <= 47) || (c >= 58 && c <= 64) ||
	       (c >= 91 && c <= 96) || (c >= 123 && c <= 126) || lp_punct(c);
}

static J *text_result(char *s)
{
	J *r = STR(s);
	free(s);
	return r;
}

static J *window(const J *tokens, int width)
{
	if (width > SIZE(tokens))
		return NIL();
	J *out = ARR();
	if (width == SIZE(tokens)) {
		ADD(out, DUP(tokens));
		return out;
	}
	for (int i = 0; i < SIZE(tokens); i++) {
		J *row = ARR();
		for (int j = 0; j < width && i + j < SIZE(tokens); j++)
			ADD(row, DUP(AT(tokens, i + j)));
		if (SIZE(row) == width)
			ADD(out, row);
		else
			DEL(row);
	}
	return out;
}

static J *char_set(const char *text)
{
	J *set = ARR();
	while (*text) {
		Buf b = { 0 };
		uwrite(&b, uread(&text));
		unique(set, b.p);
		free(b.p);
	}
	return set;
}

static double jaccard(const char *a, const char *b, mc_error *err)
{
	J *left = char_set(a), *right = char_set(b);
	int inter = 0, uni = SIZE(left);
	EACH(v, right) {
		if (contains(left, S(v)))
			inter++;
		else
			uni++;
	}
	DEL(left);
	DEL(right);
	if (!uni) {
		fail(err, 2,
		     "Jaccard similarity is undefined for two empty strings");
		return 0;
	}
	char rounded[64];
	snprintf(rounded, sizeof rounded, "%.3f", (double)inter / uni);
	return strtod(rounded, NULL);
}

static int vowel_start(const char *text, mc_error *err)
{
	if (!*text) {
		fail(err, 2, "startswith_vowel requires nonempty text");
		return 0;
	}
	if ((*text == '\'' || *text == '\"') && text[1])
		text++;
	const char *end = text;
	uread(&end);
	char *first = slice(text, (size_t)(end - text));
	char *lo = lower(first);
	free(first);
	int yes = *lo && !lo[1] && strchr("aeiou", *lo) != NULL;
	free(lo);
	return yes;
}

static J *normalized_strings(const J *a)
{
	J *r = ARR();
	EACH(t, a) {
		char *v = norm(S(t), 1, 0);
		ADD(r, STR(v));
		free(v);
	}
	return r;
}

static J *phrase_grams(const J *a, int n)
{
	J *r = ARR();
	if (n == SIZE(a)) {
		EACH(t, a) {
			J *characters = ARR();
			const char *s = S(t);
			while (*s) {
				Buf b = { 0 };
				uwrite(&b, uread(&s));
				ADD(characters, STR(b.p));
				free(b.p);
			}
			char *v = join(characters, " ");
			unique(r, v);
			free(v);
			DEL(characters);
		}
	} else {
		for (int i = 0; i + n <= SIZE(a); i++) {
			J *w = ARR();
			for (int j = 0; j < n; j++)
				ADD(w, DUP(AT(a, i + j)));
			char *v = join(w, " ");
			unique(r, v);
			free(v);
			DEL(w);
		}
	}
	return r;
}

J *lp_text_method(const char *method, const J *q, mc_error *err)
{
	const char *text = S(GET(q, "text")), *text2 = S(GET(q, "text2"));
	J *tokens = GET(q, "tokens"), *tokens2 = GET(q, "tokens2");
	if (!strcmp(method, "remove_duplicated_phrases")) {
		char *v = copy(text);
		if (strstr(text, text2))
			v = lp_strip(lp_replace(v, text2, ""));
		return text_result(v);
	}
	if (!strcmp(method, "sliding_window"))
		return window(tokens, GET(q, "window_size") ?
					      GET(q, "window_size")->valueint :
					      0);
	if (!strcmp(method, "jaccard_similarity"))
		return NUM(jaccard(text, text2, err));
	if (!strcmp(method, "most_similar_phrase")) {
		J *a = normalized_strings(tokens),
		  *b = normalized_strings(tokens2);
		int width = GET(q, "window_size") ?
				    GET(q, "window_size")->valueint :
				    0;
		J *wa = window(a, width), *wb = window(b, width);
		DEL(a);
		DEL(b);
		J *out = OBJ();
		if (cJSON_IsNull(wa) || cJSON_IsNull(wb))
			fail(err, 2, "Window exceeds available tokens");
		EACH(x, wa) {
			char *left = join(x, " ");
			EACH(y, wb) {
				char *right = join(y, " ");
				int exact = !strcmp(left, right);
				double score =
					exact ? 100 : jaccard(left, right, err);
				if (exact ||
				    score >= (GET(q, "score_threshold") ?
						      GET(q, "score_threshold")
							      ->valuedouble :
						      0)) {
					J *pair = OBJ();
					PUT(pair, "tokens_1", STR(left));
					PUT(pair, "tokens_2", STR(right));
					if (exact) {
						DEL(out);
						out = OBJ();
						PUT(out, "100", pair);
						free(left);
						free(right);
						DEL(wa);
						DEL(wb);
						return out;
					}
					char key[64];
					snprintf(key, sizeof key, "%.3f",
						 score);
					size_t n = strlen(key);
					while (n > 2 && key[n - 1] == '0' &&
					       key[n - 2] != '.')
						key[--n] = 0;
					set(out, key, pair);
				}
				free(right);
			}
			free(left);
		}
		DEL(wa);
		DEL(wb);
		return out;
	}
	if (!strcmp(method, "longest_common_phrase")) {
		char *original = lp_strip(join(tokens2, " ")),
		     *lo = lower(original);
		int n = SIZE(tokens) < SIZE(tokens2) ? SIZE(tokens) :
						       SIZE(tokens2);
		J *result = NIL();
		for (; n > 2; n--) {
			J *a = phrase_grams(tokens, n),
			  *b = phrase_grams(tokens2, n), *common = ARR();
			EACH(v, a)
				if (contains(b, S(v)))
					unique(common, S(v));
			sort_strings(common, 0);
			if (SIZE(common)) {
				const char *phrase = S(AT(common, 0)),
					   *found = strstr(lo, phrase);
				if (found) {
					size_t prefix = 0;
					const char *walk = lo;
					while (walk < found) {
						uread(&walk);
						prefix++;
					}
					const char *begin = original;
					for (size_t i = 0; i < prefix && *begin;
					     i++)
						uread(&begin);
					const char *end = begin;
					for (size_t i = 0;
					     i < ulen(phrase) && *end; i++)
						uread(&end);
					char *v = slice(begin,
							(size_t)(end - begin));
					DEL(result);
					result = lp_words(v);
					free(v);
				}
				DEL(a);
				DEL(b);
				DEL(common);
				break;
			}
			DEL(a);
			DEL(b);
			DEL(common);
		}
		free(original);
		free(lo);
		return result;
	}
	if (!strcmp(method, "is_punctuation")) {
		if (ulen(text) > 1)
			return BOOL(0);
		if (!*text) {
			fail(err, 2, "is_punctuation requires one character");
			return NULL;
		}
		return BOOL(text_punct(uread(&text)));
	}
	if (!strcmp(method, "has_punctuation") ||
	    !strcmp(method, "remove_punctuation")) {
		Buf b = { 0 };
		int found = 0;
		const char *s = text;
		while (*s) {
			uint32_t c = uread(&s);
			if (text_punct(c))
				found = 1;
			else
				uwrite(&b, c);
		}
		if (!strcmp(method, "has_punctuation")) {
			free(b.p);
			return BOOL(found);
		}
		return text_result(buf_take(&b));
	}
	if (!strcmp(method, "find_subsumed_tokens")) {
		J *out = ARR();
		EACH(a, tokens)
			EACH(b, tokens) {
				const char *first = S(a), *second = S(b);
				if (strcmp(first, second) >= 0)
					continue;
				Buf both = { 0 }, left = { 0 }, right = { 0 };
				buf_put(&both, " ");
				buf_put(&both, first);
				buf_put(&both, " ");
				buf_put(&left, " ");
				buf_put(&left, first);
				buf_put(&right, first);
				buf_put(&right, " ");
				if (strstr(second, both.p) ||
				    lp_ends(second, left.p) ||
				    !strncmp(second, right.p, right.n))
					unique(out, first);
				free(both.p);
				free(left.p);
				free(right.p);
			}
		sort_strings(out, 0);
		return out;
	}
	if (!strcmp(method, "split_on_len")) {
		if (cJSON_IsNull(GET(q, "text")))
			return NIL();
		int threshold =
			GET(q, "threshold") ? GET(q, "threshold")->valueint : 7;
		if ((double)ulen(text) <= threshold)
			return STR(text);
		J *parts = split(text, " "), *out = ARR(), *buffer = ARR();
		EACH(v, parts) {
			char *trim = norm(S(v), 0, 0);
			ADD(buffer, STR(trim));
			free(trim);
			char *temp = lp_strip(join(buffer, " "));
			if ((double)ulen(temp) >= threshold) {
				ADD(out, STR(temp));
				DEL(buffer);
				buffer = ARR();
			}
			free(temp);
		}
		if (SIZE(buffer))
			ADD(out, text_result(lp_strip(join(buffer, " "))));
		DEL(parts);
		DEL(buffer);
#ifdef _WIN32
		const char *sep = "\\";
#else
		const char *sep = "/";
#endif
		if (GET(q, "separator"))
			sep = S(GET(q, "separator"));
		char *result = lp_strip(join(out, sep));
		DEL(out);
		return text_result(result);
	}
	if (!strcmp(method, "ends_with_punctuation") ||
	    !strcmp(method, "remove_ending_punctuation")) {
		size_t len = strlen(text);
		int has = len && strchr(".?!", text[len - 1]);
		if (!strcmp(method, "ends_with_punctuation"))
			return BOOL(has);
		if (cJSON_IsNull(GET(q, "text")))
			return NIL();
		return text_result(has ? slice(text, len - 1) : copy(text));
	}
	if (!strcmp(method, "split_on_punctuation")) {
		J *punkt = GET(q, "punkt");
		char *s = copy(text);
		if (punkt) {
			EACH(v, punkt)
				s = lp_replace(s, S(v), ".");
		} else {
			s = lp_replace(s, "!", ".");
			s = lp_replace(s, "?", ".");
		}
		J *parts = split(s, "."), *out = ARR();
		free(s);
		EACH(v, parts) {
			char *t = norm(S(v), 0, 0);
			if (*t)
				ADD(out, STR(t));
			free(t);
		}
		DEL(parts);
		return out;
	}
	if (!strcmp(method, "update_spacing")) {
		char *s = replace(text, " !", "!");
		s = lp_replace(s, ".?", "?");
		s = lp_replace(s, ".!", "!");
		s = lp_strip(lp_replace(s, ". .", ". "));
		return text_result(lp_collapse(s));
	}
	if (!strcmp(method, "remove_double_spaces"))
		return text_result(lp_collapse(copy(text)));
	if (!strcmp(method, "update_csvs"))
		return GET(q, "text") ? DUP(GET(q, "text")) : NIL();
	if (!strcmp(method, "startswith_vowel"))
		return BOOL(vowel_start(text, err));
	if (!strcmp(method, "update_determiners")) {
		if (!strchr(text, ' '))
			return STR(text);
		J *parts = split(text, " "), *out = ARR();
		for (int i = 0; i < SIZE(parts); i++) {
			const char *s = S(AT(parts, i));
			char *lo = lower(s);
			int replace_an = !strcmp(lo, "a") &&
					 i + 1 < SIZE(parts) &&
					 vowel_start(S(AT(parts, i + 1)), err);
			free(lo);
			ADD(out,
			    STR(replace_an ? (lp_all_upper(s) ? "An" : "an") :
					     s));
		}
		char *s = lp_strip(join(out, " "));
		DEL(parts);
		DEL(out);
		return text_result(s);
	}
	if (!strcmp(method, "lower_case")) {
		char *s = lower(text);
		return text_result(strchr(text, ' ') ? lp_strip(s) : s);
	}
	if (!strcmp(method, "sentence_case") || !strcmp(method, "title_case")) {
		if (!strchr(text, ' '))
			return text_result(lp_capitalize(text));
		J *parts = split(text, " "), *out = ARR();
		int i = 0;
		EACH(v, parts) {
			const char *s = S(v);
			char *result = NULL;
			if (!i++)
				result = lp_capitalize(s);
			else if (!strcmp(method, "sentence_case"))
				result = ulen(s) > 1 && !lp_all_upper(s) ?
						 lower(s) :
						 copy(s);
			else {
				char *lo = norm(s, 1, 0);
				J *stop = split(
					"a all for from of i in into is to the",
					" ");
				int found = contains(stop, lo);
				DEL(stop);
				free(lo);
				result = found && ulen(s) > 1 &&
							 !lp_all_upper(s) ?
						 lower(s) :
						 lp_capitalize(s);
			}
			ADD(out, STR(result));
			free(result);
		}
		char *result = lp_strip(join(out, " "));
		DEL(parts);
		DEL(out);
		return text_result(result);
	}
	fail(err, 2, "Unknown TextUtils method: %s", method);
	return NULL;
}
