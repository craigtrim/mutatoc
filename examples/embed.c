#include "mutatoc.h"
#include <stdio.h>
static int request(mc_engine *engine, const char *json) {
    mc_error error;
    char *response = mc_request(engine, json, &error);
    if (!response)
        return 0;
    puts(response);
    mc_free(response);
    return error.code == 0;
}
int main(void) {
    mc_engine *engine = mc_create();
    if (!engine)
        return 1;
    int ok =
        request(
            engine,
            "{\"op\":\"load\",\"name\":\"example\",\"snapshot\":{\"labels\":{\"dog\":\"Dog\"},"
            "\"spans\":{},\"synonyms\":{\"fwd\":{\"dog\":[\"hound\",\"dog\"]},\"rev\":{\"hound\":["
            "\"dog\"],\"dog\":[\"dog\"]},\"lookup\":{\"1\":[\"hound\",\"dog\"]}}}}") &&
        request(engine, "{\"op\":\"parse_tokens\",\"tokens\":[{\"id\":0,\"text\":\"hound\","
                        "\"normal\":\"hound\",\"x\":0,\"y\":5,\"ent\":\"\"}]}");
    mc_destroy(engine);
    return ok ? 0 : 1;
}
