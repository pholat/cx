# Runnable state-machine example: split across example_sm.cx, calls.cx and sm.cx
#
#   make        build build/example_sm.out
#   make run    build and execute build/example_sm.out
#   make c      build the generated C files only
#   make clean

CX     ?= ../../cx
CC     ?= cc
CFLAGS ?= -std=gnu11 -Wall -Wextra -Wno-unused-parameter
BUILD_DIR := build

.PHONY: all run c clean

all: $(BUILD_DIR)/example_sm.out

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# 1. Cx -> C
$(BUILD_DIR)/sm.h: sm.hx $(CX) | $(BUILD_DIR)
	$(CX) build sm.hx -o $(BUILD_DIR)/sm.h

$(BUILD_DIR)/calls.h: calls.hx $(BUILD_DIR)/sm.h $(CX) | $(BUILD_DIR)
	$(CX) build calls.hx -o $(BUILD_DIR)/calls.h

$(BUILD_DIR)/sm.c: sm.cx $(CX) | $(BUILD_DIR)
	$(CX) build sm.cx -o $(BUILD_DIR)/sm.c

$(BUILD_DIR)/calls.c: calls.cx $(BUILD_DIR)/calls.h $(BUILD_DIR)/sm.h $(CX) | $(BUILD_DIR)
	$(CX) build calls.cx -o $(BUILD_DIR)/calls.c

$(BUILD_DIR)/example_sm.c: example_sm.cx $(BUILD_DIR)/calls.h $(BUILD_DIR)/sm.h $(CX) | $(BUILD_DIR)
	$(CX) build example_sm.cx -o $(BUILD_DIR)/example_sm.c

# 2. C -> native executable
$(BUILD_DIR)/example_sm.out: $(BUILD_DIR)/example_sm.c $(BUILD_DIR)/calls.c $(BUILD_DIR)/sm.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(BUILD_DIR)/example_sm.c $(BUILD_DIR)/calls.c $(BUILD_DIR)/sm.c -o $(BUILD_DIR)/example_sm.out

run: $(BUILD_DIR)/example_sm.out
	./$(BUILD_DIR)/example_sm.out

# Build the generated C files only.
c: $(BUILD_DIR)/example_sm.c $(BUILD_DIR)/calls.c $(BUILD_DIR)/sm.c

clean:
	rm -rf $(BUILD_DIR)
