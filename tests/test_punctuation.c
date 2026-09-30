/*
 * test_punctuation.c - Punctuation contracts on small authored ontologies.
 *
 * Expected matches come from the phrase used to author each ontology, not
 * from snapshots or an earlier run of the engine under test. Covers the
 * tokenizer, prepared and raw-text matching around dotted abbreviations,
 * whitespace inside phrases, repeated matches, long exact windows,
 * longest-leftmost selection and recovery from malformed lookups.
 * craigtrim/mutatoc#1
 */

#include "testlib.h"

#define PREFIXES                                                    \
	"@prefix : <https://example.org/punctuation#> .\n"          \
	"@prefix owl: <http://www.w3.org/2002/07/owl#> .\n"         \
	"@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n" \
	"@prefix skos: <http://www.w3.org/2004/02/skos/core#> .\n"

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

static void equal_json(const char *category, const char *label, const J *actual,
		       const J *expected)
{
	assertions++;
	if (!cJSON_Compare(actual, expected, 1)) {
		if (++failures <= 30) {
			char text[512];
			snprintf(text, sizeof(text), "%s | %s", category,
				 label);
			test_print_diff(text, expected, actual);
		}
	}
}

static void equal_int(const char *category, const char *label, int actual,
		      int expected)
{
	J *a = NUM(actual), *b = NUM(expected);
	equal_json(category, label, a, b);
	DEL(a);
	DEL(b);
}

static void equal_str(const char *category, const char *label,
		      const char *actual, const char *expected)
{
	J *a = STR(actual), *b = STR(expected);
	equal_json(category, label, a, b);
	DEL(a);
	DEL(b);
}

static char *printf_alloc(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	int n = vsnprintf(NULL, 0, format, ap);
	va_end(ap);
	char *out = malloc((size_t)n + 1);
	va_start(ap, format);
	vsnprintf(out, (size_t)n + 1, format, ap);
	va_end(ap);
	return out;
}

/* A Turtle string literal; the authored phrases are plain text. */
static char *quoted(const char *s)
{
	J *v = STR(s);
	char *out = cJSON_PrintUnformatted(v);
	DEL(v);
	return out;
}

static char *ontology(const char *phrase, const char *predicate)
{
	char *lit = quoted(phrase);
	char *out = printf_alloc(
		PREFIXES
		":Target a owl:Class; rdfs:label \"Target course\"; %s %s .\n"
		":History a owl:Class; rdfs:label \"History\" .\n"
		":Other a owl:Class; rdfs:label \"Other course\"; rdfs:seeAlso "
		"\"U.S. History since 1865\" .\n",
		predicate, lit);
	free(lit);
	return out;
}

static mc_engine *engine;

static J *call(J *q)
{
	J *r = test_request(engine, q);
	DEL(q);
	return r;
}

static void load(const char *turtle)
{
	J *q = OBJ();
	PUT(q, "op", STR("load"));
	PUT(q, "turtle", STR(turtle));
	PUT(q, "class_based", BOOL(1));
	PUT(q, "interface", STR("data"));
	J *r = call(q);
	if (!cJSON_IsTrue(GET(r, "ok")))
		fprintf(stderr, "load failed: %s\n",
			S(GET(GET(r, "error"), "message")));
	DEL(r);
}

static J *parse(const char *text)
{
	J *q = OBJ();
	PUT(q, "op", STR("parse"));
	PUT(q, "text", STR(text));
	J *r = call(q),
	  *out = cJSON_DetachItemFromObjectCaseSensitive(r, "result");
	DEL(r);
	return out;
}

static J *parse_tokens(const J *tokens)
{
	J *q = OBJ();
	PUT(q, "op", STR("parse_tokens"));
	PUT(q, "tokens", DUP(tokens));
	J *r = call(q),
	  *out = cJSON_DetachItemFromObjectCaseSensitive(r, "result");
	DEL(r);
	return out;
}

static J *tokenize(const char *text)
{
	J *q = OBJ();
	PUT(q, "op", STR("tokenize"));
	PUT(q, "text", STR(text));
	J *r = call(q), *texts = ARR();
	EACH(t, GET(r, "result"))
		ADD(texts, DUP(GET(t, "text")));
	DEL(r);
	return texts;
}

static void add_leaves(J *out, const J *token)
{
	J *swaps = GET(token, "swaps");
	if (!swaps) {
		ADD(out, DUP(token));
		return;
	}
	EACH(child, GET(swaps, "tokens"))
		add_leaves(out, child);
}

