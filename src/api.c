#include "mc.h"
#include "lingpatlab.h"
mc_engine *mc_create(void) {
    mc_engine *e = calloc(1, sizeof(*e));
    if (e) {
        e->distance = 4;
        e->name = copy("ontology");
    }
    return e;
}
static void clear_ontology(mc_engine *e) {
    sparql_invalidate(e->sparql);
    match_index_free(e->index);
    e->index = NULL;
    DEL(e->snapshot);
    e->snapshot = NULL;
    DEL(e->live);
    e->live = NULL;
    DEL(e->names);
    e->names = NULL;
    rdf_free(e->graph);
    e->graph = NULL;
    free(e->directory);
    e->directory = NULL;
    free(e->source_path);
    e->source_path = NULL;
    free(e->name);
    e->name = NULL;
    for (size_t i = 0; i < e->part_count; i++)
        mc_destroy(e->parts[i]);
    free(e->parts);
    e->parts = NULL;
    e->part_count = 0;
}
void mc_destroy(mc_engine *e) {
    if (!e)
        return;
    spacy_free(e->spacy);
    spacy_free(e->sparql);
    e->sparql = NULL;
    clear_ontology(e);
    free(e);
}
void mc_free(void *p) {
    free(p);
}
/* Every path that replaces the snapshot or live view frees the index first, so a
   matching view pointer always identifies the view the index was built from. */
