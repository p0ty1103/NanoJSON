/*
 * NanoJSON - C99 JSON parser & serializer
 *
 * Implementation file. Compile and link this against any translation
 * unit that includes "nanojson.h".
 *
 * Repository: https://github.com/p0ty1103/nanojson
 * License:    MIT (see LICENSE)
 */

#include "nanojson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifndef NANOJSON_MAX_DEPTH
#define NANOJSON_MAX_DEPTH 1024
#endif

/* ------------------------------------------------------------------ */
/* Internal: element allocation / lifecycle                            */
/* ------------------------------------------------------------------ */

static nanojson_element *nanojson__alloc_element(nanojson_type type) {
    nanojson_element *e = (nanojson_element *)malloc(sizeof(nanojson_element));
    if (e == NULL) {
        return NULL;
    }
    e->type          = type;
    e->key           = NULL;
    e->key_length    = 0;
    e->boolean       = false;
    e->number        = 0.0;
    e->string        = NULL;
    e->string_length = 0;
    e->first_child   = NULL;
    e->last_child    = NULL;
    e->child_count   = 0;
    e->prev          = NULL;
    e->next          = NULL;
    e->parent        = NULL;
    return e;
}

static char *nanojson__strdup_n(const char *src, size_t length) {
    char *dst = (char *)malloc(length + 1);
    if (dst == NULL) {
        return NULL;
    }
    if (length > 0 && src != NULL) {
        memcpy(dst, src, length);
    }
    dst[length] = '\0';
    return dst;
}

void nanojson_free(nanojson_element *element) {
    if (element == NULL) {
        return;
    }
    /* Depth-first iterative free using a manual stack, to avoid C stack
     * overflow on deeply nested structures. On allocation failure of the
     * stack itself, fall back to recursion. */
    {
        size_t cap   = 32;
        size_t depth = 0;
        nanojson_element **stack =
            (nanojson_element **)malloc(sizeof(nanojson_element *) * cap);
        if (stack == NULL) {
            /* Recursive fallback - acceptable for normal data sizes. */
            nanojson_element *c = element->first_child;
            while (c != NULL) {
                nanojson_element *n = c->next;
                nanojson_free(c);
                c = n;
            }
            if (element->key)    free(element->key);
            if (element->string) free(element->string);
            free(element);
            return;
        }

        stack[depth++] = element;
        while (depth > 0) {
            nanojson_element *top = stack[depth - 1];
            if (top->first_child != NULL) {
                nanojson_element *child = top->first_child;
                /* Detach the first child from its parent. */
                top->first_child = child->next;
                if (top->first_child != NULL) {
                    top->first_child->prev = NULL;
                } else {
                    top->last_child = NULL;
                }
                child->parent = NULL;
                child->next   = NULL;
                child->prev   = NULL;

                if (depth == cap) {
                    size_t new_cap = cap * 2;
                    nanojson_element **new_stack =
                        (nanojson_element **)realloc(stack,
                            sizeof(nanojson_element *) * new_cap);
                    if (new_stack == NULL) {
                        /* Could not grow stack: recursively free remaining
                         * subtree and continue. */
                        nanojson_free(child);
                        continue;
                    }
                    stack = new_stack;
                    cap   = new_cap;
                }
                stack[depth++] = child;
            } else {
                /* Leaf: release this element. */
                depth--;
                if (top->key)    free(top->key);
                if (top->string) free(top->string);
                free(top);
            }
        }
        free(stack);
    }
}

void nanojson_string_free(char *str) {
    if (str != NULL) {
        free(str);
    }
}

/* ------------------------------------------------------------------ */
/* Internal: container helpers                                         */
/* ------------------------------------------------------------------ */

static void nanojson__attach_child(nanojson_element *parent,
                                   nanojson_element *child) {
    child->parent = parent;
    child->prev   = parent->last_child;
    child->next   = NULL;
    if (parent->last_child != NULL) {
        parent->last_child->next = child;
    } else {
        parent->first_child = child;
    }
    parent->last_child = child;
    parent->child_count++;
}

void nanojson_detach(nanojson_element *element) {
    if (element == NULL || element->parent == NULL) {
        return;
    }
    nanojson_element *p = element->parent;
    if (element->prev != NULL) {
        element->prev->next = element->next;
    } else {
        p->first_child = element->next;
    }
    if (element->next != NULL) {
        element->next->prev = element->prev;
    } else {
        p->last_child = element->prev;
    }
    p->child_count--;
    element->parent = NULL;
    element->prev   = NULL;
    element->next   = NULL;
}

bool nanojson_array_append(nanojson_element *array, nanojson_element *child) {
    if (array == NULL || child == NULL || array->type != NANOJSON_ARRAY) {
        return false;
    }
    if (child->parent != NULL) {
        return false;
    }
    /* Array children don't use keys. */
    if (child->key != NULL) {
        free(child->key);
        child->key = NULL;
        child->key_length = 0;
    }
    nanojson__attach_child(array, child);
    return true;
}