static J *leaves(const J *token)
{
	J *out = ARR();
	add_leaves(out, token);
	return out;
}

static J *matches(const J *result, const char *canon)
{
	J *out = ARR();
	EACH(t, GET(result, "tokens"))
		if (!strcmp(S(GET(GET(t, "swaps"), "canon")), canon))
			ADD(out, DUP(t));
	return out;
}

/* Text with all whitespace removed. */
static char *visible(const char *text)
{
	Buf b = { 0 };
	while (*text) {
		uint32_t c = uread(&text);
		if (!uspace(c))
			uwrite(&b, c);
	}
	return b.p ? b.p : copy("");
}

static char *leaf_text(const J *token)
{
	J *parts = leaves(token);
	Buf b = { 0 };
	EACH(t, parts)
		buf_put(&b, S(GET(t, "text")));
	DEL(parts);
	return b.p ? b.p : copy("");
}

/* Caller-supplied tokens with unrelated metadata to check lossless history. */
static J *prepared(const char **parts, int n)
{
	J *out = ARR();
	int offset = 0;
	for (int i = 0; i < n; i++) {
		J *t = OBJ(), *provenance = OBJ();
		char *id = printf_alloc("source-%d", i);
		char *normal = norm(parts[i], 1, 0);
		int length = (int)ulen(parts[i]);
		PUT(t, "id", STR(id));
		PUT(t, "x", NUM(offset));
		PUT(t, "y", NUM(offset + length));
		PUT(t, "text", STR(parts[i]));
		PUT(t, "normal", STR(normal));
		PUT(provenance, "index", NUM(i));
		PUT(provenance, "original", STR(parts[i]));
		PUT(t, "provenance", provenance);
		ADD(out, t);
		offset += length;
		free(id);
		free(normal);
	}
	return out;
}

static int tilde_count(const char *s)
{
	int n = 0;
	for (; *s; s++)
		n += *s == '~';
	return n;
}

static uint64_t rng_state = 1865;
static uint32_t rng(uint32_t bound)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 7;
	rng_state ^= rng_state << 17;
	return (uint32_t)(rng_state % bound);
}

static void tokenizer(void)
{
	static const char *explicit_cases[][8] = {
		{ "U.S. History to 1865", "U", ".", "S", ". ", "History ",
		  "to ", "1865" },
		{ "U.S.A.", "U", ".", "S", ".", "A", "." },
		{ "Dog... Cat", "Dog", ".", ".", ". ", "Cat" },
		{ "~~", "~", "~" },
		{ "U~S~", "U", "~", "S", "~" },
		{ "1.2.3", "1.2.3" },
		{ "3.14", "3.14" },
		{ "dr.", "dr", "." },
		{ "mr.", "mr", "." },
		{ "mrs.", "mrs", "." },
		{ "" },
	};
	for (size_t i = 0; i < sizeof(explicit_cases) / sizeof(*explicit_cases);
	     i++) {
		J *expected = ARR();
		for (int j = 1; j < 8 && explicit_cases[i][j]; j++)
			ADD(expected, STR(explicit_cases[i][j]));
		J *actual = tokenize(explicit_cases[i][0]);
		count_case("tokenizer_explicit");
		equal_json("tokenizer_explicit", explicit_cases[i][0], actual,
			   expected);
		DEL(actual);
		DEL(expected);
	}
	static const char *frames[][2] = { { "", "" },	     { "(", ")" },
					   { "[", "]" },     { "~", "~~" },
					   { "\t", "\r\n" }, { "😀 ", " café" },
					   { "", "..." },    { "\"", "\"" } };
	for (char a = 'A'; a <= 'Z'; a++)
		for (char b = 'A'; b <= 'Z'; b++)
			for (size_t f = 0; f < 8; f++) {
				char *text = printf_alloc(
					"%s%c.%c. History to 1865%s",
					frames[f][0], a, b, frames[f][1]);
				J *tokens = tokenize(text);
				char *joined = join(tokens, "");
				char *v1 = visible(joined), *v2 = visible(text);
				count_case("tokenizer_initialisms");
				equal_str("tokenizer_initialisms", text, v1,
					  v2);
				equal_int("tokenizer_initialisms",
					  "tilde count", tilde_count(joined),
					  tilde_count(text));
				free(v1);
				free(v2);
				free(joined);
				DEL(tokens);
				free(text);
			}
	static const char *symbols[] = { "A", "B", "C",	 "X",  "Y",  "Z",  "a",
					 "b", "c", "x",	 "y",  "z",  "0",  "1",
					 "9", ".", "~",	 ",",  ";",  ":",  "!",
					 "?", "(", ")",	 "[",  "]",  "{",  "}",
					 " ", "/", "\\", "\t", "\n", "\r", "_",
					 "é", "Ω", "中", "😀" };
	for (int index = 0; index < 3000; index++) {
		/*
		 * Leading repeated periods prevent dictionary substitution; the
		 * alphabet deliberately excludes contraction and quote rules.
		 */
		Buf text = { 0 };
		buf_put(&text, "..");
		int length = 1 + (int)rng(150);
		for (int i = 0; i < length; i++)
			buf_put(&text, symbols[rng(sizeof(symbols) /
						   sizeof(*symbols))]);
		J *tokens = tokenize(text.p);
		char *joined = join(tokens, "");
		char *v1 = visible(joined), *v2 = visible(text.p);
		char label[32];
		snprintf(label, sizeof(label), "%d", index);
		count_case("tokenizer_seeded_source_preservation");
		equal_str("tokenizer_seeded_source_preservation", label, v1,
			  v2);
		free(v1);
		free(v2);
		free(joined);
		DEL(tokens);
		free(text.p);
	}
}

