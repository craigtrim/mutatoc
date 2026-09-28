#include "mc.h"
J *graph_predicate(Graph *g, const char *predicate, int lc, int rev, mc_error *err) {
    J *out = OBJ();
    size_t rows = 0;
    for (size_t i = 0; i < g->n && !err->code; i++) {
        Triple *t = &g->ts[i];
        if (strcmp(t->p.value, predicate))
            continue;
        rows++;
        J *ks = rdf_flatten(g, rev ? &t->o : &t->s, lc, err);
        J *vs = rdf_flatten(g, rev ? &t->s : &t->o, lc, err);
        EACH(k, ks) {
            if (!*S(k) || !strcmp(S(k), "nil") || !strcmp(S(k), "class"))
                continue;
            J *a = ensure(out, S(k), 1);
            EACH(v, vs) if (*S(v) && strcmp(S(v), S(k)) && strcmp(S(v), "nil")) unique(a, S(v));
        }
        DEL(ks);
        DEL(vs);
    }
    EACH(k, out) sort_strings(k, 0);
    if (!rows)
        fail(err, 4, "Predicate query returned no rows: %s", predicate);
    if (err->code) {
        DEL(out);
        return NULL;
    }
    return out;
}
static void closure(Graph *g, const char *node, J *out) {
    if (contains(out, node))
        return;
    unique(out, node);
    J *values = rdf_values(g, node, RDFS "subClassOf");
    EACH(v, values) closure(g, g->ts[(size_t)v->valuedouble].o.value, out);
    DEL(values);
}
static J *reverse_strings(J *d, int chars) {
    J *r = OBJ();
    EACH(k, d) {
        if (chars) {
            const char *p = S(k);
            while (*p) {
                const char *a = p;
                uread(&p);
                char *s = slice(a, (size_t)(p - a));
                ADD(ensure(r, s, 1), STR(k->string));
                free(s);
            }
        } else
            EACH(v, k) ADD(ensure(r, S(v), 1), STR(k->string));
    }
    return r;
}
static void ner_views(Graph *g, J *d, mc_error *err) {
    J *roots = ARR(), *tags = ARR(), *depth = OBJ(), *tax = OBJ();
    for (size_t i = 0; i < g->n; i++) {
        Triple *t = &g->ts[i];
        if (!strcmp(t->p.value, OWL "backwardCompatibleWith"))
            ADD(tags, NUM((double)i));
        if (strcmp(t->p.value, RDF "type") || strcmp(t->o.value, OWL "Class"))
            continue;
        int root = 1;
        J *parents = rdf_values(g, t->s.value, RDFS "subClassOf");
        EACH(p, parents) if (strcmp(g->ts[(size_t)p->valuedouble].o.value, t->s.value)) root = 0;
        DEL(parents);
        if (root)
            unique(roots, t->s.value);
    }
    J *counts = OBJ();
    EACH(tag, tags) {
        Triple *t = &g->ts[(size_t)tag->valuedouble];
        J *labels = rdf_flatten(g, &t->o, 0, err), *ancestors = ARR();
        closure(g, t->s.value, ancestors);
        size_t count = 0;
        EACH(mid, ancestors) {
            J *reachable = ARR();
            closure(g, S(mid), reachable);
            EACH(root, roots) if (contains(reachable, S(root))) count++;
            DEL(reachable);
        }
        EACH(label, labels) {
            char *key = upper(S(label));
            if (count) {
                J *old = GET(counts, key);
                set(counts, key, NUM((old ? old->valuedouble : 0) + (double)count));
            }
            EACH(ancestor, ancestors) {
                J *values = rdf_values(g, S(ancestor), OWL "backwardCompatibleWith");
                EACH(v, values) {
                    J *names = rdf_flatten(g, &g->ts[(size_t)v->valuedouble].o, 0, err);
                    EACH(name, names) {
                        char *s = upper(S(name));
                        J *a = ARR();
                        ADD(a, STR(s));
                        set(tax, key, a);
                        free(s);
                    }
                    DEL(names);
                }
                DEL(values);
            }
            free(key);
        }
        DEL(labels);
        DEL(ancestors);
    }
    if (SIZE(counts)) {
        PUT(depth, "NER", STR("0"));
        EACH(k, counts) {
            char n[32];
            snprintf(n, sizeof(n), "%.0f", k->valuedouble);
            set(depth, k->string, STR(n));
        }
    }
    if (!SIZE(depth)) {
        DEL(depth);
        depth = NIL();
    }
    if (!SIZE(tax)) {
        DEL(tax);
        tax = NIL();
    }
    set(d, "ner_depth", depth);
    set(d, "ner_taxonomy", tax);
    set(d, "ner_depth_rev", cJSON_IsNull(depth) ? NIL() : reverse_strings(depth, 1));
    set(d, "ner_taxonomy_rev", cJSON_IsNull(tax) ? NIL() : reverse_strings(tax, 0));
    DEL(roots);
    DEL(tags);
    DEL(counts);
}
J *external_synonyms(const char *text) {
    J *result = OBJ(), *lines = split(text, "\n");
    EACH(line, lines) {
        char *clean = norm(S(line), 1, 0);
        char *first = strchr(clean, '~'), *last = strrchr(clean, '~');
        if (!*clean || *clean == '#' || !first) {
            free(clean);
            continue;
        }
        char *key = slice(clean, (size_t)(first - clean));
        J *values = split(last + 1, ","), *variants = ARR();
        EACH(v, values) {
            char *s = norm(S(v), 0, 0);
            if (*s)
                unique(variants, s);
            free(s);
        }
        sort_strings(variants, 1);
        J *descending = ARR();
        while (SIZE(variants))
            ADD(descending, cJSON_DetachItemFromArray(variants, SIZE(variants) - 1));
        set(result, key, descending);
        DEL(variants);
        DEL(values);
        free(key);
        free(clean);
    }
    DEL(lines);
    return result;
}
J *merge_views(J *out, const J *source) {
    if (cJSON_IsObject(out) && cJSON_IsObject(source)) {
        EACH(k, source) {
            J *value = GET(out, k->string);
            if (!value || cJSON_IsNull(value))
                set(out, k->string, DUP(k));
            else
                merge_views(value, k);
        }
    } else if (cJSON_IsArray(out) && cJSON_IsArray(source)) {
        EACH(v, source) {
            int found = 0;
            EACH(old, out) if (cJSON_Compare(old, v, 1)) {
                found = 1;
                break;
            }
            if (!found)
                ADD(out, DUP(v));
        }
    }
    return out;
}
J *live_view(mc_engine *e, mc_error *err) {
    if (e->live)
        return e->live;
    if (e->part_count) {
        J *merged = OBJ();
        int bound = 0;
        for (size_t i = 0; i < e->part_count; i++) {
            J *part = live_view(e->parts[i], err);
            if (!part) {
                DEL(merged);
                return NULL;
            }
            merge_views(merged, part);
            bound |= cJSON_IsTrue(GET(part, "_namespace_bound"));
        }
        set(merged, "_namespace_bound", BOOL(bound));
        J *syns = GET(merged, "synonyms");
        set(syns, "lookup", generate_lookup(GET(syns, "fwd")));
        e->live = merged;
        return merged;
    }
    J *d = DUP(e->snapshot);
    if (!d || !e->graph) {
        DEL(d);
        fail(err, 4, "Live finder requires a loaded RDF graph");
        return NULL;
    }
    mc_error local_error = {0};
    J *labels = graph_predicate(e->graph, RDFS "label", 0, 0, &local_error);
    set(d, "_labels", labels ? labels : NIL());
    const char *ns = S(GET(e->graph->prefixes, e->name));
    set(d, "_namespace_bound", BOOL(*ns != 0));
    J *custom = OBJ();
    if (*ns) {
        const char *names[] = {"effects", "requires", "similarTo", "implies", "uses"};
        for (size_t i = 0; i < sizeof(names) / sizeof(*names); i++) {
            Buf full = {0};
            buf_put(&full, ns);
            buf_put(&full, names[i]);
            mc_error ignored = {0};
            J *view = graph_predicate(e->graph, full.p, 1, 0, &ignored);
            free(full.p);
            if (view)
                PUT(custom, names[i], view);
        }
    }
    set(d, "_custom_predicates", custom);
    set(d, "_live", BOOL(1));
    J *entities = ARR(), *types = GET(GET(d, "by_predicate"), "rdfs:subClassOf");
    EACH(k, types) {
        unique(entities, k->string);
        EACH(v, k) unique(entities, S(v));
    }
    set(d, "entities", entities);
    ner_views(e->graph, d, err);
    J *fwd = GET(GET(d, "synonyms"), "fwd"), *rev = GET(GET(d, "synonyms"), "rev");
    if (e->source_path && *e->source_path) {
        Buf path = {0};
        buf_put(&path, e->source_path);
        buf_put(&path, ".txt");
        FILE *f = mc_fopen(path.p, "rb");
        if (f) {
            fclose(f);
            char *text = read_file(path.p, err);
            J *external = text ? external_synonyms(text) : NULL;
            free(text);
            EACH(k, external) {
                EACH(v, k) {
                    unique(ensure(fwd, k->string, 1), S(v));
                    unique(ensure(rev, S(v), 1), k->string);
                }
            }
            DEL(external);
        }
        free(path.p);
    }
    EACH(k, fwd) sort_strings(k, 0);
    EACH(k, rev) sort_strings(k, 0);
    set(GET(d, "synonyms"), "lookup", generate_lookup(fwd));
    if (err->code) {
        DEL(d);
        return NULL;
    }
    e->live = d;
    return d;
}
