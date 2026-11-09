# Cx — Generics, traits & attributes (design rationale)

> **Status: implemented.** Static traits (`trait` / `implements`), attributes
> `#[…]`, and `macro_rules!` all ship. This file records the **design
> rationale** only — syntax and lowering live in
> [`language.md`](./language.md) §9–§11, the implementation map in
> [`technical.md`](./technical.md) §5–§7, and the full macro reference in
> [`macro-rules.md`](./macro-rules.md). Pending work is in
> [`roadmap.md`](./roadmap.md).

## 1. The axes

The problem has several independent axes; forcing one mechanism to cover all of
them was the main design risk:

| Axis | Mechanism | Status |
| :-- | :-- | :-- |
| Type parameters (`List<T>`) | template-like monomorphization | deferred |
| Behaviour interfaces (capabilities) | static **traits** | implemented |
| Metadata / codegen directives | **attributes** `#[…]` | implemented |
| Generativity (types *and* functions) | **`macro_rules!`** | implemented |

C++-style `template<…>` syntax was deliberately not chosen up front: it is the
readability problem that motivated Cx. The container layer instead uses
`macro_rules!` over klib (see §3).

## 2. Traits: static, explicit, no inheritance

Cx has **no inheritance and no class vtables**. A trait is a named set of
required member signatures; a struct opts in with an `implements` block inside
the struct.

Design decisions:

- **Explicit conformance.** Merely having members named `len`/`at` is not
  enough; the struct must write `implements Iterable { … }`. This keeps intent
  explicit, makes the check unambiguous, and lets the LSP index "which type
  implements which trait".
- **One pointer per trait.** A struct stores a single pointer to a shared
  **static per-type table**, not one function pointer per member. Smaller
  structs, C-idiomatic layout, and a natural target for `$x:method` /
  `requires` in macros.
- **Static checking.** Sema verifies every required method is present with a
  matching signature (the `self` receiver is ignored in the comparison).
- **No hidden type id.** Dispatch is explicit: `r.Iterable->len(&r)` (or a
  function-pointer local, since Cx has no postfix-call syntax yet). A `malloc`'d
  struct is the user's responsibility to initialize.
- **Why this covers list/map.** A `Container` / `Iterable` / `KeyValue`-style
  trait gives macros the indirection they need over *any* conforming type —
  most of what a generic `List` / `Map` needs — without inheritance.

Open trait work (associated types/constants, supertraits, default method
bodies, postfix vtable calls, derived/blanket impls, receiver forms other than
`self`) is listed in [`roadmap.md`](./roadmap.md) under *Traits* and *Considered
extensions*. For now a trait's members are **method signatures only**.

## 3. Macros and the container layer

`macro_rules!` was chosen partly because C++ templates are hard to read. It is
the generative mechanism for containers: the `std_*.hx` modules define list,
vector, map and set macros over klib, and owners such as `define_vec!` carry a
`requires $t: Drop` bound to require element teardown.

The macro system is *one* axis, not the whole generics plan. Template-like
generics remain deferred until tooling and type-error quality can be judged on
real code. See [`macro-rules.md`](./macro-rules.md) for the macro model and
[`roadmap.md`](./roadmap.md) for the open macro items (hygiene, non-greedy
fragments, nested repetitions, more fragments).

## 4. Attributes

Attributes are the metadata axis: `#[inline]`, `#[noreturn]`,
`#[export("c_name")]`, `#[cleanup(fn)]`, `#[test]`, plus pass-through C
attributes (`#[packed]`, `#[aligned(n)]`, …). They are documented in
[`language.md`](./language.md) §10; the emitter's leading/trailing handling is
in [`technical.md`](./technical.md) §5.

`#[ctor]` / `#[dtor]` were considered and **rejected**: the existing
`Type(args) { … }` / `~Type() { … }` syntax is already readable, and the
attributes now raise an error pointing at it.