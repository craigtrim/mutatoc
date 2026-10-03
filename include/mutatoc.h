/*
 * mutatoc.h - Public C API for ontology loading and matching.
 *
 * Declares engine ownership and JSON requests. Everything runs in process.
 * craigtrim/mutatoc#1
 */

#ifndef MUTATOC_H
#define MUTATOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MUTATOC_VERSION "0.5.0"

typedef struct mc_engine mc_engine;
typedef struct {
	int code;
	size_t line, column;
	char message[512];
} mc_error;

/*
 * Each engine owns its state. Callers serialize access to the same engine. All
 * input and returned strings are UTF-8. Returned buffers belong to the caller
 * and must be released with mc_free. Errors are caller-owned.
 */
mc_engine *mc_create(void);
void mc_destroy(mc_engine *engine);
void mc_free(void *buffer);

/*
 * Execute one JSON request and return one JSON response. Operation errors are
 * represented as {"ok":false,"error":...}; allocation failure returns NULL.
 */
char *mc_request(mc_engine *engine, const char *request_json, mc_error *error);

#ifdef __cplusplus
}
#endif

#endif
