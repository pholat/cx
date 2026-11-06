# Cx — Roadmap

> **Known LSP issue (to fix):** jump-to-definition for the macro-generated
> methods in `tests/golden/klib_list.cx` — `IntList::new`, `IntList::push`,
> `IntList::pop`, `IntList::size`, `IntList::free` — does **not** land in
> `macro_rules! define_list` yet. The generated-name → macro-origin map is
> recorded, but the LSP `definition` path still needs fixing.

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
and **trait requirements in macros** (`requires $x: Trait` / `requires $k: A + B`
parsed and recorded on the rule; trait-member access is textual).

## Ordered items

1. **klib-backed containers** — `List<T>` / `Map<K,V>` on klib, via macros or
   (later) templates. Built with the `requires` syntax from the done list, so
   container macros exercise trait-member access and reveal which traits (and
   associated types/constants) are actually needed.
   * **Progress:** `tests/golden/klib_list.cx` adds a macro-defined `IntList`
     over `klist.h` — `KLIST_INIT!` passthrough, generated `kl_init_<Name>` /
     `kl_pushp_<Name>` / `kl_shift_<Name>` / `kl_destroy_<Name>` wrappers, a
     `void *` handle (Cx can't spell `klist_t(<Name>)`), and push/pop/size/free.
     `tests/run.sh` now adds `submodules/klib` to the include path. The entry
     points use the new **associated call** syntax: `IntList::new()`,
     `IntList::push(&l, 10)`, `IntList::pop(&l, 0)`, `IntList::size(&l)`,
     `IntList::free(&l)` (lowers to `IntList__new`/`__push`/…). The macro body
     writes `$name::new`/`$name::push`/… (no `__`). The expander records those
     names in the origin map, but **LSP jump-to-definition for these methods is
     still broken** — see the known-issue note at the top.
   * **Still needed:** `Map<K,V>`, a user-facing stdlib module, an element
     free/RAII policy (the no-op callback leaks non-POD payloads), `requires`
     bounds for key/value capabilities once checked `requires` lands,
     `.`-method calls (`l.push(…)`) — currently only `Type::func(&l, …)` works
     because receiver local types aren't in the symbol table — and the **LSP
     jump-to-definition fix** for `klib_list.cx` methods (top note).
2. **Traits in macros — checked `requires`** — statically verify, after
   expansion/parsing/sema, that the concrete type of each constrained
   metavariable implements the required traits. First cut can be limited to
   fragments whose type is syntactically known (`$t:ty`, named globals/locals)
   until an expansion map links expanded nodes back to invocations. This is where
   `requires` becomes a real conformance check rather than documentation.
3. A paired `.hx`/`.cx` model (the `.c` includes its header and skips duplicate
   declarations).

Macro follow-ups (not blockers): hygiene for body-introduced identifiers,
LSP indexing/hover for macro definitions, and a richer fragment matcher
(backtracking beyond the current greedy `expr`/`ty` capture). Trait associated
types/constants (needed by real generic containers) should be designed together
with item 2.

> **Ordering rationale.** The cheap half of traits-in-macros (syntax + recorded
> requirements + textual member access) is **implemented** so container macros
> are written against the final spelling. The checked `requires` half comes after
> the first containers, so the checker targets real usage (which traits, which
> associated types, how bounds bind to multiple type parameters) instead of a
> speculative design. The macro expander is textual and type-blind, so the check
> is architecturally larger than the syntax/member-access work; deferring it
> avoids blocking the container layer.

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
