#include "mc.h"
static int failures = 0, total = 0;
static void same(char *actual, const char *expected) {
    total++;
    if (!actual || strcmp(actual, expected)) {
        fprintf(stderr, "Expected [%s], got [%s]\n", expected, actual ? actual : "NULL");
        failures++;
    }
    free(actual);
}
int main(void) {
    const char *vectors[][2] = {{"g:h", "g:h"},
                                {"g", "http://a/b/c/g"},
                                {"./g", "http://a/b/c/g"},
                                {"g/", "http://a/b/c/g/"},
                                {"/g", "http://a/g"},
                                {"//g", "http://g"},
                                {"?y", "http://a/b/c/d;p?y"},
                                {"g?y", "http://a/b/c/g?y"},
                                {"#s", "http://a/b/c/d;p?q#s"},
                                {"g#s", "http://a/b/c/g#s"},
                                {"g?y#s", "http://a/b/c/g?y#s"},
                                {";x", "http://a/b/c/;x"},
                                {"g;x", "http://a/b/c/g;x"},
                                {"g;x?y#s", "http://a/b/c/g;x?y#s"},
                                {"", "http://a/b/c/d;p?q"},
                                {".", "http://a/b/c/"},
                                {"./", "http://a/b/c/"},
                                {"..", "http://a/b/"},
                                {"../", "http://a/b/"},
                                {"../g", "http://a/b/g"},
                                {"../..", "http://a/"},
                                {"../../", "http://a/"},
                                {"../../g", "http://a/g"},
                                {"../../../g", "http://a/g"},
                                {"../../../../g", "http://a/g"},
                                {"/./g", "http://a/g"},
                                {"/../g", "http://a/g"},
                                {"g.", "http://a/b/c/g."},
                                {".g", "http://a/b/c/.g"},
                                {"g..", "http://a/b/c/g.."},
                                {"..g", "http://a/b/c/..g"},
                                {"./../g", "http://a/b/g"},
                                {"./g/.", "http://a/b/c/g/"},
                                {"g/./h", "http://a/b/c/g/h"},
                                {"g/../h", "http://a/b/c/h"},
                                {"g;x=1/./y", "http://a/b/c/g;x=1/y"},
                                {"g;x=1/../y", "http://a/b/c/y"},
                                {"g?y/./x", "http://a/b/c/g?y/./x"},
                                {"g?y/../x", "http://a/b/c/g?y/../x"},
                                {"g#s/./x", "http://a/b/c/g#s/./x"},
                                {"g#s/../x", "http://a/b/c/g#s/../x"},
                                {"http:g", "http:g"}};
    for (size_t i = 0; i < sizeof(vectors) / sizeof(*vectors); i++)
        same(uri_resolve("http://a/b/c/d;p?q", vectors[i][0]), vectors[i][1]);
    same(lower("ΟΣ"), "ος");
    same(lower("ΟΣΑ"), "οσα");
    same(lower("AΣ'A"), "aσ'a");
    same(lower("AΣ' A"), "aς' a");
    same(lower("Σ"), "σ");
    same(lower("İSTANBUL"), "i̇stanbul");
    same(upper("Straße ﬃ"), "STRASSE FFI");
    same(norm("\t Dog\tCat\nPoodle  ", 1, 0), "dog\tcat\npoodle");
    same(norm("\t Dog\tCat\nPoodle  ", 1, 1), "dog cat poodle");
    same(norm("\xc2\xa0"
              "Dog\xc2\xa0"
              "Cat\xc2\xa0",
              0, 0),
         "Dog\xc2\xa0"
         "Cat");
    mc_error error = {0};
    J *value = json_parse("\"a\\u0000b\"", &error);
    same(cJSON_PrintUnformatted(value), "\"a\\u0000b\"");
    DEL(value);
    const char invalid[] = {'"', (char)0xc0, (char)0x80, '"', 0};
    value = json_parse(invalid, &error);
    total++;
    if (value || !error.code)
        failures++;
    DEL(value);
    printf("Text/URI contracts: %d checks, %d failures.\n", total, failures);
    return failures ? 1 : 0;
}
