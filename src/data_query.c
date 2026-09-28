#include "mc.h"
static J *reverse(J *d) {
    J *r = OBJ();
    EACH(k, d) {
        EACH(v, k) unique(ensure(r, S(v), 1), k->string);
    }
    return r;
}
static void walk(J *d, const char *key, J *out, Map *active, int depth, mc_error *e) {
    if (depth > 1024 || map_get(active, key)) {
        fail(e, 4, "Cycle or excessive depth in hierarchy at %s", key);
        return;
    }
    map_put(active, key, (void *)1);
    EACH(v, GET(d, key)) {
        ADD(out, DUP(v));
        walk(d, S(v), out, active, depth + 1, e);
        if (e->code)
            break;
    }
    map_put(active, key, NULL);
}
static char *entity(const char *s) {
    char *a = norm(s, 1, 0), *b = replace(a, " ", "_");
    free(a);
    return b;
}
J *data_query(J *d, const char *m, const J *args, const J *kwargs, mc_error *e) {
    if (!d || e->code)
        return NULL;
    const char *s = S(AT(args, 0));
    if (!strcmp(m, "ner_depth") || !strcmp(m, "ner_depth_rev") || !strcmp(m, "ner_taxonomy") ||
        !strcmp(m, "ner_taxonomy_rev")) {
        J *value = GET(d, m);
        return value ? DUP(value) : NIL();
    }
    if (!strcmp(m, "infer_by_requires")) {
        fail(e, 4, "infer_by_requires is explicitly unimplemented in the Python reference");
        return NULL;
    }
    J *types = GET(GET(d, "by_predicate"), "rdfs:subClassOf");
    if (!strcmp(m, "graffl_ner") || !strcmp(m, "graffl_ner_rev") || !strcmp(m, "spacy_ner") ||
        !strcmp(m, "spacy_ner_rev"))
        return OBJ();
    if (!strcmp(m, "find_ner"))
        return STR("NER");
    if (!strcmp(m, "types"))
        return types ? DUP(types) : NIL();
    if (!strcmp(m, "types_rev"))
        return reverse(types);
    if (!strcmp(m, "entity_exists") || !strcmp(m, "children") || !strcmp(m, "parents") ||
        !strcmp(m, "ancestors") || !strcmp(m, "descendants") || strstr(m, "_and_self") ||
        !strcmp(m, "has_parent") || !strcmp(m, "has_ancestor")) {
        char *key = entity(s);
        J *rev = reverse(types);
        int exists = GET(types, key) || GET(rev, key);
        if (!strcmp(m, "entity_exists")) {
            free(key);
            DEL(rev);
            return BOOL(exists);
        }
        if (!*s) {
            free(key);
            DEL(rev);
            fail(e, 2, "Entity must be non-empty");
            return NULL;
        }
        int parent = strstr(m, "parent") || strstr(m, "ancestor");
        int transitive = strstr(m, "ancestor") || strstr(m, "descendant");
        int self = strstr(m, "_and_self") != NULL;
        J *out = ARR(), *view = parent ? types : rev;
        if (!self || exists) {
            if (transitive) {
                Map active = {0};
                walk(view, key, out, &active, 0, e);
                map_free(&active);
            } else {
                EACH(v, GET(view, key)) ADD(out, DUP(v));
            }
            if (self)
                unique(out, key);
        }
        if (!strcmp(m, "has_parent") || !strcmp(m, "has_ancestor")) {
            int yes = contains(out, S(AT(args, 1)));
            DEL(out);
            out = BOOL(yes);
        } else if (self || !strcmp(m, "children") || !strcmp(m, "ancestors")) {
            J *dedup = ARR();
            EACH(v, out) unique(dedup, S(v));
            DEL(out);
            out = dedup;
            sort_strings(out, 0);
        }
        DEL(rev);
        free(key);
        return out;
    }
    if (!strcmp(m, "labels") || !strcmp(m, "labels_rev") || !strcmp(m, "label_by_entity")) {
        J *labels = GET(d, "_labels");
        if (!cJSON_IsObject(labels)) {
            fail(e, 4, "Label predicate query returned no rows");
            return NULL;
        }
        J *out = OBJ();
        int lc = !cJSON_IsFalse(GET(kwargs, "force_lowercase"));
        EACH(k, labels) {
            char *key = lc ? lower(k->string) : copy(k->string);
            if (!strcmp(m, "labels_rev")) {
                EACH(v, k) ADD(ensure(out, S(v), 1), STR(key));
            } else
                set(out, key, DUP(k));
            free(key);
        }
        if (!strcmp(m, "label_by_entity")) {
            char *key = entity(s);
            J *v = GET(out, key);
            J *r = v && v->child ? DUP(v->child) : NIL();
            free(key);
            DEL(out);
            return r;
        }
        return out;
    }
    if (!strcmp(m, "by_predicate") || !strcmp(m, "by_predicate_rev")) {
        const char *key = s;
        char *qualified = NULL;
        if (!strchr(s, ':')) {
            if (!cJSON_IsTrue(GET(d, "_namespace_bound"))) {
                fail(e, 4, "Unknown ontology namespace prefix");
                return NULL;
            }
            J *view = GET(GET(d, "_custom_predicates"), s);
            if (!view) {
                fail(e, 4, "Predicate query returned no rows: %s", s);
                return NULL;
            }
            return !strcmp(m, "by_predicate_rev") ? reverse(view) : DUP(view);
        }
        J *v = GET(GET(d, "by_predicate"), key);
        J *r = !strcmp(m, "by_predicate_rev") ? reverse(v) : (v ? DUP(v) : NIL());
        free(qualified);
        return r;
    }
    const char *predicate = NULL;
    int rev = 0;
    if (!strcmp(m, "effects") || !strcmp(m, "effects_rev")) {
        predicate = "effects";
        rev = strstr(m, "rev") != NULL;
    } else if (!strncmp(m, "require", 7)) {
        predicate = "requires";
        rev = !strncmp(m, "required_by", 11);
    } else if (!strncmp(m, "similar", 7)) {
        predicate = "similarTo";
        rev = !strcmp(m, "similar_rev");
    } else if (!strncmp(m, "impl", 4)) {
        predicate = "implies";
        rev = !strncmp(m, "implied_by", 10);
    } else if (!strncmp(m, "uses", 4)) {
        predicate = "uses";
        rev = !strcmp(m, "uses_rev");
    }
    if (predicate) {
        J *a = ARR();
        ADD(a, STR(predicate));
        J *view = data_query(d, rev ? "by_predicate_rev" : "by_predicate", a, kwargs, e);
        DEL(a);
        if (!strstr(m, "_by_entity"))
            return view;
        char *key = entity(s);
        J *v = GET(view, key), *r = v ? DUP(v) : NIL();
        if (!strcmp(m, "similar_by_entity")) {
            if (cJSON_IsNull(r)) {
                DEL(r);
                r = ARR();
            }
            J *rv = reverse(view);
            EACH(x, GET(rv, key)) ADD(r, DUP(x));
            DEL(rv);
        }
        DEL(view);
        free(key);
        return r;
    }
    if (!strcmp(m, "is_canon") || !strcmp(m, "is_variant")) {
        char *a = norm(s, 1, 0), *b = replace(a, !strcmp(m, "is_canon") ? " " : "_",
                                              !strcmp(m, "is_canon") ? "_" : " ");
        free(a);
        if (!strcmp(m, "is_canon")) {
            a = replace(b, "'", "");
            free(b);
            b = a;
        }
        J *aargs = ARR();
        ADD(aargs, STR(b));
        J *r = ontology_query(d, m, aargs, e);
        DEL(aargs);
        free(b);
        return r;
    }
    if (!strcmp(m, "find_variants")) {
        char *a = norm(s, 1, 0), *key = replace(a, "_", " ");
        free(a);
        J *fwd = GET(GET(d, "synonyms"), "fwd"), *v = GET(fwd, key), *r = ARR();
        if (v) {
            EACH(x, v) ADD(r, DUP(x));
        } else {
            EACH(canon, GET(GET(GET(d, "synonyms"), "rev"), key)) {
                EACH(x, GET(fwd, S(canon))) unique(r, S(x));
            }
            sort_strings(r, 1);
        }
        if (!*key) {
            DEL(r);
            r = NIL();
        }
        free(key);
        return r;
    }
    if (!strcmp(m, "synonyms") || !strcmp(m, "synonyms_rev")) {
        J *r = ontology_query(d, m, args, e);
        EACH(k, r) sort_strings(k, 0);
        return r;
    }
    if (!strcmp(m, "equivalents")) {
        J *entities = AT(args, 0), *single = NULL;
        if (cJSON_IsString(entities)) {
            single = ARR();
            ADD(single, DUP(entities));
            entities = single;
        }
        if (!cJSON_IsArray(entities)) {
            DEL(single);
            fail(e, 2, "equivalents requires an entity or array");
            return NULL;
        }
        J *out = OBJ();
        EACH(v, entities) {
            char *a = replace(S(v), "_", " "), *key = norm(a, 1, 0);
            free(a);
            J *values = GET(GET(d, "equivalents"), key);
            if (values)
                PUT(out, key, DUP(values));
            free(key);
        }
        DEL(single);
        int flat = !cJSON_IsFalse(GET(kwargs, "flat_list"));
        if (!flat)
            return out;
        J *r = ARR();
        EACH(k, out) {
            EACH(v, k) {
                char *s2 = !cJSON_IsFalse(GET(kwargs, "underscore_entities"))
                               ? replace(S(v), " ", "_")
                               : copy(S(v));
                unique(r, s2);
                free(s2);
            }
        }
        sort_strings(r, 0);
        DEL(out);
        return r;
    }
    return ontology_query(d, m, args, e);
}
