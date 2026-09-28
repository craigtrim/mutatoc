#include "mc.h"
static J *read_json(const char *path) {
    mc_error err = {0};
    char *text = read_file(path, &err);
    if (!text) {
        fprintf(stderr, "%s\n", err.message);
        return NULL;
    }
    J *out = json_parse(text, &err);
    free(text);
    return out;
}
static J *call(mc_engine *engine, J *q) {
    char *text = cJSON_PrintUnformatted(q);
    mc_error err;
    char *response = mc_request(engine, text, &err);
    free(text);
    J *out = response ? cJSON_Parse(response) : NULL;
    mc_free(response);
    return out;
}
/* Query collection order is unspecified without ORDER BY. Preserve multiplicity,
   object keys, scalar types, and integer values; token tests compare in order. */
static int equal(const J *a, const J *b) {
    if (!a || !b)
        return a == b;
    if ((a->type & 255) != (b->type & 255))
        return 0;
    if (cJSON_IsObject(a)) {
        if (SIZE(a) != SIZE(b))
            return 0;
        EACH(k, a) if (!equal(k, GET(b, k->string))) return 0;
        return 1;
    }
    if (cJSON_IsArray(a)) {
        if (SIZE(a) != SIZE(b))
            return 0;
        unsigned char *used = calloc((size_t)SIZE(b) + 1, 1);
        int ok = 1;
        EACH(v, a) {
            int found = 0, index = 0;
            EACH(w, b) {
                if (!used[index] && equal(v, w)) {
                    used[index] = 1;
                    found = 1;
                    break;
                }
                index++;
            }
            if (!found) {
                ok = 0;
                break;
            }
        }
        free(used);
        return ok;
    }
    return cJSON_Compare(a, b, 1);
}
static char *path(const char *root, const char *folder, const char *name, const char *suffix) {
    Buf b = {0};
    buf_put(&b, root);
    buf_put(&b, folder);
    buf_put(&b, name);
    buf_put(&b, suffix);
    return buf_take(&b);
}
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    char *corpus_path = path(argv[1], "/tests/fixtures/api/", "queries", ".json");
    J *rows = read_json(corpus_path);
    free(corpus_path);
    if (!rows)
        return 2;
    mc_engine *engine = mc_create();
    char *current = NULL, *directory = NULL;
    int total = 0, failed = 0;
    EACH(row, rows) {
        const char *name = S(GET(row, "fixture"));
        J *q = GET(row, "request");
        const char *interface = S(GET(q, "interface"));
        Buf key = {0};
        buf_put(&key, name);
        buf_put(&key, interface);
        if (!current || strcmp(current, key.p)) {
            free(current);
            current = copy(key.p);
            free(directory);
            const char *folder = !strcmp(name, "animals-test") ? "/tests/fixtures/ontologies/"
                                                               : "/tests/fixtures/api/";
            directory = path(argv[1], folder, "", "");
            directory[strlen(directory) - 1] = 0;
            J *load = OBJ();
            PUT(load, "op", STR("load"));
            PUT(load, "name", STR(name));
            if (!strcmp(interface, "owl") || !strcmp(interface, "data")) {
                char *file = path(argv[1], folder, name, ".owl");
                PUT(load, "path", STR(file));
                free(file);
                PUT(load, "class_based", BOOL(1));
                PUT(load, "interface", STR(interface));
            } else {
                char *file = path(argv[1], "/tests/fixtures/api/", name, ".snapshot.json");
                PUT(load, "snapshot", read_json(file));
                free(file);
            }
            J *loaded = call(engine, load);
            DEL(load);
            if (!cJSON_IsTrue(GET(loaded, "ok"))) {
                fprintf(stderr, "API fixture load failed\n");
                return 2;
            }
            DEL(loaded);
        }
        free(key.p);
        J *response = call(engine, q);
        total++;
        J *expected = GET(row, "expected"), *replacement = NULL;
        if (!strcmp(S(expected), "$FIXTURE_DIRECTORY"))
            expected = replacement = STR(directory);
        int ok = GET(row, "error") ? cJSON_IsFalse(GET(response, "ok"))
                                   : cJSON_IsTrue(GET(response, "ok")) &&
                                         equal(GET(response, "result"), expected);
        if (!ok) {
            failed++;
            fprintf(stderr, "%s %s.%s\n", name, interface, S(GET(q, "method")));
        }
        DEL(replacement);
        DEL(response);
    }
    mc_destroy(engine);
    DEL(rows);
    free(current);
    free(directory);
    printf("Public query API parity: %d/%d passed.\n", total - failed, total);
    return failed ? 1 : 0;
}
