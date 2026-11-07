# Cx — Rust-style `macro_rules!` (design)

> Status: **core implemented**. `raw { … }` / `NAME!( … )` C passthrough, and
> `macro_rules!` definitions, matching (`$x:frag`, repetitions, `##`) and
> expansion in item/statement/expression position — see `TECHNICAL.md` §4 and
> `src/macro.c`. Also **implemented**: the `requires` clause bound to
> metavariables (`requires $x: Trait`, `requires $k: A + B, …`), trait-member
> access in transcriptions, the **checked conformance pass** (a struct binding
> must `implements` the trait; an `ident`/`path` binding must name one of the
> trait's methods), and the **`$x:method` fragment** (`Trait::member`, expanded
> to `Trait->member`). The expander records a generated-name → macro-definition
> origin map, and **jump-to-definition for the macro-generated container methods
> in `tests/golden/klib_*.cx` is fixed** (see `ROADMAP.md`).
>
> Still **not implemented**: hygienic renaming, full LSP macro symbols /
> expansion preview, a fully backtracking matcher (fragment capture is currently
> greedy), and macro definitions crossing `import` boundaries. The rest of this
> document is the original design sketch; where it differs from the
> implementation, `TECHNICAL.md` §4/§7 wins.

The implementation lives in `src/macro.c` and runs as a **textual pre-parse
pass**: it tokenises into a small token tree, removes definitions, substitutes
matching invocations, and feeds the result to the normal Cx parser.

## 1. Motivation

Two goals drive this:

1. **Full C compliance / "use C in Cx".** We want to be able to drop real C into
   a Cx translation unit — including things Cx cannot parse on its own, such as
   macro-generated types (`klist_t(x)`, `khash_t(x)`) and function-like macro
   invocations (`KLIST_INIT(...)`, `KHASH_MAP_INIT_INT(...)`).
2. **Generativity.** Containers and boilerplate are naturally *generative*: given
   a type, produce a struct + functions. Cx needs a way to express that without
   reinventing C++ templates, and `macro_rules!`-style macros are a well-understood
   model that also covers codegen/DSL cases templates don't.

Three complementary mechanisms, in increasing power:

| Mechanism | Covers | Effort | Status |
| :-- | :-- | :-- | :-- |
| `raw { … }` (verbatim C) + `#include` passthrough | arbitrary C bodies/decls | low | **done** |
| C function-like macro **invocation** passthrough (`NAME!(…)`) | `KLIST_INIT(...)` etc. | low | **done** (file/statement position) |
| Cx `macro_rules!` | generative Cx (types + functions) | high | **core done** |

`raw {}` and macro-invocation passthrough give the "use C in Cx" story. Cx
`macro_rules!` gives the generative story (and backs `List<T>`/`Map<K,V>`,
see §7).

## 2. Syntax

Definition (Rust-like):

```cxc
macro_rules! vec_of {
    ($name:ident, $t:ty) => {
        struct $name {
            $t* data;
            int len;
        };
        $name* $name##_new();
    };
}
```

A rule may carry an optional `requires` clause between its pattern and `=>`,
binding trait requirements to metavariables:

```cxc
macro_rules! total {
    ($x:expr) requires $x: Iterable => {
        /* … */
    };
}

macro_rules! pair_of {
    ($name:ident, $k:ty, $v:ty)
        requires $k: Hashable + Equatable, $v: Clone
    => { /* … */ };
}
```

The clause is parsed, recorded and **checked** (see `TECHNICAL.md` §6). A
`$x:method` fragment may also name a `Trait::member` reference, which is checked
and expands to `Trait->member`.

A transcription may write `$name::method` as the public spelling of the internal
`Name__method` identifier; the expander rewrites it automatically and records a
generated-name → macro-definition origin for LSP `definition` (jump-to-definition
now works for the container test — see `ROADMAP.md`). (Plain `##` concatenation
remains available for klib/C names such as `kl_init_##$name`.)

Invocation:

```cxc
vec_of!(IntVec, int);
```

Design choices vs Rust:

* `$name##_new` / `$name##_dtor` — **concatenation** (`##`) instead of Rust's
  `concat_idents!`. Needed to derive companion names (very common in C).
* Fragments: `$x:ident`, `$x:ty`, `$x:expr`, `$x:stmt`, `$x:tt`, `$x:lit`,
  `$x:path` (namespace path), `$x:field`, `$x:method` (`Trait::member`).
* Repetition: `$( … ),*`, `$( … );*`, `$( … )*` (Rust syntax).

## 3. Expansion model

Macros are a **token-level, two-phase** transform, matching how Rust works:

1. **Lex** — the scanner produces tokens *and* captures balanced token trees for
   `macro_rules!` bodies and invocations.