static MatchIndex *view_index(mc_engine *e, J *view, mc_error *err) {
    if (e->index && match_index_view(e->index) == view)
        return e->index;
    match_index_free(e->index);
    e->index = match_index_build(view);
    if (!e->index)
        fail(err, 1, "Cannot allocate match index");
    return e->index;
}
static const char *schema(Graph *g) {
    int skos = 0, individual = 0, subclass = 0;
    for (size_t i = 0; i < g->n; i++) {
        Triple *t = &g->ts[i];
        if (!strcmp(t->p.value, RDF "type")) {
            skos |= !strcmp(t->o.value, SKOS "Concept");
            individual |= !strcmp(t->o.value, OWL "NamedIndividual");
        }
        subclass |= !strcmp(t->p.value, RDFS "subClassOf");
    }
    return skos ? "SKOS" : individual ? (subclass ? "MIXED" : "INDIVIDUAL") : "CLASS_BASED";
}
static void walk_owl(J *edges, const char *node, J *out, Map *visited) {
    if (map_get(visited, node))
        return;
    map_put(visited, node, (void *)1);
    EACH(v, GET(edges, node)) {
        unique(out, S(v));
        walk_owl(edges, S(v), out, visited);
    }
}
static J *materialize(mc_engine *e, mc_error *err) {
    if (e->graph_only) {
        J *next = ontology_build(e->graph, e->distance, e->force_class, err);
        if (!next || err->code) {
            DEL(next);
            return NULL;
        }
        match_index_free(e->index);
        e->index = NULL;
        DEL(e->snapshot);
        e->snapshot = next;
        e->graph_only = 0;
    }
    return e->snapshot;
}
static J *owl_query(mc_engine *e, J *q, mc_error *err) {
    Graph *g = e->graph;
    const char *m = S(GET(q, "method"));
    J *args = GET(q, "args");
    const char *s = S(AT(args, 0));
    J *out = ARR();
    int data = !strcmp(S(GET(q, "interface")), "data");
    if (data && strcmp(m, "by_predicate") && strcmp(m, "by_predicate_rev")) {
        DEL(out);
        if (!materialize(e, err))
            return NULL;
        return data_query(live_view(e, err), m, args, GET(q, "kwargs"), err);
    }
    if (!strcmp(m, "keyed_labels")) {
        DEL(out);
        return materialize(e, err) ? DUP(GET(e->snapshot, "labels")) : NULL;
    }
    if (!strcmp(m, "labels") || !strcmp(m, "entities") || !strcmp(m, "types") ||
        !strcmp(m, "ngrams")) {
        int level = AT(args, 0) ? AT(args, 0)->valueint : 1;
        for (size_t i = 0; i < g->n; i++) {
            Triple *t = &g->ts[i];
            int take = (!strcmp(m, "labels") || !strcmp(m, "entities"))
                           ? !strcmp(t->p.value, RDFS "label")
                       : !strcmp(m, "types") ? !strcmp(t->p.value, RDF "type")
                                             : !strcmp(t->p.value, RDFS "subClassOf");
            if (!take)
                continue;
            Term *v = !strcmp(m, "labels") ? &t->o : &t->s;
            J *vs = rdf_flatten(
                g, v,
                !strcmp(m, "ngrams") ||
                    (!strcmp(m, "types") && !cJSON_IsFalse(GET(GET(q, "kwargs"), "to_lowercase"))),
                err);
            EACH(x, vs) {
                int n = 1;
                for (const char *c = S(x); *c; c++)
                    n += *c == '_';
                if (strcmp(m, "ngrams") || n == level)
                    ADD(out, DUP(x));
            }
            DEL(vs);
        }
        if (!SIZE(out) && strcmp(m, "ngrams")) {
            DEL(out);
            return NIL();
        }
        return out;
    }
    if (!strcmp(m, "entity_exists")) {
        int yes = 0;
        for (size_t i = 0; i < g->n; i++)
            if (!strcmp(g->ts[i].p.value, RDFS "label") && !strcmp(local(g->ts[i].s.value), s))
                yes = 1;
        DEL(out);
        return BOOL(yes);
    }
    if (!strcmp(m, "predicates")) {
        DEL(out);
        out = OBJ();
        Map seen = {0};
        for (size_t i = 0; i < g->n; i++) {
            const char *p = g->ts[i].p.value, *prefix = "";
            if (map_get(&seen, p))
                continue;
            map_put(&seen, p, (void *)1);
            if (!strncmp(p, RDFS, strlen(RDFS)))
                prefix = "rdfs";
            else if (!strncmp(p, SKOS, strlen(SKOS)))
                prefix = "skos";
            else if (!strncmp(p, OWL, strlen(OWL)))
                prefix = "owl";
            else if (!strncmp(p, RDF, strlen(RDF)))
                prefix = "rdf";
            ADD(ensure(out, prefix, 1), STR(local(p)));
        }
        map_free(&seen);
        ensure(out, "", 1);
        return out;
    }
    if (!strcmp(m, "parents") || !strcmp(m, "children") || !strcmp(m, "ancestors") ||
        !strcmp(m, "descendants")) {
        for (const char *p = s; *p; p++)
            if (isspace((unsigned char)*p)) {
                fail(err, 4, "Invalid prefixed entity name: %s", s);
                DEL(out);
                return NULL;
            }
        int parent = !strcmp(m, "parents") || !strcmp(m, "ancestors");
        int transitive = !strcmp(m, "ancestors") || !strcmp(m, "descendants");
        J *edges = OBJ();
        for (size_t i = 0; i < g->n; i++) {
            Triple *t = &g->ts[i];
            if (strcmp(t->p.value, RDFS "subClassOf"))
                continue;
            if (t->s.kind == 0 && t->o.kind == 0)
                unique(ensure(edges, parent ? local(t->s.value) : local(t->o.value), 1),
                       parent ? local(t->o.value) : local(t->s.value));
            else if (!transitive) {
                Term *key = parent ? &t->s : &t->o, *value = parent ? &t->o : &t->s;
                if (key->kind == 0 && !strcmp(local(key->value), s)) {
                    J *values = rdf_flatten(g, value, 0, err);
                    EACH(v, values) if (strcmp(S(v), "nil")) ADD(out, DUP(v));
                    DEL(values);
                }
            }
        }
        if (transitive) {
            Map visited = {0};
            walk_owl(edges, s, out, &visited);
            map_free(&visited);
        } else
            EACH(v, GET(edges, s)) ADD(out, DUP(v));
        DEL(edges);
        if (!SIZE(out)) {
            DEL(out);
            return NIL();
        }
        if (!strcmp(m, "ancestors")) {
            sort_strings(out, 0);
            J *rev = ARR();
            while (SIZE(out))
                ADD(rev, cJSON_DetachItemFromArray(out, SIZE(out) - 1));
            DEL(out);
            out = rev;
        }
        return out;
    }
    if (!strcmp(m, "by_predicate") || !strcmp(m, "by_predicate_rev")) {
        DEL(out);
        out = OBJ();
        char *full = NULL;
        const char *colon = strchr(s, ':');
        if (colon) {
            char *pre = slice(s, (size_t)(colon - s));
            const char *ns = S(GET(g->prefixes, pre));
            if (!*ns) {
                if (!strcmp(pre, "rdfs"))
                    ns = RDFS;
                else if (!strcmp(pre, "rdf"))
                    ns = RDF;
                else if (!strcmp(pre, "owl"))
                    ns = OWL;
                else if (!strcmp(pre, "skos"))
                    ns = SKOS;
            }
            if (!*ns) {
                fail(err, 4, "Unknown namespace prefix: %s", pre);
                free(pre);
                DEL(out);
                return NULL;
            }
            Buf b = {0};
            buf_put(&b, ns);
            buf_put(&b, colon + 1);
            full = buf_take(&b);
            free(pre);
        } else {
            Buf b = {0};
            const char *ns = S(GET(g->prefixes, e->name));
            if (!*ns) {
                fail(err, 4, "Unknown namespace prefix: %s", e->name);
                DEL(out);
                return NULL;
            }
            buf_put(&b, ns);
            buf_put(&b, s);
            full = buf_take(&b);
        }
        int lc = !cJSON_IsFalse(GET(GET(q, "kwargs"), "to_lowercase")),
            rev = cJSON_IsTrue(GET(GET(q, "kwargs"), "reverse")) || !strcmp(m, "by_predicate_rev");
        J *result = graph_predicate(g, full, lc, rev, err);
        free(full);
        DEL(out);
        return result;
    }
    DEL(out);
    if (data && !strcmp(m, "find_ner"))
        return NIL();
    return materialize(e, err) ? ontology_query(e->snapshot, m, args, err) : NULL;
}
static J *dispatch(mc_engine *, J *, mc_error *);
static J *transitive_query(mc_engine *engine, const char *interface, const char *entity,
                           const char *method, Map *active, int depth, mc_error *err) {
    if (depth > 1024 || map_get(active, entity)) {
        fail(err, 4, "Cycle in transitive query at %s", entity);
        return NULL;
    }
    if (!*method || !strcmp(method, "transitive")) {
        fail(err, 2, "transitive requires a query method name");
        return NULL;
    }
    map_put(active, entity, (void *)1);
    J *q = OBJ(), *args = ARR();
    ADD(args, STR(entity));
    PUT(q, "op", STR("query"));
    PUT(q, "interface", STR(interface));
    PUT(q, "method", STR(method));
    PUT(q, "args", args);
    J *values = dispatch(engine, q, err), *out = ARR();
    EACH(v, values) ADD(out, cJSON_IsObject(values) ? STR(v->string) : DUP(v));
    DEL(values);
    set(q, "method", STR("parents"));
    J *parents = err->code ? NULL : dispatch(engine, q, err);
    DEL(q);
    EACH(parent, parents) {
        if (err->code)
            break;
        J *child = transitive_query(engine, interface, S(parent), method, active, depth + 1, err);
        EACH(v, child) ADD(out, DUP(v));
        DEL(child);
    }
    DEL(parents);
    map_put(active, entity, NULL);
    if (err->code) {
        DEL(out);
        return NULL;
    }
    return out;
}
static J *load_collection(mc_engine *e, J *q, mc_error *err) {
    J *sources = GET(q, "sources");
    if (!sources)
        sources = GET(q, "paths");
    if (!cJSON_IsArray(sources) || !SIZE(sources)) {
        fail(err, 2, "sources/paths must be a nonempty array");
        return NULL;
    }
    mc_engine *next = mc_create();
    if (!next) {
        fail(err, 1, "Out of memory");
        return NULL;
    }
    next->parts = calloc((size_t)SIZE(sources), sizeof(*next->parts));
    if (!next->parts) {
        mc_destroy(next);
        fail(err, 1, "Out of memory");
        return NULL;
    }
    next->snapshot = OBJ();
    next->names = ARR();
    EACH(source, sources) {
        if (!cJSON_IsString(source) && !cJSON_IsObject(source)) {
            fail(err, 2, "Each source must be a path or load object");
            break;
        }
        J *request = cJSON_IsObject(source) ? DUP(source) : OBJ();
        if (cJSON_IsString(source))
            PUT(request, "path", DUP(source));
        if (GET(request, "sources") || GET(request, "paths") ||
            cJSON_IsTrue(GET(request, "graph_only"))) {
            DEL(request);
            fail(err, 2, "Nested collections are not supported; supply a flat sources array");
            break;
        }
        set(request, "op", STR("load"));
        const char *options[] = {"class_based", "distance", "interface"};
        for (size_t i = 0; i < 3; i++)
            if (!GET(request, options[i]) && GET(q, options[i]))
                PUT(request, options[i], DUP(GET(q, options[i])));
        mc_engine *part = mc_create();
        if (!part) {
            DEL(request);
            fail(err, 1, "Out of memory");
            break;
        }
        next->parts[next->part_count++] = part;
        part->sparql = e->sparql;
        J *loaded = dispatch(part, request, err);
        part->sparql = NULL;
        DEL(request);
        DEL(loaded);
        if (err->code)
            break;
        merge_views(next->snapshot, part->snapshot);
        ADD(next->names, STR(part->name));
    }
    if (err->code) {
        mc_destroy(next);
        return NULL;
    }
    J *syns = GET(next->snapshot, "synonyms");
    set(syns, "lookup", generate_lookup(GET(syns, "fwd")));
    Graph **graphs = calloc(next->part_count, sizeof(*graphs));
    if (!graphs) {
        mc_destroy(next);
        fail(err, 1, "Out of memory");
        return NULL;
    }
    for (size_t i = 0; i < next->part_count; i++)
        graphs[i] = next->parts[i]->graph;
    next->graph = rdf_merge(graphs, next->part_count);
    free(graphs);
    next->live_mode = !strcmp(S(GET(q, "interface")), "data");
    next->directory = copy(next->parts[0]->directory);
    free(next->name);
    next->name = copy(*S(GET(q, "name")) ? S(GET(q, "name")) : "collection");
    clear_ontology(e);
    mc_spacy *worker = e->spacy, *rdf_worker = e->sparql;
    *e = *next;
    e->spacy = worker;
    e->sparql = rdf_worker;
    free(next);
    J *result = OBJ();
    PUT(result, "name", STR(e->name));
    PUT(result, "ontologies", DUP(e->names));
    PUT(result, "triples", NUM(e->graph ? (double)e->graph->n : 0));
    PUT(result, "entities", NUM(SIZE(GET(e->snapshot, "labels"))));
    return result;
}
static J *dispatch(mc_engine *e, J *q, mc_error *err) {
    const char *op = S(GET(q, "op"));
    if (!*op) {
        fail(err, 2, "Request requires string op");
        return NULL;
    }
    if (!strcmp(op, "configure_spacy") || !strcmp(op, "configure_sparql")) {
        J *timeout = GET(q, "timeout_ms");
        if (timeout &&
            (!cJSON_IsNumber(timeout) || timeout->valuedouble < 0 ||
             timeout->valuedouble > 3600000 || timeout->valuedouble != (double)timeout->valueint)) {
            fail(err, 2, "timeout_ms must be an integer from 0 to 3600000");
            return NULL;
        }
        int ok = !strcmp(op, "configure_sparql")
                     ? mc_use_sparql(e, S(GET(q, "python")), S(GET(q, "worker")),
                                     timeout ? (unsigned)timeout->valueint : 0, err)
                     : mc_use_spacy(e, S(GET(q, "python")), S(GET(q, "worker")),
                                    GET(q, "model") ? S(GET(q, "model")) : "en_core_web_sm",
                                    timeout ? (unsigned)timeout->valueint : 0, err);
        return ok ? BOOL(1) : NULL;
    }
    if (!strcmp(op, "lingpatlab"))
        return lp_request(e, q, err);
    if (!strcmp(op, "spacy_info"))
        return spacy_info(e, err);
    if (!strcmp(op, "sparql") || (!strcmp(op, "query") && !strcmp(S(GET(q, "method")), "adhoc")))
        return sparql_query(e, q, err);
    if (!strcmp(op, "version"))
        return STR(MUTATOC_VERSION);
    if (!strcmp(op, "load")) {
        if (GET(q, "sources") || GET(q, "paths")) {
            if (cJSON_IsTrue(GET(q, "graph_only"))) {
                fail(err, 2, "graph_only applies to a single source");
                return NULL;
            }
            return load_collection(e, q, err);
        }
        const char *path = S(GET(q, "path")), *name = S(GET(q, "name"));
        J *d = GET(q, "snapshot");
        Graph *g = NULL;
        J *next = NULL;
        if (d) {
            if (!cJSON_IsObject(d)) {
                fail(err, 2, "snapshot must be an object");
                return NULL;
            }
            next = DUP(d);
        } else {
            char *data = GET(q, "turtle") ? copy(S(GET(q, "turtle"))) : read_file(path, err);
            if (!data)
                return NULL;
            const char *p = data;
            while (isspace((unsigned char)*p))
                p++;
            if (*p == '{')
                next = json_parse(data, err);
            else {
                char *base =
                    GET(q, "base") ? copy(S(GET(q, "base"))) : file_uri(*path ? path : ".");
                g = rdf_parse(data, base, err);
                free(base);
                int normalized = g ? normalize_special_literals(e, g, err) : 0;
                if (normalized == 2) {
                    Graph *rebuilt = rdf_merge(&g, 1);
                    rdf_free(g);
                    g = rebuilt;
                    if (!g)
                        fail(err, 1, "Out of memory rebuilding normalized graph");
                }
                if (g && normalized && cJSON_IsTrue(GET(q, "graph_only"))) {
                    next = OBJ();
                    PUT(next, "synonyms", OBJ());
                } else if (g && normalized)
                    next = ontology_build(
                        g, GET(q, "distance") ? GET(q, "distance")->valueint : e->distance,
                        cJSON_IsTrue(GET(q, "class_based")), err);
            }
            free(data);
        }
        if (!next || err->code) {
            DEL(next);
            rdf_free(g);
            return NULL;
        }
        if (!cJSON_IsObject(GET(next, "synonyms"))) {
            DEL(next);
            rdf_free(g);
            fail(err, 2, "Snapshot requires synonyms object");
            return NULL;
        }
        clear_ontology(e);
        e->snapshot = next;
        e->graph = g;
        e->graph_only = g && cJSON_IsTrue(GET(q, "graph_only"));
        e->force_class = cJSON_IsTrue(GET(q, "class_based"));
        if (GET(q, "distance"))
            e->distance = GET(q, "distance")->valueint;
        if (*name)
            e->name = copy(name);
        else {
            const char *p = strrchr(path, '/'), *win = strrchr(path, '\\');
            if (win && (!p || win > p))
                p = win;
            e->name = copy(p ? p + 1 : *path ? path : "ontology");
            char *dot = strrchr(e->name, '.');
            if (dot)
                *dot = 0;
        }
        e->names = ARR();
        ADD(e->names, STR(e->name));
        const char *slash = strrchr(path, '/'), *backslash = strrchr(path, '\\');
        if (backslash && (!slash || backslash > slash))
            slash = backslash;
        e->directory = slash ? slice(path, (size_t)(slash - path)) : copy(".");
        e->source_path = copy(path);
        e->live_mode = !strcmp(S(GET(q, "interface")), "data");
        J *r = OBJ();
        PUT(r, "name", STR(e->name));
        PUT(r, "schema", g ? STR(schema(g)) : NIL());
        PUT(r, "triples", NUM(g ? (double)g->n : 0));
        PUT(r, "entities", NUM(SIZE(GET(next, "labels"))));
        return r;
    }
    if (!strcmp(op, "read_rdf")) {
        Graph *g = rdf_parse(S(GET(q, "turtle")), S(GET(q, "base")), err);
        if (!g)
            return NULL;
        J *out = rdf_json(g);
        rdf_free(g);
        return out;
    }
    if (!strcmp(op, "detect_schema")) {
        Graph *g = rdf_parse(S(GET(q, "turtle")), "", err);
        if (!g)
            return NULL;
        J *r = STR(schema(g));
        rdf_free(g);
        return r;
    }
    if (!strcmp(op, "tokenize")) {
        if (!cJSON_IsString(GET(q, "text"))) {
            fail(err, 2, "tokenize requires string text");
            return NULL;
        }
        return spacy_tokens(e, S(GET(q, "text")), err);
    }
    if (!strcmp(op, "generate_spans"))
        return generate_spans(GET(q, "data"), GET(q, "distance") ? GET(q, "distance")->valueint : 4,
                              cJSON_IsTrue(GET(q, "plus_only")));
    if (!strcmp(op, "generate_synonyms"))
        return generate_synonyms(GET(q, "data"), cJSON_IsTrue(GET(q, "reverse")));
    if (!strcmp(op, "generate_lookup"))
        return generate_lookup(GET(q, "data"));
    if (!e->snapshot) {
        fail(err, 4, "Load an ontology before this operation");
        return NULL;
    }
    if (!strcmp(op, "snapshot"))
        return materialize(e, err) ? DUP(e->snapshot) : NULL;
    if (!strcmp(op, "triples")) {
        if (!e->graph) {
            fail(err, 4, "RDF graph is unavailable for a JSON snapshot");
            return NULL;
        }
        return rdf_json(e->graph);
    }
    if (!strcmp(op, "schema"))
        return e->graph ? STR(schema(e->graph)) : NIL();
    if (!strcmp(op, "query") && strcmp(S(GET(q, "interface")), "owl") && !materialize(e, err))
        return NULL;
    if ((!strcmp(op, "parse") || !strcmp(op, "parse_tokens") || !strcmp(op, "transform_tokens")) &&
        !materialize(e, err))
        return NULL;
    if (!strcmp(op, "query")) {
        const char *method = S(GET(q, "method"));
        J *args = GET(q, "args");
        if (!strcmp(method, "ontologies"))
            return DUP(e->names);
        if (!strcmp(method, "absolute_path"))
            return STR(e->directory ? e->directory : "");
        const char *interface = S(GET(q, "interface"));
        if (!strcmp(method, "transitive")) {
            const char *target = S(GET(GET(q, "kwargs"), "query"));
            if (!*target)
                target = S(AT(args, 1));
            Map active = {0};
            J *r = transitive_query(e, interface, S(AT(args, 0)), target, &active, 0, err);
            map_free(&active);
            return r;
        }
        if (!strcmp(interface, "ask_json")) {
            if (!strcmp(method, "types") || !strcmp(method, "entities")) {
                J *value = GET(e->snapshot, method);
                return value ? DUP(value) : ARR();
            }
        } else if (!strcmp(method, "equivalents") && strcmp(interface, "owl") &&
                   strcmp(interface, "data")) {
            fail(err, 4,
                 "FindOntologyJSON.equivalents has no implementation in the reference; use "
                 "ask_json for the stored view or data for entity queries");
            return NULL;
        }
        if (e->part_count && !strcmp(interface, "data") &&
            (!strcmp(method, "by_predicate") || !strcmp(method, "by_predicate_rev"))) {
            J *out = OBJ();
            int successes = 0;
            mc_error last = {0};
            for (size_t i = 0; i < e->part_count; i++) {
                mc_error local_error = {0};
                J *value = owl_query(e->parts[i], q, &local_error);
                if (local_error.code)
                    last = local_error;
                else {
                    merge_views(out, value);
                    successes++;
                }
                DEL(value);
            }
            if (!successes && last.code) {
                *err = last;
                DEL(out);
                return NULL;
            }
            return out;
        }
        if (!strcmp(interface, "data")) {
            if (e->part_count || !strcmp(method, "ner_depth") || !strcmp(method, "ner_depth_rev") ||
                !strcmp(method, "ner_taxonomy") || !strcmp(method, "ner_taxonomy_rev"))
                return data_query(live_view(e, err), method, args, GET(q, "kwargs"), err);
        }
        if (e->graph && (!strcmp(interface, "owl") || !strcmp(interface, "data")))
            return owl_query(e, q, err);
        return ontology_query(e->snapshot, method, args, err);
    }
    if (!strcmp(op, "transform_tokens")) {
        J *view = e->live_mode ? live_view(e, err) : e->snapshot;
        MatchIndex *index = view ? view_index(e, view, err) : NULL;
        return index ? transform_tokens(view, index, GET(q, "tokens"), e->names,
                                        S(GET(q, "stage")), err)
                     : NULL;
    }
    if (!strcmp(op, "parse") || !strcmp(op, "parse_tokens")) {
        J *view = e->live_mode ? live_view(e, err) : e->snapshot;
        if (!view)
            return NULL;
        J *synonyms = GET(view, "synonyms"), *lookup = GET(synonyms, "lookup"),
          *fwd = GET(synonyms, "fwd");
        if (!lookup || !lookup->child || !fwd || !fwd->child) {
            fail(err, 4, "Empty ontology");
            return NULL;
        }
        MatchIndex *index = view_index(e, view, err);
        if (!index)
            return NULL;
        J *tokens;
        if (!strcmp(op, "parse_tokens"))
            tokens = DUP(GET(q, "tokens"));
        else {
            if (!cJSON_IsString(GET(q, "text"))) {
                fail(err, 2, "parse requires string text");
                return NULL;
            }
            tokens = spacy_tokens(e, S(GET(q, "text")), err);
        }
        if (!tokens)
            return NULL;
        J *out = match_tokens(index, tokens, e->names, GET(q, "ctr") ? GET(q, "ctr")->valueint : 0,
                              err);
        DEL(tokens);
        if (!out)
            return NULL;
        J *r = OBJ();
        char *s = render(out);
        PUT(r, "text", STR(s));
        free(s);
        PUT(r, "tokens", out);
        return r;
    }
    fail(err, 4, "Unknown operation: %s", op);
    return NULL;
}
static void warm_index(mc_engine *e) {
    /* Pay for the match index during load so parsing starts with it. A view that
       cannot be built yet reports its error from the first parse, as before. */
    if (!e->snapshot || e->graph_only)
        return;
    mc_error ignored = {0};
    J *view = e->live_mode ? live_view(e, &ignored) : e->snapshot;
    if (view && !ignored.code)
        view_index(e, view, &ignored);
}
char *mc_request(mc_engine *engine, const char *request, mc_error *error) {
    mc_error local_error = {0};
    mc_error *err = error ? error : &local_error;
    memset(err, 0, sizeof(*err));
    J *q = request ? json_parse(request, err) : NULL;
    if (!engine || !request)
        fail(err, 2, "Engine and request must be non-null");
    if (q && !cJSON_IsObject(q))
        fail(err, 2, "Request must be a JSON object");
    J *result = err->code ? NULL : dispatch(engine, q, err);
    if (!err->code && !strcmp(S(GET(q, "op")), "load"))
        warm_index(engine);
    DEL(q);
    J *r = OBJ();
    PUT(r, "ok", BOOL(!err->code));
    if (err->code) {
        J *x = OBJ();
        PUT(x, "code", NUM(err->code));
        PUT(x, "message", STR(err->message));
        if (err->line) {
            PUT(x, "line", NUM((double)err->line));
            PUT(x, "column", NUM((double)err->column));
        }
        PUT(r, "error", x);
        DEL(result);
    } else
        PUT(r, "result", result ? result : NIL());
    char *out = cJSON_PrintUnformatted(r);
    DEL(r);
    return out;
}
