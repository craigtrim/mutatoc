#include "lingpatlab.h"
#include <limits.h>
static int require_value(const J *q, const char *key, int kind, int optional, mc_error *err) {
    J *v = GET(q, key);
    if (!v && optional)
        return 1;
    int ok = kind == 1   ? cJSON_IsString(v)
             : kind == 2 ? cJSON_IsArray(v)
             : kind == 3 ? cJSON_IsObject(v)
             : kind == 4 ? (cJSON_IsNumber(v) && v->valuedouble >= INT_MIN &&
                            v->valuedouble <= INT_MAX && v->valuedouble == (double)v->valueint)
                         : v != NULL;
    if (!ok)
        fail(err, 2, "LingPatLab requires %s %s",
             kind == 1   ? "string"
             : kind == 2 ? "array"
             : kind == 3 ? "object"
             : kind == 4 ? "integer"
                         : "value",
             key);
    return ok;
}
static int strings(const J *a, mc_error *err) {
    if (!cJSON_IsArray(a)) {
        fail(err, 2, "Expected an array of strings");
        return 0;
    }
    EACH(v, a) if (!cJSON_IsString(v)) {
        fail(err, 2, "Expected string array element");
        return 0;
    }
    return 1;
}
static int tokens(const J *a, mc_error *err) {
    if (!cJSON_IsArray(a)) {
        fail(err, 2, "Expected an array of token objects");
        return 0;
    }
    EACH(v, a)
    if (!cJSON_IsObject(v) || !cJSON_IsString(GET(v, "text")) || !cJSON_IsString(GET(v, "pos")) ||
        !cJSON_IsString(GET(v, "ent"))) {
        fail(err, 2, "Tokens require string text, pos and ent fields");
        return 0;
    }
    return 1;
}
int lp_validate(const J *q, mc_error *err) {
    if (!require_value(q, "method", 1, 0, err))
        return 0;
    const char *m = S(GET(q, "method"));
    if (!strncmp(m, "text.", 5)) {
        m += 5;
        if (!strcmp(m, "sliding_window") || !strcmp(m, "most_similar_phrase") ||
            !strcmp(m, "longest_common_phrase") || !strcmp(m, "find_subsumed_tokens")) {
            if (!strings(GET(q, "tokens"), err))
                return 0;
            if ((!strcmp(m, "most_similar_phrase") || !strcmp(m, "longest_common_phrase")) &&
                !strings(GET(q, "tokens2"), err))
                return 0;
        } else if (strcmp(m, "update_csvs")) {
            int nullable = !strcmp(m, "split_on_len") || !strcmp(m, "ends_with_punctuation") ||
                           !strcmp(m, "remove_ending_punctuation");
            if (!(nullable && cJSON_IsNull(GET(q, "text"))) && !require_value(q, "text", 1, 0, err))
                return 0;
        }
        if ((!strcmp(m, "remove_duplicated_phrases") || !strcmp(m, "jaccard_similarity")) &&
            !require_value(q, "text2", 1, 0, err))
            return 0;
        if ((!strcmp(m, "sliding_window") || !strcmp(m, "most_similar_phrase")) &&
            !require_value(q, "window_size", 4, 0, err))
            return 0;
        if (GET(q, "threshold") && !require_value(q, "threshold", 4, 0, err))
            return 0;
        if (GET(q, "score_threshold") && !cJSON_IsNumber(GET(q, "score_threshold"))) {
            fail(err, 2, "score_threshold must be numeric");
            return 0;
        }
        if (GET(q, "punkt") && !strings(GET(q, "punkt"), err))
            return 0;
        return 1;
    }
    if (!strcmp(m, "people_sequence") || !strcmp(m, "topic_sequence"))
        return tokens(GET(q, "tokens"), err);
    if (!strcmp(m, "extract_people") || !strcmp(m, "extract_topics")) {
        if (!require_value(q, "sentences", 2, 0, err))
            return 0;
        EACH(s, GET(q, "sentences")) if (!tokens(s, err)) return 0;
        return 1;
    }
    if (!strcmp(m, "people_analyze"))
        return strings(GET(q, "exact"), err) && strings(GET(q, "fuzzy"), err);
    if (!strcmp(m, "people_index"))
        return strings(GET(q, "unigrams"), err) && strings(GET(q, "ngrams"), err);
    if (!strcmp(m, "people_remove_subsumed") || !strcmp(m, "people_aggregate") ||
        !strcmp(m, "people_cleanse")) {
        if (!require_value(q, "people", 3, 0, err))
            return 0;
        EACH(v, GET(q, "people")) if (!strings(v, err)) return 0;
        return 1;
    }
    if (!strcmp(m, "filter_title_phrases"))
        return strings(GET(q, "phrases"), err);
    if (!strcmp(m, "post_process_sentences"))
        return strings(GET(q, "sentences"), err);
    if (!strcmp(m, "parse_input_lines"))
        return strings(GET(q, "lines"), err);
    if (!strcmp(m, "parse_input_tokens"))
        return strings(GET(q, "tokens"), err);
    if (!strcmp(m, "dictionary"))
        return require_value(q, "name", 1, 0, err);
    if (!strcmp(m, "pronouns"))
        return 1;
    if (!strncmp(m, "dto.", 4)) {
        m += 4;
        if (!strcmp(m, "token_to_string") || !strcmp(m, "is_noun") || !strcmp(m, "is_hyphen")) {
            J *token = GET(q, "token");
            if (!require_value(q, "token", 3, 0, err))
                return 0;
            if (strcmp(m, "is_noun") && !require_value(token, "text", 1, 0, err))
                return 0;
            if (strcmp(m, "is_hyphen") && !require_value(token, "pos", 1, 0, err))
                return 0;
            if (!strcmp(m, "token_to_string") && !require_value(token, "ent", 1, 0, err))
                return 0;
            return 1;
        }
        if (!strcmp(m, "sentence_text") || !strcmp(m, "sentence_to_string"))
            return tokens(GET(q, "tokens"), err);
        if (!strcmp(m, "sentences_text") || !strcmp(m, "sentences_to_string")) {
            if (!require_value(q, "sentences", 2, 0, err))
                return 0;
            EACH(s, GET(q, "sentences")) if (!tokens(s, err)) return 0;
            return 1;
        }
        if (!strcmp(m, "size"))
            return require_value(q, GET(q, "tokens") ? "tokens" : "sentences", 2, 0, err);
        return require_value(q, "data", 0, 0, err);
    }
    if (!strcmp(m, "generate_sample_prompt"))
        return require_value(q, "version", 4, 1, err);
    if (GET(q, "version") && !require_value(q, "version", 4, 0, err))
        return 0;
    if (GET(q, "phrases") && !cJSON_IsNull(GET(q, "phrases")) && !strings(GET(q, "phrases"), err))
        return 0;
    if (GET(q, "delimiter") && !require_value(q, "delimiter", 1, 0, err))
        return 0;
    return require_value(q, "text", 1, 0, err);
}
