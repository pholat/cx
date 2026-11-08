# Cx — a thin extension over C (namespaces · RAII · traits · macros)

Cx transpiles `.cx` / `.hx` sources to **portable C** that any normal toolchain
(GCC, Clang, `arm-none-eabi-gcc`) compiles, using
`__attribute__((cleanup))` for RAII. It adds namespaces, C++-style
constructors/destructors, static traits, attributes, and `macro_rules!`-style
macros — and ships a language server that shares the exact parser + symbol table
the transpiler uses.

## How it works

```text
  .cx / .hx
     │
     ▼
┌─────────┐    ┌──────────┐    ┌────────┐    ┌─────────┐
│  Lexer  │───▶│  Parser  │───▶│  AST   │───▶│  Sema   │
│ (Flex)  │    │ (Bison)  │    │ shared │    │ symbols │
└─────────┘    └──────────┘    └────────┘    │ + RAII  │
                                             └────┬────┘
                                   ┌──────────────┴──────────────┐
                                   ▼                             ▼
                             ┌───────────┐                 ┌───────────┐
                             │  Codegen  │                 │ LSP server│
                             │ generic C │                 │ (cx lsp)  │
                             └─────┬─────┘                 └─────┬─────┘
                                   ▼                             ▼
                            build/*.c, build/*.h        diagnostics, gd,
                            (+ #line back to .cx)       hover, completion
```

The **AST + semantic layer is the contract**: the C backend and the language
server are two consumers of the *same* tree, so editor diagnostics cannot
diverge from what the transpiler accepts.

## Why a transpiler

Cx adds syntax that Clang/GCC's C frontends cannot parse (`namespace`,
`~Dtor()`, `trait`, `implements`, `#[…]`, `macro_rules!`). Rather than patch a
compiler, Cx is a Flex/Bison source-to-source transpiler:

- the output is plain C, so it works with any compiler version;
- the same reentrant parser powers the LSP, so there is one grammar and no
  backend/editor drift;
- Clang stays optional — a semantic verifier after transpilation, never the
  frontend.

The approach rationale (why a transpiler, why a shared AST) is summarized in
[`doc/technical.md`](./doc/technical.md) §1.

## Quick start

```sh
make            # build ./cx
make test       # transpile + compile every tests/golden/*.cx
./cx build file.cx -o file.c
```

A runnable, end-to-end example (generated C + RAII + a klib-backed list) lives
in [`tests/example/`](./tests/example): `make run` there after building `./cx`.

## Documentation

All documentation lives in [`doc/`](./doc). Filenames are **lowercase
kebab-case**.

| Doc | Contents |
| :-- | :-- |
| [`doc/technical.md`](./doc/technical.md) | **Start here.** Living map of the codebase: build, codegen passes, sema, gotchas. |
| [`doc/language.md`](./doc/language.md) | The implemented language surface, rule by rule (syntax → lowering). |
| [`doc/roadmap.md`](./doc/roadmap.md) | Priorities, known issues, and the **Refinement** backlog. |
| [`doc/changelog.md`](./doc/changelog.md) | Session log / history. |
| [`doc/generics.md`](./doc/generics.md) | Traits, attributes, generics rationale. |
| [`doc/macro-rules.md`](./doc/macro-rules.md) | `macro_rules!` reference. |
| [`doc/integration.md`](./doc/integration.md) | Using Cx from a C build + editor setup. |
| [`doc/lsp.md`](./doc/lsp.md) | LSP features and resolution order. |
| [`doc/required-features.md`](./doc/required-features.md) | Remaining low-level C / klib gaps. |