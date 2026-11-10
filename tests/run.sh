#!/bin/sh
# Transpile every golden input and compile the result with the host compiler.
# Headers (.hx) are produced first so importing sources can #include them.
#
# Usage:
#   sh tests/run.sh                 transpile + compile every golden input
#   sh tests/run.sh save            write every cx output to tests/reference-output/
#   sh tests/run.sh compare         diff every cx output against tests/reference-output/
#
# `save` / `compare` capture `cx build <src>` stdout (paths relative to the repo
# root), so a single case can be checked by hand, e.g.
#   ./cx build tests/golden/hello.cx | diff tests/reference-output/hello.c - && echo ok
set -e

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

cx="$root/cx"
golden="tests/golden"
ref="tests/reference-output"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Normalise the mode argument.
mode=${1:-run}
case "$mode" in
    run|save|compare) ;;
    --save-ref)    mode=save ;;
    --compare-ref) mode=compare ;;
    *) echo "unknown option: $mode" >&2; exit 2 ;;
esac

[ -x "$cx" ] || { echo "cx binary not found; run 'make' first" >&2; exit 1; }

# Reference stem for an input: its path under tests/golden without extension
# (`tests/golden/std/std.hx` -> `std/std`).
stem() { p=${1#"$golden/"}; printf '%s' "${p%.*}"; }

fail=0
# Save or compare one input's `cx build` stdout.  $1=src $2=stem $3=ext
check_ref() {
    case "$mode" in
    save)
        mkdir -p "$ref/$(dirname "$2")"
        "$cx" build "$1" > "$ref/$2.$3"
        echo "  saved $2.$3"
        ;;
    compare)
        if ! "$cx" build "$1" > "$tmp/cmp.out" 2>"$tmp/cmp.err"; then
            echo "  $2.$3: cx failed"; cat "$tmp/cmp.err" >&2; fail=1
        elif diff -u "$ref/$2.$3" "$tmp/cmp.out" > "$tmp/cmp.diff"; then
            echo "  $2.$3: ok"
        else
            echo "  $2.$3: DIFFERS"; cat "$tmp/cmp.diff"; fail=1
        fi
        ;;
    esac
    return 0
}

# --- reference catalog: save / compare --------------------------------------
if [ "$mode" = save ] || [ "$mode" = compare ]; then
    if [ "$mode" = save ]; then
        rm -rf "$ref"
        mkdir -p "$ref"
    elif [ ! -d "$ref" ]; then
        echo "no reference catalog at $ref; run 'sh tests/run.sh save' first" >&2
        exit 1
    fi

    # Headers first (top-level and `std/` subdirectory), then sources.
    for src in "$golden"/*.hx "$golden"/std/*.hx; do
        [ -e "$src" ] || continue
        check_ref "$src" "$(stem "$src")" h
    done
    for src in "$golden"/*.cx; do
        [ -e "$src" ] || continue
        case "$src" in *.fail.cx) continue ;; esac
        check_ref "$src" "$(stem "$src")" c
    done

    if [ "$mode" = save ]; then
        echo "reference: saved to $ref"
    elif [ "$fail" -ne 0 ]; then
        echo "reference: MISMATCH" >&2
        exit 1
    else
        echo "reference: OK"
    fi
    exit 0
fi

# --- default: transpile + compile -------------------------------------------

# Headers first (top-level and `std/` subdirectory).
for src in "$golden"/*.hx "$golden"/std/*.hx; do
    [ -e "$src" ] || continue
    rel=${src#"$golden/"}
    name=${rel%.hx}
    echo "== $name (header)"
    mkdir -p "$tmp/$(dirname "$name")"
    "$cx" build "$src" -o "$tmp/$name.h"
    ${CC:-cc} -std=gnu11 -Wall -Wextra -I"$tmp" -I"$root/submodules/klib" \
        -fsyntax-only -x c "$tmp/$name.h"
done

# Sources (`*.fail.cx` are negative tests, checked separately below).
for src in "$golden"/*.cx; do
    [ -e "$src" ] || continue
    name=$(basename "$src" .cx)
    case "$name" in *.fail) continue ;; esac
    echo "== $name"
    "$cx" build "$src" -o "$tmp/$name.c"
done

# Negative tests: `cx build` must report an error and exit non-zero (e.g. a
# checked-`requires` conformance failure).
for src in "$golden"/*.fail.cx; do
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
