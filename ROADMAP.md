# Cx — Roadmap

> **Resolved (2026-11-07):** jump-to-definition for the macro-generated methods
> in `tests/golden/klib_list.cx` / `klib_map.cx` (`IntList::new`, `IntMap::new`,
> …) now lands in the `macro_rules!` definition: the LSP splits the
> `Type::method` pair at the cursor, forms `Type__method`, and prefers the
> recorded macro origin. `gd` on the macro *invocation* (`define_int_map!`) also
> jumps to the `macro_rules!` definition.

> Living document. Update **this file** (not `TECHNICAL.md`) when priorities or
> the design direction change. `TECHNICAL.md` §8 is just a pointer here.
>
> Related: `TECHNICAL.md` (implemented language/codegen/sema details),
> `GENERICS.md` (traits/attributes/generics design), `MACRO_RULES.md`
> (`macro_rules!` design), `examples/klib/REQUIRED_FEATURES.md` (C/klib gaps).

## Status

Done: **C interop** (`raw { … }` + `#include` + `NAME!(…)` macro invocation
passthrough), **static traits** (`trait`/`implements`), **attributes** (`#[…]`),
**`macro_rules!`** (item + expression-position expansion, see `TECHNICAL.md` §4),
and **checked trait requirements in macros** — `requires $x: Trait` /
`requires $k: A + B` is parsed, recorded on the rule, and verified after the AST
is built (a bound type that resolves to a struct must implement the required
traits). See the Done list below.

## Ordered items

1. **klib-backed containers** — `List<T>` / `Map<K,V>` on klib, via macros or
   (later) templates. Built with the `requires` syntax from the done list, so
   container macros exercise trait-member access and reveal which traits (and
   associated types/constants) are actually needed.
   * **Progress:** `tests/golden/klib_list.cx` adds a macro-defined `IntList`
     over `klist.h` (`KLIST_INIT!` passthrough, generated `kl_init_<Name>` /
     `kl_pushp_<Name>` / `kl_shift_<Name>` / `kl_destroy_<Name>` wrappers, a
     `void *` handle, and push/pop/size/free). `tests/golden/klib_map.cx` adds
     the `Map<K,V>` half: a macro-defined `IntMap` over `khash.h`
     (`KHASH_MAP_INIT_INT!` plus a per-instantiation `CX_KHASH_MAP_HELPERS`
     C macro that emits typed accessors over a `void *` handle, since Cx cannot
     spell `khash_t(<Name>)`), with put/get/has/del/size/free.
     `tests/golden/klib_bag.cx` adds an **element-typed** container over
     `klist.h` (`define_bag!($name, $t) requires $t: Drop`) that stores
     `$t *` in `void *` slots, so the concrete element type appears only in
     Cx-generated code — this is the container that exercises the checked
     `requires` bound on a struct element type. All use the **associated call**
     syntax (`IntList::new()`, `IntMap::put(&m, k, v)`, …).
     `tests/run.sh` adds `submodules/klib` to the include path.
     Jump-to-definition for the container methods is **fixed** (top note).
   * **Still needed:** an element free/RAII policy that actually invokes the
     trait on teardown (klib's pool free callback is for internal nodes;
     `klib_bag.cx` stores `$t *` and checks `requires $t: Drop` but does not yet
     call `drop` on `free`), a user-facing stdlib module (blocked on macro
     definitions crossing `import` boundaries), `requires` bounds for
     primitive key/value capabilities (built-ins cannot implement a trait in the
     current model, so a bound on e.g. `int` is skipped by the checker), and
     `.`-method calls (`l.push(…)`) — currently only `Type::func(&l, …)` works
     because receiver local types aren't in the symbol table.
2. A paired `.hx`/`.cx` model (the `.c` includes its header and skips duplicate
   declarations).

Macro follow-ups (not blockers): hygiene for body-introduced identifiers,
LSP indexing/hover for macro definitions, and a richer fragment matcher
(backtracking beyond the current greedy `expr`/`ty` capture). Trait associated
types/constants (needed by real generic containers) should be designed together
with the remaining `requires` work.

> **Ordering rationale.** Traits in macros — syntax, recorded requirements,
> textual member access, **and checked conformance** — are now implemented, so
> container macros are written against the final spelling. The checked half is
> deliberately a first cut: it verifies concrete types that resolve to a struct
> and skips built-ins/unknowns. Deeper work (associated types/constants in
> `requires`, resolving namespaced types, requirement bounds for primitive
> elements) is folded into the container/`.hx` items above.

## Done

* **Traits in macros — checked `requires`**: after expansion/parse/sema, each
  `requires` binding recorded at an invocation site is verified. A **type** bound
  to the metavariable must implement every required trait (the trait must exist);
  an **`ident`/`path`** binding that names no struct is a function and must name
  one of the trait's methods (`requires $a: A` for the function `a` passed as
  A's method). Built-ins/unknown types are skipped. Positive tests:
  `tests/golden/traits_make_total.cx` (`make_total!(total, Row)`; `Row: Iterable`)
  and `tests/golden/macro_requires.cx` (`make_both_total!(both_total, a, b)`;
  `$a: A, $b: B`). Negative tests: `tests/golden/macro_requires_fail.fail.cx`
  (struct) and `tests/golden/macro_requires_method_fail.fail.cx` (method); `cx
  build` must fail on `*.fail.cx`.

## Generics & macros (design note — under discussion)

> The type-abstraction direction is **not settled**. `macro_rules!` was chosen
> partly because C++ templates are hard to read; that rationale is being
> revisited in favour of a **traits + attributes + `macro!`** approach. See
> `GENERICS.md` (traits/attributes/`macro!`) and `MACRO_RULES.md`
> (`macro_rules!`). These are different axes (type parameters, behaviour
> interfaces, metadata, generativity) and need not share one mechanism.

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

**Draft recommendation (see `GENERICS.md` §8).** Treat the axes separately: adopt
**attributes `#[…]`** early (metadata/codegen); add **`macro!`** (generativity)
per `MACRO_RULES.md`; **explicit static traits are implemented** (`implements`
block inside a struct → one pointer per trait to a shared static per-type table,
statically checked) — macros opt in with `requires`; keep klib as the container
backend. Option C (`macro_rules!`) is *a* mechanism, not the whole plan; option B
(template-like generics) is deferred until tooling/type-error quality can be
judged on real code. Exposing klib's macros directly (A) stays confined to the
`raw { … }` / invocation-passthrough escape hatch — not the container API.

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