static const char *predicates[] = { "rdfs:label", "rdfs:seeAlso",
				    "skos:altLabel" };

static void prepared_matching(void)
{
	for (char a = 'A'; a <= 'Z'; a++)
		for (char b = 'A'; b <= 'Z'; b++)
			for (int p = 0; p < 3; p++) {
				char *phrase = printf_alloc(
					"%c.%c. History to 1865", a, b);
				char *turtle = ontology(phrase, predicates[p]);
				load(turtle);
				char sa[2] = { a, 0 }, sb[2] = { b, 0 };
				const char *parts[] = { sa,	    ".",
							sb,	    ". ",
							"History ", "to ",
							"1865" };
				J *original = prepared(parts, 7);
				J *result = parse_tokens(original);
				J *found = matches(result, "target");
				char *label = printf_alloc("%s %s", phrase,
							   predicates[p]);
				count_case("prepared_ontology_matching");
				equal_int("prepared_ontology_matching", label,
					  SIZE(found), 1);
				if (SIZE(found)) {
					J *history = leaves(AT(found, 0));
					equal_json("prepared_ontology_matching",
						   label, history, original);
					equal_str("prepared_ontology_matching",
						  label,
						  S(GET(GET(AT(found, 0),
							    "swaps"),
							"type")),
						  "exact");
					DEL(history);
				}
				DEL(found);
				DEL(result);
				/*
				 * A different year must not become the target,
				 * even though the generic History class can
				 * still match.
				 */
				parts[6] = "1866";
				J *negative = prepared(parts, 7);
				result = parse_tokens(negative);
				found = matches(result, "target");
				count_case("prepared_negative_year");
				equal_int("prepared_negative_year", label,
					  SIZE(found), 0);
				DEL(found);
				DEL(result);
				DEL(negative);
				DEL(original);
				free(label);
				free(turtle);
				free(phrase);
			}
}

static char *ascii_case(const char *s, int upper_case)
{
	char *out = copy(s);
	for (char *c = out; *c; c++)
		*c = (char)(upper_case ? toupper((unsigned char)*c) :
					 tolower((unsigned char)*c));
	return out;
}