bool nanojson_object_set(nanojson_element *object,
                         const char *key,
                         nanojson_element *child) {
    if (object == NULL || key == NULL || child == NULL ||
        object->type != NANOJSON_OBJECT) {
        return false;
    }
    if (child->parent != NULL) {
        return false;
    }
    size_t key_len = strlen(key);
    char *key_copy = nanojson__strdup_n(key, key_len);
    if (key_copy == NULL) {
        return false;
    }

    /* Replace an existing key if present. */
    nanojson_element *existing = object->first_child;
    while (existing != NULL) {
        if (existing->key != NULL &&
            existing->key_length == key_len &&
            memcmp(existing->key, key, key_len) == 0) {
            /* Replace in place: unlink existing and link new in its slot. */
            child->parent = object;
            child->prev   = existing->prev;
            child->next   = existing->next;
            if (existing->prev != NULL) {
                existing->prev->next = child;
            } else {
                object->first_child = child;
            }
            if (existing->next != NULL) {
                existing->next->prev = child;
            } else {
                object->last_child = child;
            }
            if (child->key) {
                free(child->key);
            }
            child->key        = key_copy;
            child->key_length = key_len;

            existing->parent = NULL;
            existing->prev   = NULL;
            existing->next   = NULL;
            nanojson_free(existing);
            return true;
        }
        existing = existing->next;
    }

    /* Append new pair. */
    if (child->key) {
        free(child->key);
    }
    child->key        = key_copy;
    child->key_length = key_len;
    nanojson__attach_child(object, child);
    return true;
}

nanojson_element *nanojson_object_get(const nanojson_element *object,
                                      const char *key) {
    if (object == NULL || key == NULL || object->type != NANOJSON_OBJECT) {
        return NULL;
    }
    size_t key_len = strlen(key);
    nanojson_element *child = object->first_child;
    while (child != NULL) {
        if (child->key != NULL &&
            child->key_length == key_len &&
            memcmp(child->key, key, key_len) == 0) {
            return child;
        }
        child = child->next;
    }
    return NULL;
}

nanojson_element *nanojson_array_at(const nanojson_element *array, size_t index) {
    if (array == NULL || array->type != NANOJSON_ARRAY) {
        return NULL;
    }
    if (index >= array->child_count) {
        return NULL;
    }
    nanojson_element *child = array->first_child;
    size_t i = 0;
    while (child != NULL && i < index) {
        child = child->next;
        i++;
    }
    return child;
}

size_t nanojson_count(const nanojson_element *container) {
    if (container == NULL) {
        return 0;
    }
    if (container->type != NANOJSON_ARRAY &&
        container->type != NANOJSON_OBJECT) {
        return 0;
    }
    return container->child_count;
}

/* ------------------------------------------------------------------ */
/* Constructors                                                        */
/* ------------------------------------------------------------------ */

nanojson_element *nanojson_new_null(void) {
    return nanojson__alloc_element(NANOJSON_NULL);
}

nanojson_element *nanojson_new_bool(bool value) {
    nanojson_element *e = nanojson__alloc_element(NANOJSON_BOOL);
    if (e != NULL) {
        e->boolean = value;
    }
    return e;
}

nanojson_element *nanojson_new_number(double value) {
    nanojson_element *e = nanojson__alloc_element(NANOJSON_NUMBER);
    if (e != NULL) {
        e->number = value;
    }
    return e;
}

nanojson_element *nanojson_new_string_n(const char *value, size_t length) {
    nanojson_element *e = nanojson__alloc_element(NANOJSON_STRING);
    if (e == NULL) {
        return NULL;
    }
    e->string = nanojson__strdup_n(value, length);
    if (e->string == NULL) {
        free(e);
        return NULL;
    }
    e->string_length = length;
    return e;
}

nanojson_element *nanojson_new_string(const char *value) {
    if (value == NULL) {
        return nanojson_new_string_n("", 0);
    }
    return nanojson_new_string_n(value, strlen(value));
}

nanojson_element *nanojson_new_array(void) {
    return nanojson__alloc_element(NANOJSON_ARRAY);
}

nanojson_element *nanojson_new_object(void) {
    return nanojson__alloc_element(NANOJSON_OBJECT);
}

/* ------------------------------------------------------------------ */
/* Internal: parser                                                    */
/* ------------------------------------------------------------------ */

typedef struct nanojson__parser {
    const char     *src;
    size_t          pos;
    size_t          len;
    size_t          line;
    size_t          column;
    size_t          depth;
    nanojson_error *err;
} nanojson__parser;

static void nanojson__set_error(nanojson__parser *p, const char *msg) {
    if (p->err == NULL || p->err->occurred) {
        return;
    }
    size_t i = 0;
    while (msg[i] != '\0' && i + 1 < sizeof(p->err->message)) {
        p->err->message[i] = msg[i];
        i++;
    }
    p->err->message[i] = '\0';
    p->err->line     = p->line;
    p->err->column   = p->column;
    p->err->offset   = p->pos;
    p->err->occurred = true;
}

