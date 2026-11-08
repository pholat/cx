# Cx — Roadmap

> Living document. Update **this file** (not `technical.md`) when priorities or
> the design direction change. `technical.md` §8 is just a pointer here.
>
> Related: [`technical.md`](./technical.md) (implementation map),
> [`language.md`](./language.md) (language surface),
> [`generics.md`](./generics.md) (traits/attributes/generics rationale),
> [`macro-rules.md`](./macro-rules.md) (`macro_rules!` reference),
> [`required-features.md`](./required-features.md) (remaining C/klib gaps),
> [`changelog.md`](./changelog.md) (session-by-session history).

## Status

Implemented: **C interop** (`raw { … }`, `#include`, `NAME!(…)` passthrough),
**static traits** (`trait`/`implements`), **attributes** (`#[…]`),
**`macro_rules!`** (item/statement/expression position, repetitions, `##`,
`requires`, `$x:method`, imports), **checked trait requirements** (structs,
primitives, function/method bindings), the **paired `.hx`/`.cx` model**,
**`auto` return types**, **`.`/`->` method calls** (receiver-type tracking),
the **klib-backed container layer** (`std_*.hx` modules, owning variants,
teardown), **macros across `import` boundaries**, **`do…while`** and the
**conditional `?:`**, and the **LSP** navigation/hover/completion/signature/
occurrence features.

For the dated details of any item, see [`changelog.md`](./changelog.md). This
file keeps only what is **next**.

## Ordered items

Roughly prioritised; themes are not strict serial order. Detail for each theme
is in **Refinement** below.

| # | Item | Area |
| --: | :-- | :-- |
| 1 | **Chained receiver calls** (`a.b().c()`) and **computed receivers**. | Receiver types |
| 2 | **Trait associated types/constants** (`trait Iterable { type Item; … }`), then checked `requires` for them. | Traits |
| 3 | **Macro hygiene** (fresh-name renaming) and **non-greedy fragments** (so `($k:expr => $v:expr)` can be written). | Macro system |
| 4 | **Cross-file (workspace) `references`** and a **binding-aware `rename`**. | LSP |
| 5 | **`for!` over containers** — an iterator protocol lowering to `at`/`len` (or a `next` method) trait members. | Containers / stdlib |
| 6 | **More macro fragments** (`$x:block`, `$x:item`) and **nested repetitions** (`$( $( … )* )*`). | Macro system |
| 7 | **Doc comments on macro definitions** (`/// …`) for hover, and **LSP indexing of macro definitions**. | Macro system |
| 8 | **More traits**: supertraits and default method bodies; postfix vtable calls (`x.T->m(a)` / `x.m(a)`); derived/blanket implementations; receiver forms other than `self`. | Traits |
| 9 | **LSP polish**: read/write `documentHighlight` kinds; `semanticTokens`; inlay hints; doc comments on **all** hover targets; `codeAction` "implement missing trait method"; `positionEncoding` (UTF-16 columns); workspace symbols; call hierarchy. | LSP |
| 10 | **Language surface**: the comma operator; `static`/`extern`/`inline` specifiers; array types; **multiple value return/unpacking** (`(a, b) = f();`); **scoped imports**; `defer`. | Language surface |
| 11 | **Tooling / infrastructure**: a **compile database** (`compile_commands.json`); **golden-output tests** (diff the generated C, not just "does it compile") plus fuzzing; `#line`/debug-info polish and a CI target; **paired `.hx`/`.cx` follow-ups** (out-of-line ctor/dtor definitions and `.c`-only helper prototypes). | Tooling |
| 12 | **Considered extensions (not committed)** — see the section at the end. | Extensions |
| 13 | **Struct-keyed maps** (user-supplied hash/equal pair) and a real **capacity/growth policy** for the container layer; then a `std` overview. | Containers / stdlib |

## Known issues

- **`gd` on a macro invocation inside an imported `.hx` module.** In
  `tests/golden/std_demo.cx`, `gd` on `define_list!` should jump into
  `std_list.hx`; `define_int_map!` can report `Invalid cursor line: out of …`,
  and `std_list.hx` does not jump to itself. The `CxMacroMap` records the
  definition span but not reliably the **source file** for imported
  definitions. See *LSP* in Refinement.

## Refinement

### Receiver types → `.`-method calls

Landed: the transpiler lowering (block-scoped environment; `self` bound in
constructor/destructor/`implements` bodies) and the LSP tooling (dot-completion
for methods **and** fields, hover, `gd`, signature help). Remaining: chained
`a.b().c()` and computed receivers. `src/sema/scope.c` is still the scaffold;
the current environment lives in `resolve_receivers`
(`src/sema/symbols.c`).

### Containers / stdlib

- **Struct-keyed maps + capacity policy.** String keys and `Set` landed
  (`std_strmap.hx`, `std_set.hx` / `std_strset.hx`); remaining are struct keys
  (which need a user hash/equal pair) and a real capacity/growth policy.
  `requires` bounds on primitives are checked against the built-in capability
  traits (`Hashable`, `Equatable`, `Comparable`, `Ordered`).
