# NanoJSON - Makefile
#
# Common targets:
#   make            - build static library + examples
#   make lib        - build static library only (build/libnanojson.a)
#   make examples   - build all examples (build/examples/)
#   make test       - build & run unit tests
#   make asan       - build & run tests under AddressSanitizer + UBSan
#   make clean      - remove build artifacts

CC          ?= cc
AR          ?= ar
CSTD        ?= -std=c99
CFLAGS_BASE ?= $(CSTD) -Wall -Wextra -Wpedantic -O2 -g
CFLAGS_LIB  ?= $(CFLAGS_BASE) -fPIC
INCLUDES    := -Iinclude

BUILD_DIR   := build
LIB_DIR     := $(BUILD_DIR)
EX_DIR      := $(BUILD_DIR)/examples
TST_DIR     := $(BUILD_DIR)/tests

SRC         := src/nanojson.c
OBJ         := $(BUILD_DIR)/nanojson.o
LIB         := $(LIB_DIR)/libnanojson.a

EXAMPLES_SRC := $(wildcard examples/*.c)
EXAMPLES_BIN := $(patsubst examples/%.c,$(EX_DIR)/%,$(EXAMPLES_SRC))

TEST_SRC    := tests/test_nanojson.c
TEST_BIN    := $(TST_DIR)/test_nanojson

.PHONY: all lib examples test asan clean help

all: lib examples

help:
	@echo "Targets:"
	@echo "  all       - lib + examples (default)"
	@echo "  lib       - static library only"
	@echo "  examples  - sample programs"
	@echo "  test      - build & run unit tests"
	@echo "  asan      - build & run tests under ASan + UBSan"
	@echo "  clean     - remove build/"

# ----- library -----

lib: $(LIB)

$(LIB): $(OBJ)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(OBJ): $(SRC) include/nanojson.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_LIB) $(INCLUDES) -c $< -o $@

# ----- examples -----

examples: $(EXAMPLES_BIN)

$(EX_DIR)/%: examples/%.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) $(INCLUDES) $< $(LIB) -o $@

# ----- tests -----

test: $(TEST_BIN)
	@$(TEST_BIN)

$(TEST_BIN): $(TEST_SRC) $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_BASE) $(INCLUDES) $< $(LIB) -o $@

# ----- sanitizer build -----

asan:
	@mkdir -p $(TST_DIR)
	$(CC) $(CFLAGS_BASE) -fsanitize=address,undefined -fno-omit-frame-pointer \
	      $(INCLUDES) $(SRC) $(TEST_SRC) -o $(TST_DIR)/test_asan
	@$(TST_DIR)/test_asan

# ----- housekeeping -----

clean:
	rm -rf $(BUILD_DIR)