static int nanojson__peek(const nanojson__parser *p) {
    if (p->pos >= p->len) {
        return -1;
    }
    return (int)(unsigned char)p->src[p->pos];
}

static int nanojson__peek_at(const nanojson__parser *p, size_t offset) {
    if (p->pos + offset >= p->len) {
        return -1;
    }
    return (int)(unsigned char)p->src[p->pos + offset];
}

static int nanojson__advance(nanojson__parser *p) {
    if (p->pos >= p->len) {
        return -1;
    }
    int c = (int)(unsigned char)p->src[p->pos];
    p->pos++;
    if (c == '\n') {
        p->line++;
        p->column = 1;
    } else {
        p->column++;
    }
    return c;
}

static void nanojson__skip_whitespace(nanojson__parser *p) {
    while (p->pos < p->len) {
        char c = p->src[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            nanojson__advance(p);
        } else {
            break;
        }
    }
}

/* Forward declarations of recursive descent helpers. */
static nanojson_element *nanojson__parse_value (nanojson__parser *p);
static nanojson_element *nanojson__parse_object(nanojson__parser *p);
static nanojson_element *nanojson__parse_array (nanojson__parser *p);
static char             *nanojson__parse_raw_string(nanojson__parser *p, size_t *out_length);
static nanojson_element *nanojson__parse_string_element(nanojson__parser *p);
static nanojson_element *nanojson__parse_number(nanojson__parser *p);
static nanojson_element *nanojson__parse_keyword(nanojson__parser *p);

/* ---- String parsing ---- */

