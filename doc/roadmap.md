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
**`do…while`** and the **conditional `?:`**, and the **LSP**
navigation/hover/completion/signature/occurrence features (read/write highlight
kinds, doc comments on hover, cross-open-document references/rename).

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
| 6 | **Tooling / infrastructure**: a **compile database** (`compile_commands.json`); **golden-output tests** (diff the generated C, not just "does it compile") plus fuzzing; `#line`/debug-info polish and a CI target; **paired `.hx`/`.cx` follow-ups** (out-of-line ctor/dtor definitions and `.c`-only helper prototypes). | Tooling |
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
  struct providing `process_event`, `reset`, `get_state`, `init`.
  The sm accepts `[*]` as entry state.
  The FSM has to support:
  - events, guards, actions, parralel states and sub-states.
  - parallel states will be defined with '|' before the state line, therefore line:
    ```
    | StateA -> StateB: Action1
    | StateD -> StateG: Action2
    ```
    means two parallel states.
    Sub-states can be defined with:
    ```
    StateA { 
        StateB->StateC: Action1 
        StateC->StateB: Action2
    }
    ```
  - state machine will be detected similar to raw code block. it will start with ` sm { `
  - state machine will be affedted by `namespace` - therefore a sm created in namespace
    will have all it's elements affected with namespace.
  - multiple guards can be passed via coma, result of coma separated guards is a
    result of `guard1(...) && guard2(...) && ...` the if any guard in the chain fails
    then next guard is not called ( which is typical && c behaviour )
  - multiple actions can be passed coma separated
  - the sm follows first element in transmission capture table rule
  - comments can be added with `//`
  - there is a set of non named state handlings:
    - `[*]` default entry state
    - `[any]` any entry state
    - `[current]` current state captured "on the left of `->`
    With that the user can:
    - specify entry state with `[*]` 
    - create a handling for unexpected events i.e. `[any] -> [current] / log_unexpected` 
      at the end of sm definition will call log_unexpected if nothing else matched and
      consumed the event
  - guads and actions are given context with:
    - entry event
    - exit event
    - user context registered
  - events and guards are callbacks that need to be registered - if not registered
    these are null and for guards it results in `false`, while for actions it results
    in lack of call to the action
  - complete sm example
    ```
    namespace test{
      [*] -> playing : start / log_play, play
      playing -> done : finish [can_stop] / log_stopped, stop
      // This transmission will be done if previous wasn't executed
      playing -> done : finish / log_wont_stop
      Done -> [*] / log_done
      [any] -> [current] / log_unexpected
    }
    ```
- **`#[str]`** — automatically create a `c_str` function for an enum that
  returns a static string per element via a `switch` (and `""` as fallback).
- move semantics for real RAAI - but I've no idea how to make it plain c transpilable
  at the moment.
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

