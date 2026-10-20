/*
 * mc.h - Internal engine types and shared interfaces.
 *
 * Declares graph storage, matching indexes, the tokenizer, and utility helpers.
 * craigtrim/mutatoc#1
 */

#ifndef MC_INTERNAL_H
#define MC_INTERNAL_H
#include "cJSON.h"
#include "mutatoc.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef cJSON J;
#define EACH(v, a) for (J *v = (a) ? (a)->child : NULL; v; v = v->next)
#define OBJ cJSON_CreateObject
#define ARR cJSON_CreateArray
#define STR cJSON_CreateString
#define NUM cJSON_CreateNumber
#define NIL cJSON_CreateNull
#define BOOL cJSON_CreateBool
#define DEL cJSON_Delete
#define DUP(v) cJSON_Duplicate((v), 1)
#define GET cJSON_GetObjectItemCaseSensitive
#define AT cJSON_GetArrayItem
#define SIZE cJSON_GetArraySize
#define ADD cJSON_AddItemToArray
#define PUT cJSON_AddItemToObject
#define S(v) ((v) && cJSON_IsString(v) ? (v)->valuestring : "")
#define RDF "http://www.w3.org/1999/02/22-rdf-syntax-ns#"
#define RDFS "http://www.w3.org/2000/01/rdf-schema#"
#define OWL "http://www.w3.org/2002/07/owl#"
#define SKOS "http://www.w3.org/2004/02/skos/core#"
#define XSD "http://www.w3.org/2001/XMLSchema#"
typedef struct {
	char *p;
	size_t n, cap;
} Buf;
void buf_add(Buf *, const char *, size_t);
void buf_put(Buf *, const char *);
char *buf_take(Buf *);
char *copy(const char *);
char *slice(const char *, size_t);
char *replace(const char *, const char *, const char *);
char *norm(const char *, int lower, int spaces);
/* Normalization views borrow unchanged ASCII; caller frees only *owned. */
const char *norm_view(const char *, int, int, char **owned);
const char *norm_borrowed(const char *, char **owned);
char *lower(const char *);
char *upper(const char *);
char *uri_resolve(const char *, const char *);
char *file_uri(const char *);
size_t ulen(const char *);
uint32_t uread(const char **);
void uwrite(Buf *, uint32_t);
int ualpha(uint32_t);
int unumeric(uint32_t);
int uspace(uint32_t);
void fail(mc_error *, int, const char *, ...);
FILE *mc_fopen(const char *, const char *);
char *read_file(const char *, mc_error *);
J *json_parse(const char *, mc_error *);
J *json_parse_record(const char *, size_t, const char **, mc_error *);
void set(J *, const char *, J *);
J *ensure(J *, const char *, int);
void unique(J *, const char *);
int contains(const J *, const char *);
void sort_strings(J *, int mode);
char *join(const J *, const char *);
J *split(const char *, const char *);
const char *local(const char *);
typedef struct {
	char *key;
	void *value;
} Slot;
typedef struct {
	Slot *slots;
	size_t size, cap;
} Map;
typedef struct PrefixIndex PrefixIndex;
void prefix_index_change(PrefixIndex **, const char *, int);
int prefix_index_has(const PrefixIndex *, const char *);
void prefix_index_free(PrefixIndex *);
void parallel_three(void (*)(void *), void (*)(void *), void (*)(void *),
		    void *, int);