static void raw_matching(void)
{
	static const char *abbreviations[] = { "U.S.", "U.K.",	 "E.U.",
					       "U.N.", "D.C.",	 "B.C.",
					       "A.D.", "U.S.A.", "N.A.T.O." };
	static const char *years[] = { "1865", "1776", "2026" };
	static const char *frames[][2] = {
		{ "", "" },	    { "(", ")" },
		{ "[", "]" },	    { "\"", "\"" },
		{ "😀 ", " café" }, { "\n\t", "\r\n" },
		{ "~ ", " ~~" },    { "prefix: ", "; suffix" }
	};
	for (int ai = 0; ai < 9; ai++)
		for (int yi = 0; yi < 3; yi++)
			for (int p = 0; p < 3; p++) {
				char *phrase = printf_alloc("%s History to %s",
							    abbreviations[ai],
							    years[yi]);
				char *turtle = ontology(phrase, predicates[p]);
				load(turtle);
				char *spellings[] = { copy(phrase),
						      ascii_case(phrase, 0),
						      ascii_case(phrase, 1) };
				for (int f = 0; f < 8; f++)
					for (int s = 0; s < 3; s++) {
						char *text = printf_alloc(
							"%s%s%s", frames[f][0],
							spellings[s],
							frames[f][1]);
						J *parsed = parse(text);
						J *found = matches(parsed,
								   "target");
						char *label = printf_alloc(
							"%s %s", predicates[p],
							text);
						count_case(
							"raw_ontology_matching");
						equal_int(
							"raw_ontology_matching",
							label, SIZE(found), 1);
						if (SIZE(found)) {
							char *o = leaf_text(
								AT(found, 0));
							char *v1 = visible(o),
							     *v2 = visible(
								     spellings[s]);
							equal_str(
								"raw_ontology_matching",
								label, v1, v2);
							free(v1);
							free(v2);
							free(o);
						}
						Buf all = { 0 };
						EACH(t, GET(parsed, "tokens")) {
							char *o = leaf_text(t);
							buf_put(&all, o);
							free(o);
						}
						char *v1 = visible(
							     all.p ? all.p :
								     ""),
						     *v2 = visible(text);
						equal_str(
							"raw_ontology_matching",
							label, v1, v2);
						free(v1);
						free(v2);
						free(all.p);
						free(label);
						DEL(found);
						DEL(parsed);
						free(text);
					}
				for (int s = 0; s < 3; s++)
					free(spellings[s]);
				char next_year[16];
				snprintf(next_year, sizeof(next_year), "%d",
					 atoi(years[yi]) + 1);
				char *negatives[] = {
					replace(phrase, years[yi], next_year),
					replace(phrase, "History", "Chemistry"),
					printf_alloc("X.%s", phrase),
					replace(phrase, ".", "~"),
					replace(phrase, ".", "")
				};
				for (int n = 0; n < 5; n++) {
					J *parsed = parse(negatives[n]);
					J *found = matches(parsed, "target");
					count_case("raw_negative");
					/*
					 * A prefixed initial can leave a valid
					 * exact suffix; only the suffix is
					 * allowed and may not absorb the extra
					 * initial.
					 */
					if (n == 2) {
						EACH(token, found) {
							char *o = leaf_text(
								token);
							char *v1 = visible(o),
							     *v2 = visible(
								     phrase);
							equal_str(
								"raw_negative",
								negatives[n],
								v1, v2);
							free(v1);
							free(v2);
							free(o);
						}
					} else
						equal_int("raw_negative",
							  negatives[n],
							  SIZE(found), 0);
					DEL(found);
					DEL(parsed);
					free(negatives[n]);
				}
				free(turtle);
				free(phrase);
			}
}

static void contexts(void)
{
	const char *phrase = "U.S. History to 1865";
	char *turtle = ontology(phrase, "rdfs:seeAlso");
	load(turtle);
	free(turtle);
	static const char *separators[] = { " ",	   "  ",   "\t",
					    "\n",	   "\r\n", "\xc2\xa0",
					    "\xe2\x80\x83" };
	for (int i = 0; i < 7; i++) {
		char *text = printf_alloc("U.S.%sHistory%sto%s1865",
					  separators[i], separators[i],
					  separators[i]);
		J *parsed = parse(text), *found = matches(parsed, "target");
		count_case("internal_whitespace");
		equal_int("internal_whitespace", text, SIZE(found), 1);
		DEL(found);
		DEL(parsed);
		free(text);
	}
	static const char *joins[] = { " / ", "\n", " ... ", " ~~ " };
	for (int i = 0; i < 4; i++) {
		char *text = printf_alloc("%s%s%s", phrase, joins[i], phrase);
		J *parsed = parse(text), *found = matches(parsed, "target");
		count_case("repeated_occurrences");
		equal_int("repeated_occurrences", text, SIZE(found), 2);
		DEL(found);
		DEL(parsed);
		free(text);
	}
	/* Exercise ontology replacement and snapshot reload in one engine. */
	J *q = OBJ();
	PUT(q, "op", STR("snapshot"));
	J *r = call(q),
	  *snapshot = cJSON_DetachItemFromObjectCaseSensitive(r, "result");
	DEL(r);
	turtle = ontology("U.K. History to 1865", "rdfs:seeAlso");
	load(turtle);
	free(turtle);
	J *parsed = parse(phrase), *found = matches(parsed, "target");
	equal_int("reload", "old phrase removed", SIZE(found), 0);
	DEL(found);
	DEL(parsed);
	q = OBJ();
	PUT(q, "op", STR("load"));
	PUT(q, "snapshot", snapshot);
	PUT(q, "name", STR("roundtrip"));
	DEL(call(q));
	parsed = parse(phrase);
	found = matches(parsed, "target");
	equal_int("reload", "snapshot phrase restored", SIZE(found), 1);
	DEL(found);
	DEL(parsed);
	count_case("reload");
	count_case("reload");
}

