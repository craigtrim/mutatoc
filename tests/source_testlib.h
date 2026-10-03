/* Test-only serialization. Production readers never convert source formats. */
#ifndef MC_SOURCE_TESTLIB_H
#define MC_SOURCE_TESTLIB_H
#include "mc.h"

static J *source_test_term(const Term *term)
{
	J *value = OBJ();
	PUT(value, "value", STR(term->value));
	PUT(value, "kind",
	    STR(term->kind == 0 ? "iri" :
		term->kind == 1 ? "literal" :
				  "blank"));
	if (term->datatype)
		PUT(value, "datatype", STR(term->datatype));
	if (term->language)
		PUT(value, "language", STR(term->language));
	return value;
}

static void source_test_record(Buf *out, J *record, int lines, int first)
{
	if (!lines && !first)
		buf_put(out, ",\n");
	char *wire = cJSON_PrintUnformatted(record);
	buf_put(out, wire);
	if (lines)
		buf_put(out, "\n");
	free(wire);
	DEL(record);
}

static char *source_test_encode(Graph *graph, int lines)
{
	Buf out = { 0 };
	if (!lines)
		buf_put(&out, "[\n");
	J *header = OBJ();
	PUT(header, "format", STR("mutatoc/1"));
	PUT(header, "prefixes", DUP(graph->prefixes));
	source_test_record(&out, header, lines, 1);
	for (size_t i = 0; i < graph->n;) {
		J *record = OBJ(), *facts = ARR();
		Term *subject = &graph->ts[i].s;
		PUT(record, "id", source_test_term(subject));
		PUT(record, "facts", facts);
		do {
			J *fact = OBJ();
			PUT(fact, "predicate",
			    source_test_term(&graph->ts[i].p));
			PUT(fact, "value", source_test_term(&graph->ts[i].o));
			ADD(facts, fact);
			i++;
		} while (i < graph->n && graph->ts[i].s.kind == subject->kind &&
			 !strcmp(graph->ts[i].s.value, subject->value));
		source_test_record(&out, record, lines, 0);
	}
	if (!lines)
		buf_put(&out, "\n]\n");
	return buf_take(&out);
}
#endif
