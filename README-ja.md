# NanoJSON

[![C99](https://img.shields.io/badge/C-99-blue.svg)](https://en.wikipedia.org/wiki/C99)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**日本語** | [English](README.md)

小さく、依存関係のない C99 製 JSON パーサ / シリアライザです。RFC 8259 に準拠しており、
4 つの標準ヘッダ（`<stdlib.h>`, `<string.h>`, `<stdio.h>`, `<stdbool.h>`）のみを使用して実装されています。

## Features

- **Zero dependencies** — `libc` のみを使用します。数値は `strtod` を使わずに parse されるため、
  locale に依存しない動作になります。
- **Strict C99** — `-Wall -Wextra -Wpedantic -Werror` で clean に build できます。
- **Sanitizer-clean** — テストスイート全体で AddressSanitizer または UndefinedBehaviorSanitizer の
  report はありません。
- **Full string escape support** — `\n`, `\t`, `\"`, `\\`, `\/`, `\b`, `\f`,
  `\r` に加えて、UTF-16 surrogate pair を UTF-8 に decode する `\uXXXX` に対応しています。
- **Pretty-print and minify** — 呼び出し時に選択できます。indent 幅も設定可能です。
- **Safe on failure** — parse 途中で失敗した場合でも、確保済みのすべての node を clean up します。
  leak はありません。
- **Stack-overflow-resistant cleanup** — `nanojson_free` は独自の heap stack 上で iterative な
  depth-first traversal を行うため、深く nest した tree でも C stack を破壊しません。

## Build

```sh
make            # static library + examples
make test       # build and run the unit tests
make asan       # build and run the tests under ASan + UBSan
```

成果物は `build/` 以下に配置されます。

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

source を直接 link したい場合:

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

すべての JSON value は、単一の `nanojson_element` struct で表現され、
`type` field によって識別されます。

| `nanojson_type`   | 使用される field                                      |
| ----------------- | ----------------------------------------------------- |
| `NANOJSON_NULL`   | なし                                                  |
| `NANOJSON_BOOL`   | `boolean`                                             |
| `NANOJSON_NUMBER` | `number` (double)                                     |
| `NANOJSON_STRING` | `string`, `string_length`                             |
| `NANOJSON_ARRAY`  | `first_child` / `last_child` / `child_count`          |
| `NANOJSON_OBJECT` | 同上。各 child の `key` / `key_length` が設定されます |

array と object の children は doubly-linked list に格納されるため、
`next` をたどることで順番通りに走査できます。

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

- 成功時は heap 上に確保された tree を返します。呼び出し側は
  `nanojson_free` で解放します。
- 失敗時は `NULL` を返し、`err` が non-NULL の場合は `message`,
  `line`, `column`, `offset` を設定します。
- `nanojson_parse_n` は、null-terminated ではない入力に便利です。例として、
  `mmap` された領域や、より大きな buffer の slice などがあります。

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

すべての constructor は allocation failure 時に `NULL` を返します。string constructor は
入力を copy するため、source buffer を保持し続ける必要はありません。

### Container manipulation

```c
bool              nanojson_array_append(nanojson_element *array,  nanojson_element *child);
bool              nanojson_object_set  (nanojson_element *object, const char *key, nanojson_element *child);
nanojson_element *nanojson_object_get  (const nanojson_element *object, const char *key);
nanojson_element *nanojson_array_at    (const nanojson_element *array,  size_t index);
size_t            nanojson_count       (const nanojson_element *container);
void              nanojson_detach      (nanojson_element *element);
```

- `nanojson_object_set` は、同じ key を持つ既存の entry を置き換えます。
  以前の value は解放されます。
- `nanojson_array_append` と `nanojson_object_set` は、child がすでに parent を持っている場合
  `false` を返します。node を container 間で移動する場合は、先に `nanojson_detach` を呼び出してください。

### Serialization

```c
char *nanojson_serialize     (const nanojson_element *element, bool pretty);
char *nanojson_serialize_with(const nanojson_element *element, bool pretty, size_t indent_spaces);
```

- 成功時は、heap 上に確保された null-terminated UTF-8 string を返します。
  `nanojson_string_free` で解放してください。
- `pretty=false` は minified output を生成します。`true` は newline と
  indentation を追加します。
- `nanojson_serialize` は 2-space indent を使用します。custom width を使う場合は
  `nanojson_serialize_with` を使用してください。

### Cleanup

```c
void nanojson_free       (nanojson_element *element);
void nanojson_string_free(char *str);
```

どちらも `NULL` を渡しても安全です。

## Configuration

以下の macro は compile 時に `-D` で override できます。

| Macro                | Default | Description                                  |
| -------------------- | ------- | -------------------------------------------- |
| `NANOJSON_MAX_DEPTH` | `1024`  | parser が受け付ける最大 nesting depth        |

Example:

```sh
cc -std=c99 -DNANOJSON_MAX_DEPTH=256 -Iinclude src/nanojson.c ...
```

## Memory Ownership

| Function                            | Caller owns the result?                                            |
| ----------------------------------- | ------------------------------------------------------------------ |
| `nanojson_parse*`                   | Yes — `nanojson_free` で解放します                                 |
| `nanojson_new_*`                    | Yes — container に attach するまでは所有します。その後 ownership は移動します |
| `nanojson_serialize*`               | Yes — `nanojson_string_free` で解放します                          |
| `nanojson_object_get` / `_array_at` | No — borrowed reference。**free してはいけません**                 |

child が `_append` / `_set` によって container に attach されると、その container が
child を所有します。root を free すると、tree 全体が free されます。

## Limitations

- number は `double` (IEEE 754 binary64) として格納されます。独立した
  `int64_t` API はありません。
- duplicate key を持つ object は、parse 時に "last value wins" semantics に従います。
- `NaN` または `Infinity` を含む tree を serialize すると、その位置は `null` に置き換えられ、
  output は常に valid JSON になります。

## Testing

```sh
make test     # standard build
make asan     # build & run under ASan + UBSan
```

test suite は以下を cover しています。

- Scalar parsing (null / bool / number / string)
- Escape sequences と `\uXXXX` surrogate pair の UTF-8 decode
- Arrays and objects (parsing, ordering, lookup)
- Malformed input に対する error reporting
- Construction, serialization, and round-trip stability
- Object における key replacement
- Container 間の detach and reattach
- 100-level deep nested parse and free
- Stress: 500-level nested free, 10000-element wide object cleanup

## License

[MIT](LICENSE)
