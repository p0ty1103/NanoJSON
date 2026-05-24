/*
 * NanoJSON - test suite
 *
 * Exercises the parser, constructors, container operations and serializer.
 * Run via `make test` from the repository root.
 */

#include "nanojson.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failed_tests = 0;
static int total_tests  = 0;

#define CHECK(cond) do { \
    total_tests++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #cond); \
        failed_tests++; \
    } \
} while (0)

static void test_parse_scalars(void) {
    nanojson_error err;
    nanojson_element *e;

    e = nanojson_parse("null", &err);
    CHECK(e != NULL); CHECK(e && e->type == NANOJSON_NULL);
    nanojson_free(e);

    e = nanojson_parse("true", &err);
    CHECK(e != NULL); CHECK(e && e->type == NANOJSON_BOOL && e->boolean == true);
    nanojson_free(e);

    e = nanojson_parse("false", &err);
    CHECK(e != NULL); CHECK(e && e->type == NANOJSON_BOOL && e->boolean == false);
    nanojson_free(e);

    e = nanojson_parse("0", &err);
    CHECK(e != NULL); CHECK(e && e->type == NANOJSON_NUMBER && e->number == 0.0);
    nanojson_free(e);

    e = nanojson_parse("-42", &err);
    CHECK(e != NULL); CHECK(e && e->number == -42.0);
    nanojson_free(e);

    e = nanojson_parse("3.14", &err);
    CHECK(e != NULL); CHECK(e && e->number > 3.13 && e->number < 3.15);
    nanojson_free(e);

    e = nanojson_parse("1.5e2", &err);
    CHECK(e != NULL); CHECK(e && e->number == 150.0);
    nanojson_free(e);

    e = nanojson_parse("1.5E-2", &err);
    CHECK(e != NULL); CHECK(e && e->number > 0.014 && e->number < 0.016);
    nanojson_free(e);

    e = nanojson_parse("\"hello\"", &err);
    CHECK(e != NULL);
    CHECK(e && e->type == NANOJSON_STRING);
    CHECK(e && e->string_length == 5 && memcmp(e->string, "hello", 5) == 0);
    nanojson_free(e);

    /* Escape sequences */
    e = nanojson_parse("\"a\\nb\\tc\\\"d\\\\e\\/f\\b\\f\\rg\"", &err);
    CHECK(e != NULL);
    if (e) {
        const char *expected = "a\nb\tc\"d\\e/f\b\f\rg";
        CHECK(e->string_length == strlen(expected));
        CHECK(memcmp(e->string, expected, e->string_length) == 0);
        nanojson_free(e);
    }

    /* \uXXXX BMP */
    e = nanojson_parse("\"\\u00E9\"", &err); /* é -> 0xC3 0xA9 */
    CHECK(e != NULL);
    if (e) {
        CHECK(e->string_length == 2);
        CHECK((unsigned char)e->string[0] == 0xC3 && (unsigned char)e->string[1] == 0xA9);
        nanojson_free(e);
    }

    /* \uXXXX with surrogate pair: U+1F600 = "😀" = F0 9F 98 80 */
    e = nanojson_parse("\"\\uD83D\\uDE00\"", &err);
    CHECK(e != NULL);
    if (e) {
        CHECK(e->string_length == 4);
        CHECK((unsigned char)e->string[0] == 0xF0);
        CHECK((unsigned char)e->string[1] == 0x9F);
        CHECK((unsigned char)e->string[2] == 0x98);
        CHECK((unsigned char)e->string[3] == 0x80);
        nanojson_free(e);
    }
}