uint64_t hash_text(const char *);
void *map_get(Map *, const char *);
void map_put(Map *, const char *, void *);
/* Referenced keys must outlive their entries; deletion does not free them. */
void map_put_ref(Map *, const char *, void *);
/* Construction maps borrow keys; a returned slot lasts until the next mutation. */
Slot *map_entry_ref(Map *, const char *);
void map_reserve(Map *, size_t);
void map_put_small(Map *, const char *, void *);
void map_free(Map *);
void map_delete(Map *, const char *);
/* Indexed builders avoid repeated object scans (craigtrim/axiom#129). */
void unique_indexed(J *, Map *, const char *);
J *indexed_ensure(J *, Map *, const char *, int);
void indexed_set(J *, Map *, const char *, J *);
/* Optional annotations do not occupy two pointers in every RDF term (#129). */
typedef struct {
	char *datatype, *language;
} TermAttributes;
typedef struct {
	char *value;
	TermAttributes *attributes;
	int kind;
	int shared_value; /* Graph-owned string (craigtrim/axiom#129). */
} Term;
static inline const char *rdf_datatype(const Term *term)
{
	return term->attributes ? term->attributes->datatype : NULL;
}
static inline const char *rdf_language(const Term *term)
{
	return term->attributes ? term->attributes->language : NULL;
}
void rdf_term_attributes(Term *, const char *, const char *);
typedef struct {
	Term s, p, o;
} Triple;
typedef struct LookupTrace LookupTrace;
typedef struct GraphIds GraphIds;
typedef struct GraphStrings GraphStrings;
LookupTrace *lookup_trace_create(void);
void lookup_trace_reserve(LookupTrace *, size_t);
void lookup_trace_add(LookupTrace *, J *, J *);
void lookup_trace_free(LookupTrace *);
typedef struct {
	uint64_t hash;
	size_t next;
} TripleLink;
typedef struct {
	Triple *ts;
	size_t n, cap;
	J *prefixes;
	Map subjects;
	GraphIds *subject_ids; /* Pooled read-only JSON wrappers for triple IDs. */
	size_t *triple_heads;
	TripleLink *triple_links;
	size_t triple_capacity;
	Map resources; /* Shared IRI and blank-node values owned by this graph. */
	LookupTrace *
		lookup_trace; /* Borrowed JSON strings; owned construction records. */
	GraphStrings *strings;
} Graph;
void literal_normalize(Term *);
Term rdf_term(const char *, int);
Term rdf_term_copy(const Term *);
void rdf_term_free(Term *);
Graph *rdf_create(mc_error *);
void rdf_add(Graph *, const Term *, const Term *, const Term *);
void rdf_replace_object(Graph *, size_t, const Term *);
void rdf_bind_prefix(Graph *, const char *, const char *);
Graph *rdf_parse(const char *, const char *, mc_error *);
/* Source readers return a graph, or a legacy prepared snapshot, atomically. */
Graph *source_read(const char *, const char *, const char *, J **, mc_error *);
Graph *json_graph_read(const char *, const char *, int, J **, mc_error *);
void rdf_free(Graph *);
Graph *rdf_merge(Graph **, size_t);
J *rdf_values(Graph *, const char *, const char *);
J *rdf_flatten(Graph *, Term *, int, mc_error *);
J *rdf_json(Graph *);
J *ontology_build(Graph *, int, int, int, mc_error *);
J *ontology_update(mc_engine *, J *, mc_error *);
J *graph_predicate(Graph *, const char *, int, int, mc_error *);
J *merge_views(J *, const J *);
J *external_synonyms(const char *);
J *live_view(mc_engine *, mc_error *);
J *data_query(J *, const char *, const J *, const J *, mc_error *);
J *ontology_query(J *, const char *, const J *, mc_error *);
J *generate_synonyms(J *, int);
J *generate_lookup(J *);
J *generate_lookup_shared(J *, J *);
J *generate_lookup_cached(J *, J *, J *);
J *generate_lookup_traced(J *, LookupTrace **);
char *lookup_leading_gram(const char *text, int words);
int lookup_rewritable(const char *text, int words);
char *lookup_rewrite(const char *text, const char *gram);
typedef struct LookupState LookupState;
LookupState *lookup_state_build(J *);
LookupState *lookup_state_reuse(J *, J *);
LookupState *lookup_state_reuse_traced(J *, J *, LookupTrace *);
void lookup_state_entity(LookupState *, J *, int);
void lookup_state_flush(LookupState *);
J *lookup_state_json(LookupState *);
void lookup_state_free(LookupState *);
void lookup_state_mirror(LookupState *, J *);
void lookup_state_order(LookupState *, J *, int);
void lookup_state_watch(LookupState *, void *,
			void (*)(void *, int, const char *, int));
J *generate_spans(J *, int, int, int);
J *generate_spans_indexed(J *, int, int, int, Map *);
J *generate_trie(J *);
J *tokenize_text(const char *);
char *tokenize_key(const char *);
/* Entity text from the input between x and y (craigtrim/mutatoc#7). */
void source_entities(J *, const char *);
typedef struct MatchIndex MatchIndex;
MatchIndex *match_index_build(const J *);
MatchIndex *match_index_build_mutable(const J *);
const J *match_index_view(const MatchIndex *);
void match_index_free(MatchIndex *);
void match_index_mutable(MatchIndex *);
void match_index_group_order(MatchIndex *, J *);
void match_index_word(void *, int, const char *, int);
void match_index_member(MatchIndex *, const char *, const char *, J *);
J *match_tokens(const MatchIndex *, J *, const J *, int, mc_error *);
J *transform_tokens(J *, const MatchIndex *, const J *, const J *, const char *,
		    mc_error *);
char *render(const J *);
typedef struct UpdateState UpdateState;
void update_state_build(mc_engine *);
void update_state_free(UpdateState *);
int update_fast(mc_engine *, Graph *, Map *);
struct mc_engine {
	J *snapshot;
	J *live;
	J *names;
	char *directory;
	char *source_path;
	int live_mode;
	int graph_only, force_class, comma_lists;
	Graph *graph;
	char *name;
	int distance;
	mc_engine **parts;
	size_t part_count;
	UpdateState *updates;
	MatchIndex *index; /* hash index of the view matching last used; owned */
};
#endif