static int nanojson__hex_digit(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool nanojson__append_byte(char **buf,
                                  size_t *length,
                                  size_t *capacity,
                                  unsigned char byte) {
    if (*length + 2 > *capacity) {
        size_t new_cap = (*capacity == 0) ? 16 : (*capacity * 2);
        while (*length + 2 > new_cap) {
            size_t doubled = new_cap * 2;
            if (doubled < new_cap) { /* overflow */
                return false;
            }
            new_cap = doubled;
        }
        char *new_buf = (char *)realloc(*buf, new_cap);
        if (new_buf == NULL) {
            return false;
        }
        *buf      = new_buf;
        *capacity = new_cap;
    }
    (*buf)[(*length)++] = (char)byte;
    (*buf)[*length]     = '\0';
    return true;
}

static bool nanojson__encode_utf8(unsigned int cp,
                                  char **buf,
                                  size_t *length,
                                  size_t *capacity) {
    if (cp < 0x80U) {
        return nanojson__append_byte(buf, length, capacity, (unsigned char)cp);
    } else if (cp < 0x800U) {
        return nanojson__append_byte(buf, length, capacity,
                                     (unsigned char)(0xC0U | (cp >> 6)))
            && nanojson__append_byte(buf, length, capacity,
                                     (unsigned char)(0x80U | (cp & 0x3FU)));
    } else if (cp < 0x10000U) {
        return nanojson__append_byte(buf, length, capacity,
                                     (unsigned char)(0xE0U | (cp >> 12)))
            && nanojson__append_byte(buf, length, capacity,
                                     (unsigned char)(0x80U | ((cp >> 6) & 0x3FU)))
            && nanojson__append_byte(buf, length, capacity,
                                     (unsigned char)(0x80U | (cp & 0x3FU)));
    } else if (cp <= 0x10FFFFU) {
        return nanojson__append_byte(buf, length, capacity,
                                     (unsigned char)(0xF0U | (cp >> 18)))
            && nanojson__append_byte(buf, length, capacity,
                                     (unsigned char)(0x80U | ((cp >> 12) & 0x3FU)))
            && nanojson__append_byte(buf, length, capacity,
                                     (unsigned char)(0x80U | ((cp >> 6) & 0x3FU)))
            && nanojson__append_byte(buf, length, capacity,
                                     (unsigned char)(0x80U | (cp & 0x3FU)));
    }
    return false;
}

static bool nanojson__read_hex4(nanojson__parser *p, unsigned int *out_cp) {
    unsigned int cp = 0;
    for (int i = 0; i < 4; i++) {
        int c = nanojson__peek(p);
        if (c == -1) {
            nanojson__set_error(p, "incomplete \\u escape");
            return false;
        }
        int v = nanojson__hex_digit(c);
        if (v < 0) {
            nanojson__set_error(p, "invalid hex digit in \\u escape");
            return false;
        }
        cp = (cp << 4) | (unsigned int)v;
        nanojson__advance(p);
    }
    *out_cp = cp;
    return true;
}

static char *nanojson__parse_raw_string(nanojson__parser *p, size_t *out_length) {
    if (nanojson__peek(p) != '"') {
        nanojson__set_error(p, "expected '\"' at start of string");
        return NULL;
    }
    nanojson__advance(p); /* consume opening quote */

    char  *buf      = NULL;
    size_t length   = 0;
    size_t capacity = 0;

    /* Ensure non-NULL buffer (so callers can free unconditionally) by
     * allocating an initial single byte. */
    if (!nanojson__append_byte(&buf, &length, &capacity, 0)) {
        nanojson__set_error(p, "out of memory");
        return NULL;
    }
    /* Undo the placeholder byte. */
    length = 0;
    buf[0] = '\0';

    while (1) {
        int c = nanojson__peek(p);
        if (c == -1) {
            nanojson__set_error(p, "unterminated string");
            free(buf);
            return NULL;
        }
        if (c == '"') {
            nanojson__advance(p);
            if (out_length) *out_length = length;
            return buf;
        }
        if ((unsigned int)c < 0x20U) {
            nanojson__set_error(p, "invalid control character in string");
            free(buf);
            return NULL;
        }
        if (c == '\\') {
            nanojson__advance(p);
            int esc = nanojson__peek(p);
            if (esc == -1) {
                nanojson__set_error(p, "unterminated escape sequence");
                free(buf);
                return NULL;
            }
            nanojson__advance(p);
            switch (esc) {
                case '"':
                    if (!nanojson__append_byte(&buf, &length, &capacity, '"')) goto oom;
                    break;
                case '\\':
                    if (!nanojson__append_byte(&buf, &length, &capacity, '\\')) goto oom;
                    break;
                case '/':
                    if (!nanojson__append_byte(&buf, &length, &capacity, '/')) goto oom;
                    break;
                case 'b':
                    if (!nanojson__append_byte(&buf, &length, &capacity, '\b')) goto oom;
                    break;
                case 'f':
                    if (!nanojson__append_byte(&buf, &length, &capacity, '\f')) goto oom;
                    break;
                case 'n':
                    if (!nanojson__append_byte(&buf, &length, &capacity, '\n')) goto oom;
                    break;
                case 'r':
                    if (!nanojson__append_byte(&buf, &length, &capacity, '\r')) goto oom;
                    break;
                case 't':
                    if (!nanojson__append_byte(&buf, &length, &capacity, '\t')) goto oom;
                    break;
                case 'u': {
                    unsigned int cp = 0;
                    if (!nanojson__read_hex4(p, &cp)) {
                        free(buf);
                        return NULL;
                    }
                    if (cp >= 0xD800U && cp <= 0xDBFFU) {
                        /* High surrogate - require following \uXXXX low surrogate. */
                        if (nanojson__peek(p) != '\\' ||
                            nanojson__peek_at(p, 1) != 'u') {
                            nanojson__set_error(p, "missing low surrogate after high surrogate");
                            free(buf);
                            return NULL;
                        }
                        nanojson__advance(p); /* '\\' */
                        nanojson__advance(p); /* 'u'  */
                        unsigned int low = 0;
                        if (!nanojson__read_hex4(p, &low)) {
                            free(buf);
                            return NULL;
                        }
                        if (low < 0xDC00U || low > 0xDFFFU) {
                            nanojson__set_error(p, "invalid low surrogate value");
                            free(buf);
                            return NULL;
                        }
                        cp = 0x10000U +
                             ((cp - 0xD800U) << 10) +
                             (low - 0xDC00U);
                    } else if (cp >= 0xDC00U && cp <= 0xDFFFU) {
                        nanojson__set_error(p, "unexpected low surrogate");
                        free(buf);
                        return NULL;
                    }
                    if (!nanojson__encode_utf8(cp, &buf, &length, &capacity)) {
                        goto oom;
                    }
                    break;
                }
                default:
                    nanojson__set_error(p, "invalid escape character");
                    free(buf);
                    return NULL;
            }
        } else {
            nanojson__advance(p);
            if (!nanojson__append_byte(&buf, &length, &capacity, (unsigned char)c)) {
                goto oom;
            }
        }
    }

oom:
    nanojson__set_error(p, "out of memory");
    free(buf);
    return NULL;
}

static nanojson_element *nanojson__parse_string_element(nanojson__parser *p) {
    size_t length = 0;
    char *s = nanojson__parse_raw_string(p, &length);
    if (s == NULL) {
        return NULL;
    }
    nanojson_element *e = nanojson__alloc_element(NANOJSON_STRING);
    if (e == NULL) {
        free(s);
        nanojson__set_error(p, "out of memory");
        return NULL;
    }
    e->string        = s;
    e->string_length = length;
    return e;
}

/* ---- Number parsing (locale-independent) ---- */

static nanojson_element *nanojson__parse_number(nanojson__parser *p) {
    size_t start = p->pos;
    double sign  = 1.0;
    int    c     = nanojson__peek(p);

    if (c == '-') {
        sign = -1.0;
        nanojson__advance(p);
        c = nanojson__peek(p);
    }

    /* Integer part */
    if (c == '0') {
        nanojson__advance(p);
    } else if (c >= '1' && c <= '9') {
        while (1) {
            int d = nanojson__peek(p);
            if (d < '0' || d > '9') break;
            nanojson__advance(p);
        }
    } else {
        nanojson__set_error(p, "invalid number: expected digit");
        return NULL;
    }

    bool has_fraction = false;
    bool has_exponent = false;
    /* Fraction */
    if (nanojson__peek(p) == '.') {
        has_fraction = true;
        nanojson__advance(p);
        c = nanojson__peek(p);
        if (c < '0' || c > '9') {
            nanojson__set_error(p, "invalid number: expected digit after '.'");
            return NULL;
        }
        while (1) {
            int d = nanojson__peek(p);
            if (d < '0' || d > '9') break;
            nanojson__advance(p);
        }
    }

    /* Exponent */
    c = nanojson__peek(p);
    if (c == 'e' || c == 'E') {
        has_exponent = true;
        nanojson__advance(p);
        c = nanojson__peek(p);
        if (c == '+' || c == '-') {
            nanojson__advance(p);
        }
        c = nanojson__peek(p);
        if (c < '0' || c > '9') {
            nanojson__set_error(p, "invalid number: expected digit in exponent");
            return NULL;
        }
        while (1) {
            int d = nanojson__peek(p);
            if (d < '0' || d > '9') break;
            nanojson__advance(p);
        }
    }

    /* Convert the scanned slice [start, p->pos) to a double without
     * relying on strtod's locale-sensitive decimal point. */
    size_t      idx = start;
    size_t      end = p->pos;
    double      integer_part  = 0.0;
    double      fraction_part = 0.0;
    int         frac_digits   = 0;
    int         exp_sign      = 1;
    long        exp_value     = 0;

    if (p->src[idx] == '-') {
        idx++;
    } else if (p->src[idx] == '+') {
        /* JSON does not allow leading '+', but be defensive. */
        idx++;
    }

    while (idx < end && p->src[idx] >= '0' && p->src[idx] <= '9') {
        integer_part = integer_part * 10.0 + (double)(p->src[idx] - '0');
        idx++;
    }

    if (has_fraction && idx < end && p->src[idx] == '.') {
        idx++;
        while (idx < end && p->src[idx] >= '0' && p->src[idx] <= '9') {
            fraction_part = fraction_part * 10.0 + (double)(p->src[idx] - '0');
            frac_digits++;
            idx++;
        }
    }

    if (has_exponent && idx < end && (p->src[idx] == 'e' || p->src[idx] == 'E')) {
        idx++;
        if (idx < end && p->src[idx] == '+') {
            idx++;
        } else if (idx < end && p->src[idx] == '-') {
            exp_sign = -1;
            idx++;
        }
        while (idx < end && p->src[idx] >= '0' && p->src[idx] <= '9') {
            exp_value = exp_value * 10 + (long)(p->src[idx] - '0');
            if (exp_value > 100000L) {
                /* Clamp to a huge value; the result will be inf/0 below. */
                exp_value = 100000L;
            }
            idx++;
        }
    }

    /* Combine fraction. */
    double frac_scale = 1.0;
    for (int i = 0; i < frac_digits; i++) {
        frac_scale *= 10.0;
    }
    double value = integer_part + (frac_digits > 0 ? fraction_part / frac_scale : 0.0);

    if (has_exponent && exp_value != 0) {
        /* Apply exponent via fast exponentiation by squaring. */
        double scale = 1.0;
        long   n     = exp_value;
        double base  = 10.0;
        while (n > 0) {
            if (n & 1L) {
                scale *= base;
            }
            base *= base;
            n >>= 1;
        }
        if (exp_sign > 0) {
            value *= scale;
        } else {
            value /= scale;
        }
    }

    value *= sign;

    nanojson_element *e = nanojson__alloc_element(NANOJSON_NUMBER);
    if (e == NULL) {
        nanojson__set_error(p, "out of memory");
        return NULL;
    }
    e->number = value;
    return e;
}

/* ---- Keyword parsing (true/false/null) ---- */

static bool nanojson__match_literal(nanojson__parser *p, const char *literal) {
    size_t lit_len = strlen(literal);
    if (p->pos + lit_len > p->len) {
        return false;
    }
    for (size_t i = 0; i < lit_len; i++) {
        if (p->src[p->pos + i] != literal[i]) {
            return false;
        }
    }
    for (size_t i = 0; i < lit_len; i++) {
        nanojson__advance(p);
    }
    return true;
}

static nanojson_element *nanojson__parse_keyword(nanojson__parser *p) {
    int c = nanojson__peek(p);
    if (c == 't') {
        if (!nanojson__match_literal(p, "true")) {
            nanojson__set_error(p, "invalid token, expected 'true'");
            return NULL;
        }
        return nanojson_new_bool(true);
    }
    if (c == 'f') {
        if (!nanojson__match_literal(p, "false")) {
            nanojson__set_error(p, "invalid token, expected 'false'");
            return NULL;
        }
        return nanojson_new_bool(false);
    }
    if (c == 'n') {
        if (!nanojson__match_literal(p, "null")) {
            nanojson__set_error(p, "invalid token, expected 'null'");
            return NULL;
        }
        return nanojson_new_null();
    }
    nanojson__set_error(p, "unexpected character");
    return NULL;
}

/* ---- Array / Object parsing ---- */

static nanojson_element *nanojson__parse_array(nanojson__parser *p) {
    if (nanojson__peek(p) != '[') {
        nanojson__set_error(p, "expected '['");
        return NULL;
    }
    nanojson__advance(p);

    nanojson_element *array = nanojson_new_array();
    if (array == NULL) {
        nanojson__set_error(p, "out of memory");
        return NULL;
    }

    nanojson__skip_whitespace(p);
    if (nanojson__peek(p) == ']') {
        nanojson__advance(p);
        return array;
    }

    while (1) {
        nanojson__skip_whitespace(p);
        nanojson_element *value = nanojson__parse_value(p);
        if (value == NULL) {
            nanojson_free(array);
            return NULL;
        }
        nanojson__attach_child(array, value);

        nanojson__skip_whitespace(p);
        int c = nanojson__peek(p);
        if (c == ',') {
            nanojson__advance(p);
            continue;
        }
        if (c == ']') {
            nanojson__advance(p);
            return array;
        }
        nanojson__set_error(p, "expected ',' or ']' in array");
        nanojson_free(array);
        return NULL;
    }
}

static nanojson_element *nanojson__parse_object(nanojson__parser *p) {
    if (nanojson__peek(p) != '{') {
        nanojson__set_error(p, "expected '{'");
        return NULL;
    }
    nanojson__advance(p);

    nanojson_element *object = nanojson_new_object();
    if (object == NULL) {
        nanojson__set_error(p, "out of memory");
        return NULL;
    }

    nanojson__skip_whitespace(p);
    if (nanojson__peek(p) == '}') {
        nanojson__advance(p);
        return object;
    }

    while (1) {
        nanojson__skip_whitespace(p);
        if (nanojson__peek(p) != '"') {
            nanojson__set_error(p, "expected string key in object");
            nanojson_free(object);
            return NULL;
        }
        size_t key_len = 0;
        char *key = nanojson__parse_raw_string(p, &key_len);
        if (key == NULL) {
            nanojson_free(object);
            return NULL;
        }
        nanojson__skip_whitespace(p);
        if (nanojson__peek(p) != ':') {
            nanojson__set_error(p, "expected ':' after object key");
            free(key);
            nanojson_free(object);
            return NULL;
        }
        nanojson__advance(p);
        nanojson__skip_whitespace(p);

        nanojson_element *value = nanojson__parse_value(p);
        if (value == NULL) {
            free(key);
            nanojson_free(object);
            return NULL;
        }
        value->key        = key;
        value->key_length = key_len;
        nanojson__attach_child(object, value);

        nanojson__skip_whitespace(p);
        int c = nanojson__peek(p);
        if (c == ',') {
            nanojson__advance(p);
            continue;
        }
        if (c == '}') {
            nanojson__advance(p);
            return object;
        }
        nanojson__set_error(p, "expected ',' or '}' in object");
        nanojson_free(object);
        return NULL;
    }
}

/* ---- Value dispatch ---- */

static nanojson_element *nanojson__parse_value(nanojson__parser *p) {
    if (p->depth >= NANOJSON_MAX_DEPTH) {
        nanojson__set_error(p, "maximum nesting depth exceeded");
        return NULL;
    }
    p->depth++;

    nanojson__skip_whitespace(p);
    int c = nanojson__peek(p);
    nanojson_element *result = NULL;

    if (c == -1) {
        nanojson__set_error(p, "unexpected end of input");
    } else if (c == '"') {
        result = nanojson__parse_string_element(p);
    } else if (c == '{') {
        result = nanojson__parse_object(p);
    } else if (c == '[') {
        result = nanojson__parse_array(p);
    } else if (c == 't' || c == 'f' || c == 'n') {
        result = nanojson__parse_keyword(p);
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        result = nanojson__parse_number(p);
    } else {
        nanojson__set_error(p, "unexpected character");
    }

    p->depth--;
    return result;
}

/* ---- Entry points ---- */

nanojson_element *nanojson_parse_n(const char *json,
                                   size_t length,
                                   nanojson_error *error_out) {
    nanojson_error scratch;
    if (error_out == NULL) {
        error_out = &scratch;
    }
    error_out->message[0] = '\0';
    error_out->line       = 1;
    error_out->column     = 1;
    error_out->offset     = 0;
    error_out->occurred   = false;

    if (json == NULL) {
        size_t i = 0;
        const char *msg = "input is NULL";
        while (msg[i] != '\0' && i + 1 < sizeof(error_out->message)) {
            error_out->message[i] = msg[i];
            i++;
        }
        error_out->message[i] = '\0';
        error_out->occurred = true;
        return NULL;
    }

    nanojson__parser p;
    p.src     = json;
    p.pos     = 0;
    p.len     = length;
    p.line    = 1;
    p.column  = 1;
    p.depth   = 0;
    p.err     = error_out;

    nanojson__skip_whitespace(&p);
    nanojson_element *root = nanojson__parse_value(&p);
    if (root == NULL) {
        if (!error_out->occurred) {
            nanojson__set_error(&p, "parse failed");
        }
        return NULL;
    }
    nanojson__skip_whitespace(&p);
    if (p.pos != p.len) {
        nanojson__set_error(&p, "trailing data after JSON value");
        nanojson_free(root);
        return NULL;
    }
    return root;
}

nanojson_element *nanojson_parse(const char *json, nanojson_error *error_out) {
    if (json == NULL) {
        return nanojson_parse_n(NULL, 0, error_out);
    }
    return nanojson_parse_n(json, strlen(json), error_out);
}

/* ------------------------------------------------------------------ */
/* Internal: serializer                                                */
/* ------------------------------------------------------------------ */

typedef struct nanojson__buffer {
    char  *data;
    size_t size;
    size_t capacity;
    bool   failed;
} nanojson__buffer;

static bool nanojson__buf_init(nanojson__buffer *b) {
    b->capacity = 64;
    b->size     = 0;
    b->failed   = false;
    b->data     = (char *)malloc(b->capacity);
    if (b->data == NULL) {
        b->failed = true;
        return false;
    }
    b->data[0] = '\0';
    return true;
}

static bool nanojson__buf_reserve(nanojson__buffer *b, size_t extra) {
    if (b->failed) {
        return false;
    }
    if (b->size + extra + 1 <= b->capacity) {
        return true;
    }
    size_t new_cap = (b->capacity == 0) ? 64 : b->capacity;
    while (b->size + extra + 1 > new_cap) {
        size_t doubled = new_cap * 2;
        if (doubled < new_cap) { /* overflow */
            b->failed = true;
            return false;
        }
        new_cap = doubled;
    }
    char *new_data = (char *)realloc(b->data, new_cap);
    if (new_data == NULL) {
        b->failed = true;
        return false;
    }
    b->data     = new_data;
    b->capacity = new_cap;
    return true;
}

static bool nanojson__buf_append_n(nanojson__buffer *b, const char *s, size_t n) {
    if (!nanojson__buf_reserve(b, n)) {
        return false;
    }
    if (n > 0) {
        memcpy(b->data + b->size, s, n);
        b->size += n;
        b->data[b->size] = '\0';
    }
    return true;
}

static bool nanojson__buf_append_c(nanojson__buffer *b, char c) {
    if (!nanojson__buf_reserve(b, 1)) {
        return false;
    }
    b->data[b->size++] = c;
    b->data[b->size]   = '\0';
    return true;
}

static bool nanojson__buf_append_indent(nanojson__buffer *b,
                                        size_t depth,
                                        size_t indent_spaces) {
    if (!nanojson__buf_append_c(b, '\n')) {
        return false;
    }
    size_t total = depth * indent_spaces;
    if (!nanojson__buf_reserve(b, total)) {
        return false;
    }
    for (size_t i = 0; i < total; i++) {
        b->data[b->size++] = ' ';
    }
    b->data[b->size] = '\0';
    return true;
}

static bool nanojson__buf_append_escaped_string(nanojson__buffer *b,
                                                const char *s,
                                                size_t n) {
    if (!nanojson__buf_append_c(b, '"')) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"':
                if (!nanojson__buf_append_n(b, "\\\"", 2)) return false;
                break;
            case '\\':
                if (!nanojson__buf_append_n(b, "\\\\", 2)) return false;
                break;
            case '\b':
                if (!nanojson__buf_append_n(b, "\\b", 2)) return false;
                break;
            case '\f':
                if (!nanojson__buf_append_n(b, "\\f", 2)) return false;
                break;
            case '\n':
                if (!nanojson__buf_append_n(b, "\\n", 2)) return false;
                break;
            case '\r':
                if (!nanojson__buf_append_n(b, "\\r", 2)) return false;
                break;
            case '\t':
                if (!nanojson__buf_append_n(b, "\\t", 2)) return false;
                break;
            default:
                if (c < 0x20U) {
                    char tmp[8];
                    int written = snprintf(tmp, sizeof(tmp), "\\u%04x", (unsigned int)c);
                    if (written < 0 || written >= (int)sizeof(tmp)) {
                        return false;
                    }
                    if (!nanojson__buf_append_n(b, tmp, (size_t)written)) return false;
                } else {
                    if (!nanojson__buf_append_c(b, (char)c)) return false;
                }
                break;
        }
    }
    if (!nanojson__buf_append_c(b, '"')) {
        return false;
    }
    return true;
}

