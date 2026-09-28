#include "mc.h"
static J *read_json_path(const char *root, const char *name) {
    Buf p = {0};
    buf_put(&p, root);
    buf_put(&p, "/tests/fixtures/parity/");
    buf_put(&p, name);
    mc_error e = {0};
    char *s = read_file(p.p, &e);
    free(p.p);
    if (!s) {
        fprintf(stderr, "%s\n", e.message);
        return NULL;
    }
    J *j = json_parse(s, &e);
    free(s);
    return j;
}
static J *call(mc_engine *e, J *q) {
    char *s = cJSON_PrintUnformatted(q);
    mc_error err;
    char *r = mc_request(e, s, &err);
    free(s);
    J *j = r ? cJSON_Parse(r) : NULL;
    mc_free(r);
    return j;
}
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    J *index = read_json_path(argv[1], "index.json");
    if (!index)
        return 2;
    int total = 0, failed = 0;
    EACH(group, index) {
        mc_engine *engine = mc_create();
        J *snapshot = read_json_path(argv[1], S(GET(group, "snapshot")));
        J *cases = read_json_path(argv[1], S(GET(group, "cases")));
        J *q = OBJ();
        PUT(q, "op", STR("load"));
        PUT(q, "name", DUP(GET(group, "name")));
        PUT(q, "snapshot", snapshot);
        J *response = call(engine, q);
        DEL(q);
        if (!cJSON_IsTrue(GET(response, "ok"))) {
            fprintf(stderr, "load failed\n");
            return 2;
        }
        DEL(response);
        EACH(c, cases) {
            q = OBJ();
            PUT(q, "op", STR("parse_tokens"));
            PUT(q, "tokens", DUP(GET(c, "tokens")));
            PUT(q, "ctr", DUP(GET(c, "ctr")));
            response = call(engine, q);
            DEL(q);
            total++;
            int equal = GET(c, "expected_error")
                            ? cJSON_IsFalse(GET(response, "ok"))
                            : cJSON_IsTrue(GET(response, "ok")) &&
                                  cJSON_Compare(GET(GET(response, "result"), "tokens"),
                                                GET(c, "expected_tokens"), 1);
            if (!equal) {
                failed++;
                fprintf(stderr, "%s | %s\n", S(GET(group, "name")), S(GET(c, "text")));
            }
            DEL(response);
        }
        DEL(cases);
        mc_destroy(engine);
    }
    DEL(index);
    printf("Prepared token parity: %d/%d passed (complete annotations).\n", total - failed, total);
    return failed ? 1 : 0;
}
