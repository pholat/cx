# Cx — Roadmap

> Living document. Update **this file** (not `TECHNICAL.md`) when priorities or
> the design direction change. `TECHNICAL.md` §8 is just a pointer here.
>
> Related: [`TECHNICAL.md`](./TECHNICAL.md) (implemented language/codegen/sema
> details), [`GENERICS.md`](./GENERICS.md) (traits/attributes/generics design),
> [`MACRO_RULES.md`](./MACRO_RULES.md) (`macro_rules!` design),
> [`REQUIRED_FEATURES.md`](./REQUIRED_FEATURES.md) (C/klib gaps),
> [`changelog.md`](./changelog.md) (history).

## Status

Done: **C interop** (`raw { … }` + `#include` + `NAME!(…)` macro invocation
passthrough), **static traits** (`trait`/`implements`), **attributes** (`#[…]`),
**`macro_rules!`** (item + expression-position expansion, see `TECHNICAL.md` §4),
**checked trait requirements in macros** (`requires`, plus the `$x:method`
`Trait::member` fragment), and the **LSP navigation/hover/completion** work for
those (macro origins, `Type::method`, `Trait::method` + docs). See the Done list
below.

## Ordered items

1. **klib-backed containers** — `List<T>` / `Map<K,V>` on klib, via macros or
   (later) templates.
   * **Done:** `tests/golden/klib_list.cx` (`IntList` over `klist.h`),
     `tests/golden/klib_map.cx` (`IntMap` over `khash.h`, with a
     `CX_KHASH_MAP_HELPERS` C macro for typed accessors over a `void *` handle),
     and `tests/golden/klib_bag.cx` (element-typed `define_bag!($name, $t)
     requires $t: Drop`). All use the **associated call** syntax
     (`IntList::new()`, `IntMap::put(&m, k, v)`, …), and `tests/run.sh` adds
     `submodules/klib` to the include path. Jump-to-definition / hover for the
     container methods works (macro origins + `Trait::method`).
   * **Still needed:** an element free/RAII policy that actually invokes the
     trait on teardown (`klib_bag.cx` checks the `Drop` bound but does not call
     `drop` from `free`); a user-facing stdlib module (blocked on macro
     definitions crossing `import` boundaries); `requires` bounds for primitive
     key/value capabilities; and `.`-method calls (`l.push(…)`) — see item 1 of
     the Refinement backlog.
2. A paired `.hx`/`.cx` model (the `.c` includes its header and skips duplicate
   declarations).

## Done

* **Traits in macros — checked `requires`**: after expansion/parse/sema, each
  `requires` binding recorded at an invocation site is verified. A **type** bound
  to the metavariable must implement every required trait (the trait must exist);
  an **`ident`/`path`** binding that names no struct is a function and must name
  one of the trait's methods (`requires $a: A` for the function `a` passed as
  A's method). Built-ins/unknown types are skipped. Positive tests:
  `tests/golden/traits_make_total.cx` (`make_total!(total, Row)`; `Row: Iterable`)
  and `tests/golden/macro_requires.cx` (`both_total!($x, A::a, B::b)`;
  `$a: A, $b: B`). Negative tests: `tests/golden/macro_requires_fail.fail.cx`
  (struct) and `tests/golden/macro_requires_method_fail.fail.cx` (method); `cx
  build` must fail on `*.fail.cx`.
* **`Trait::member` references / LSP for macros.** The `$x:method` fragment
  matches `Trait::member`, is checked, and expands to `Trait->member`. The LSP
  resolves `gd`/hover on a `Type::method` or `Trait::method` pair, on a macro
  invocation, and shows the `//` doc comment above a declaration.

## Refinement (what can be added next)

Themes are roughly prioritised; see "Suggested order" at the end.

### Receiver types → `.`-method calls (highest leverage)

