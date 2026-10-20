/*
 * test_text.c - Unicode and text compatibility checks.
 *
 * Checks case conversion, normalization, URIs, and literal values.
 */

#include "mc.h"

static int failures = 0, total = 0;
static void same(char *actual, const char *expected)
{
	total++;
	if (!actual || strcmp(actual, expected)) {
		fprintf(stderr, "Expected [%s], got [%s]\n", expected,
			actual ? actual : "NULL");
		failures++;
	}
	free(actual);
}

/* Original normalization is the reference for the ASCII shortcut (craigtrim/axiom#129). */
static char *reference_norm(const char *text, int lc, int spaces)
{
	char *v = lc ? lower(text) : copy(text);
	const char *p = v, *begin = NULL, *end = NULL;
	while (*p) {
		const char *start = p;
		uint32_t c = uread(&p);
		if (!uspace(c)) {
			if (!begin)
				begin = start;
			end = p;
		}
	}
	if (!begin) {
		free(v);
		return copy("");
	}
	if (!spaces) {
		char *out = slice(begin, (size_t)(end - begin));
		free(v);
		return out;
	}
	Buf out = { 0 };
	int pending = 0;
	p = begin;
	while (p < end) {
		const char *start = p;
		uint32_t c = uread(&p);
		if (uspace(c))
			pending = 1;
		else {
			if (pending)
				buf_put(&out, " ");
			pending = 0;
			buf_add(&out, start, (size_t)(p - start));
		}
	}
	free(v);
	return buf_take(&out);
}
static void normalization_vectors(void)
{
	for (int byte = 1; byte < 128; byte++) {
		char text[] = {
			(char)byte, 'A', (char)byte, 'B', (char)byte, 0
		};
		for (int lc = 0; lc < 2; lc++)
			for (int spaces = 0; spaces < 2; spaces++) {
				char *expected =
					reference_norm(text, lc, spaces);
				same(norm(text, lc, spaces), expected);
				free(expected);
			}
	}
	unsigned random = 129;
	for (int run = 0; run < 400; run++) {
		char text[129];
		int length = run % 129;
		for (int i = 0; i < length; i++) {
			random = random * 1664525u + 1013904223u;
			text[i] = (char)(1 + random % 127);
		}
		text[length] = 0;
		for (int lc = 0; lc < 2; lc++)
			for (int spaces = 0; spaces < 2; spaces++) {
				char *expected =
					reference_norm(text, lc, spaces);
				same(norm(text, lc, spaces), expected);
				free(expected);
			}
	}
}

int main(void)
{
	normalization_vectors();
	const char *vectors[][2] = { { "g:h", "g:h" },
				     { "g", "http://a/b/c/g" },
				     { "./g", "http://a/b/c/g" },
				     { "g/", "http://a/b/c/g/" },
				     { "/g", "http://a/g" },
				     { "//g", "http://g" },
				     { "?y", "http://a/b/c/d;p?y" },
				     { "g?y", "http://a/b/c/g?y" },
				     { "#s", "http://a/b/c/d;p?q#s" },
				     { "g#s", "http://a/b/c/g#s" },
				     { "g?y#s", "http://a/b/c/g?y#s" },
				     { ";x", "http://a/b/c/;x" },
				     { "g;x", "http://a/b/c/g;x" },
				     { "g;x?y#s", "http://a/b/c/g;x?y#s" },
				     { "", "http://a/b/c/d;p?q" },
				     { ".", "http://a/b/c/" },
				     { "./", "http://a/b/c/" },
				     { "..", "http://a/b/" },
				     { "../", "http://a/b/" },
				     { "../g", "http://a/b/g" },
				     { "../..", "http://a/" },
				     { "../../", "http://a/" },
				     { "../../g", "http://a/g" },
				     { "../../../g", "http://a/g" },
				     { "../../../../g", "http://a/g" },
				     { "/./g", "http://a/g" },
				     { "/../g", "http://a/g" },
				     { "g.", "http://a/b/c/g." },
				     { ".g", "http://a/b/c/.g" },
				     { "g..", "http://a/b/c/g.." },
				     { "..g", "http://a/b/c/..g" },
				     { "./../g", "http://a/b/g" },
				     { "./g/.", "http://a/b/c/g/" },
				     { "g/./h", "http://a/b/c/g/h" },
				     { "g/../h", "http://a/b/c/h" },
				     { "g;x=1/./y", "http://a/b/c/g;x=1/y" },
				     { "g;x=1/../y", "http://a/b/c/y" },
				     { "g?y/./x", "http://a/b/c/g?y/./x" },
				     { "g?y/../x", "http://a/b/c/g?y/../x" },
				     { "g#s/./x", "http://a/b/c/g#s/./x" },
				     { "g#s/../x", "http://a/b/c/g#s/../x" },
				     { "http:g", "http:g" } };
	for (size_t i = 0; i < sizeof(vectors) / sizeof(*vectors); i++)
		same(uri_resolve("http://a/b/c/d;p?q", vectors[i][0]),
		     vectors[i][1]);
	same(lower("ΟΣ"), "ος");
	same(lower("ΟΣΑ"), "οσα");
	same(lower("AΣ'A"), "aσ'a");
	same(lower("AΣ' A"), "aς' a");
	same(lower("Σ"), "σ");
	same(lower("İSTANBUL"), "i̇stanbul");
	same(upper("Straße ﬃ"), "STRASSE FFI");
	same(norm("\t Dog\tCat\nPoodle  ", 1, 0), "dog\tcat\npoodle");
	same(norm("\t Dog\tCat\nPoodle  ", 1, 1), "dog cat poodle");
	same(norm("\xc2\xa0"
		  "Dog\xc2\xa0"
		  "Cat\xc2\xa0",
		  0, 0),
	     "Dog\xc2\xa0"
	     "Cat");
	mc_error error = { 0 };
	J *value = json_parse("\"a\\u0000b\"", &error);
	same(cJSON_PrintUnformatted(value), "\"a\\u0000b\"");
	DEL(value);
	const char invalid[] = { '"', (char)0xc0, (char)0x80, '"', 0 };
	value = json_parse(invalid, &error);
	total++;
	if (value || !error.code)
		failures++;
	DEL(value);
	printf("Text/URI contracts: %d checks, %d failures.\n", total,
	       failures);
	return failures ? 1 : 0;
}
