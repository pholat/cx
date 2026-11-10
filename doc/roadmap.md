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
**`macro_rules!`** (item/statement/expression position, repetitions incl.
**nested**, `##`, `requires`, `$x:method`, `$x:block` / `$x:item`, imports),
**checked trait requirements** (structs, primitives, function/method bindings),
the **paired `.hx`/`.cx` model**, **`auto` return types**, **multiple value
unpacking** (`auto [a, b] = f();`, `auto [a, c] = f()=>[a, c];`), **`.`/`->`
method calls** (receiver-type tracking, **chained and computed receivers**), the
**klib-backed container layer** (`std_*.hx` modules, owning variants, teardown,
**`std/std.hx` `for!`**), **macros across `import` boundaries**,
**`do…while`** and the **conditional `?:`**, the **LSP**
navigation/hover/completion/signature/occurrence features (read/write highlight
kinds, doc comments on hover, cross-open-document references/rename), plus
**enums** with **`#[str]`**, **`cast[T](expr)`**, **`sm` state machines**, and a
**`macro_rules!`-generated static map** (`std/static_map.hx`).

For the dated details of any item, see [`changelog.md`](./changelog.md). This
file keeps only what is **next**.

## Ordered items

Roughly prioritised; themes are not strict serial order. Detail for each theme
is in **Refinement** below.

| # | Item | Area |
| --: | :-- | :-- |
| 1 | **Macro hygiene** (fresh-name renaming) and **non-greedy fragments** (so `($k:expr => $v:expr)` can be written). | Macro system |
| 2 | **Doc comments on macro definitions** (`/// …`) for hover, and **LSP indexing of macro definitions**. | Macro system |
| 3 | **Default method bodies** (`trait X { int f(self) { … } }`). | Traits |
| 4 | **LSP polish (remaining)**: binding-aware `rename`; on-disk workspace indexing; `semanticTokens`; inlay hints; `codeAction` "implement missing trait method"; `positionEncoding` (UTF-16 columns); workspace symbols; call hierarchy. | LSP |
| 5 | **Language surface**: the comma operator; `static`/`extern`/`inline` specifiers; array types; **scoped imports**; `defer`. | Language surface |
| 6 | **Tooling / infrastructure**: **landed** (`cx compdb`, `cx new-feature`, `make fuzz`, `make ci`, `cx build -g` statement `#line`, paired out-of-line destructor + `.c`-only helper prototypes; the shared LSP position index is `src/lsp/features/nav.c`). | Tooling |
| 7 | **Considered extensions (not committed)** — see the section at the end. | Extensions |
| 8 | **Struct-keyed maps** (user-supplied hash/equal pair) and a real **capacity/growth policy** for the container layer; then a `std` overview. | Containers / stdlib |

## Refinement

### Containers / stdlib

- **Struct-keyed maps + capacity policy.** String keys and `Set` landed
  (`std_strmap.hx`, `std_set.hx` / `std_strset.hx`); remaining are struct keys
  (which need a user hash/equal pair) and a real capacity/growth policy.
  `requires` bounds on primitives are checked against the built-in capability
  traits (`Hashable`, `Equatable`, `Comparable`, `Ordered`).

### Macro system

- **Hygiene** — body-introduced identifiers are emitted verbatim; add fresh-name
  renaming (`__cx_m_<n>`).
- **Non-greedy fragments** — `$x:expr` / `$x:ty` capture to the next top-level
  `,` / `;`, so `($k:expr => $v:expr)` cannot be written. A backtracking /
  balanced matcher (or a must-not-be lookahead) fixes composition.
- **Doc comments on macro definitions** (`/// …`) captured by the expander and
  surfaced for hover; **LSP indexing of macro definitions** (currently gone
  before the AST, so `documentSymbol` / rename cannot see them).

### Traits

- **Default method bodies** (`trait X { int f(self) { … } }`).

### LSP

- **Binding-aware `rename`** and **on-disk workspace indexing** (`references` /
  `rename` currently cover open documents and are lexical).
- `semanticTokens`, inlay hints.
- `codeAction` "implement missing trait method" from the trait's required set.
- `positionEncoding` negotiation (UTF-16 columns); workspace symbols; call
  hierarchy.

### Language surface (also see `required-features.md`)

- The comma operator, `static`/`extern`/`inline` specifiers, array types.
- **Scoped imports** (names only for a scope).
- `defer`.

### Tooling / infrastructure

- **Landed 2026-11-10.** Compile database (`cx compdb`), fuzzing (`make fuzz` /
  `tests/fuzz/`), `#line` polish (`cx build -g`), a CI target (`make ci`), the
  `cx new-feature` scaffolder, and the paired `.hx`/`.cx` out-of-line destructor
  + `.c`-only helper prototypes. See [`changelog.md`](./changelog.md) and
  [`technical.md`](./technical.md) §3/§7.
- `#line` for declarations that came from an *import* (they map to the importing
  file today), and a body-less *constructor* declaration (`Counter(int);` — the
  `(` collides with a function-pointer member; see `technical.md` §7).
- LSP naming: the shared position index is
  [`src/lsp/features/nav.c`](../src/lsp/features/nav.c) (`cx_lsp_word_at`,
  `cx_lsp_node_at`, `cx_lsp_scope_at`, `cx_lsp_dot_at`, …) — the roadmap's
  `query.c` was a working name; renaming it is cosmetic.

## Considered extensions (not committed)

- **Scoped imports** — an `import` that introduces names only for a scope
  (block / function / namespace) instead of file-wide.
- **Compile database (`compile_commands.json`)** — emit include paths, flags and
  one entry per generated file so clangd and other C tooling resolve the
  transpiled output; could also help the Cx LSP discover module roots and import
  paths.
- **AVL/RB map** — for anything that provides an interface.
- **move semantics for real RAII** - but I've no idea how to make it plain c
  transpilable at the moment.
- c smartptr, in raw c it could possibly be done with double pointers and hepers
  calls on these.
- **Trait associated types/constants** — `trait Iterable { type Item; … }` (and
  a constant spelling that does not clash with the `const` type qualifier).
  Paused for now: a trait's members are method signatures only. It would let
  generic containers name an element type without a macro parameter, but needs
  per-implementation materialisation and `requires` checking.
- **Postfix vtable calls** (`x.T->m(a)` / `x.m(a)`) — removes the
  function-pointer-local boilerplate.
- **Derived/blanket implementations** (e.g. auto-`Equatable` for POD structs).
- **Receiver forms other than `self`** (`&self`, `self_`), which interacts with
  move semantics.
- **Codebase refactor** — landed 2026-11-10 (split by feature and by concern:
  `src/features/`, the `c_emitter`/`symbols`/`macro` splits, per-feature grammar
  hooks, `tests/run_one.sh`). See [`changelog.md`](./changelog.md).


### Landed extensions — remaining follow-ups

These shipped (see [`language.md`](./language.md) §16–§17 and
[`changelog.md`](./changelog.md)); only the follow-ups remain:

- **Static map** (`std/static_map.hx`) — add index access over the used slots
  and an optional built-in length/full check. (Struct-keyed *dynamic* maps still
  need the user-supplied hash/equal pair from ordered item 8.)
- **`cast[T](expr)`** — an optional second parameter carrying a compile-time
  check (e.g. a `sizeof` assertion).
- **`sm` state machines** — real hierarchical/parallel semantics (currently
  flattened), per-state entry/exit hooks, and the planned trait interfaces for
  transitions. `#[str]` on an enum is complete.

