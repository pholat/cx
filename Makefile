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
FLEX    ?= flex
BISON   ?= bison

SRC     := $(shell find src -name '*.c')
OBJ     := $(patsubst src/%.c,$(BUILD)/%.o,$(SRC))
GEN_OBJ := $(BUILD)/parser.o $(BUILD)/lexer.o

.PHONY: all clean test

all: cx

cx: $(OBJ) $(GEN_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

# Hand-written sources.
$(BUILD)/%.o: src/%.c $(BUILD)/parser.h | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

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
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD)/lexer.o: $(BUILD)/lexer.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

test: cx
	sh tests/run.sh

clean:
	rm -rf $(BUILD) cx
