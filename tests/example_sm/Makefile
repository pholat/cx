# Runnable state-machine example:  example_sm.cx -> example_sm.c -> example_sm.out
#
#   make        transpile + compile to example_sm.out
#   make run    build and execute example_sm.out
#   make c      transpile only (prints the generated C)
#   make clean

CX     ?= ../../cx
CC     ?= cc
CFLAGS ?= -std=gnu11 -Wall -Wextra -Wno-unused-parameter

.PHONY: all run c clean

all: example_sm.out

# 1. Cx -> C
example_sm.c: example_sm.cx $(CX)
	$(CX) build example_sm.cx -o example_sm.c

# 2. C -> native executable
example_sm.out: example_sm.c
	$(CC) $(CFLAGS) example_sm.c -o example_sm.out

run: example_sm.out
	./example_sm.out

# Print the generated C instead of writing it.
c: example_sm.cx $(CX)
	$(CX) build example_sm.cx

clean:
	rm -f example_sm.c example_sm.out
