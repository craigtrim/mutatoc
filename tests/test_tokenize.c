/*
 * test_tokenize.c - Native tokenizer contract.
 *
 * Pins the token texts, offsets and ids the matcher sees, including the
 * places where tokens deliberately stay whole: contractions, units written
 * against a number, and words such as cannot or id.
 * craigtrim/mutatoc#1
 */

#include "testlib.h"

static int failed = 0, total = 0;

static void expect(mc_engine *e, const char *text, const char *expected)
{
	J *q = OBJ();
	PUT(q, "op", STR("tokenize"));
	PUT(q, "text", STR(text));
	J *r = test_request(e, q), *texts = ARR();
	DEL(q);
	EACH(t, GET(r, "result"))
		ADD(texts, DUP(GET(t, "text")));
	J *want = cJSON_Parse(expected);
	total++;
	if (!cJSON_Compare(texts, want, 1)) {
		failed++;
		test_print_diff(text, want, texts);
	}
	DEL(want);
	DEL(texts);
	DEL(r);
}

static int ends_with_index(const char *id, const char *suffix)
{
	size_t a = strlen(id), b = strlen(suffix);
	return a > b && !strcmp(id + a - b, suffix);
}

int main(void)
{
	mc_engine *e = mc_create();
	/* Punctuation splits off; words keep their trailing space. */
	expect(e, "Dog walks through London.",
	       "[\"Dog \",\"walks \",\"through \",\"London\",\".\"]");
	expect(e, "U.S. History", "[\"U\",\".\",\"S\",\". \",\"History\"]");
	/* A period or comma that ends a number is its own token. */
	expect(e, "In 2020.", "[\"In \",\"2020\",\".\"]");
	expect(e, "1,234.56, then 3.14.",
	       "[\"1,234.56\",\", \",\"then \",\"3.14\",\".\"]");
	expect(e, "CG180.", "[\"CG180\",\".\"]");
	/* Underscores that open or close a word are split off. */
	expect(e, "__main__ and Muslin_",
	       "[\"_\",\"_\",\"main\",\"_\",\"_ \",\"and \",\"Muslin\",\"_\"]");
	expect(e, "snake_case", "[\"snake_case\"]");
	/*
	 * A lone single quote reads as a double quote, and a word sits
	 * directly against a closing quote, parenthesis, ! or ?.
	 */
	expect(e, "say 'hi' now", "[\"say\",\"\\\"\",\"hi\",\"\\\"\",\"now\"]");
	/* These stay whole. */
	expect(e, "I cannot", "[\"I \",\"cannot\"]");
	expect(e, "id card", "[\"id \",\"card\"]");
	expect(e, "5G at 9am, 500mg",
	       "[\"5G \",\"at \",\"9am\",\", \",\"500mg\"]");
	expect(e, "y'all and guv'nor's", "[\"y'all \",\"and \",\"guv'nor's\"]");
	expect(e, "don't O'Brien", "[\"don't \",\"O'Brien\"]");
	/* Contractions in the expansion list become their words. */
	expect(e, "can't", "[\"can\",\"not\"]");
	/* Other whitespace is kept as a separate zero-width token. */
	expect(e, "Dog\tcat\nPoodle",
	       "[\"Dog\",\"\\t \",\"cat\",\" \",\"Poodle\"]");
	expect(e, "", "[]");
	/* Ids: the text's MurmurHash64A (seed 1) and the token's position. */
	J *r = test_call(e, "{\"op\":\"tokenize\",\"text\":\"Dog walks\"}");
	total++;
	if (!r || strcmp(S(GET(AT(r, 0), "id")), "1390790794094431770#0") ||
	    !ends_with_index(S(GET(AT(r, 1), "id")), "#1") ||
	    SIZE(AT(r, 0)) != 5 || GET(AT(r, 0), "y")->valueint != 3 ||
	    strcmp(S(GET(AT(r, 1), "normal")), "walks")) {
		failed++;
		fprintf(stderr, "token fields or ids changed\n");
	}
	DEL(r);
	mc_destroy(e);
	printf("Tokenizer: %d/%d cases passed.\n", total - failed, total);
	return failed ? 1 : 0;
}