static bool nanojson__buf_append_number(nanojson__buffer *b, double n) {
    char tmp[64];
    /* NaN / Infinity are not representable in JSON - emit "null" as a safe fallback. */
    if (n != n) {
        return nanojson__buf_append_n(b, "null", 4);
    }
    if (n > 1.7976931348623157e308 || n < -1.7976931348623157e308) {
        return nanojson__buf_append_n(b, "null", 4);
    }
    /* If the number is an exact integer in a safe range, print it as an integer. */
    if (n >= -9.2233720368547758e18 && n <= 9.2233720368547758e18) {
        double truncated = (double)(long long)n;
        if (truncated == n) {
            int written = snprintf(tmp, sizeof(tmp), "%lld", (long long)n);
            if (written < 0 || written >= (int)sizeof(tmp)) {
                return false;
            }
            return nanojson__buf_append_n(b, tmp, (size_t)written);
        }
    }
    int written = snprintf(tmp, sizeof(tmp), "%.17g", n);
    if (written < 0 || written >= (int)sizeof(tmp)) {
        return false;
    }
    /* Normalize the decimal separator to '.' if the current locale used ','. */
    for (int i = 0; i < written; i++) {
        if (tmp[i] == ',') {
            tmp[i] = '.';
        }
    }
    return nanojson__buf_append_n(b, tmp, (size_t)written);
}

