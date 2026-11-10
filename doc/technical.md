# Cx — Technical Notes

> **Living document.** Read this first when starting a session. At the end of
> every session, update [`language.md`](./language.md) / §4 below,
> [`roadmap.md`](./roadmap.md), and append to [`changelog.md`](./changelog.md).
> Keep it accurate — it is the map of the codebase.
>
> Doc filenames are **lowercase kebab-case**; use that style for anything new.

Related docs:

- [`../readme.md`](../readme.md) — entry point and architecture diagram
- [`language.md`](./language.md) — **the language surface** (syntax → lowering)
- [`roadmap.md`](./roadmap.md) — priorities and the refinement backlog
- [`changelog.md`](./changelog.md) — session log / history
- [`generics.md`](./generics.md) — traits, attributes, generics rationale
- [`macro-rules.md`](./macro-rules.md) — full `macro_rules!` reference
- [`lsp.md`](./lsp.md) — language-server features
- [`integration.md`](./integration.md) — using Cx from a C build + editor setup
- [`required-features.md`](./required-features.md) — remaining low-level C gaps

---

## 1. Overview

Cx is a thin extension over C adding **namespaces**, **RAII** (struct
constructors/destructors), **static traits**, **attributes**, and
**`macro_rules!`** macros. It is transpiled to **portable C** that any normal
toolchain (GCC, Clang, `arm-none-eabi-gcc`) compiles. The architecture is a
**Flex/Bison source-to-source transpiler** with a single shared AST + semantic
layer consumed by **both** the C code generator and the LSP server.

Output relies on `__attribute__((cleanup(fn)))` (GCC/Clang).

### Why this architecture

The early design notes (approach choice) are folded into this summary:

- **New syntax, not C syntax.** Cx adds `namespace`, `~Dtor()`, `trait`,
  `implements`, `#[…]` and `macro_rules!`. Clang's C grammar cannot parse those,
  so a Clang-based frontend would mean forking the parser.
- **The parser is the LSP.** The LSP needs a reentrant parser, exact locations
  on every node, and error recovery that yields a partial AST on every edit.
  Bison provides `%locations`, `%define api.pure full`, `%define parse.lac full`
  and `error` productions directly.
- **One grammar, no drift.** Lexer, parser, AST and sema are shared verbatim by
  codegen and the LSP, so editor diagnostics cannot diverge from the transpiler.
- **Portable output.** Codegen emits standard C using
  `__attribute__((cleanup(fn)))`; the result is not tied to a compiler version.
- **Clang stays optional** — a semantic verifier after transpilation, never the
  frontend.

---

## 2. Repository layout

```
cx/
├── readme.md                   # entry point + architecture diagram
├── doc/                        # all documentation (lowercase kebab-case)
│   ├── technical.md            # this file: implementation map
│   ├── language.md             # language surface (syntax → lowering)
│   ├── roadmap.md              # priorities + refinement backlog
│   ├── changelog.md            # session log
│   ├── generics.md             # traits/attributes/generics rationale
│   ├── macro-rules.md          # macro_rules! reference
│   ├── lsp.md                  # LSP features
│   ├── integration.md          # C build + editor integration
│   └── required-features.md    # remaining low-level C gaps
├── Makefile, .gitignore, .clangd
├── grammar/
│   ├── lexer.l                 # Flex scanner (reentrant, bison-bridge/locations)
│   └── parser.y                # Bison grammar (pure, LAC, AMBIGUITY notes)
├── include/cx/                 # public headers
│   ├── span.h   ast.h   buf.h   diag.h
│   ├── parser.h                # CxParseCtx, CxTypeSet, cx_parse
│   ├── macro.h                 # macro_rules! expansion (pre-parse pass)
│   ├── symbols.h codegen.h lsp.h cx.h (umbrella)
├── src/
│   ├── ast.c buf.c diag.c typeset.c macro.c
│   ├── sema/   symbols.c scope.c raii.c
│   ├── codegen/ mangle.c line_map.c c_emitter.c
│   ├── lsp/    json.{h,c} lsp_internal.h transport.c documents.c server.c
│   │           features/{diagnostics,symbols,definition,hover,completion,
│   │                     signature,occurrences,nav}.c
│   └── cli/    main.c
├── tests/
│   ├── run.sh                  # transpile + compile every golden input
│   ├── golden/                 # *.cx, *.hx, *.fail.cx, std_*.hx modules
│   └── example/                # standalone runnable app
└── submodules/klib/            # git submodule (header-only C containers)
```

