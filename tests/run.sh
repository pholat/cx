#!/bin/sh
# Transpile every golden input and compile the generated C with the host
# compiler. This is the end-to-end guarantee: .cx/.hx always lowers to portable
# C that a normal C compiler accepts.
set -e

root=$(cd "$(dirname "$0")/.." && pwd)
cx="$root/cx"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

[ -x "$cx" ] || { echo "cx binary not found; run 'make' first" >&2; exit 1; }

for src in "$root"/tests/golden/*.cx; do
    [ -e "$src" ] || continue
    name=$(basename "$src" .cx)
    out="$tmp/$name.c"
    obj="$tmp/$name.o"

    echo "== $name"
    "$cx" build "$src" -o "$out"
    ${CC:-cc} -std=gnu11 -Wall -Wextra -c "$out" -o "$obj"
done

echo "golden: OK"