static void test_parse_compound(void) {
    nanojson_error err;

    nanojson_element *e = nanojson_parse("[]", &err);
    CHECK(e != NULL && e->type == NANOJSON_ARRAY && e->child_count == 0);
    nanojson_free(e);

    e = nanojson_parse("{}", &err);
    CHECK(e != NULL && e->type == NANOJSON_OBJECT && e->child_count == 0);
    nanojson_free(e);

    e = nanojson_parse("[1, 2, 3]", &err);
    CHECK(e != NULL && e->type == NANOJSON_ARRAY && e->child_count == 3);
    if (e) {
        CHECK(nanojson_array_at(e, 0)->number == 1.0);
        CHECK(nanojson_array_at(e, 1)->number == 2.0);
        CHECK(nanojson_array_at(e, 2)->number == 3.0);
        nanojson_free(e);
    }

    const char *json =
        "{\n"
        "  \"name\": \"Pochix1103\",\n"
        "  \"age\": 18,\n"
        "  \"active\": true,\n"
        "  \"skills\": [\"pwn\", \"crypto\", \"rev\"],\n"
        "  \"address\": null,\n"
        "  \"nested\": { \"a\": [1, [2, [3, [4]]]] }\n"
        "}";
    e = nanojson_parse(json, &err);
    CHECK(e != NULL && e->type == NANOJSON_OBJECT);
    if (e) {
        nanojson_element *name = nanojson_object_get(e, "name");
        CHECK(name && name->type == NANOJSON_STRING);
        CHECK(name && memcmp(name->string, "Pochix1103", 10) == 0);

        nanojson_element *age = nanojson_object_get(e, "age");
        CHECK(age && age->type == NANOJSON_NUMBER && age->number == 18.0);

        nanojson_element *active = nanojson_object_get(e, "active");
        CHECK(active && active->boolean == true);

        nanojson_element *skills = nanojson_object_get(e, "skills");
        CHECK(skills && skills->type == NANOJSON_ARRAY && skills->child_count == 3);
        CHECK(memcmp(nanojson_array_at(skills, 1)->string, "crypto", 6) == 0);

        nanojson_element *address = nanojson_object_get(e, "address");
        CHECK(address && address->type == NANOJSON_NULL);

        nanojson_element *nested = nanojson_object_get(e, "nested");
        CHECK(nested && nested->type == NANOJSON_OBJECT);

        nanojson_free(e);
    }
}

static void test_parse_errors(void) {
    nanojson_error err;

    nanojson_element *e = nanojson_parse("", &err);
    CHECK(e == NULL && err.occurred);

    e = nanojson_parse("xx", &err);
    CHECK(e == NULL && err.occurred);

    e = nanojson_parse("[1, 2,", &err);
    CHECK(e == NULL && err.occurred);

    e = nanojson_parse("{\"a\": 1,}", &err);
    CHECK(e == NULL && err.occurred);

    e = nanojson_parse("\"unterminated", &err);
    CHECK(e == NULL && err.occurred);

    e = nanojson_parse("\"bad\\xescape\"", &err);
    CHECK(e == NULL && err.occurred);

    e = nanojson_parse("[1, 2] extra", &err);
    CHECK(e == NULL && err.occurred);

    e = nanojson_parse("[1.]", &err);
    CHECK(e == NULL && err.occurred);

    e = nanojson_parse("[1e]", &err);
    CHECK(e == NULL && err.occurred);

    /* Unmatched low surrogate */
    e = nanojson_parse("\"\\uDC00\"", &err);
    CHECK(e == NULL && err.occurred);

    /* High surrogate without low */
    e = nanojson_parse("\"\\uD83Dabc\"", &err);
    CHECK(e == NULL && err.occurred);
}

