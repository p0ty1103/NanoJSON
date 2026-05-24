/*
 * Example 04: Error handling
 *
 * This is an example showing how nanojson_error reports and where it finds errors when invalid JSON is passed.
 */

#include "nanojson.h"

#include <stdio.h>

static void try_parse(const char *label, const char *src) {
    nanojson_error err;
    nanojson_element *root = nanojson_parse(src, &err);
    printf("[%s]\n  input: %s\n", label, src);
    if (root == NULL) {
        printf("  -> error at line %zu, column %zu, offset %zu\n",
               err.line, err.column, err.offset);
        printf("     %s\n\n", err.message);
    } else {
        printf("  -> OK\n\n");
        nanojson_free(root);
    }
}

int main(void) {
    try_parse("empty input",
              "");
    try_parse("trailing comma in array",
              "[1, 2, 3,]");
    try_parse("trailing comma in object",
              "{\"a\": 1,}");
    try_parse("unterminated string",
              "\"hello");
    try_parse("invalid escape",
              "\"bad \\x escape\"");
    try_parse("incomplete unicode escape",
              "\"\\u12\"");
    try_parse("lone high surrogate",
              "\"\\uD83Dabc\"");
    try_parse("number with no fractional digit",
              "[1.]");
    try_parse("exponent with no digit",
              "[1e]");
    try_parse("trailing data after value",
              "{\"a\":1} junk");
    try_parse("multi-line input pointing to the right spot",
              "{\n"
              "  \"a\": 1,\n"
              "  \"b\":   bad\n"
              "}");
    try_parse("control character in string",
              "\"hi\x01there\"");
    return 0;
}
