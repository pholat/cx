#!/bin/sh
# Transpile every golden input and compile the result with the host compiler.
# Headers (.hx) are produced first so importing sources can #include them.
set -e

root=$(cd "$(dirname "$0")/.." && pwd)
cx="$root/cx"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

[ -x "$cx" ] || { echo "cx binary not found; run 'make' first" >&2; exit 1; }

# Headers first.
for src in "$root"/tests/golden/*.hx; do
    [ -e "$src" ] || continue
    name=$(basename "$src" .hx)
    echo "== $name (header)"
    "$cx" build "$src" -o "$tmp/$name.h"
    ${CC:-cc} -std=gnu11 -Wall -Wextra -I"$tmp" -fsyntax-only -x c "$tmp/$name.h"
done

# Sources.
for src in "$root"/tests/golden/*.cx; do
    [ -e "$src" ] || continue
    name=$(basename "$src" .cx)
    echo "== $name"
    "$cx" build "$src" -o "$tmp/$name.c"
done

# Compile each generated source (no linkage; imports resolve via -I).
for c in "$tmp"/*.c; do
    [ -e "$c" ] || continue
    ${CC:-cc} -std=gnu11 -Wall -Wextra -I"$tmp" -c "$c" -o "${c%.c}.o"
done

echo "golden: OK"