Today `Type::func(&l, …)` is the only method spelling because the symbol table
has no locals. Adding a scope/type environment in `src/sema/scope.c` (locals,
params, and their declared/inferred types) lets `l.push(10)` lower to `l`'s
type's `push`. This is the single highest-leverage change: it also unlocks
dot-completion, hover on a receiver, and `->` methods. Ripples: `src/sema/
symbols.c`, the grammar (`expr '.' IDENT`), the emitter's call resolution, and
the LSP.

### Containers / stdlib

* **Element free/RAII policy** — `klib_bag.cx` checks `requires $t: Drop` but
  `free` never calls `drop`; iterate the list and invoke each element's method
  through the vtable, and mirror it for `Map` values.
* **Macro definitions across `import` boundaries** — each file is expanded alone
  (`cx_macro_expand` per `parse_source`), so a `.hx` cannot export a macro. A
  shared macro table (or a pre-pass that collects `macro_rules!` from imported
  files) is the prerequisite for a real `std` module.
* **Infer the trait from the object** — once receiver types exist, generate
  `($x).<Trait>::<member>` from a `requires $x: A + B` clause so the call site
  can be `both_total!($x)` again, with `A::a` reserved for the ambiguous cases.
* **`Map` variants** for string/struct keys (`KHASH_MAP_INIT_STR`), and a real
  capacity/size policy; `Set<T>`.
* **`for` over containers** — an iterator protocol lowering to `at`/`len` (or a
  `next` method) trait members.

### Macro system

* **Hygiene** — body-introduced identifiers are emitted verbatim; add fresh-name
  renaming (`__cx_m_<n>`) so a macro's `$t *tmp` can't capture a caller's name.
* **Non-greedy fragments** — `$x:expr`/`$x:ty` capture to the next top-level
  `,`/`;` (`TECHNICAL.md` §7c), so `($k:expr => $v:expr)` can't be written. A
  backtracking / balanced matcher (or a must-not-be lookahead) fixes composition.
* **Nested repetitions** — only single-level `$( … )*`/`$( … )+` is modelled;
  full recursion through `match_rep`/`transcribe` supports `$( $( … )* )*`.
