#include "lingpatlab.h"
static char *token_string(const J *token) {
    char *text = norm(S(GET(token, "text")), 0, 0), *pos = upper(S(GET(token, "pos"))),
         *ent = upper(S(GET(token, "ent")));
    Buf b = {0};
    buf_put(&b, text);
    buf_put(&b, "/");
    buf_put(&b, pos);
    if (*S(GET(token, "ent"))) {
        buf_put(&b, "/");
        buf_put(&b, ent);
    }
    free(text);
    free(pos);
    free(ent);
    return buf_take(&b);
}
static char *sentence_string(const J *tokens, int tagged) {
    J *parts = ARR();
    EACH(t, tokens) {
        char *s = tagged ? token_string(t) : copy(S(GET(t, "text")));
        ADD(parts, STR(s));
        free(s);
    }
    char *out = join(parts, tagged ? " " : "");
    DEL(parts);
    return out;
}
static J *dto_method(const char *method, const J *q, mc_error *err) {
    J *token = GET(q, "token"), *tokens = GET(q, "tokens"), *sentences = GET(q, "sentences");
    if (!strcmp(method, "token_to_string")) {
        char *s = token_string(token);
        J *r = STR(s);
        free(s);
        return r;
    }
    if (!strcmp(method, "is_noun")) {
        char *s = upper(S(GET(token, "pos")));
        int yes = !strcmp(s, "NOUN") || !strcmp(s, "PROPN");
        free(s);
        return BOOL(yes);
    }
    if (!strcmp(method, "is_hyphen")) {
        char *s = norm(S(GET(token, "text")), 0, 0);
        int yes = !strcmp(s, "-");
        free(s);
        return BOOL(yes);
    }
    if (!strcmp(method, "sentence_text") || !strcmp(method, "sentence_to_string")) {
        char *s = sentence_string(tokens, !strcmp(method, "sentence_to_string"));
        J *r = STR(s);
        free(s);
        return r;
    }
    if (!strcmp(method, "sentences_text") || !strcmp(method, "sentences_to_string")) {
        J *out = ARR();
        int tagged = !strcmp(method, "sentences_to_string");
        EACH(sentence, sentences) {
            char *s = sentence_string(sentence, tagged);
            ADD(out, STR(s));
            free(s);
        }
        if (tagged) {
            char *s = join(out, "\n\n");
            DEL(out);
            out = STR(s);
            free(s);
        }
        return out;
    }
    if (!strcmp(method, "size"))
        return NUM(SIZE(tokens ? tokens : sentences));
    if (!strcmp(method, "to_json") || !strcmp(method, "restore_sentences") ||
        !strcmp(method, "to_spacy_result")) {
        J *value = GET(q, "data");
        if (!value) {
            fail(err, 2, "DTO conversion requires data");
            return NULL;
        }
        return DUP(value);
    }
    fail(err, 2, "Unknown DTO method: %s", method);
    return NULL;
}
J *lp_request(mc_engine *e, const J *q, mc_error *err) {
    if (!lp_validate(q, err))
        return NULL;
    const char *method = S(GET(q, "method"));
    if (!strncmp(method, "text.", 5))
        return lp_text_method(method + 5, q, err);
    if (!strncmp(method, "dto.", 4))
        return dto_method(method + 4, q, err);
    if ((!strncmp(method, "people_", 7) && strcmp(method, "people_sequence")) ||
        !strcmp(method, "filter_title_phrases"))
        return lp_people_method(method, q, err);
    if (!strcmp(method, "extract_people") || !strcmp(method, "extract_topics"))
        return lp_extract(GET(q, "sentences"), !strcmp(method, "extract_people"), err);
    if (!strcmp(method, "people_sequence") || !strcmp(method, "topic_sequence"))
        return lp_sequence(GET(q, "tokens"), !strcmp(method, "people_sequence"));
    const char *segment_methods[] = {
        "segment_input_text",       "segment_paragraphs",     "segment_sentences",
        "bullet_point_cleaner",     "delimiters_to_periods",  "newlines_to_periods",
        "numbered_list_normalizer", "post_process_sentences", "spacy_doc_segmenter"};
    for (size_t i = 0; i < sizeof(segment_methods) / sizeof(*segment_methods); i++)
        if (!strcmp(method, segment_methods[i]))
            return lp_segment_method(e, method, q, err);
    if (!strcmp(method, "stopword_exists")) {
        J *words = lp_data("stopwords");
        char *s = norm(S(GET(q, "text")), 1, 0);
        int yes = contains(words, s);
        free(s);
        DEL(words);
        return BOOL(yes);
    }
    if (!strcmp(method, "pronouns") || !strcmp(method, "has_pronoun")) {
        J *groups = lp_data("pronouns"), *all = ARR();
        EACH(group, groups) EACH(v, group) unique(all, S(v));
        DEL(groups);
        sort_strings(all, 0);
        if (!strcmp(method, "pronouns"))
            return all;
        char *lo = lower(S(GET(q, "text")));
        Buf b = {0};
        for (char *p = lo; *p; p++)
            if ((*p >= 'a' && *p <= 'z') || *p == ' ')
                buf_add(&b, p, 1);
        J *words = lp_words(b.p ? b.p : "");
        int n = 0;
        EACH(w, words) if (contains(all, S(w))) n++;
        free(lo);
        free(b.p);
        DEL(words);
        DEL(all);
        return NUM(n);
    }
    if (!strcmp(method, "generate_prompt") || !strcmp(method, "generate_sample_prompt")) {
        int version = GET(q, "version") ? GET(q, "version")->valueint : 2;
        const char *text = S(GET(q, "text"));
        J *sample = NULL, *phrases = GET(q, "phrases"), *sample_phrases = NULL;
        char name[100];
        if (version != 1 && version != 2) {
            fail(err, 2, "Prompt version must be 1 or 2");
            return NULL;
        }
        if (!strcmp(method, "generate_sample_prompt")) {
            snprintf(name, sizeof name, "prompt%d_SAMPLE_ORIGINAL_TEXT", version);
            sample = lp_data(name);
            text = S(sample);
            if (version == 1) {
                sample_phrases = lp_data("prompt1_SAMPLE_PHRASES");
                phrases = sample_phrases;
            }
        }
        snprintf(name, sizeof name,
                 version == 2    ? "prompt2_SYSTEM_PROMPT_TEMPLATE"
                 : SIZE(phrases) ? "prompt1_SYSTEM_PROMPT_TEMPLATE_1"
                                 : "prompt1_SYSTEM_PROMPT_TEMPLATE_2");
        J *tmpl = lp_data(name);
        char *s = replace(S(tmpl), "#SUMMARY", text);
        if (version == 1 && SIZE(phrases)) {
            char *joined = join(phrases, ", ");
            s = lp_replace(s, "#PHRASES", joined);
            free(joined);
        }
        J *out = STR(s);
        free(s);
        DEL(tmpl);
        DEL(sample);
        DEL(sample_phrases);
        return out;
    }

    J *text = GET(q, "text");
    if (!strcmp(method, "tokenize_input_text") || !strcmp(method, "stem") ||
        !strcmp(method, "is_wordnet_term") || !strcmp(method, "parse_input_text") ||
        !strcmp(method, "spacy_document")) {
        if (!cJSON_IsString(text)) {
            fail(err, 2, "%s requires string text", method);
            return NULL;
        }
        if (!strcmp(method, "tokenize_input_text"))
            return lp_tokenize(S(text));
        if (!strcmp(method, "is_wordnet_term"))
            return BOOL(lp_wordnet(S(text)));
        if (!strcmp(method, "stem")) {
            char *stem = lp_stem(S(text));
            J *r = STR(stem);
            free(stem);
            return r;
        }
        if (!strcmp(method, "spacy_document")) {
            J *r = OBJ();
            PUT(r, "op", STR("analyze"));
            PUT(r, "text", DUP(text));
            J *d = spacy_call(e, r, err);
            DEL(r);
            return d;
        }
        J *tokens = lp_tokenize(S(text)), *result = lp_parse_tokens(e, tokens, err);
        DEL(tokens);
        if (result && !SIZE(result)) {
            DEL(result);
            return NIL();
        }
        return result;
    }
    if (!strcmp(method, "parse_input_tokens")) {
        J *r = lp_parse_tokens(e, GET(q, "tokens"), err);
        if (r && !SIZE(r)) {
            DEL(r);
            return NIL();
        }
        return r;
    }
    if (!strcmp(method, "parse_input_lines")) {
        J *lines = GET(q, "lines");
        if (!cJSON_IsArray(lines)) {
            fail(err, 2, "parse_input_lines requires string lines");
            return NULL;
        }
        J *out = ARR();
        EACH(line, lines) {
            if (!cJSON_IsString(line)) {
                fail(err, 2, "parse_input_lines requires strings");
                break;
            }
            J *tokens = lp_tokenize(S(line)), *sentence = lp_parse_tokens(e, tokens, err);
            DEL(tokens);
            if (!sentence)
                break;
            if (!SIZE(sentence)) {
                DEL(sentence);
                fail(err, 2, "Sentences cannot contain an empty Sentence");
                break;
            }
            ADD(out, sentence);
        }
        if (err->code) {
            DEL(out);
            return NULL;
        }
        return out;
    }
    if (!strcmp(method, "dictionary")) {
        J *r = lp_data(S(GET(q, "name")));
        if (!r)
            fail(err, 2, "Unknown linguistic dictionary");
        return r;
    }
    fail(err, 2, "Unknown LingPatLab method: %s", method);
    return NULL;
}
