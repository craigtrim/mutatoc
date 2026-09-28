#include "mc.h"
static int underscore_count(const char *s) {
    int n = 0;
    for (; *s; s++)
        n += *s == '_';
    return n;
}
static J *swap(J *ts, int start, int end, const char *canon, const char *kind, const J *names,
               J *ner, double confidence) {
    J *r = OBJ(), *history = OBJ(), *originals = ARR();
    Buf text = {0};
    for (int i = start; i < end; i++) {
        J *t = AT(ts, i);
        ADD(originals, DUP(t));
        char *s = norm(S(GET(t, "text")), 0, 0);
        if (i > start)
            buf_put(&text, " ");
        buf_put(&text, s);
        free(s);
    }
    PUT(r, "id", DUP(GET(AT(ts, start), "id")));
    PUT(r, "x", DUP(GET(AT(ts, start), "x")));
    PUT(r, "y", DUP(GET(AT(ts, end - 1), "y")));
    if (cJSON_IsString(ner) && *S(ner)) {
        char *value = upper(S(ner));
        PUT(r, "ner", STR(value));
        free(value);
    } else
        PUT(r, "ner", ner ? DUP(ner) : NIL());
    PUT(r, "text", STR(text.p ? text.p : ""));
    PUT(r, "normal", STR(canon));
    free(text.p);
    PUT(history, "tokens", originals);
    PUT(history, "canon", STR(canon));
    PUT(history, "type", STR(kind));
    PUT(history, "ontologies", DUP(names));
    PUT(history, "confidence", NUM(confidence));
    PUT(r, "swaps", history);
    return r;
}
static J *collapse(J *ts, int start, int end, J *r) {
    /* The replacement already owns copies of its history. Keep every unrelated
       token in place instead of copying the entire document for every match. */
    J *first = AT(ts, start), *token = first->next;
    for (int i = start + 1; i < end; i++) {
        J *next = token->next;
        DEL(cJSON_DetachItemViaPointer(ts, token));
        token = next;
    }
    cJSON_ReplaceItemViaPointer(ts, first, r);
    return ts;
}
static char *normal_sequence(J *token, int n, const char *sep) {
    Buf b = {0};
    for (int i = 0; i < n; i++, token = token->next) {
        if (i)
            buf_put(&b, sep);
        buf_put(&b, S(GET(token, "normal")));
    }
    char *s = norm(b.p ? b.p : "", 1, 0);
    free(b.p);
    return s;
}
static char *normal_window(J *ts, int start, int n, const char *sep) {
    return normal_sequence(AT(ts, start), n, sep);
}
typedef struct {
    J *token;
    int length;
    int words;
} ExactWindow;
static int spacing_token(J *token) {
    if (*S(GET(token, "normal")) || GET(token, "swaps"))
        return 0;
    const char *text = S(GET(token, "text"));
    while (*text)
        if (!uspace(uread(&text)))
            return 0;
    return 1;
}
static char *exact_sequence(J *token, int length) {
    Buf b = {0};
    int words = 0;
    for (int i = 0; i < length; i++, token = token->next) {
        if (spacing_token(token))
            continue;
        if (words++)
            buf_put(&b, " ");
        buf_put(&b, S(GET(token, "normal")));
    }
    char *s = norm(b.p ? b.p : "", 1, 0);
    free(b.p);
    return s;
}
static void window_matches(ExactWindow *window, int remaining, Map *words, int limit) {
    window->length = window->words = 0;
    if (spacing_token(window->token))
        return;
    Buf b = {0};
    int n = 0;
    J *token = window->token;
    for (int length = 1; length <= remaining && n < limit; length++, token = token->next) {
        if (spacing_token(token))
            continue;
        if (n++)
            buf_put(&b, " ");
        buf_put(&b, S(GET(token, "normal")));
        if (!words[n].size || (n == 1 && GET(window->token, "swaps")))
            continue;
        char *s = norm(b.p, 1, 0);
        if (map_get(&words[n], s)) {
            window->length = length;
            window->words = n;
        }
        free(s);
    }
    free(b.p);
}
static J *exact(J *d, J *ts, const J *names, int blacklist, mc_error *e) {
    (void)blacklist;
    J *lookup = GET(GET(d, "synonyms"), "lookup");
    if (lookup && !cJSON_IsObject(lookup)) {
        fail(e, 2, "Synonym lookup must be an object");
        return ts;
    }
    int count = SIZE(ts), operations = 0, at = 0, limit = 0;
    /* Dotted abbreviations consume punctuation tokens too. Derive the window
       bound from this ontology, instead of silently discarding long synonyms. */
    EACH(group, lookup) {
        if (!cJSON_IsArray(group)) {
            fail(e, 2, "Synonym lookup groups must be arrays");
            return ts;
        }
        char *end;
        long n = strtol(group->string, &end, 10);
        if (!*end && n > limit && n <= count && SIZE(group))
            limit = (int)n;
    }
    if (!limit)
        return ts;
    Map *words = calloc((size_t)limit + 1, sizeof(*words));
    ExactWindow *windows = malloc((size_t)count * sizeof(*windows));
    if (!words || !windows) {
        fail(e, 1, "Cannot allocate matching windows");
        free(words);
        free(windows);
        return ts;
    }
    for (int n = 1; n <= limit; n++) {
        char key[16];
        snprintf(key, sizeof(key), "%d", n);
        EACH(word, GET(lookup, key)) map_put(&words[n], S(word), (void *)1);
    }
    EACH(token, ts) {
        windows[at].token = token;
        window_matches(&windows[at], count - at, words, limit);
        at++;
    }
    while (!e->code) {
        int start = -1, length = 0, best = 0;
        /* Preserve the original longest-window, then leftmost selection order. */
        for (int i = 0; i < count; i++)
            if (windows[i].words > best) {
                start = i;
                length = windows[i].length;
                best = windows[i].words;
            }
        if (start < 0)
            break;
        char *s = exact_sequence(windows[start].token, length);
        J *a = ARR();
        ADD(a, STR(s));
        J *canon = ontology_query(d, "find_canon", a, e);
        DEL(a);
        if (!canon || !cJSON_IsString(canon)) {
            fail(e, 4, "Canonical form not found for %s", s);
            DEL(canon);
            free(s);
            break;
        }
        J *r = swap(ts, start, start + length, S(canon), "exact", names, NULL, 100.0);
        ts = collapse(ts, start, start + length, r);
        DEL(canon);
        free(s);
        memmove(windows + start + 1, windows + start + length,
                (size_t)(count - start - length) * sizeof(*windows));
        count -= length - 1;
        windows[start].token = r;
        /* Only windows containing the replacement can change. Count visible
           tokens backward, retaining intervening whitespace in swap history. */
        int first = start, preceding = 0;
        while (first > 0 && preceding < limit - 1) {
            first--;
            if (!spacing_token(windows[first].token))
                preceding++;
        }
        for (int i = first; i <= start; i++)
            window_matches(&windows[i], count - i, words, limit);
        if (++operations > 100000) {
            fail(e, 4, "Matching operation limit exceeded");
            break;
        }
    }
    free(windows);
    for (int n = 1; n <= limit; n++)
        map_free(&words[n]);
    free(words);
    return ts;
}
static J *spans(J *d, J *ts, const J *names) {
    J *rules = GET(d, "spans"), *best = NULL;
    int bx = 0, by = 0, score = -1;
    Map positions = {0};
    J *keys = ARR();
    for (int i = 0; i < SIZE(ts); i++) {
        const char *n = S(GET(AT(ts, i), "normal"));
        map_put(&positions, n, (void *)(uintptr_t)(i + 1));
        unique(keys, n);
    }
    sort_strings(keys, 0);
    EACH(key, keys) {
        EACH(r, GET(rules, S(key))) {
            J *content = DUP(GET(r, "content"));
            if (!content || !cJSON_IsArray(content)) {
                DEL(content);
                continue;
            }
            int valid = 1;
            EACH(v, content) if (!map_get(&positions, S(v))) valid = 0;
            if (!valid) {
                DEL(content);
                continue;
            }
            unique(content, S(key));
            sort_strings(content, 1);
            int x = SIZE(ts), y = -1, first = -1, last = -1;
            EACH(v, content) {
                int pos = (int)(uintptr_t)map_get(&positions, S(v)) - 1;
                if (pos < 0) {
                    valid = 0;
                    break;
                }
                if (first < 0)
                    first = pos;
                last = pos;
                if (pos < x)
                    x = pos;
                if (pos > y)
                    y = pos;
            }
            J *distance = GET(r, "distance");
            int delta = first - last, limit = distance ? distance->valueint : 4;
            if (abs(delta) > limit)
                valid = 0;
            if (delta < 0 && !cJSON_IsTrue(GET(r, "reverse")))
                valid = 0;
            if (delta > 0 && !cJSON_IsTrue(GET(r, "forward")))
                valid = 0;
            EACH(v, GET(r, "context")) if (!map_get(&positions, S(v))) valid = 0;
            int rank = underscore_count(S(GET(r, "canon")));
            if (valid && rank > score) {
                best = r;
                bx = x;
                by = y + 1;
                score = rank;
            }
            DEL(content);
        }
    }
    map_free(&positions);
    DEL(keys);
    if (best) {
        const char *canon = S(GET(best, "canon"));
        J *ner = cJSON_IsTrue(GET(d, "_live")) ? STR("NER") : DUP(GET(GET(d, "ner"), canon));
        J *r = swap(ts, bx, by, canon, "spans", names, ner, 100.0);
        DEL(ner);
        return collapse(ts, bx, by, r);
    }
    return ts;
}
static void surface(J *token, J *forms) {
    unique(forms, S(GET(token, "normal")));
    EACH(v, GET(token, "ancestors")) unique(forms, S(v));
    EACH(v, GET(token, "descendants")) unique(forms, S(v));
    EACH(t, GET(GET(token, "swaps"), "tokens")) {
        EACH(v, GET(t, "ancestors")) unique(forms, S(v));
        EACH(v, GET(t, "descendants")) unique(forms, S(v));
    }
    sort_strings(forms, 0);
}
static char *product(J *choices, int at, J *parts, J *entities, size_t *budget) {
    if (!(*budget)--)
        return NULL;
    if (at == SIZE(choices)) {
        char *s = join(parts, "_");
        char *l = lower(s);
        free(s);
        if (contains(entities, l))
            return l;
        free(l);
        return NULL;
    }
    EACH(v, AT(choices, at)) {
        ADD(parts, DUP(v));
        char *r = product(choices, at + 1, parts, entities, budget);
        DEL(cJSON_DetachItemFromArray(parts, SIZE(parts) - 1));
        if (r)
            return r;
    }
    return NULL;
}
static J *hierarchy(J *d, J *ts, const J *names, mc_error *e) {
    J *entities = GET(d, "entities");
    if (!SIZE(entities))
        return ts;
    int changed = 1;
    while (changed && !e->code) {
        changed = 0;
        const int count = SIZE(ts);
        for (int n = 9; n >= 2 && !changed; n--) {
            J *first = ts->child;
            for (int i = 0; i + n <= count; i++, first = first->next) {
                J *choices = ARR();
                int valid = 0;
                J *token = first;
                for (int j = 0; j < n; j++, token = token->next) {
                    J *t = token, *forms = ARR();
                    surface(t, forms);
                    if (SIZE(forms) > 1)
                        valid = 1;
                    ADD(choices, forms);
                }
                if (!valid) {
                    DEL(choices);
                    continue;
                }
                J *parts = ARR();
                size_t budget = 1000000;
                char *canon = product(choices, 0, parts, entities, &budget);
                DEL(parts);
                DEL(choices);
                if (!budget)
                    fail(e, 4, "Hierarchy combination limit exceeded");
                if (canon) {
                    J *t = AT(ts, i), *ner = GET(t, "ner");
                    if (!ner)
                        ner = GET(t, "ent");
                    J *r = swap(ts, i, i + n, canon, "hierarchy", names, ner, 75.0);
                    ts = collapse(ts, i, i + n, r);
                    changed = 1;
                    free(canon);
                    break;
                }
            }
        }
    }
    return ts;
}
static int valid_tokens(const J *input, mc_error *e) {
    if (!cJSON_IsArray(input)) {
        fail(e, 2, "tokens must be an array");
        return 0;
    }
    EACH(t, input) {
        if (!cJSON_IsObject(t) || !GET(t, "id") || !cJSON_IsNumber(GET(t, "x")) ||
            !cJSON_IsNumber(GET(t, "y")) || !cJSON_IsString(GET(t, "text")) ||
            !cJSON_IsString(GET(t, "normal"))) {
            fail(e, 2, "Each token requires id, numeric x/y, and string text/normal");
            return 0;
        }
    }
    return 1;
}
J *match_tokens(J *d, const J *input, const J *names, int ctr, int blacklist, mc_error *e) {
    if (!valid_tokens(input, e))
        return NULL;
    J *ts = DUP(input);
    if (ctr < -1000) {
        DEL(ts);
        fail(e, 4, "Matching recursion limit exceeded");
        return NULL;
    }
    int sweeps = ctr >= 2 ? 1 : 3 - ctr;
    for (int i = 0; i < sweeps && !e->code; i++) {
        ts = exact(d, ts, names, blacklist, e);
        if (!e->code)
            ts = spans(d, ts, names);
        if (!e->code)
            ts = hierarchy(d, ts, names, e);
    }
    if (e->code) {
        DEL(ts);
        return NULL;
    }
    return ts;
}
J *transform_tokens(J *d, const J *input, const J *names, const char *stage, mc_error *err) {
    if (!valid_tokens(input, err))
        return NULL;
    J *ts = DUP(input);
    if (!strcmp(stage, "exact"))
        return exact(d, ts, names, 0, err);
    if (!strcmp(stage, "spans"))
        return spans(d, ts, names);
    if (!strcmp(stage, "hierarchy"))
        return hierarchy(d, ts, names, err);
    if (!strcmp(stage, "augment")) {
        EACH(t, ts) {
            const char *text = S(GET(t, "normal")), *p = text;
            int alpha = *p != 0;
            while (*p)
                if (!ualpha(uread(&p)))
                    alpha = 0;
            if (!alpha)
                continue;
            J *args = ARR();
            ADD(args, STR(text));
            const char *methods[] = {"ancestors", "descendants"};
            for (size_t i = 0; i < 2; i++) {
                J *values = cJSON_IsTrue(GET(d, "_live"))
                                ? data_query(d, methods[i], args, NULL, err)
                                : ontology_query(d, methods[i], args, err);
                set(t, methods[i], values);
            }
            DEL(args);
            if (err->code) {
                DEL(ts);
                return NULL;
            }
        }
        return ts;
    }
    if (!strcmp(stage, "spacy")) {
        int changed = 1;
        while (changed) {
            changed = 0;
            for (int i = 0; i + 1 < SIZE(ts); i++) {
                const char *ent = S(GET(AT(ts, i), "ent"));
                if (!*ent || strcmp(ent, S(GET(AT(ts, i + 1), "ent"))))
                    continue;
                int end = i + 2;
                while (end < SIZE(ts) && !strcmp(ent, S(GET(AT(ts, end), "ent"))))
                    end++;
                char *canon = normal_window(ts, i, end - i, "_");
                J *r = swap(ts, i, end, canon, "spacy", names, GET(AT(ts, i), "ent"), 100.0);
                free(canon);
                ts = collapse(ts, i, end, r);
                changed = 1;
                break;
            }
        }
        return ts;
    }
    DEL(ts);
    fail(err, 2, "Unknown token transformation: %s", stage);
    return NULL;
}
char *render(const J *tokens) {
    Buf b = {0};
    EACH(t, tokens) {
        J *sw = GET(t, "swaps");
        char *s = sw ? copy(S(GET(sw, "canon"))) : norm(S(GET(t, "text")), 0, 0);
        if (*s) {
            if (b.n)
                buf_put(&b, " ");
            buf_put(&b, s);
        }
        free(s);
    }
    return buf_take(&b);
}
