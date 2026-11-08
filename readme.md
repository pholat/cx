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

## Quick start

```sh
make            # build ./cx
make test       # transpile + compile every tests/golden/*.cx
./cx build file.cx -o file.c
```

## Documentation

All documentation lives in [`doc/`](./doc).

| Doc | Contents |
| :-- | :-- |
| [`doc/TECHNICAL.md`](./doc/TECHNICAL.md) | **Start here.** Living map of the codebase: language surface, codegen, semantics, gotchas. |
| [`doc/ROADMAP.md`](./doc/ROADMAP.md) | Priorities, ordering rationale, and the **Refinement** backlog. |
| [`doc/changelog.md`](./doc/changelog.md) | Session log / history. |
| [`doc/GENERICS.md`](./doc/GENERICS.md) | Traits, attributes, generics design. |
| [`doc/MACRO_RULES.md`](./doc/MACRO_RULES.md) | `macro_rules!` design. |
| [`doc/plan.md`](./doc/plan.md) | Original implementation plan / architecture. |
| [`doc/integration.md`](./doc/integration.md) | Using Cx from a C build + editor setup. |
| [`doc/REQUIRED_FEATURES.md`](./doc/REQUIRED_FEATURES.md) | What Cx still lacks for low-level C / klib. |
| [`doc/origin.md`](./doc/origin.md) | Initial assumptions & the approach decision. |
