/*
 * mutatoc.h - Public C API for ontology loading and matching.
 *
 * Declares engine ownership, worker configuration, and JSON requests.
 */

#ifndef MUTATOC_H
#define MUTATOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MUTATOC_VERSION "0.2.3"

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
 * Configure the spaCy worker for native LingPatLab preprocessing. It starts
 * lazily, loads the model once, and exits on engine destruction or
 * reconfiguration. Arguments are copied as UTF-8 and passed without a shell.
 * A zero timeout selects 120000 ms per startup or request. Returns 1 on
 * success. Raw parse/tokenize requires configuration; parse_tokens does not.
 */
int mc_use_spacy(mc_engine *engine, const char *python_executable,
		 const char *worker_script, const char *model,
		 unsigned timeout_ms, mc_error *error);

/*
 * Configure the persistent RDFLib compatibility worker for arbitrary SPARQL and
 * date/duration/base64/XML literal normalization during ontology load.
 */
int mc_use_sparql(mc_engine *engine, const char *python_executable,
		  const char *worker_script, unsigned timeout_ms,
		  mc_error *error);

/*
 * Execute one JSON request and return one JSON response. Operation errors are
 * represented as {"ok":false,"error":...}; allocation failure returns NULL.
 */
char *mc_request(mc_engine *engine, const char *request_json, mc_error *error);

#ifdef __cplusplus
}
#endif

#endif
