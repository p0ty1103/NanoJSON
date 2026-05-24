# NanoJSON

[![C99](https://img.shields.io/badge/C-99-blue.svg)](https://en.wikipedia.org/wiki/C99)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**English** | [日本語](README-ja.md)

A small, dependency-free C99 JSON parser and serializer. RFC 8259 compliant,
implemented using only four standard headers (`<stdlib.h>`, `<string.h>`,
`<stdio.h>`, `<stdbool.h>`).

## Features

- **Zero dependencies** — `libc` only. Numbers are parsed without `strtod`, so
  behavior is locale-independent.
- **Strict C99** — builds clean under `-Wall -Wextra -Wpedantic -Werror`.
- **Sanitizer-clean** — no AddressSanitizer or UndefinedBehaviorSanitizer
  reports across the test suite.
- **Full string escape support** — `\n`, `\t`, `\"`, `\\`, `\/`, `\b`, `\f`,
  `\r`, plus `\uXXXX` with UTF-16 surrogate pair decoding to UTF-8.
- **Pretty-print and minify** — choose at call time; indent width is
  configurable.
- **Safe on failure** — partial parse failures clean up every allocated node;
  no leaks.
- **Stack-overflow-resistant cleanup** — `nanojson_free` runs an iterative
  depth-first traversal over its own heap stack, so deeply nested trees do not
  blow the C stack.

## Build

```sh
make            # static library + examples
make test       # build and run the unit tests
make asan       # build and run the tests under ASan + UBSan
```

Artifacts are placed under `build/`:

```text
build/
├── libnanojson.a
├── examples/
│   ├── 01_parse_basic
│   ├── 02_build_and_serialize
│   ├── 03_file_io
│   └── 04_error_handling
└── tests/
    └── test_nanojson
```

If you'd rather link the source directly:

```sh
cc -std=c99 -Iinclude src/nanojson.c your_program.c -o your_program
```

## Quick Start

### Parsing

```c
#include "nanojson.h"
#include <stdio.h>

int main(void) {
    const char *src = "{\"name\": \"Pochix1103\", \"level\": 42}";
    nanojson_error err;
    nanojson_element *root = nanojson_parse(src, &err);
    if (root == NULL) {
        fprintf(stderr, "parse error %zu:%zu - %s\n",
                err.line, err.column, err.message);
        return 1;
    }

    nanojson_element *name = nanojson_object_get(root, "name");
    if (name && name->type == NANOJSON_STRING) {
        printf("name = %.*s\n", (int)name->string_length, name->string);
    }

    nanojson_free(root);
    return 0;
}
```

### Building and Serializing

```c
nanojson_element *root = nanojson_new_object();
nanojson_object_set(root, "id",   nanojson_new_number(1337));
nanojson_object_set(root, "tags", nanojson_new_array());

nanojson_element *tags = nanojson_object_get(root, "tags");
nanojson_array_append(tags, nanojson_new_string("pwn"));
nanojson_array_append(tags, nanojson_new_string("crypto"));

char *out = nanojson_serialize(root, /*pretty=*/true);
printf("%s\n", out);
nanojson_string_free(out);
nanojson_free(root);
```

Output:

```json
{
  "id": 1337,
  "tags": [
    "pwn",
    "crypto"
  ]
}
```

## Data Model

Every JSON value is represented by a single `nanojson_element` struct,
discriminated by its `type` field.

| `nanojson_type`   | Fields in use                                          |
| ----------------- | ----------------------------------------------------- |
| `NANOJSON_NULL`   | (none)                                                 |
| `NANOJSON_BOOL`   | `boolean`                                              |
| `NANOJSON_NUMBER` | `number` (double)                                      |
| `NANOJSON_STRING` | `string`, `string_length`                              |
| `NANOJSON_ARRAY`  | `first_child` / `last_child` / `child_count`           |
| `NANOJSON_OBJECT` | same, with each child's `key` / `key_length` populated |

Children of arrays and objects are stored in a doubly-linked list, so you can
walk them in order by following `next`:

```c
for (nanojson_element *c = arr->first_child; c != NULL; c = c->next) {
    /* ... */
}
```

## API Reference

### Parsing

```c
nanojson_element *nanojson_parse  (const char *json, nanojson_error *err);
nanojson_element *nanojson_parse_n(const char *json, size_t length, nanojson_error *err);
```

- On success, returns a heap-allocated tree; the caller frees it with
  `nanojson_free`.
- On failure, returns `NULL` and (if `err` is non-NULL) populates `message`,
  `line`, `column`, and `offset`.
- `nanojson_parse_n` is useful for inputs that are not null-terminated, such as
  `mmap`'d regions or slices of a larger buffer.

### Constructors

```c
nanojson_element *nanojson_new_null    (void);
nanojson_element *nanojson_new_bool    (bool);
nanojson_element *nanojson_new_number  (double);
nanojson_element *nanojson_new_string  (const char *);
nanojson_element *nanojson_new_string_n(const char *, size_t);
nanojson_element *nanojson_new_array   (void);
nanojson_element *nanojson_new_object  (void);
```

All constructors return `NULL` on allocation failure. The string constructors
copy their input, so you do not need to keep the source buffer alive.

### Container manipulation

```c
bool              nanojson_array_append(nanojson_element *array,  nanojson_element *child);
bool              nanojson_object_set  (nanojson_element *object, const char *key, nanojson_element *child);
nanojson_element *nanojson_object_get  (const nanojson_element *object, const char *key);
nanojson_element *nanojson_array_at    (const nanojson_element *array,  size_t index);
size_t            nanojson_count       (const nanojson_element *container);
void              nanojson_detach      (nanojson_element *element);
```

- `nanojson_object_set` replaces any existing entry with the same key (the
  previous value is freed).
- `nanojson_array_append` and `nanojson_object_set` return `false` if the
  child already has a parent. Call `nanojson_detach` first to move a node
  between containers.

### Serialization

```c
char *nanojson_serialize     (const nanojson_element *element, bool pretty);
char *nanojson_serialize_with(const nanojson_element *element, bool pretty, size_t indent_spaces);
```

- Returns a heap-allocated, null-terminated UTF-8 string on success. Free it
  with `nanojson_string_free`.
- `pretty=false` produces minified output; `true` adds newlines and
  indentation.
- `nanojson_serialize` uses a 2-space indent. Use `nanojson_serialize_with`
  for a custom width.

### Cleanup

```c
void nanojson_free       (nanojson_element *element);
void nanojson_string_free(char *str);
```

Both are safe to call with `NULL`.

## Configuration

The following macros can be overridden at compile time with `-D`:

| Macro                | Default | Description                                  |
| -------------------- | ------- | -------------------------------------------- |
| `NANOJSON_MAX_DEPTH` | `1024`  | Maximum nesting depth the parser will accept |

Example:

```sh
cc -std=c99 -DNANOJSON_MAX_DEPTH=256 -Iinclude src/nanojson.c ...
```

## Memory Ownership

| Function                            | Caller owns the result?                                            |
| ----------------------------------- | ------------------------------------------------------------------ |
| `nanojson_parse*`                   | Yes — release with `nanojson_free`                                 |
| `nanojson_new_*`                    | Yes — until you attach it to a container, then ownership transfers |
| `nanojson_serialize*`               | Yes — release with `nanojson_string_free`                          |
| `nanojson_object_get` / `_array_at` | No — borrowed reference, **do not free**                           |

Once a child is attached to a container via `_append` / `_set`, the container
owns it. Freeing the root frees the whole tree.

## Limitations

- Numbers are stored as `double` (IEEE 754 binary64). There is no separate
  `int64_t` API.
- Objects with duplicate keys follow "last value wins" semantics during
  parsing.
- Serializing a tree containing `NaN` or `Infinity` substitutes `null` at
  those positions so the output is always valid JSON.

## Testing

```sh
make test     # standard build
make asan     # build & run under ASan + UBSan
```

The test suite covers:

- Scalar parsing (null / bool / number / string)
- Escape sequences and `\uXXXX` surrogate pair decoding into UTF-8
- Arrays and objects (parsing, ordering, lookup)
- Error reporting on malformed inputs
- Construction, serialization, and round-trip stability
- Key replacement in objects
- Detach and reattach across containers
- 100-level deep nested parse and free
- Stress: 500-level nested free, 10000-element wide object cleanup

## License

[MIT](LICENSE)
