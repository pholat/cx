# Cx — Rust-style `macro_rules!` (design)

> Status: **core implemented**. `raw { … }` / `NAME!( … )` C passthrough, and
> now `macro_rules!` definitions, matching (`$x:frag`, repetitions, `##`) and
> expansion in item/statement/expression position — see `TECHNICAL.md` §4 and
> `src/macro.c`. Also **implemented**: the `requires` clause bound to
> metavariables (`requires $x: Trait`, `requires $k: A + B, …`) and trait-member
> access in transcriptions; the requirements are parsed/recorded but the
> **checked conformance pass** is deferred until after the container layer.
> Still **not implemented**: that check, hygienic renaming, full LSP macro
> symbols / expansion preview, and a fully backtracking matcher (fragment capture
> is currently greedy). The expander records a generated-name →
> macro-definition origin map, but **jump-to-definition for the macro-generated
> `IntList` methods in `tests/golden/klib_list.cx` is still broken/open** — see
> `ROADMAP.md`. The rest of this document is the original design sketch; where
> it differs from the implementation, `TECHNICAL.md` §4/§7 wins.

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
`macro_rules!` gives the generative story (and can back `List<T>`/`Map<K,V>`,
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

The clause is currently parsed and recorded (the metavariable must exist in the
pattern); static conformance checking is a later roadmap step.

A transcription may write `$name::method` as the public spelling of the internal
`Name__method` identifier; the expander rewrites it automatically and records a
generated-name → macro-definition origin for LSP `definition` (the jump is still
being fixed for the container test — see `ROADMAP.md`). (Plain `##`
concatenation remains available for klib/C names such as `kl_init_##$name`.)

Invocation:

```cxc
vec_of!(IntVec, int);
```

Design choices vs Rust:

* `$name##_new` / `$name##_dtor` — **concatenation** (`##`) instead of Rust's
  `concat_idents!`. Needed to derive companion names (very common in C).
* Fragments: `$x:ident`, `$x:ty`, `$x:expr`, `$x:stmt`, `$x:tt`, `$x:lit`,
  `$x:path` (namespace path), `$x:field`.
* Repetition: `$( … ),*`, `$( … );*`, `$( … )*` (Rust syntax).

## 3. Expansion model

Macros are a **token-level, two-phase** transform, matching how Rust works:

1. **Lex** — the scanner produces tokens *and* captures balanced token trees for
   `macro_rules!` bodies and invocations (reuse the balanced-delimiter capture
   already sketched for `raw { … }`).
2. **Define** — `macro_rules!` definitions are collected into a table (name →
   rule list). A definition produces no C.
3. **Expand** — an invocation `name!( … )` is matched against the rules' patterns
   (producing fragment bindings + repetition) and the matching arm is expanded:
   bindings substituted, `##`-concatenations performed, repetitions unrolled,
   hygiene applied. The result is a **token stream**.
4. **Re-parse** — the expanded tokens are fed back into the Cx parser, so a macro
   may produce *declarations, statements, expressions, or types*.

Where it runs: conceptually between the lexer and parser, but implemented as a
recursive "pull a token; if it's an invocation, expand and push back". The
simplest first cut: expand **whole top-level invocations and statement-level
invocations** (i.e. allow `name!(...)` only where a declaration or a statement is
expected), which avoids mid-expression expansion subtleties.

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
hygiene. Document the boundary clearly.

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
  generated functions and `struct $name`, but **jump-to-definition for the
  `klib_list.cx` methods is still broken** (see `ROADMAP.md`).
* **Inside** a macro body/invocation, completion/definition are best-effort
  (the tokens are macro-typed, not yet Cx-typed).
* Optional later: **expansion preview** (rust-analyzer style) — show the expanded
  token stream / generated C.

## 7. Relation to klib and to generics

`macro_rules!` can implement `List<T>`/`Map<K,V>` by *generating* the klib
instantiations:

```cxc
macro_rules! list_of {
    ($name:ident, $t:ty) => {
        raw { KLIST_INIT($name, $t, 0); }
        struct $name { raw { klist_t($name) *v; } };
        $t $name##_get($name* l, int i);
        // ...
    };
}
```

So the layering can be:

1. `raw { … }` + macro-invocation passthrough — "use C in Cx" (unblocks klib).
2. `macro_rules!` — generative Cx.
3. `List<T>`/`Map<K,V>` — either defined with `macro_rules!` in a stdlib `.hx`,
   or later promoted to first-class template-like generics (see `ROADMAP.md`
   design note). `macro_rules!` is strictly more general; templates are nicer
   for tooling and type errors. Decision deferred until `macro_rules!` exists.

## 8. Implementation plan (phases)

1. **Balanced token-tree capture** — **done** (the expander's own tokenizer; the
   lexer keeps its separate `RAW`/`MACROCAP`/`ATTR` capture for passthrough).
2. **`macro_rules!` parser** → rule table (patterns, fragment specifiers, arms) —
   **done** (`src/macro.c`).
3. **Matcher**: token tree against a pattern arm; bindings + repetitions —
   **done** (greedy `expr`/`ty`; no general backtracking).
4. **Transcriber**: substitute bindings, handle `##` and repetitions; hygiene
   renaming — **done except hygiene**.
5. **Expansion driver**: recognize `name!( … )` in item/statement/expression
   position; expand; re-parse — **done** (textual pre-parse pass; iterative for
   nested invocations).
6. **Diagnostics + `cx expand`** — **done** (`cx expand`; errors point at the
   invocation). `#line` mapping through expansion is approximate (definitions are
   replaced by blank lines; expansions may shift columns).
7. **LSP**: index macro definitions; definition/hover; expansion preview — **not
   done** (definitions are removed before the AST is built). Partial:
   generated-name → macro-origin map exists, but jump-to-definition for the
   `klib_list.cx` methods still needs fixing.
8. **Stdlib** built with `macro_rules!` (e.g. `List<T>`, `Map<K,V>`, `defer`, …) —
   **next** (the klib-backed container layer).

## 9. Open questions

* Concat `##` vs a `concat` fragment — confirm `##` reads acceptably.
* Hygiene scope: local bindings only, or also item-level names?
* Do we allow **expression position** invocation (`let x = max!(a, b);`) in phase
  1, or restrict to statements/declarations first?
* Interaction with the lexer type-name set: a macro can introduce a *type* name
  (`struct $name`), so expansion must update the type set before subsequent
  lexing — i.e. expansion must run before the tokens that follow are lexed
  (same constraint as `import`).
* Do we want a `macro!()`-style *function macro* (expression macros) separate
  from `macro_rules!` item macros?
