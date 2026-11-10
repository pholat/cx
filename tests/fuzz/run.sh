#!/bin/sh
# Fuzz the Cx front end: every golden source plus generated random inputs are
# fed to `cx_macro_expand` and to `cx_parse` + `cx_sema_build`. A crash, a
# non-zero exit or a timeout is a failure. Inputs need not be valid Cx — the
# compiler must simply never crash or hang on them.
#
#   sh tests/fuzz/run.sh [iterations]     (default 200 random inputs)
#
# Needs `build/fuzz_driver`; run `make fuzz` (or `make ci`).
set -e

root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"

driver="$root/build/fuzz_driver"
[ -x "$driver" ] || {
    echo "fuzz: build/fuzz_driver missing; run 'make fuzz'" >&2
    exit 1
}

iters=${1:-200}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Run one input; on failure keep it so the crash is reproducible.
run() {
    _mode=$1
    _file=$2
    if command -v timeout >/dev/null 2>&1; then
        timeout 10 "$driver" "$_mode" "$_file" >/dev/null 2>&1 && return 0
    else
        "$driver" "$_mode" "$_file" >/dev/null 2>&1 && return 0
    fi
    cp "$_file" "fuzz-crash-$_mode.cx" 2>/dev/null || true
    echo "fuzz: FAIL ($_mode) on '$_file' (kept as fuzz-crash-$_mode.cx)" >&2
    exit 1
}

# 1. Seeded corpus: every golden source, all valid Cx.
for f in tests/golden/*.cx tests/golden/*.hx tests/golden/std/*.hx; do
    [ -e "$f" ] || continue
    run macro "$f"
    run parse "$f"
done

# 2. Random inputs, filtered to Cx-ish characters so the lexer/parser is really
#    exercised rather than rejected on the first byte.
keep='A-Za-z0-9_ {}();,.*&~!<>=+-/%|^?:#'
n=1
while [ "$n" -le "$iters" ]; do
    len=$(( (n * 977) % 512 + 1 ))
    head -c "$len" /dev/urandom 2>/dev/null | tr -dc "$keep" > "$tmp/rand.cx" || true
    run macro "$tmp/rand.cx"
    run parse "$tmp/rand.cx"
    n=$((n + 1))
done

echo "fuzz: OK (seeded corpus + $iters random inputs)"
