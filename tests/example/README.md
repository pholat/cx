# Minimal Cx application

A single-file, runnable example of what Cx is for: write `.cx`, transpile it to
portable C, compile that with any C toolchain.

```text
example.cx  --cx build-->  example.c  --cc-->  example.out
```

## Build & run

```sh
make            # example.cx -> example.c -> example.out
make run        # build then execute
make c          # print the generated C to stdout
```

`cx` must be built first (`make` in the repository root). The generated `C` is
built with `-I ../../submodules/klib` because the list comes from klib.

Expected output:

```text
hello world
list holds 5 values:
  10
  20
  30
  40
  50
bye (RAII)
```

## What it demonstrates

| Feature | Where |
| :-- | :-- |
| `namespace` | everything lives in `namespace example` |
| `macro_rules!` container | `define_list!` generates a klib-backed list (same technique as `tests/golden/klib_list.cx`) |
| macros that generate code | `fill_list!` (5 pushes, one per `$value`) and `print_list!` (drain loop) |
| klib interop | `KLIST_INIT!` + the `void *` wrapper |
| RAII | `example::IntListOwner` ctor/dtor; `__attribute__((cleanup))` frees the list and prints `bye (RAII)` at scope exit |
| `#[export]` / C `main` | `#[export("main")] int run()` becomes the C `main` |
| C interop | `raw { … }` for the klib free callback, `#include`/`printf` passthrough |

## Notes

* `T v;` does **not** call a zero-arg constructor, so the owner is created as
  `IntListOwner owner = IntListOwner();`.
* The `$( … )*` repetition in `fill_list!` infers its count from the first
  repeated metavariable (`$value`), which is why the list pointer is bound to a
  local (`dst`) outside the repetition.
* Only `*.cx` files under `tests/golden/` are picked up by `make test`; this
  directory is a standalone, *runnable* example.
