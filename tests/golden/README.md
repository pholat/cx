# Golden tests

Each `*.cx` file here is transpiled by `cx build` and the generated C is then
compiled by the host compiler (`$CC`, default `cc`). This proves the round trip
`.cx -> generic C` stays buildable.

`*.hx` files are transpiled to headers and `-fsyntax-only`-checked first (so
importing sources can `#include` them), and `*.fail.cx` files are negative
tests: `cx build` must fail with a conformance diagnostic. The `std_*.hx` files
are reusable `macro_rules!` container modules.

Run them with:

```
make test
```

To inspect the generated C for a single case:

```
./cx build tests/golden/hello.cx
./cx dump  tests/golden/hello.cx
```

A standalone, runnable application lives in [`../example/`](../example).
