#!/bin/sh
# Transpile every golden input and compile the result with the host compiler.
# Headers (.hx) are produced first so importing sources can #include them.
set -e

root=$(cd "$(dirname "$0")/.." && pwd)
cx="$root/cx"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

[ -x "$cx" ] || { echo "cx binary not found; run 'make' first" >&2; exit 1; }

# Headers first (top-level and `std/` subdirectory).
for src in "$root"/tests/golden/*.hx "$root"/tests/golden/std/*.hx; do
    [ -e "$src" ] || continue
    rel=${src#"$root/tests/golden/"}
    name=${rel%.hx}
    echo "== $name (header)"
    mkdir -p "$tmp/$(dirname "$name")"
    "$cx" build "$src" -o "$tmp/$name.h"
    ${CC:-cc} -std=gnu11 -Wall -Wextra -I"$tmp" -I"$root/submodules/klib" \
        -fsyntax-only -x c "$tmp/$name.h"
done

# Sources (`*.fail.cx` are negative tests, checked separately below).
for src in "$root"/tests/golden/*.cx; do
    [ -e "$src" ] || continue
    name=$(basename "$src" .cx)
    case "$name" in *.fail) continue ;; esac
    echo "== $name"
    "$cx" build "$src" -o "$tmp/$name.c"
done

# Negative tests: `cx build` must report an error and exit non-zero (e.g. a
# checked-`requires` conformance failure).
for src in "$root"/tests/golden/*.fail.cx; do
    [ -e "$src" ] || continue
    echo "== $(basename "$src") (expected error)"
    if "$cx" build "$src" -o /dev/null 2>"$tmp/fail.err"; then
        echo "expected '$src' to fail, but it succeeded" >&2
        exit 1
    fi
    if ! grep -qE "does not implement trait|does not name a method" "$tmp/fail.err"; then
        echo "unexpected diagnostic for '$src':" >&2
        cat "$tmp/fail.err" >&2
        exit 1
    fi
done

# Compile each generated source (no linkage; imports resolve via -I).
for c in "$tmp"/*.c; do
    [ -e "$c" ] || continue
    ${CC:-cc} -std=gnu11 -Wall -Wextra -I"$tmp" -I"$root/submodules/klib" \
        -c "$c" -o "${c%.c}.o"
done

echo "golden: OK"