- **Infer the trait from the object** — generate `($x).<Trait>::<member>` from a
  `requires $x: A + B` clause so the call site can be `both_total!($x)`, with
  `A::a` reserved for ambiguous cases.
- **`for` over containers** — an iterator protocol lowering to `at`/`len` (or a
  `next` method) trait members.

### Macro system

- **Hygiene** — body-introduced identifiers are emitted verbatim; add fresh-name
  renaming (`__cx_m_<n>`).
- **Non-greedy fragments** — `$x:expr` / `$x:ty` capture to the next top-level
  `,` / `;`, so `($k:expr => $v:expr)` cannot be written. A backtracking /
  balanced matcher (or a must-not-be lookahead) fixes composition.
- **Nested repetitions** — only single-level `$( … )*` / `$( … )+` is modelled;
  full recursion through `match_rep` / `transcribe` should support
  `$( $( … )* )*`.
- **More fragments** — `$x:block`, `$x:item`.
- **Doc comments on macro definitions** (`/// …`) captured by the expander and
  surfaced for hover; **LSP indexing of macro definitions** (currently gone
  before the AST, so `documentSymbol` / rename cannot see them).

### Traits

- **Associated types/constants** — needed by real generic containers
  (`trait Iterable { type Item; … }`); `check_macro_requires` and the vtable
  emitter would carry them.
- **Supertraits and default method bodies**
  (`trait X: A + B { int f(self) { … } }`).
- **Postfix vtable calls** (`x.T->m(a)` / `x.m(a)`) — removes the
  function-pointer-local boilerplate.
- **Derived/blanket implementations** (e.g. auto-`Equatable` for POD structs).
- **Receiver forms other than `self`** (`&self`, `self_`), which interacts with
  move semantics.

### LSP

- Fix **`gd` on macro invocations in imported `.hx` modules** (Known issues):
  record and use the definition's source file.
- Richer `documentHighlight` kinds (read/write), workspace-wide (cross-file)
  `references`; `semanticTokens`, inlay hints.
- Chained receiver calls (`a.b().c()`) and computed receivers.
- Doc comments (`//`, `///`) attached to **all** hover targets, not just trait
  methods.
- `codeAction` "implement missing trait method" from the trait's required set.
- `positionEncoding` negotiation (UTF-16 columns); workspace symbols; call
  hierarchy.

### Language surface (also see `required-features.md`)

- The comma operator, `static`/`extern`/`inline` specifiers, array types.
- **Multiple value return/unpacking** (`(a, b) = f();`) — lowering to a struct
  return plus per-field unpacking.
- **Scoped imports** (names only for a scope).
- `defer`.

### Tooling / infrastructure

- **Compile database** (`compile_commands.json`) emitted alongside generated C,
  so clangd resolves the transpiled output.
- **Golden-output tests** (diff the generated C, not just "does it compile"),
  plus fuzzing `cx_macro_expand` / the parser.
- `#line`/debug-info polish and a CI target.
- **Paired `.hx`/`.cx` follow-ups**: an out-of-line constructor/destructor
  definition (so the `.hx` can declare `Counter(int);` / `~Counter();` without a
  body and the `.cx` provide it), and inter-procedural `.c`-only declaration
  handling (prototypes for helpers the header does not declare).

## Considered extensions (not committed)

- **Multiple value return and unpacking** — return and destructure several
  values, e.g. `(a, b) = f();` or `a, b := f();`. A natural lowering is a struct
  return plus per-field unpacking. For the struct form, something like
  `a,b := struc_sub[field-name, field-name]` collides with standard `[]`
  unpacking, so a spelling such as `a,b := struc_sub=>[field-name, field-name]`
  is more attractive.
- **Scoped imports** — an `import` that introduces names only for a scope
  (block / function / namespace) instead of file-wide.
- **Compile database (`compile_commands.json`)** — emit include paths, flags and
  one entry per generated file so clangd and other C tooling resolve the
  transpiled output; could also help the Cx LSP discover module roots and import
  paths.
- **Static map** — constant-size map up to *n* elements that uses the whole
  required space but is always usable via index. Optional built-in length check.
  Useful for typical switch-case table-dispatch scenarios.
- **AVL/RB map** — for anything that provides an interface.
- **`#[cast]` / `cast[type]`** — syntax sugar for a C cast, because the standard
  C cast is not very readable. Consider an optional second parameter with a
  compile-time check.
- **FSM syntax** — `StateA -> StateB : Event [Guard] / Action`, generating:
  1. an enum with states; 2. an enum with events; 3. an SM struct with guard
  function pointers, action function pointers and a context pointer; 4. the SM
  struct providing `process_event`, `reset`, `get_state`.
- **`#[str]`** — automatically create a `c_str` function for an enum that
  returns a static string per element via a `switch` (and `""` as fallback).
- move semantics for real RAAI - but I've no idea how to make it plain c transpilable
  at the moment.
