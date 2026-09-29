/*
 * lingpatlab.h - Internal interfaces for native LingPatLab operations.
 *
 * Connects token processing, text utilities, and request validation.
 */

#ifndef MUTATOC_LINGPATLAB_H
#define MUTATOC_LINGPATLAB_H
#include "mc.h"

J *lp_data(const char *);
int lp_numeric(uint32_t);
int lp_upper(uint32_t);
int lp_punct(uint32_t);
int lp_all_upper(const char *);
int lp_all_numeric(const char *);
int lp_ends(const char *, const char *);
char *lp_replace(char *, const char *, const char *);
char *lp_strip(char *);
char *lp_collapse(char *);
J *lp_words(const char *);
char *lp_capitalize(const char *);
char *lp_stem(const char *);
int lp_wordnet(const char *);
J *lp_tokenize(const char *);
J *lp_parse_tokens(mc_engine *, const J *, mc_error *);
J *lp_postprocess(const J *, mc_error *);
J *lp_sequence(const J *, int);
J *lp_analyze(const J *, const J *);
J *lp_people_method(const char *, const J *, mc_error *);
J *lp_extract(const J *, int, mc_error *);
J *lp_text_method(const char *, const J *, mc_error *);
J *lp_segment_method(mc_engine *, const char *, const J *, mc_error *);
int lp_validate(const J *, mc_error *);
J *lp_request(mc_engine *, const J *, mc_error *);
J *spacy_call(mc_engine *, const J *, mc_error *);
#endif
