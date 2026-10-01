/*
 * test_gold.c - Hand-written entity expectations for paragraphs and documents.
 *
 * Each text in tests/fixtures/gold/corpus.json was written and its expected
 * entities frozen before the engine ran on it, so this suite checks that the
 * engine finds the right entities rather than the same ones as a reference.
 * Every text must produce exactly its expected entities, in order: the same
 * canonical form, match type and NER label, over the same source text.
 * craigtrim/mutatoc#2
 */

#include "testlib.h"

/* Appends the source text of every original token under t, in order. */
static void leaf_text(Buf *b, const J *t)
{
	const J *swaps = GET(t, "swaps");
	if (!swaps) {
		buf_put(b, S(GET(t, "text")));
		return;
	}
	EACH(child, GET(swaps, "tokens"))
		leaf_text(b, child);
}

/* Text with every whitespace run collapsed to one space and the ends trimmed. */
static char *collapse_space(const char *s)
{
	char *out = norm(s, 0, 1);
	return out ? out : copy("");
}

static J *entity(const char *canon, const char *surface, const char *type,
		 const J *ner)
{
	J *e = OBJ();
	char *collapsed = collapse_space(surface);
	PUT(e, "canon", STR(canon));
	PUT(e, "surface", STR(collapsed));
	PUT(e, "type", STR(type));
	PUT(e, "ner", ner && !cJSON_IsNull(ner) ? DUP(ner) : NIL());
	free(collapsed);
	return e;
}

static J *found(const J *result)
{
	J *out = ARR();
	EACH(t, GET(result, "tokens")) {
		const J *swaps = GET(t, "swaps");
		if (!swaps)
			continue;
		Buf b = { 0 };
		leaf_text(&b, t);
		ADD(out, entity(S(GET(swaps, "canon")), b.p ? b.p : "",
				S(GET(swaps, "type")), GET(t, "ner")));
		free(b.p);
	}
	return out;
}

int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	J *corpus = test_read_json(argv[1], "tests/fixtures/gold/corpus.json");
	if (!test_require_cases(GET(corpus, "texts"), "gold corpus"))
		return 2;
	int texts = 0, failed = 0, entities = 0;
	EACH(t, GET(corpus, "texts")) {
		mc_engine *e = mc_create();
		Buf rel = { 0 };
		buf_put(&rel, "tests/fixtures/ontologies/");
		buf_put(&rel, S(GET(t, "ontology")));
		buf_put(&rel, ".owl");
		char *path = test_path(argv[1], rel.p);
		free(rel.p);
		J *q = OBJ();
		PUT(q, "op", STR("load"));
		PUT(q, "path", STR(path));
		free(path);
		J *r = test_request(e, q);
		DEL(q);
		int ok = cJSON_IsTrue(GET(r, "ok"));
		DEL(r);
		q = OBJ();
		PUT(q, "op", STR("parse"));
		PUT(q, "text", DUP(GET(t, "text")));
		r = test_request(e, q);
		DEL(q);
		J *expected = ARR();
		EACH(x, GET(t, "expected")) {
			ADD(expected,
			    entity(S(GET(x, "canon")), S(GET(x, "surface")),
				   S(GET(x, "type")), GET(x, "ner")));
			entities++;
		}
		J *actual = cJSON_IsTrue(GET(r, "ok")) ?
				    found(GET(r, "result")) :
				    NULL;
		ok = ok && actual && SIZE(expected) > 0 &&
		     cJSON_Compare(actual, expected, 1);
		if (!ok) {
			failed++;
			test_print_diff(S(GET(t, "id")), expected, actual);
		}
		texts++;
		DEL(actual);
		DEL(expected);
		DEL(r);
		mc_destroy(e);
	}
	printf("Gold corpus: %d/%d texts produced exactly their %d hand-written entities.\n",
	       texts - failed, texts, entities);
	DEL(corpus);
	return failed ? 1 : 0;
}
