# Minimal Cx application:  example.cx -> example.c -> example.out
#
#   make        transpile + compile to example.out
#   make run    build and execute example.out
#   make c      transpile only (prints the generated C)
#   make clean

CX     ?= ../../cx
CC     ?= cc
CFLAGS ?= -std=gnu11 -Wall -Wextra -Wno-unused-parameter
KLIB   ?= ../../submodules/klib

.PHONY: all run c clean

all: example.out

# 1. Cx -> C
example.c: example.cx $(CX)
	$(CX) build example.cx -o example.c

# 2. C -> native executable
example.out: example.c
	$(CC) $(CFLAGS) -I$(KLIB) example.c -o example.out

run: example.out
	./example.out

# Print the generated C instead of writing it.
c: example.cx $(CX)
	$(CX) build example.cx

clean:
	rm -f example.c example.out