2. **Define** — `macro_rules!` definitions are collected into a table (name →
   rule list). A definition produces no C.
3. **Expand** — an invocation `name!( … )` is matched against the rules' patterns
   (producing fragment bindings + repetition) and the matching arm is expanded:
   bindings substituted, `##`-concatenations performed, repetitions unrolled,
   hygiene applied. The result is a **token stream**.
4. **Re-parse** — the expanded tokens are fed back into the Cx parser, so a macro
   may produce *declarations, statements, expressions, or types*.

Where it runs: conceptually between the lexer and parser, but implemented as a
**textual pre-parse pass** that expands, writes the text back, and re-scans
(repeated for nested invocations).

## 4. Hygiene

Rust's hygiene means identifiers introduced by the macro body can't accidentally
capture (or be captured by) identifiers at the call site.

Proposed rule for Cx:

* **Local bindings introduced by a macro body** get a unique suffix
  (`__cx_m_<n>`) so they don't collide (e.g. a temporary `$tmp` a macro declares).
* **Identifiers passed in as `$x:ident`** are used verbatim (that's the point).
* **Free identifiers in the body** (e.g. a call to another function) refer to the
  macro's *definition* scope, not the call site.

This matches developer expectations and is far simpler than full syntax-context
hygiene. Document the boundary clearly. **Not implemented yet.**

## 5. Diagnostics

Macro expansion destroys the 1:1 source mapping. Mitigation:

* Attribute expansion errors to the **invocation site** (span of `name!(...)`),
  not the expanded tokens.
* Keep a lightweight **expansion map** (token → originating macro/def position)
  so the emitter can interleave `#line` directives pointing at the Cx source
  (the invocation site) rather than the macro body.
* Provide a `cx expand` command (and an LSP "expand macro" action) that prints the
  expansion for debugging.

## 6. Tooling (LSP)

Macros are the hard case for language tooling; state the limits up front:

* **Macro definitions are not indexed yet** (they are removed before the AST is
  built), so `definition`/`hover` on a macro name are not reliable.
* Generated-name → macro-definition origins are recorded for `$name::method`,
  generated functions and `struct $name`; **jump-to-definition for the
  `klib_*.cx` container methods now works** (see `ROADMAP.md`).
* **Inside** a macro body/invocation, completion/definition are best-effort
  (the tokens are macro-typed, not yet Cx-typed).
* Optional later: **expansion preview** (rust-analyzer style) — show the expanded
  token stream / generated C.

## 7. Relation to klib and to generics

`macro_rules!` implements `List`/`Map` by *generating* the klib instantiations
(see `tests/golden/klib_list.cx`, `klib_map.cx`, `klib_bag.cx`):

```cxc
macro_rules! define_list {
    ($name:ident, $t:ty) => {
        KLIST_INIT!($name, $t, cx_nofree);
        struct $name { void *v; int n; };
        $name $name::new() { … }
        // ...
    };
}
```

So the layering is:

1. `raw { … }` + macro-invocation passthrough — "use C in Cx" (unblocks klib).
2. `macro_rules!` — generative Cx.
3. A container/stdlib layer — either defined with `macro_rules!` in a stdlib
   `.hx`, or later promoted to first-class template-like generics (see
   `ROADMAP.md` design note). `macro_rules!` is strictly more general; templates
   are nicer for tooling and type errors. Decision deferred.

## 8. Implementation plan (phases)

1. **Balanced token-tree capture** — **done** (the expander's own tokenizer).
2. **`macro_rules!` parser** → rule table (patterns, fragment specifiers, arms) —
   **done** (`src/macro.c`).
3. **Matcher**: token tree against a pattern arm; bindings + repetitions —
   **done** (greedy `expr`/`ty`; no general backtracking).
4. **Transcriber**: substitute bindings, handle `##` and repetitions —
   **done except hygiene**.
5. **Expansion driver**: recognize `name!( … )` in item/statement/expression
   position; expand; re-parse — **done**.
6. **Diagnostics + `cx expand`** — **done** (`cx expand`; errors point at the
   invocation). `#line` mapping through expansion is approximate.
7. **LSP**: index macro definitions; definition/hover; expansion preview — **not
   done** for definitions; generated-name → macro-origin map exists and
   jump-to-definition works for the container methods.
8. **Stdlib** built with `macro_rules!` (`List`, `Map`, `defer`, …) — **in
   progress** (the klib-backed container layer).

## 9. Open questions

* Concat `##` vs a `concat` fragment — confirm `##` reads acceptably.
* Hygiene scope: local bindings only, or also item-level names?
* Expression-macro capture: `$x:expr` is greedy to a top-level `,`/`;`; a
  backtracking matcher is needed for richer patterns.
* Do we want a separate `macro!()`-style *function macro* distinct from
  `macro_rules!` item macros?
