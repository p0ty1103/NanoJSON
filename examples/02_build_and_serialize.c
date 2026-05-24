/*
 * Example 02: Build a JSON tree and serialize it
 *
 * A sample demonstrating how to construct from an empty object/array and output using both Pretty and Minify methods.
 *
 * Build (from the repo root):
 *   make examples
 */

#include "nanojson.h"

#include <stdio.h>

int main(void) {
    /* construction: {
     *   "user": { "name": "Pochix1103", "id": 1337 },
     *   "scores": [100, 95.5, 88],
     *   "won": true,
     *   "next": null
     * }
     */
    nanojson_element *root = nanojson_new_object();
    if (root == NULL) return 1;

    /* user object */
    nanojson_element *user = nanojson_new_object();
    nanojson_object_set(user, "name", nanojson_new_string("Pochix1103"));
    nanojson_object_set(user, "id",   nanojson_new_number(1337));
    nanojson_object_set(root, "user", user);

    /* scores array */
    nanojson_element *scores = nanojson_new_array();
    nanojson_array_append(scores, nanojson_new_number(100));
    nanojson_array_append(scores, nanojson_new_number(95.5));
    nanojson_array_append(scores, nanojson_new_number(88));
    nanojson_object_set(root, "scores", scores);

    nanojson_object_set(root, "won",  nanojson_new_bool(true));
    nanojson_object_set(root, "next", nanojson_new_null());

    /* --- Minified --- */
    char *min = nanojson_serialize(root, false);
    if (min) {
        printf("--- minified ---\n%s\n\n", min);
        nanojson_string_free(min);
    }

    /* --- Pretty (default 2-space indent) --- */
    char *pretty = nanojson_serialize(root, true);
    if (pretty) {
        printf("--- pretty (2 spaces) ---\n%s\n\n", pretty);
        nanojson_string_free(pretty);
    }

    /* --- Pretty with custom indent --- */
    char *pretty4 = nanojson_serialize_with(root, true, 4);
    if (pretty4) {
        printf("--- pretty (4 spaces) ---\n%s\n", pretty4);
        nanojson_string_free(pretty4);
    }

    nanojson_free(root);
    return 0;
}