* **More fragments** — `$x:block`, `$x:item`.
* **Doc comments on macro definitions** (`/// …`) captured by the expander and
  surfaced for hover; **LSP indexing of macro definitions** (currently gone
  before the AST, so `documentSymbol`/rename can't see them).

### Traits

* **Associated types/constants** — needed by real generic containers
  (`trait Iterable { type Item; … }`); `check_macro_requires` and the vtable
  emitter would carry them.
* **Supertraits and default method bodies**
  (`trait X: A + B { int f(self) { … } }`).
* **Built-in impls for primitives** so `requires $k: Hashable` on `int` isn't
  skipped.
* **Postfix vtable calls** (`x.T->m(a)` / `x.m(a)`) — removes the
  function-pointer-local boilerplate (`TECHNICAL.md` §4).
* **Derived/blanket implementations** (e.g. auto-`Equatable` for POD structs).
* **Receiver forms other than `self`** (`&self`, `self_`), which interacts with
  move semantics.

### LSP (builds on receiver types)

* `textDocument/signatureHelp` for `Type::method(...)` and trait methods.
* `.`/`->` member completion and hover on receivers.
* `textDocument/references`, `rename`, `documentHighlight`, `semanticTokens`,
  inlay hints.
* `codeAction` "implement missing trait method" from the trait's required set.
* Doc comments (`//`, `///`) attached to **all** hover targets, not just trait
  methods.
* `positionEncoding` negotiation (UTF-16 columns); workspace symbols; call
  hierarchy.

### Language surface (also see `REQUIRED_FEATURES.md`)

* `do…while`, ternary `?:`, the comma operator, `static`/`extern`/`inline`
  specifiers, array types.
* **Multiple value return/unpacking** (`(a, b) = f();`) — lowering to a struct
  return plus per-field unpacking (see "Considered extensions").
* **Scoped imports** (names only for a scope).
* `defer`.

### Tooling / infrastructure

* **Compile database** (`compile_commands.json`) emitted alongside generated C,
  so clangd resolves the transpiled output (see "Considered extensions").
* **Golden-output tests** (diff the generated C, not just "does it compile"),
  plus fuzzing `cx_macro_expand` / the parser.
* `#line`/debug-info polish and a CI target.
* The paired `.hx`/`.cx` model (ordered item 2).

### Suggested order

1. Receiver-type tracking (unblocks `.`-methods, dot-completion, hover).
2. Container teardown + `Map` variants, and macro definitions across imports.
3. Trait associated types/constants, then checked `requires` for them.
4. Macro hygiene + non-greedy fragments.
5. LSP signature help / references once receiver types land.

> **Ordering rationale.** Traits in macros (syntax, recorded requirements,
> textual member access, **and checked conformance**) are implemented, so
> container macros are written against the final spelling. The checked half is a
> first cut: it verifies concrete types that resolve to a struct and skips
> built-ins/unknowns. Deeper work (associated types/constants, resolving
> namespaced types, bounds for primitive elements) is folded into the container
> items above.

## Generics & macros (design note — under discussion)

> The type-abstraction direction is **not settled**. `macro_rules!` was chosen
> partly because C++ templates are hard to read; that rationale is being
> revisited in favour of a **traits + attributes + `macro!`** approach. See
> [`GENERICS.md`](./GENERICS.md) (traits/attributes/`macro!`) and
> [`MACRO_RULES.md`](./MACRO_RULES.md) (`macro_rules!`). These are different axes
> (type parameters, behaviour interfaces, metadata, generativity) and need not
> share one mechanism.

The question: support `List<T>` / `Map<K,V>` by exposing klib's macros directly,
or with a first-class Cx generics facility that *uses klib* as the backend?

* **A. C-macro passthrough (klib directly).** Ship `raw { … }` and let users call
  `KLIST_INIT` / `KHASH_MAP_INIT_INT`. Lowest effort, but `klist_t(name)` /
  `khash_t(name)` *types* can't be written in Cx (a type is a single token), the
  LSP can't see through macros, and errors are cryptic. Fine as an escape hatch,
  weak as the container story.
* **B. Template-like monomorphizing generics.** `struct List<T> { … }`,
  `List<int> l;`; collect instantiations and emit `List__int` (tag mangling
  already works this way). Type-safe, fully visible to the LSP, composes with
  ctors/dtors/namespaces/`auto`. Cost: parse `<T>` / `List<T>`, substitute types,
  collect instantiations.
* **C. Rust-style `macro_rules!`-like hygienic macros.** A declarative macro
  sub-language (patterns, repetition, hygiene) that can generate types *and*
  functions. Most expressive, but a whole language to design, implement, and
  surface to tooling.

**Draft recommendation (see [`GENERICS.md`](./GENERICS.md) §8).** Treat the axes
separately: adopt **attributes `#[…]`** early (metadata/codegen); add **`macro!`**
(generativity) per `MACRO_RULES.md`; **explicit static traits are implemented**
(`implements` block inside a struct → one pointer per trait to a shared static
per-type table, statically checked) — macros opt in with `requires`; keep klib as
the container backend. Option C (`macro_rules!`) is *a* mechanism, not the whole
plan; option B (template-like generics) is deferred until tooling/type-error
quality can be judged on real code. Exposing klib's macros directly (A) stays
confined to the `raw { … }` / invocation-passthrough escape hatch.

## Considered extensions (not committed)

* **Multiple value return and unpacking** — return and destructure several
  values, e.g. `(a, b) = f();` or `a, b := f();`. No grammar chosen yet; a
  natural lowering is a struct return plus per-field unpacking (leaning on the
  existing struct-by-value return support).
* **Scoped imports** — an `import` that introduces names only for a scope
  (block / function / namespace) instead of file-wide, so imported symbols do
  not pollute the whole translation unit. Would likely interact with the lexer
  type set and the symbol table's scope model.
* **Compile database (`compile_commands.json`)** — emit a compile database
  (include paths, flags, one entry per generated file) alongside the generated C,
  so clangd and other C tooling resolve the transpiled output; could also help
  the Cx LSP discover module roots and import paths.
