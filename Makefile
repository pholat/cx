# Cx — build the transpiler and the language server.
#
#   make            build ./cx
#   make test       transpile + compile the golden inputs
#   make clean
#
# Requires: cc, flex, bison.

BUILD   := build
CC      ?= cc
CFLAGS  ?= -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter
CPPFLAGS += -Iinclude -I$(BUILD)
DEPFLAGS := -MMD -MP          # emit .d files so header edits trigger rebuilds
FLEX    ?= flex
BISON   ?= bison

SRC     := $(shell find src -name '*.c')
OBJ     := $(patsubst src/%.c,$(BUILD)/%.o,$(SRC))
GEN_OBJ := $(BUILD)/parser.o $(BUILD)/lexer.o
# The front end, without the CLI's `main`, for the fuzz driver.
FUZZ_OBJ := $(filter-out $(BUILD)/cli/main.o,$(OBJ) $(GEN_OBJ))

.PHONY: all clean test fuzz ci

all: cx

cx: $(OBJ) $(GEN_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

# Hand-written sources.
$(BUILD)/%.o: src/%.c $(BUILD)/parser.h | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(DEPFLAGS) -c $< -o $@

# Bison: grammar -> parser.c + parser.h
$(BUILD)/parser.c: grammar/parser.y | $(BUILD)
	$(BISON) -d -o $@ $<

$(BUILD)/parser.h: $(BUILD)/parser.c
	@:

# Flex: lexer -> lexer.c (needs the generated token header)
$(BUILD)/lexer.c: grammar/lexer.l $(BUILD)/parser.h | $(BUILD)
	$(FLEX) -o $@ $<

$(BUILD)/parser.o: $(BUILD)/parser.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD)/lexer.o: $(BUILD)/lexer.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

# Auto-generated header dependencies (from -MMD -MP). Without these, editing
# include/cx/*.h would not trigger a rebuild of the objects that use it.
-include $(OBJ:.o=.d) $(GEN_OBJ:.o=.d)

test: cx
	sh tests/run.sh

# Fuzz driver: the front end linked with its own `main`.
$(BUILD)/fuzz_driver: tests/fuzz/fuzz_driver.c $(FUZZ_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) $(CPPFLAGS) tests/fuzz/fuzz_driver.c $(FUZZ_OBJ) -o $@

# Short fuzz run over the golden corpus + random inputs.
fuzz: cx $(BUILD)/fuzz_driver
	sh tests/fuzz/run.sh

# Everything CI should run: build, golden tests, output compare, fuzz.
# (Requires `tests/reference-output/`; regenerate with `sh tests/run.sh save`.)
ci: cx $(BUILD)/fuzz_driver
	sh tests/run.sh
	sh tests/run.sh compare
	sh tests/fuzz/run.sh

clean:
	rm -rf $(BUILD) cx