Generated C and objects go to `build/` (git-ignored).

---

## 3. Build & test

Requires `cc`, `flex`, `bison`.

| Command | Effect |
| :-- | :-- |
| `make` | build `./cx` |
| `make test` | transpile every `tests/golden/*.cx` and compile the result |
| `make clean` | remove `build/` and `cx` |

The Makefile regenerates `build/parser.{c,h}` and `build/lexer.c` from the
grammar, and uses `-MMD -MP` so editing `include/cx/*.h` triggers rebuilds.

`tests/run.sh`:

1. transpiles every `tests/golden/*.hx` and `-fsyntax-only`-checks the header;
2. transpiles every `tests/golden/*.cx` (except `*.fail.cx`);
3. runs the negative `*.fail.cx` files and requires `cx build` to fail with a
   conformance diagnostic;
4. compiles every generated `.c` with `-I submodules/klib`.

### CLI

| Command | Effect |
| :-- | :-- |
| `cx build <file.cx\|file.hx> [-o <out>]` | transpile to C (stdout without `-o`) |
| `cx expand <file.cx>` | print the source after `macro_rules!` expansion |
| `cx dump <file.cx>` | print the AST |
| `cx lsp` | run the language server on stdio |
| `cx --version` | print version |

### LSP

Feature documentation lives in [`lsp.md`](./lsp.md); editor setup lives in
[`integration.md`](./integration.md). Update `lsp.md` after any LSP change.

---

## 4. Language reference

The complete implemented surface — namespaces, structs/RAII, types, expressions,
statements, preprocessor/C interop, traits, attributes, macros, headers/imports,
name mangling and "what is not an extension" — lives in
[`language.md`](./language.md). Update that file when the language changes.

This document only records the implementation seams:

- codegen passes and emission order: §5;
- sema passes and checked `requires`: §6;
- grammar/lexer traps and design decisions: §7;
- macro expansion internals: [`macro-rules.md`](./macro-rules.md);
- trait lowering and design rationale: [`generics.md`](./generics.md).

---

## 5. Code generation

`src/codegen/c_emitter.c` emits a translation unit in **five passes** so the
dependency order is valid C:

0. **preamble** — trait vtable types (`struct T__vtable`), verbatim `raw { … }`
   blocks, and C macro invocations;
1. **struct definitions** (nested types first) and typedefs;
2. **function + constructor + destructor prototypes** (plus impl-method
   prototypes in `.c` output);
3. **destructor + constructor helpers**, preceded by impl methods and their
   `static const` vtables;
4. **function bodies + globals**.

Details:

- Struct tags are the mangled names (`struct ns__Foo`).
- In ctor/dtor bodies, a bare identifier that names a field is emitted as
  `self->field`.
- `#line N "file.cx"` is emitted before declarations so downstream compiler
  errors and GDB map back to the Cx source.
- Destructor helpers have **external linkage** (`void T__dtor(struct T *)`), so
  a generated header can declare them and a consumer TU can link the `.c`.
- Headers use `cx_emit_h` (phases 0–2) wrapped in an include guard. The vtable
  *types* and the struct's trait pointer fields are declared, but the `static`
  impl methods/tables are `.c`-only.
- `raw { … }` is emitted verbatim (first line indented; the rest kept as
  written). A top-level `raw`/macro line goes in phase 0.
- **Paired `.c`** (`foo.cx` with a sibling `foo.hx`): `cx_emit_c` prefixes the
  output with `#include "foo.h"` and skips phases 0–1. Phase 2 and phases 3–4
  are still emitted.
- In an `implements` method body the receiver is a local
  `struct T *self = self_; (void)self;`.
- Attributes are emitted by `emit_attr_leading` (before the return type) and
  `emit_attr_trailing` (after the declarator / struct body). `#[export]` and
  `#[cleanup]` are resolved through the symbol table rather than emitted.
- **Enums** (`emit_enum_def`, phase 1) become `typedef enum <tag> { … } <tag>;`;
  the tag is namespace-mangled (via the `CX_NODE_ENUM` symbol) and the
  enumerators are prefixed with the *bare* enum name (`E__A`), so the
  `E::A` → `E__A` spelling resolves. `#[str]` adds `E__c_str(E)` (prototype in
  phase 2, body in phase 4).
