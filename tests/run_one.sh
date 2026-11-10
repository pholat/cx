#!/bin/sh
# Transpile and compile one golden input — the sub-second inner loop.
#
#   sh tests/run_one.sh tests/golden/hello.cx        .cx -> C, then -c compile
#   sh tests/run_one.sh tests/golden/std/std.hx      .hx -> header, syntax-check
#   sh tests/run_one.sh --c      <file.cx|file.hx>   print the generated C
#   sh tests/run_one.sh --dump   <file.cx|file.hx>   print the AST
#   sh tests/run_one.sh --expand <file.cx|file.hx>   print the expanded source
#
# The sibling headers under tests/golden are generated into a scratch dir first
# so imports and paired `.hx` includes resolve during the compile step.
set -e

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
cx="$root/cx"
golden="tests/golden"

[ -x "$cx" ] || { echo "cx binary not found; run 'make' first" >&2; exit 1; }

mode=build
case "$1" in
    --c)      mode=c;      shift ;;
    --dump)   mode=dump;   shift ;;
    --expand) mode=expand; shift ;;
    -*) echo "unknown option: $1" >&2; exit 2 ;;
esac

src=$1
if [ -z "$src" ] || [ ! -e "$src" ]; then
    echo "usage: sh tests/run_one.sh [--c|--dump|--expand] <file.cx|file.hx>" >&2
    exit 2
fi

case "$mode" in
    c)      exec "$cx" build "$src" ;;
    dump)   exec "$cx" dump "$src" ;;
    expand) exec "$cx" expand "$src" ;;
esac

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Headers first, so imports and paired `.hx` includes resolve during compile.
for h in "$golden"/*.hx "$golden"/std/*.hx; do
    [ -e "$h" ] || continue
    rel=${h#"$golden/"}
    name=${rel%.hx}
    mkdir -p "$tmp/$(dirname "$name")"
    "$cx" build "$h" -o "$tmp/$name.h"
done

case "$src" in
    *.hx)
        rel=${src#"$golden/"}
        name=${rel%.hx}
        mkdir -p "$tmp/$(dirname "$name")"
        "$cx" build "$src" -o "$tmp/$name.h"
        ${CC:-cc} -std=gnu11 -Wall -Wextra -I"$tmp" -I"$root/submodules/klib" \
            -fsyntax-only -x c "$tmp/$name.h"
        ;;
    *)
        name=$(basename "$src" .cx)
        "$cx" build "$src" -o "$tmp/$name.c"
        ${CC:-cc} -std=gnu11 -Wall -Wextra -I"$tmp" -I"$root/submodules/klib" \
            -c "$tmp/$name.c" -o "$tmp/$name.o"
        ;;
esac

echo "ok: $src"
