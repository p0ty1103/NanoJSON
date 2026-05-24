/*
 * NanoJSON - C99 JSON parser & serializer
 *
 * Public header. Include this from any translation unit that wants to
 * use the API. Link against the compiled `nanojson.c`.
 *
 * Repository: https://github.com/p0ty1103/nanojson
 * License:    MIT (see LICENSE)
 */

#ifndef NANOJSON_H_INCLUDED
#define NANOJSON_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdlib.h>  /* for size_t */

/* ------------------------------------------------------------------ */
/* Types                                                                */
/* ------------------------------------------------------------------ */

typedef enum nanojson_type {
    NANOJSON_NULL   = 0,
    NANOJSON_BOOL   = 1,
    NANOJSON_NUMBER = 2,
    NANOJSON_STRING = 3,
    NANOJSON_ARRAY  = 4,
    NANOJSON_OBJECT = 5
} nanojson_type;

typedef struct nanojson_element {
    nanojson_type             type;

    /* Key is non-NULL only when this element is a direct child of an object. */
    char                     *key;
    size_t                    key_length;

    /* Scalar payloads (only one is meaningful per type). */
    bool                      boolean;
    double                    number;
    char                     *string;
    size_t                    string_length;

    /* Container children (doubly-linked list). */
    struct nanojson_element  *first_child;
    struct nanojson_element  *last_child;
    size_t                    child_count;

    /* Sibling links (set only when contained in an array or object). */
    struct nanojson_element  *prev;
    struct nanojson_element  *next;
    struct nanojson_element  *parent;
} nanojson_element;

typedef struct nanojson_error {
    char    message[128];
    size_t  line;        /* 1-based */
    size_t  column;      /* 1-based */
    size_t  offset;      /* 0-based byte offset */
    bool    occurred;
} nanojson_error;

/* ------------------------------------------------------------------ */
/* Parsing                                                              */
/* ------------------------------------------------------------------ */

/* Parse a null-terminated JSON string. On failure, returns NULL and
 * (if `error_out` is non-NULL) populates it with diagnostic info. */
nanojson_element *nanojson_parse  (const char *json, nanojson_error *error_out);

/* Same as `nanojson_parse`, but accepts an explicit byte length so the
 * input does not need to be null-terminated. */
nanojson_element *nanojson_parse_n(const char *json, size_t length, nanojson_error *error_out);

/* ------------------------------------------------------------------ */
/* Cleanup                                                              */
/* ------------------------------------------------------------------ */

/* Recursively free an element and all its descendants.
 * Safe to call with NULL. */
void  nanojson_free       (nanojson_element *element);

/* Free a string returned by `nanojson_serialize` / `nanojson_serialize_with`.
 * Safe to call with NULL. */
void  nanojson_string_free(char *str);

/* ------------------------------------------------------------------ */
/* Constructors                                                         */
/* ------------------------------------------------------------------ */

nanojson_element *nanojson_new_null    (void);
nanojson_element *nanojson_new_bool    (bool value);
nanojson_element *nanojson_new_number  (double value);
nanojson_element *nanojson_new_string  (const char *value);
nanojson_element *nanojson_new_string_n(const char *value, size_t length);
nanojson_element *nanojson_new_array   (void);
nanojson_element *nanojson_new_object  (void);

/* ------------------------------------------------------------------ */
/* Container manipulation                                               */
/* ------------------------------------------------------------------ */

/* Appends `child` to the end of `array`. `child` must not already have a
 * parent. Returns true on success. On failure (wrong types / OOM /
 * already-parented child), `child` is left untouched. */
bool              nanojson_array_append(nanojson_element *array,
                                        nanojson_element *child);

/* Sets `key` -> `child` on `object`, replacing any existing entry with
 * the same key. `child` must not already have a parent. Returns true on
 * success. */
bool              nanojson_object_set  (nanojson_element *object,
                                        const char *key,
                                        nanojson_element *child);

/* Look up a value by key. Returns NULL when not found. */
nanojson_element *nanojson_object_get  (const nanojson_element *object,
                                        const char *key);

/* O(index) array access. Returns NULL on out-of-bounds. */
nanojson_element *nanojson_array_at    (const nanojson_element *array,
                                        size_t index);

/* Number of children for an array/object; 0 otherwise. */
size_t            nanojson_count       (const nanojson_element *container);

/* Unlink `element` from its parent without freeing it. After this call
 * the element has no parent and can be inserted into another container.
 * Safe to call on root elements (no-op). */
void              nanojson_detach      (nanojson_element *element);

/* ------------------------------------------------------------------ */
/* Serialization                                                        */
/* ------------------------------------------------------------------ */

/* Serialize an element tree to a heap-allocated null-terminated UTF-8
 * string. Returns NULL on allocation failure. The caller is responsible
 * for releasing the returned string with `nanojson_string_free`.
 *
 * `pretty` selects pretty-printing (indented, newlines) vs. minified. */
char *nanojson_serialize     (const nanojson_element *element, bool pretty);

/* Same as `nanojson_serialize` but lets you choose the indent width
 * (number of spaces per level) when pretty-printing. */
char *nanojson_serialize_with(const nanojson_element *element,
                              bool pretty,
                              size_t indent_spaces);

#ifdef __cplusplus
}
#endif

#endif /* NANOJSON_H_INCLUDED */