- **State machines** (`emit_fsm_def`/`emit_fsm_protos`/`emit_fsm_funcs`) parse
  the verbatim `sm` body (`fsm_parse`; line/block comments are stripped) and
  emit `<M>__State`/`<M>__Event` enums (values `<M>__state__<s>` /
  `<M>__event__<e>`), the guard/action callback typedefs (`<M>__Guard` =
  `int (*)(<M>*, <M>__State from, <M>__State to, <M>__Event event)`,
  `<M>__Action` = `void (*)(…)`), the machine struct (`state`, `void *ctx`, one
  `guard_<g>` / `action_<a>` field), the `<M>__init` / `__reset` /
  `__get_state` (returns `<M>__State`) / `__process_event` (takes `<M>__Event`)
  functions. An explicit `#[str] enum <M>::State;` / `#[str] enum <M>::Event;`
  marker (`CX_NODE_ENUM_STR`, `emit_fsm_cstr_proto`/`emit_fsm_cstr_def`) is
  required to get the `c_str` name helpers, so they are opt-in and emitted where
  the user asked (after the machine declaration, before the functions). The
  `.cx` file names the enums in Cx syntax (`<M>::State`, `<M>::Event`) via the
  `type: TYPE_NAME SCOPE IDENT` rule; the parser's `sm_block` action registers
  only `<M>` in the lexer type set. Sema registers the four API functions as
  **synthetic function symbols** (kind `CX_NODE_FUNCTION`, bare C names), so
  `m.process_event(ev)` lowers to `Machine__process_event(&m, ev)`. All FSM C
  names are bare (not namespace-mangled); `emit_type` emits the machine name
  as-is.
- File-scope constructor calls are not supported (emitted as a comment).

---

## 6. Semantics & diagnostics

`src/sema/symbols.c` (`cx_sema_build`) walks the tree building a symbol table
(structs, enums, state machines, fields, functions, globals, traits) with
namespace-qualified names, and:

- enforces **dtor/ctor name == struct name** and rejects duplicates;
- `check_calls` rejects a call whose callee is a struct with no constructor;
- `resolve_receivers` **lowers `.`/`->` method calls**: it keeps a
  **block-scoped** environment (parameters + locals, pushed/popped per block)
  and rewrites `recv.method(args)` whose receiver has a known struct type into
  the associated call `Type__method(&recv, args)`. It resolves the receiver
  **first**, and `expr_type` types chained/computed receivers (`a.b().c()`,
  `make().m()`, `arr[i].m()`) from call return types, struct members and
  subscripts, so those lower too. Constructor/destructor/`implements` method
  bodies are walked with `self` bound to the enclosing struct, and struct fields
  are kept out of the local scope. It runs between two `infer_auto` passes (a
  silent one first, a reporting one after);
- `lower_unpack` (called from `resolve_receivers`) rewrites a `CX_NODE_UNPACK`
  (`auto [a, b] = expr;` / `auto [a, c] = expr=>[a, c];`) into a block: a
  temporary of the source struct plus one typed declaration per target. The
  source type comes from `expr_type` (so locals/params work); positional targets
  take fields in declaration order, `=>[…]` selects by name;
- `check_macro_requires` verifies **checked `requires`**: for each binding the
  macro expander recorded on the TU (`CxMacroMap`), the required trait must
  exist, and then:
  - a binding that resolves to a **struct** must have a matching `implements`
    block;
  - a **primitive** binding (`int`, `unsigned int`, `const char`, `float *`, … —
    `is_primitive_type`) is checked against a fixed set of built-in capability
    traits (`Hashable`, `Equatable`, `Comparable`, `Ordered`);
  - an **`ident`/`path`** binding that names no struct is a function binding and
    must name one of the trait's required methods;
  - anything else (a C type Cx does not know) is not checkable and is skipped.

Negative tests: `tests/golden/macro_requires_fail.fail.cx` (struct),
`macro_requires_method_fail.fail.cx` (method),
`primitive_requires_fail.fail.cx` (primitive); positive:
`tests/golden/primitive_requires.cx`.

The LSP diagnostics path runs `cx_parse` **and** `cx_sema_build`, so editor
diagnostics match the transpiler. `CxSpan` is 1-based inclusive; the LSP
converts to 0-based, end-exclusive ranges.

---

## 7. Design decisions & gotchas

Read this before changing the grammar/lexer.

**Conventions.**

- **Prefer Cx syntax in `.cx` / `.hx` files.** Write the Cx spelling, not the C
  it lowers to: `cast[T](x)` over `(T)x`, `Type::member` / `Type::sub::member`
  over a mangled `Type__member`, `.`/`->` method calls over the associated
  `Type__m(&x)` form, a generated `Type::c_str` over a hand-written `switch`,
  Cx-qualified types (`Type::Sub`) over raw `Type__Sub` names, and so on. The
  generated C is an implementation detail of the transpiler, not the language.