static void window_boundaries(void)
{
	static const int lengths[] = { 1,  2,  6,  9,  10, 11, 15,  16,
				       31, 32, 33, 63, 64, 65, 127, 128 };
	static const char *gaps[] = { " ",    "  ",	  "\t",		 "\n",
				      "\r\n", "\xc2\xa0", "\xe2\x80\x83" };
	static const char *punctuation[] = { "~", ".", ",", ":",
					     "!", "?", "(", ")" };
	for (int l = 0; l < 16; l++) {
		int size = lengths[l];
		char **words = calloc((size_t)size, sizeof(char *));
		Buf phrase = { 0 };
		for (int i = 0; i < size; i++) {
			words[i] = printf_alloc("word%d", i);
			if (i)
				buf_put(&phrase, " ");
			buf_put(&phrase, words[i]);
		}
		char *turtle = ontology(phrase.p, "rdfs:seeAlso");
		load(turtle);
		free(turtle);
		for (int g = 0; g < 7; g++) {
			int n = 2 * size - 1;
			const char **source =
				calloc((size_t)n + 4, sizeof(char *));
			for (int i = 0; i < size; i++) {
				source[2 * i] = words[i];
				if (i + 1 < size)
					source[2 * i + 1] = gaps[g];
			}
			static const char *prefixes[][2] = { { NULL, NULL },
							     { "before", " " },
							     { NULL, NULL },
							     { "~", " " } };
			static const char *suffixes[][2] = { { NULL, NULL },
							     { NULL, NULL },
							     { " ", "after" },
							     { " ", "~" } };
			for (int ps = 0; ps < 4; ps++) {
				const char **parts =
					calloc((size_t)n + 4, sizeof(char *));
				int count = 0, lead = 0;
				if (prefixes[ps][0]) {
					parts[count++] = prefixes[ps][0];
					parts[count++] = prefixes[ps][1];
					lead = 2;
				}
				for (int i = 0; i < n; i++)
					parts[count++] = source[i];
				if (suffixes[ps][0]) {
					parts[count++] = suffixes[ps][0];
					parts[count++] = suffixes[ps][1];
				}
				J *original = prepared(parts, count);
				J *result = parse_tokens(original);
				J *found = matches(result, "target");
				char *label = printf_alloc(
					"size %d gap %d frame %d", size, g, ps);
				count_case("window_boundaries");
				equal_int("window_boundaries", label,
					  SIZE(found), 1);
				if (SIZE(found)) {
					J *history = leaves(AT(found, 0)),
					  *expected = ARR();
					for (int i = lead; i < lead + n; i++)
						ADD(expected,
						    DUP(AT(original, i)));
					equal_json("window_boundaries", label,
						   history, expected);
					DEL(history);
					DEL(expected);
				}
				free(label);
				DEL(found);
				DEL(result);
				DEL(original);
				free((void *)parts);
			}
			/* Punctuation is not whitespace and cannot be skipped. */
			if (size > 1)
				for (int p = 0; p < 8; p++) {
					const char *saved = source[1];
					source[1] = punctuation[p];
					J *tokens = prepared(source, n);
					J *result = parse_tokens(tokens);
					J *found = matches(result, "target");
					char *label = printf_alloc(
						"size %d gap %d punct %s", size,
						g, punctuation[p]);
					count_case(
						"punctuation_is_not_whitespace");
					equal_int(
						"punctuation_is_not_whitespace",
						label, SIZE(found), 0);
					free(label);
					DEL(found);
					DEL(result);
					DEL(tokens);
					source[1] = saved;
				}
			free((void *)source);
		}
		for (int i = 0; i < size; i++)
			free(words[i]);
		free(words);
		free(phrase.p);
	}
}

