/*
 * Example 01: Basic parsing
 *
 * This is an example of parsing a JSON string and extracting various fields.
 *
 * Build (from the repo root):
 *   make examples
 *
 * Or manually:
 *   cc -std=c99 -Iinclude examples/01_parse_basic.c src/nanojson.c -o 01_parse_basic
 */

#include "nanojson.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    const char *src =
        "{"
        "  \"name\": \"Pochix1103\","
        "  \"level\": 42,"
        "  \"active\": true,"
        "  \"tags\": [\"pwn\", \"crypto\", \"rev\"]"
        "}";

    nanojson_error err;
    nanojson_element *root = nanojson_parse(src, &err);
    if (root == NULL) {
        fprintf(stderr, "parse error at %zu:%zu: %s\n",
                err.line, err.column, err.message);
        return 1;
    }

    /* --- Scalar fields --- */
    nanojson_element *name   = nanojson_object_get(root, "name");
    nanojson_element *level  = nanojson_object_get(root, "level");
    nanojson_element *active = nanojson_object_get(root, "active");

    if (name && name->type == NANOJSON_STRING) {
        printf("name   = %.*s\n", (int)name->string_length, name->string);
    }
    if (level && level->type == NANOJSON_NUMBER) {
        printf("level  = %g\n", level->number);
    }
    if (active && active->type == NANOJSON_BOOL) {
        printf("active = %s\n", active->boolean ? "true" : "false");
    }

    /* --- Iterate an array using the linked-list interface --- */
    nanojson_element *tags = nanojson_object_get(root, "tags");
    if (tags && tags->type == NANOJSON_ARRAY) {
        printf("tags   = [");
        const nanojson_element *t = tags->first_child;
        bool first = true;
        while (t != NULL) {
            if (!first) printf(", ");
            if (t->type == NANOJSON_STRING) {
                printf("%.*s", (int)t->string_length, t->string);
            }
            first = false;
            t = t->next;
        }
        printf("]\n");
    }

    /* --- Indexed access --- */
    printf("tags[1] = %.*s\n",
           (int)nanojson_array_at(tags, 1)->string_length,
           nanojson_array_at(tags, 1)->string);

    nanojson_free(root);
    return 0;
}