- **Prefer traits in `macro_rules!`.** When a macro body depends on an operation
  an argument type must provide, express it as a trait and attach it with a
  `requires $x: Trait` clause (checked after expansion) instead of assuming the
  operation exists.

- **Type-name lexer hack** (`CxTypeSet`, `src/typeset.c`). To disambiguate
  `T *x` from `a * b`, the lexer returns `TYPE_NAME` for identifiers in a known
  set (built-ins + struct names). Struct names are registered by a **mid-rule
  action** in `struct_decl`, before the body, so self-references work. An
  identifier that shadows a type name is mislexed; types are single tokens.
- **Dangling `else`** is resolved with `%prec LOWER_THAN_ELSE` vs `%nonassoc
  ELSE`.
- **Unary vs binary** `*`/`&` and pointer `type '*'` live in different parser
  states; unary precedences are `UDEREF`/`UADDR`, postfix is `POSTFIX`.
- **Prelude lifecycle**: preprocessor lines are accumulated in
  `CxParseCtx.prelude` (`CxBuf`), attached to the TU node, and emitted at the
  top; the buffer is freed in `cx_parse`.
- **Locations** are tracked explicitly in the lexer (`ctx.line`/`column`, newline
  counting in `YY_USER_ACTION`) — do not rely on Flex `yylineno`.
- **No move semantics.** Returning an RAII local by value runs its destructor, so
  factories returning destructible handles are unsafe. klib wrappers avoid this
  by not using a destructor (manual `free`).
- **Default construction.** `T v;` does **not** call a zero-arg constructor; only
  `T v(args);` / `T v = T(args);` do.
- **No implicit destructor.** If a struct embeds a destructible member but
  declares no `~T()`, member dtors are not called (no place to hook them).