static void longest_leftmost(void)
{
	static const char *lines[] = {
		":Short a owl:Class; rdfs:label \"U.S. History\" .",
		":Long a owl:Class; rdfs:label \"U.S. History to 1865\" .",
		":Tail a owl:Class; rdfs:label \"History to 1865\" .",
		":Other a owl:Class; rdfs:label \"U.S. History since 1865\" ."
	};
	static const char *gaps[] = { " ", "\t", "\n", "\r\n" };
	static const int copies[] = { 1, 2, 5, 10 };
	for (int order = 0; order < 2; order++) {
		Buf turtle = { 0 };
		buf_put(&turtle, PREFIXES);
		for (int i = 0; i < 4; i++) {
			buf_put(&turtle, lines[order ? 3 - i : i]);
			buf_put(&turtle, "\n");
		}
		load(turtle.p);
		free(turtle.p);
		for (int g = 0; g < 4; g++) {
			char *phrase = printf_alloc("U.S.%sHistory%sto%s1865",
						    gaps[g], gaps[g], gaps[g]);
			for (int c = 0; c < 4; c++) {
				Buf text = { 0 };
				J *expected = ARR();
				for (int i = 0; i < copies[c]; i++) {
					if (i)
						buf_put(&text, " / ");
					buf_put(&text, phrase);
					ADD(expected, STR("long"));
				}
				J *result = parse(text.p), *canons = ARR();
				EACH(t, GET(result, "tokens"))
					if (GET(t, "swaps"))
						ADD(canons,
						    DUP(GET(GET(t, "swaps"),
							    "canon")));
				count_case("longest_leftmost");
				equal_json("longest_leftmost", text.p, canons,
					   expected);
				DEL(canons);
				DEL(expected);
				DEL(result);
				free(text.p);
			}
			free(phrase);
		}
	}
}

static void malformed_lookup(void)
{
	char *turtle = ontology("U.S. History to 1865", "rdfs:seeAlso");
	load(turtle);
	free(turtle);
	J *q = OBJ();
	PUT(q, "op", STR("snapshot"));
	J *r = call(q),
	  *snapshot = cJSON_DetachItemFromObjectCaseSensitive(r, "result");
	DEL(r);
	const char *parts[] = { "U", ".", "S", ".", "History", "to", "1865" };
	J *tokens = prepared(parts, 7);
	static const char *lookups[] = {
		"[]",	   "[\"bad\"]", "[{\"words\":[]}]", "3",
		"\"bad\"", "{\"7\":3}", "{\"7\":null}",	    "{\"7\":\"bad\"}"
	};
	for (int i = 0; i < 8; i++) {
		J *lookup = cJSON_Parse(lookups[i]), *invalid = DUP(snapshot);
		set(GET(invalid, "synonyms"), "lookup", DUP(lookup));
		q = OBJ();
		PUT(q, "op", STR("load"));
		PUT(q, "snapshot", invalid);
		DEL(call(q));
		q = OBJ();
		PUT(q, "op", STR("parse_tokens"));
		PUT(q, "tokens", DUP(tokens));
		r = call(q);
		count_case("malformed_lookup_recovery");
		/*
		 * The API rejects childless lookup values before matching with
		 * its Empty ontology error. Nonempty malformed structures
		 * reach the exact matcher's type validation.
		 */
		int nonempty =
			(cJSON_IsArray(lookup) || cJSON_IsObject(lookup)) &&
			SIZE(lookup);
		equal_int("malformed_lookup_recovery", lookups[i],
			  cJSON_IsFalse(GET(r, "ok")), 1);
		equal_int("malformed_lookup_recovery", lookups[i],
			  GET(GET(r, "error"), "code") ?
				  GET(GET(r, "error"), "code")->valueint :
				  0,
			  nonempty ? 2 : 4);
		DEL(r);
		q = OBJ();
		PUT(q, "op", STR("load"));
		PUT(q, "snapshot", DUP(snapshot));
		DEL(call(q));
		J *result = parse_tokens(tokens),
		  *found = matches(result, "target");
		equal_int("malformed_lookup_recovery", "recovered", SIZE(found),
			  1);
		DEL(found);
		DEL(result);
		DEL(lookup);
	}
	DEL(tokens);
	DEL(snapshot);
}

int main(int argc, char **argv)
{
	(void)argv;
	if (argc != 2)
		return 2;
	engine = mc_create();
	tokenizer();
	prepared_matching();
	raw_matching();
	contexts();
	window_boundaries();
	longest_leftmost();
	malformed_lookup();
	mc_destroy(engine);
	int total = 0;
	for (int i = 0; i < category_count; i++) {
		printf("  %-40s %d\n", categories[i].name, categories[i].cases);
		total += categories[i].cases;
	}
	printf("Punctuation contracts: %d cases, %d assertions, %d failed.\n",
	       total, assertions, failures);
	return failures ? 1 : 0;
}
