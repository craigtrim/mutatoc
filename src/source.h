/*
 * source.h - Shared semantic boundary for structured ontology readers.
 *
 * Readers resolve terms and emit facts directly into the runtime graph.
 */
#ifndef MC_SOURCE_H
#define MC_SOURCE_H
#include "mc.h"

typedef struct {
	Graph *graph;
	char *base;
	J *prefixes;
	mc_error *error;
} Source;

int source_init(Source *, const char *, mc_error *);
void source_clear(Source *);
void source_base(Source *, const char *);
void source_prefix(Source *, const char *, const char *);
Term source_resource(Source *, const char *, int);
/* Returns the alias predicate and its default value kind, or NULL. */
const char *source_field(const char *, int *);
void source_emit(Source *, const Term *, const Term *, Term *);
#endif
