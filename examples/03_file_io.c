/*
 * Example 03: File I/O
 *
 * This is an example of reading a JSON file, rewriting the tree structure, and writing the result to a separate file.
 *
 * Usage:
 *   ./03_file_io <input.json> <output.json>
 */

#include "nanojson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *slurp_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    size_t read = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (read != (size_t)sz) {
        free(buf);
        return NULL;
    }
    buf[sz] = '\0';
    if (out_size) *out_size = (size_t)sz;
    return buf;
}

static int dump_file(const char *path, const char *data) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) return -1;
    size_t len = strlen(data);
    size_t w   = fwrite(data, 1, len, f);
    fclose(f);
    return (w == len) ? 0 : -1;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <input.json> <output.json>\n", argv[0]);
        return 1;
    }

    size_t size = 0;
    char *src = slurp_file(argv[1], &size);
    if (src == NULL) {
        fprintf(stderr, "could not read '%s'\n", argv[1]);
        return 1;
    }

    nanojson_error err;
    nanojson_element *root = nanojson_parse_n(src, size, &err);
    free(src);
    if (root == NULL) {
        fprintf(stderr, "parse error at %zu:%zu (offset %zu): %s\n",
                err.line, err.column, err.offset, err.message);
        return 1;
    }

    /* As an example, add "processed_by" to the top level of the object. */
    if (root->type == NANOJSON_OBJECT) {
        nanojson_object_set(root, "processed_by",
                            nanojson_new_string("NanoJSON"));
    }

    char *out = nanojson_serialize(root, true);
    nanojson_free(root);
    if (out == NULL) {
        fprintf(stderr, "serialize failed\n");
        return 1;
    }

    int rc = dump_file(argv[2], out);
    nanojson_string_free(out);
    if (rc != 0) {
        fprintf(stderr, "could not write '%s'\n", argv[2]);
        return 1;
    }
    printf("wrote %s\n", argv[2]);
    return 0;
}