static bool nanojson__serialize(nanojson__buffer *b,
                                const nanojson_element *e,
                                bool pretty,
                                size_t indent_spaces,
                                size_t depth) {
    if (e == NULL) {
        return nanojson__buf_append_n(b, "null", 4);
    }

    switch (e->type) {
        case NANOJSON_NULL:
            return nanojson__buf_append_n(b, "null", 4);

        case NANOJSON_BOOL:
            return e->boolean
                 ? nanojson__buf_append_n(b, "true",  4)
                 : nanojson__buf_append_n(b, "false", 5);

        case NANOJSON_NUMBER:
            return nanojson__buf_append_number(b, e->number);

        case NANOJSON_STRING:
            if (e->string == NULL) {
                return nanojson__buf_append_n(b, "\"\"", 2);
            }
            return nanojson__buf_append_escaped_string(b, e->string, e->string_length);

        case NANOJSON_ARRAY: {
            if (!nanojson__buf_append_c(b, '[')) return false;
            if (e->first_child == NULL) {
                return nanojson__buf_append_c(b, ']');
            }
            const nanojson_element *child = e->first_child;
            bool first = true;
            while (child != NULL) {
                if (!first) {
                    if (!nanojson__buf_append_c(b, ',')) return false;
                }
                if (pretty) {
                    if (!nanojson__buf_append_indent(b, depth + 1, indent_spaces)) return false;
                }
                if (!nanojson__serialize(b, child, pretty, indent_spaces, depth + 1)) return false;
                first = false;
                child = child->next;
            }
            if (pretty) {
                if (!nanojson__buf_append_indent(b, depth, indent_spaces)) return false;
            }
            return nanojson__buf_append_c(b, ']');
        }

        case NANOJSON_OBJECT: {
            if (!nanojson__buf_append_c(b, '{')) return false;
            if (e->first_child == NULL) {
                return nanojson__buf_append_c(b, '}');
            }
            const nanojson_element *child = e->first_child;
            bool first = true;
            while (child != NULL) {
                if (!first) {
                    if (!nanojson__buf_append_c(b, ',')) return false;
                }
                if (pretty) {
                    if (!nanojson__buf_append_indent(b, depth + 1, indent_spaces)) return false;
                }
                /* Key (must not be NULL inside a well-formed object, but be defensive). */
                if (child->key != NULL) {
                    if (!nanojson__buf_append_escaped_string(b, child->key, child->key_length)) return false;
                } else {
                    if (!nanojson__buf_append_n(b, "\"\"", 2)) return false;
                }
                if (!nanojson__buf_append_c(b, ':')) return false;
                if (pretty) {
                    if (!nanojson__buf_append_c(b, ' ')) return false;
                }
                if (!nanojson__serialize(b, child, pretty, indent_spaces, depth + 1)) return false;
                first = false;
                child = child->next;
            }
            if (pretty) {
                if (!nanojson__buf_append_indent(b, depth, indent_spaces)) return false;
            }
            return nanojson__buf_append_c(b, '}');
        }
    }

    return false;
}

char *nanojson_serialize_with(const nanojson_element *element,
                              bool pretty,
                              size_t indent_spaces) {
    nanojson__buffer b;
    if (!nanojson__buf_init(&b)) {
        return NULL;
    }
    if (!nanojson__serialize(&b, element, pretty, indent_spaces, 0) || b.failed) {
        free(b.data);
        return NULL;
    }
    /* Shrink to fit (best effort; ignore failure since the original buffer
     * is still valid). */
    char *shrunk = (char *)realloc(b.data, b.size + 1);
    if (shrunk != NULL) {
        return shrunk;
    }
    return b.data;
}

char *nanojson_serialize(const nanojson_element *element, bool pretty) {
    return nanojson_serialize_with(element, pretty, 2);
}