static void test_construction_and_serialization(void) {
    nanojson_element *root = nanojson_new_object();
    CHECK(root != NULL);

    CHECK(nanojson_object_set(root, "name", nanojson_new_string("Pochix1103")));
    CHECK(nanojson_object_set(root, "level", nanojson_new_number(42)));
    CHECK(nanojson_object_set(root, "active", nanojson_new_bool(true)));
    CHECK(nanojson_object_set(root, "next", nanojson_new_null()));

    nanojson_element *arr = nanojson_new_array();
    CHECK(arr != NULL);
    CHECK(nanojson_array_append(arr, nanojson_new_number(1)));
    CHECK(nanojson_array_append(arr, nanojson_new_number(2.5)));
    CHECK(nanojson_array_append(arr, nanojson_new_string("three")));
    CHECK(nanojson_object_set(root, "items", arr));

    char *min = nanojson_serialize(root, false);
    CHECK(min != NULL);
    if (min) {
        nanojson_error err;
        nanojson_element *re = nanojson_parse(min, &err);
        CHECK(re != NULL);
        if (re) {
            nanojson_element *name = nanojson_object_get(re, "name");
            CHECK(name && memcmp(name->string, "Pochix1103", name->string_length) == 0);
            nanojson_free(re);
        }
        nanojson_string_free(min);
    }

    char *pretty = nanojson_serialize(root, true);
    CHECK(pretty != NULL);
    nanojson_string_free(pretty);

    char *pretty4 = nanojson_serialize_with(root, true, 4);
    CHECK(pretty4 != NULL);
    nanojson_string_free(pretty4);

    nanojson_free(root);
}

static void test_object_replacement(void) {
    nanojson_element *o = nanojson_new_object();
    CHECK(nanojson_object_set(o, "x", nanojson_new_number(1)));
    CHECK(nanojson_object_set(o, "y", nanojson_new_number(2)));
    CHECK(nanojson_object_set(o, "x", nanojson_new_number(99))); /* replace */
    CHECK(o->child_count == 2);
    nanojson_element *x = nanojson_object_get(o, "x");
    CHECK(x && x->number == 99.0);
    nanojson_free(o);
}

static void test_detach_and_reattach(void) {
    nanojson_element *root = nanojson_new_object();
    nanojson_object_set(root, "value", nanojson_new_number(7));

    nanojson_element *v = nanojson_object_get(root, "value");
    CHECK(v && v->parent == root);
    nanojson_detach(v);
    CHECK(v->parent == NULL);
    CHECK(root->child_count == 0);

    nanojson_element *arr = nanojson_new_array();
    CHECK(nanojson_array_append(arr, v));
    CHECK(arr->child_count == 1);

    nanojson_free(root);
    nanojson_free(arr);
}

static void test_deep_nesting(void) {
    /* Build [[[...]]] of depth ~100 */
    char buf[512];
    size_t n = 0;
    int depth = 100;
    for (int i = 0; i < depth; i++) buf[n++] = '[';
    for (int i = 0; i < depth; i++) buf[n++] = ']';
    buf[n] = '\0';

    nanojson_error err;
    nanojson_element *e = nanojson_parse(buf, &err);
    CHECK(e != NULL);
    if (e) {
        nanojson_element *cur = e;
        int d = 0;
        while (cur->first_child != NULL) {
            cur = cur->first_child;
            d++;
        }
        CHECK(d == depth - 1);
        nanojson_free(e);
    }
}

static void test_round_trip(void) {
    const char *src =
        "{\"a\":1,\"b\":[true,false,null,\"hi\\nthere\"],"
        "\"unicode\":\"\\u00E9\\uD83D\\uDE00\","
        "\"nested\":{\"k\":\"v\"}}";
    nanojson_error err;
    nanojson_element *e = nanojson_parse(src, &err);
    CHECK(e != NULL);
    if (e) {
        char *out = nanojson_serialize(e, false);
        CHECK(out != NULL);
        if (out) {
            nanojson_element *e2 = nanojson_parse(out, &err);
            CHECK(e2 != NULL);
            nanojson_free(e2);
            nanojson_string_free(out);
        }
        nanojson_free(e);
    }
}

int main(void) {
    test_parse_scalars();
    test_parse_compound();
    test_parse_errors();
    test_construction_and_serialization();
    test_object_replacement();
    test_detach_and_reattach();
    test_deep_nesting();
    test_round_trip();

    printf("=== Results: %d/%d passed ===\n",
           total_tests - failed_tests, total_tests);
    return failed_tests == 0 ? 0 : 1;
}
