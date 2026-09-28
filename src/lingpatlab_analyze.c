#include "lingpatlab.h"
static void reverse_lex(J *a) {
    sort_strings(a, 0);
    J *out = ARR();
    while (SIZE(a))
        ADD(out, cJSON_DetachItemFromArray(a, SIZE(a) - 1));
    while (SIZE(out))
        ADD(a, cJSON_DetachItemFromArray(out, 0));
    DEL(out);
}
static char *sequence_text(const J *tokens, int start, int count, int people) {
    J *parts = ARR();
    for (int j = 0; j < count; j++)
        ADD(parts, DUP(GET(AT(tokens, start + j), "text")));
    char *s = lp_strip(lp_collapse(join(parts, " ")));
    DEL(parts);
    if (people)
        while (strstr(s, " . "))
            s = lp_strip(lp_replace(s, " . ", ". "));
    return s;
}
static int capitalized(const char *sequence, int people) {
    J *words = lp_words(sequence);
    int valid = 1;
    EACH(w, words) {
        const char *s = S(w);
        uint32_t first = *s ? uread(&s) : 0;
        if (!lp_upper(first) && strcmp(S(w), ".") && (people || strcmp(S(w), "of")))
            valid = 0;
    }
    DEL(words);
    return valid;
}
static J *extract_patterns(const J *sentence, const J *patterns, int people) {
    J *out = ARR();
    EACH(rule, patterns) {
        J *pattern = lp_words(S(rule));
        int width = SIZE(pattern);
        for (int i = 0; i + width <= SIZE(sentence); i++) {
            if (!strcmp(S(GET(AT(sentence, i), "pos")), "DET"))
                continue;
            int match = 1, persons = 0;
            J *ents = ARR();
            for (int j = 0; j < width; j++) {
                J *token = AT(sentence, i + j);
                const char *want = S(AT(pattern, j)), *pos = S(GET(token, "pos")),
                           *ent = S(GET(token, "ent"));
                char *text = norm(S(GET(token, "text")), 0, 0);
                persons += !strcmp(ent, "PERSON");
                ADD(ents, STR(ent));
                if (people) {
                    if (!strcmp(want, "*") && i != 0) {
                    } else if (!strcmp(want, "PUNCT") && !strcmp(text, ".")) {
                    } else if (!strcmp(want, ent) && !strcmp(want, pos))
                        match = 0;
                    else if (strcmp(want, pos) && strcmp(want, ent))
                        match = 0;
                } else if (strcmp(want, "*") && !(!strcmp(want, "NOUN") && !strcmp(pos, "PROPN")) &&
                           !(!strcmp(want, "PROPN") && !strcmp(pos, "NOUN")) &&
                           !(!strcmp(want, "PUNCT") && !strcmp(text, ".")) && strcmp(want, pos))
                    match = 0;
                free(text);
            }
            char *ent_sequence = join(ents, " ");
            DEL(ents);
            if (people) {
                if (!((width <= 3 && persons >= 1) || (width <= 6 && persons >= 2) || persons >= 3))
                    match = 0;
            } else if (strstr(ent_sequence, "PERSON PERSON"))
                match = 0;
            free(ent_sequence);
            if (match) {
                char *s = sequence_text(sentence, i, width, people);
                if (!people)
                    s = lp_strip(lp_replace(s, "'s", ""));
                if (capitalized(s, people)) {
                    if (people)
                        s = lp_strip(lp_collapse(lp_replace(s, "'s", "")));
                    if (*s != '.')
                        ADD(out, STR(s));
                }
                free(s);
            }
        }
        DEL(pattern);
    }
    return out;
}
static J *filter_initials(J *matches) {
    J *out = ARR();
    EACH(v, matches) {
        J *words = lp_words(S(v));
        const char *first = S(AT(words, 0));
        if (!(ulen(first) == 2 && lp_ends(first, ".")))
            ADD(out, DUP(v));
        DEL(words);
    }
    DEL(matches);
    return out;
}
J *lp_sequence(const J *sentence, int people) {
    J *p1 = lp_data(people ? "people_PATTERNS_1" : "topic_PATTERNS_1"),
      *p2 = lp_data(people ? "people_PATTERNS_2" : "topic_PATTERNS_2");
    J *exact = extract_patterns(sentence, p1, people),
      *fuzzy = extract_patterns(sentence, p2, people);
    DEL(p1);
    DEL(p2);
    if (people) {
        J *a = ARR(), *b = ARR();
        EACH(v, exact) unique(a, S(v));
        EACH(v, fuzzy) if (!contains(exact, S(v))) unique(b, S(v));
        DEL(exact);
        DEL(fuzzy);
        reverse_lex(a);
        reverse_lex(b);
        exact = filter_initials(a);
        fuzzy = filter_initials(b);
    }
    J *out = OBJ();
    PUT(out, "exact", exact);
    PUT(out, "fuzzy", fuzzy);
    return out;
}
static J *remove_subsumed_safe(const J *input) {
    J *people = DUP(input), *keys = ARR();
    EACH(v, input) ADD(keys, STR(v->string));
    EACH(first, keys) {
        const char *key1 = S(first);
        J *names1 = DUP(GET(people, key1));
        EACH(second, keys) {
            const char *key2 = S(second);
            if (!strcmp(key1, key2))
                continue;
            J *names2 = DUP(GET(people, key2));
            EACH(a, names1) EACH(b, names2) {
                if (!strcmp(S(a), S(b))) {
                    J *out = ARR();
                    EACH(n, names1) if (strcmp(S(n), S(b))) ADD(out, DUP(n));
                    set(people, key1, out);
                } else if (strstr(S(b), S(a)))
                    set(people, key1, ARR());
                else if (strstr(S(a), S(b)))
                    set(people, key2, ARR());
            }
            DEL(names2);
        }
        DEL(names1);
    }
    J *out = OBJ();
    EACH(v, people) if (SIZE(v)) PUT(out, v->string, DUP(v));
    DEL(people);
    DEL(keys);
    return out;
}
static J *aggregate(const J *people) {
    J *out = OBJ();
    EACH(person, people) {
        const char *name = person->string;
        J *words = lp_words(name);
        int n = SIZE(words);
        if (n == 1)
            set(out, name, ARR());
        else if (n) {
            const char *last = S(AT(words, n - 1));
            char *lo = lower(last);
            if (!strcmp(lo, "jr") && n >= 2)
                last = S(AT(words, n - 2));
            free(lo);
            lo = lower(last);
            if (strcmp(lo, "the"))
                ADD(ensure(out, last, 1), STR(name));
            free(lo);
        }
        DEL(words);
    }
    return out;
}
static J *cleanse(const J *people) {
    J *out = OBJ();
    EACH(row, people) {
        J *names = ARR();
        EACH(name, row) {
            char *s = lp_strip(replace(S(name), " . ", ". "));
            ADD(names, STR(s));
            free(s);
        }
        PUT(out, row->string, names);
    }
    return out;
}
J *lp_analyze(const J *exact, const J *fuzzy) {
    J *people = OBJ();
    const J *lists[] = {exact, fuzzy};
    for (int i = 0; i < 2; i++)
        EACH(v, lists[i]) {
            J *words = lp_words(S(v));
            int n = SIZE(words);
            if (ulen(S(v)) > 1 && n > 1 && ulen(S(AT(words, n - 1))) > 1 && !GET(people, S(v)))
                PUT(people, S(v), ARR());
            DEL(words);
        }
    J *a = aggregate(people), *b = remove_subsumed_safe(a), *out = cleanse(b);
    DEL(people);
    DEL(a);
    DEL(b);
    return out;
}
J *lp_extract(const J *sentences, int people, mc_error *err) {
    if (!cJSON_IsArray(sentences)) {
        fail(err, 2, "Extraction requires an array of sentence token arrays");
        return NULL;
    }
    J *exact = ARR(), *fuzzy = ARR();
    EACH(sentence, sentences) {
        if (!cJSON_IsArray(sentence)) {
            fail(err, 2, "Each sentence must be a token array");
            break;
        }
        J *r = lp_sequence(sentence, people);
        EACH(v, GET(r, "exact")) unique(exact, S(v));
        EACH(v, GET(r, "fuzzy")) unique(fuzzy, S(v));
        DEL(r);
    }
    if (err->code) {
        DEL(exact);
        DEL(fuzzy);
        return NULL;
    }
    reverse_lex(exact);
    reverse_lex(fuzzy);
    J *out = SIZE(exact) || SIZE(fuzzy) ? lp_analyze(exact, fuzzy) : NIL();
    DEL(exact);
    DEL(fuzzy);
    return out;
}
J *lp_people_method(const char *method, const J *q, mc_error *err) {
    if (!strcmp(method, "people_analyze"))
        return lp_analyze(GET(q, "exact"), GET(q, "fuzzy"));
    if (!strcmp(method, "people_remove_subsumed"))
        return remove_subsumed_safe(GET(q, "people"));
    if (!strcmp(method, "people_aggregate"))
        return aggregate(GET(q, "people"));
    if (!strcmp(method, "people_cleanse"))
        return cleanse(GET(q, "people"));
    if (!strcmp(method, "people_index")) {
        J *out = OBJ();
        EACH(n, GET(q, "ngrams")) {
            J *words = lp_words(S(n));
            if (!SIZE(words)) {
                DEL(out);
                DEL(words);
                fail(err, 2, "Name cannot be empty");
                return NULL;
            }
            const char *last = S(AT(words, SIZE(words) - 1));
            J *values = ARR();
            if (contains(GET(q, "unigrams"), last))
                ADD(values, STR(last));
            set(out, S(n), values);
            DEL(words);
        }
        return out;
    }
    if (!strcmp(method, "filter_title_phrases")) {
        J *out = ARR();
        EACH(phrase, GET(q, "phrases")) {
            char *s = lp_ends(S(phrase), "'s") ? slice(S(phrase), strlen(S(phrase)) - 2)
                                               : copy(S(phrase));
            J *words = lp_words(s);
            int all = 1, acceptable = SIZE(words) >= 3;
            EACH(word, words) {
                const char *p = S(word);
                uint32_t c = *p ? uread(&p) : 0;
                if (!((lp_upper(c) || lp_all_numeric(S(word))) && ulen(S(word)) > 1))
                    all = 0;
                if (strcmp(S(word), "of") && !lp_upper(c))
                    acceptable = 0;
            }
            if (all || acceptable)
                ADD(out, STR(s));
            free(s);
            DEL(words);
        }
        return out;
    }
    fail(err, 2, "Unknown entity extraction method: %s", method);
    return NULL;
}
