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
char *lower(const char *);
char *upper(const char *);
char *uri_resolve(const char *, const char *);
char *file_uri(const char *);
size_t ulen(const char *);
uint32_t uread(const char **);
void uwrite(Buf *, uint32_t);
int ualpha(uint32_t);
int uspace(uint32_t);
void fail(mc_error *, int, const char *, ...);
FILE *mc_fopen(const char *, const char *);
char *read_file(const char *, mc_error *);
J *json_parse(const char *, mc_error *);
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
void *map_get(Map *, const char *);
void map_put(Map *, const char *, void *);
void map_free(Map *);
void unique_indexed(J *, Map *, const char *);
typedef struct {
	char *value, *datatype, *language;
	int kind;
} Term;
typedef struct {
	Term s, p, o;
} Triple;
typedef struct {
	Triple *ts;
	size_t n, cap;
	J *prefixes;
	Map subjects;
	Map triple_keys;
} Graph;
void literal_normalize(Term *);
Graph *rdf_parse(const char *, const char *, mc_error *);
void rdf_free(Graph *);
Graph *rdf_merge(Graph **, size_t);
J *rdf_values(Graph *, const char *, const char *);
J *rdf_flatten(Graph *, Term *, int, mc_error *);
J *rdf_json(Graph *);
J *ontology_build(Graph *, int, int, mc_error *);
J *graph_predicate(Graph *, const char *, int, int, mc_error *);
J *merge_views(J *, const J *);
J *external_synonyms(const char *);
J *live_view(mc_engine *, mc_error *);
J *data_query(J *, const char *, const J *, const J *, mc_error *);
J *ontology_query(J *, const char *, const J *, mc_error *);
J *generate_synonyms(J *, int);
J *generate_lookup(J *);
J *generate_spans(J *, int, int);
J *generate_trie(J *);
J *tokenize_text(const char *);
char *tokenize_key(const char *);
typedef struct MatchIndex MatchIndex;
MatchIndex *match_index_build(const J *);
const J *match_index_view(const MatchIndex *);
void match_index_free(MatchIndex *);
J *match_tokens(const MatchIndex *, J *, const J *, int, mc_error *);
J *transform_tokens(J *, const MatchIndex *, const J *, const J *, const char *,
		    mc_error *);
char *render(const J *);
struct mc_engine {
	J *snapshot;
	J *live;
	J *names;
	char *directory;
	char *source_path;
	int live_mode;
	int graph_only, force_class;
	Graph *graph;
	char *name;
	int distance;
	mc_engine **parts;
	size_t part_count;
	MatchIndex *index; /* hash index of the view matching last used; owned */
};
#endif