- **`auto` inference** is a sema pass (`infer_auto`) that rewrites the var's
  type in place. It resolves call return types (including `auto` functions whose
  type was already inferred), constructor calls, literals, strings, parens,
  casts, function parameters, comparison/logical operands (`int`), binary
  arithmetic/bitwise operands (the left operand's type), and identifiers of
  symbols in the table (globals/functions); anything else reports
  *cannot infer type for 'auto'*. Locals are not in the table. An `auto`
  function's return type is inferred from the first `return expr;`.
- **Receiver types / `.`-method calls.** `resolve_receivers` keeps a
  **block-scoped** environment; only an `IDENT` receiver is resolved this way —
  use the associated `Type::method(…)` form for a computed receiver. An
  unresolved method call is emitted verbatim, so a call through a C
  function-pointer field keeps working. The grammar distinguishes the method call
  from field access by the `(` after the member.
- **Typedef aliases** are emitted verbatim and registered in the lexer type set,
  but struct-specific behaviour (cleanup, member ctor/dtor calls, `auto`
  inference) keys off the **struct name**, not the alias — aliasing a
  destructible struct loses its RAII.
- **Multi-word types** come from `type TYPE_NAME` (concatenation), so any two
  type words combine (`int int` is accepted by the parser and left to the C
  compiler to reject).
- **Keyword style**: Cx-introduced keywords are **lowercase full words** — no
  abbreviations (`implements`, not `impl`; `function`, not `func`). C keywords
  keep their C spelling. There is **no inheritance**; abstractions are built from
  an explicit `implements Trait { … }` block inside a struct — see
  [`generics.md`](./generics.md).
- **Imports** (`import "mod.hx";`) use a nested `parse_source` that shares the
  same `CxParseCtx` but saves/restores the per-parse fields. Paths resolve
  relative to the importing file; cycles are skipped via `ctx->active`.
  Diagnostics raised while parsing an import are attributed to the importing
  file. The macro pass separately collects `macro_rules!` definitions from
  imported files (recursively, cycle-bounded). A `.cx` is *paired* with a
  sibling `.hx` of the same basename (detected in the CLI, not the parser).
- **Verbatim capture** (`raw { … }`, `NAME!( … )`) uses two Flex start
  conditions (`RAW`/`MACRO`) that copy balanced `{}`/`()` into a buffer; the text
  is copied into the arena when the token is produced, so it survives LAC
  lookahead. `raw` is a block only when followed by `{`, and `NAME!(…)` requires
  the `!` immediately after the identifier. The C `NAME!( … )` passthrough is
  file/statement position only; Cx `macro_rules!` invocations work in
  expression position too (they are expanded before parsing).
- **Traits** are static and check-only: `implements T { … }` is verified against
  the trait's members (names + signatures; the `self` receiver is ignored). No
  postfix-call syntax exists, so a call *through* the vtable needs a
  function-pointer local. The table pointer is installed by the ctor or by a
  statement emitted after a default declaration `T v;` (and by a designated
  initializer for globals); a `malloc`'d struct is the user's responsibility.
- **Attributes** (`#[…]`) are captured by a Flex `ATTR` start condition; the
  preprocessor-line rule excludes `#[`. Leading (`inline`/`noreturn`) become
  specifiers; anything else becomes `__attribute__((…))`. `export`, `cleanup`
  and `test` are front-end only; `ctor`/`dtor` are rejected with an error.
  Attributes are accepted before external declarations and struct members, not
  statements.
- **Macro expansion is a text pass before parsing** (`src/macro.c`). It
  tokenises the source, removes `macro_rules!` definitions, and substitutes
  matching invocations; expansion is iterative (max 64 passes). Fragments are
  `ident`/`field`, `tt`, `item` (a balanced group or `;`-terminated run),
  `block` (a `{ … }`), `lit`, `path`, `method`, `ty`, `expr`, `stmt`.
  Repetitions capture an **index path** (`MBind.paths`/`depths`, `MEnv.path`) so
  nested `$( $( … )* )*` transcribes the right per-iteration binding. A
  repetition is counted from the metavariable that has captures at its depth, so
  a body may name an outer metavariable (`$k $slot##_key;`) before the repeated
  one (`trans_count`). `raw { … }`, preprocessor lines and `#[…]` attributes are
  opaque to the expander. The expander records generated-name origins and
  invocation sites in a `CxMacroMap` on the TU, **including the defining file**
  (so `gd` on an invocation of an imported macro lands in the importing module).
  Consequences and limits are documented in [`macro-rules.md`](./macro-rules.md)
  §3–§4.
- **Enums** register their name in the lexer type set (a mid-rule action, like
  structs) and emit `typedef enum …`; enumerators are prefixed with the *bare*
  enum name so the `E::A` → `E__A` spelling resolves in any namespace.
  **`sm`** blocks are captured verbatim by an `SMCAP` Flex start condition
  (mirroring `raw`) and parsed by codegen; the machine name is registered as a
  type. **`cast[T](expr)`** is a parser-only sugar that builds the existing
  `CX_NODE_EXPR_CAST`. `cast`, `enum` and `sm` are reserved keywords.
- `Type::member` and `Type::sub::member` in expression position lower to the
  identifiers `Type__member` / `Type__sub__member`; there is no shift/reduce
  conflict because `(`/`::` never follow an `expr`. They resolve to enum
  members, generated container names or the `sm` event constants
  (`Machine::event::start`); a 3-part form with `(…)` is a call
  (`Machine::State::c_str(x)`). `Type::Sub` is also a **type** (the
  `type: TYPE_NAME SCOPE IDENT` rule, e.g. `Machine::State`). To keep the shared
  `'('` state unambiguous (a qualified name is both a type and an expression),
  casts/`sizeof` use a separate `cast_type` nonterminal that excludes the
  qualified form — cast to a qualified type with `cast[T](x)`.
- Bison blocks are written for **GNU Bison 3.8**; the parser is pure/reentrant
  and the scanner is `reentrant bison-bridge bison-locations`.
- In the Neovim syntax file, `~` must be escaped (`\~`) — `~` is the
  "previous substitute string" atom.

---

## 8. Roadmap

See [`roadmap.md`](./roadmap.md) (including the "Refinement" backlog). Update it
there so roadmap changes do not churn this file.

---

## 9. Changelog

The session log lives in [`changelog.md`](./changelog.md). Append a dated entry
there at the end of every session.

---

## 10. Maintaining this document

At the end of a session:

1. Append a dated entry to [`changelog.md`](./changelog.md).
2. Move finished items out of [`roadmap.md`](./roadmap.md), and add any new
   gaps/gotchas to §7.
3. Update [`language.md`](./language.md) when the language surface changes; if
   affected, update §5 (codegen) and §6 (semantics) here.
4. Keep [`roadmap.md`](./roadmap.md) and
   [`required-features.md`](./required-features.md) consistent.
5. Keep doc filenames **lowercase kebab-case**.
