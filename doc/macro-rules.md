# Cx — `macro_rules!` reference

> **Status: implemented core.** The expander is a token-level pre-parse pass in
> `src/macro.c`; it runs before the Cx parser and feeds it the expanded source.
>
> Implemented: `raw { … }` / `NAME!( … )` C passthrough; `macro_rules!`
> definitions and invocation in **item, statement and expression** position;
> fragments (`ident`, `ty`, `expr`, `stmt`, `tt`, `lit`, `path`, `field`,
> `method`, `block`, `item`); repetitions (`$( … )*` / `$( … )+`, optional
> single separator, **nested** `$( $( … )* )*`); `##` concatenation;
> `$name::method` spelling; `requires` clauses with checked conformance;
> `$x:method` (`Trait::member` → `Trait->member`); nested invocations;
> hoisting; macro definitions crossing `import`; `cx expand`; and the
> `CxMacroMap` origins used by the LSP (including the **defining file**, so
> imported-macro `gd` lands in the module).
>
> Not implemented: hygiene, a fully backtracking matcher (fragment capture is
> greedy), macro-definition indexing in the LSP, and expansion preview.
>
> Syntax/lowering summary: [`language.md`](./language.md) §11. Implementation
> notes: [`technical.md`](./technical.md) §7. Pending work:
> [`roadmap.md`](./roadmap.md) under *Macro system*.

## 1. Motivation

Two goals:

1. **Full C compliance / "use C in Cx".** Drop real C into a Cx translation
   unit, including macro-generated types (`klist_t(x)`, `khash_t(x)`) and
   function-like macro invocations (`KLIST_INIT(...)`).
2. **Generativity.** Containers and boilerplate are naturally generative: given
   a type, produce a struct + functions. `macro_rules!` covers that without
   inventing C++ templates, and also covers codegen/DSL cases templates do not.

Three complementary mechanisms:

| Mechanism | Covers | Status |
| :-- | :-- | :-- |
| `raw { … }` (verbatim C) + `#include` passthrough | arbitrary C bodies/decls | done |
| C function-like macro **invocation** passthrough (`NAME!(…)`) | `KLIST_INIT(...)` etc. | done (file/statement position) |
| Cx `macro_rules!` | generative Cx (types + functions) | core done |

## 2. Syntax

```cxc
macro_rules! vec_of {
    ($name:ident, $t:ty) => {
        struct $name {
            $t *data;
            int len;
        };
        $name *$name##_new();
    };
}

vec_of!(IntVec, int);
```

A rule may carry an optional `requires` clause between its pattern and `=>`,
binding trait requirements to metavariables:

```cxc
macro_rules! total {
    ($x:expr) requires $x: Iterable => {
        /* … reach ($x).Iterable->len / ->at … */
    };
}

macro_rules! pair_of {
    ($name:ident, $k:ty, $v:ty)
        requires $k: Hashable + Equatable, $v: Clone
    => { /* … */ };
}
```

The clause is parsed and recorded on the rule; after the AST is built, sema
checks every concrete binding (see §5 and [`technical.md`](./technical.md) §6).

A `$x:method` fragment matches a qualified `Trait::member` reference such as
`A::a`. It is checked (the member must be a required method of the trait) and,
in a transcription, expands to the vtable access `Trait->member` — so `($x).$a`
with `$a = A::a` becomes `($x).A->a`.

A transcription may write `$name::method` as the public spelling of the internal
`Name__method` identifier; the expander rewrites it automatically and records a
generated-name → macro-definition origin for LSP `definition`.

### Design choices vs Rust

- `$name##_new` — **concatenation** (`##`) instead of Rust's `concat_idents!`.
  Needed to derive companion names, very common in C.
- Fragments: `$x:ident`, `$x:ty`, `$x:expr`, `$x:stmt`, `$x:tt`, `$x:lit`,
  `$x:path`, `$x:field`, `$x:method`, `$x:block`, `$x:item`.
- Repetition: `$( … ),*`, `$( … );*`, `$( … )*`, `$( … )+`, and nested
  `$( $( … )* )*`. Each capture records the index path that reaches it, so a
  nested repetition transcribes the right per-iteration binding. A trailing
  separator is accepted.
- `NAME!(…)` whose `NAME` is not a `macro_rules!` macro is a **C** macro
  invocation, emitted verbatim. This keeps the klib passthrough working.

## 3. Expansion model

Macros are a **token-level transform** before parsing:

1. **Tokenise** — the expander builds a small token tree; `raw { … }`,
   preprocessor lines and `#[…]` attributes are opaque and passed through
   verbatim.
2. **Collect definitions** — `macro_rules!` definitions are collected into a
   table (name → rule list), removed from the source, and hoisted for
   resolution (an invocation may precede its definition). Imported files are
   scanned too: definitions from `import`ed `.hx` files are collected first,
   recursively and cycle-bounded (depth limit), so a module can export a macro;
   the importer's own definitions take precedence.
3. **Expand** — an invocation is matched against the rules' patterns
   (fragment bindings + repetition) and the matching arm is transcribed:
   bindings substituted, `##` concatenations performed, repetitions unrolled,
   `$name::method` rewritten. The result is text.
4. **Re-parse** — the expanded source goes through the normal Cx parser, so a
   macro may produce declarations, statements, expressions, or types.

Expansion is **iterative** (max 64 passes), so a macro may invoke another macro.
`cx expand <file.cx>` prints the expanded source for debugging.

Consequences:

- `$x:expr` / `$x:ty` capture greedily to the next top-level `,` / `;`; a
  pattern cannot put another literal fragment after them unambiguously.
- A trailing `;` after an item-position invocation is consumed (the expansion
  supplies the declarations), while an expression-position `;` is preserved.
- Nested repetitions are supported through a per-capture index path; a
  metavariable bound at a shallower depth is repeated (its single capture is
  emitted on every iteration).
- Hygiene is not implemented: identifiers introduced by the body are emitted
  verbatim.

## 4. Hygiene (not implemented)

Rust's hygiene means identifiers introduced by the macro body cannot accidentally
capture (or be captured by) identifiers at the call site. The proposed rule for
Cx:

- **Local bindings introduced by a macro body** get a unique suffix
  (`__cx_m_<n>`) so they do not collide (e.g. a temporary a macro declares).
- **Identifiers passed in as `$x:ident`** are used verbatim.
- **Free identifiers in the body** refer to the macro's *definition* scope, not
  the call site.

This is simpler than full syntax-context hygiene and matches developer
expectations. It is not implemented yet; see
[`roadmap.md`](./roadmap.md) under *Macro system*.

## 5. Diagnostics

Macro expansion destroys the 1:1 source mapping. Mitigation:

- Expansion errors are attributed to the **invocation site**, not the expanded
  tokens.
- `CxMacroMap` records generated-name → macro-definition origins and invocation
  sites, so the LSP can resolve `definition`/hover and reconstruct the
  `Type::method` spelling.
- `cx expand` prints the expansion for debugging.
- Checked `requires` failures name the trait and the concrete binding.
  Negative tests: `tests/golden/macro_requires_fail.fail.cx` (struct) and
  `macro_requires_method_fail.fail.cx` (method),
  `primitive_requires_fail.fail.cx` (primitive).

`#line` mapping through expansion remains approximate: diagnostics point at the
invocation/source region rather than every expanded token.

## 6. Tooling (LSP)

- **Macro definitions are not indexed** (they are removed before the AST is
  built), so `documentSymbol`/rename cannot see them; `definition` on an
  invocation still works through `CxMacroMap`.
- Generated-name → macro-definition origins are recorded for `$name::method`,
  generated functions (`$name(params) { … }`) and `struct $name`; **jump-to-
  definition for the `klib_*.cx` / `std_*.hx` container methods works**.
- `gd` on a `macro_rules!` invocation jumps to the definition, **including an
  invocation of a macro defined in an imported `.hx` module** (the origin now
  records the defining file, not just a span).
- Completion after `Type::` lists generated members as `Type::method`; the
  general list shows macro-generated `A__B` names as `A::B`.
- **Inside** a macro body/invocation, completion/definition are best-effort.
- Optional later: expansion preview (rust-analyzer style).

### Traits in the golden tests

Traits are exercised together with macros in:

- `tests/golden/macro_requires.cx` — `requires $x: A + B` and
  `$x:method` references (`A::a`, `B::b`);
- `tests/golden/traits_macro.cx` — expression macro over an `Iterable` helper;
- `tests/golden/traits_make_total.cx` — item-position macro generating a
  function against the static vtable;
- `tests/golden/std_vec.hx` — `requires $t: Drop` with element teardown through
  the installed vtable (`p->Drop->drop`).

## 7. Relation to klib and to generics

`macro_rules!` implements `List` / `Map` by *generating* the klib
instantiations. The user-facing container layer now ships as module files:

- `tests/golden/std_list.hx` — `define_list!`
- `tests/golden/std_map.hx` — `define_int_map!`
- `tests/golden/std_strmap.hx` — `define_str_map!`
- `tests/golden/std_set.hx` / `std_strset.hx` — `define_int_set!` /
  `define_str_set!`
- `tests/golden/std_vec.hx` — element-typed `define_vec!` with
  `requires $t: Drop`
- `tests/golden/std/std.hx` — the std catalog; `for!` (the
  `for`-over-containers lowering over `.len()` / `.at(i)`, element type via
  `auto`)
- `tests/golden/std_drop.hx` — the `Drop` interface
- `tests/golden/std_demo.cx`, `std_owned_demo.cx`, `std_str_demo.cx`,
  `std_iter_demo.cx` — examples

Each module carries the C context it needs (`#include`s and helpers) in its
generated header, so an importer needs nothing extra.

Layering:

1. `raw { … }` + macro-invocation passthrough — "use C in Cx".
2. `macro_rules!` — generative Cx.
3. A container/stdlib layer built with `macro_rules!` (the `std_*.hx` modules).

`macro_rules!` is strictly more general than templates; templates are nicer for
tooling and type errors. The template decision remains deferred — see
[`generics.md`](./generics.md) and [`roadmap.md`](./roadmap.md).

## 8. Implementation status (phases)

1. **Balanced token-tree capture** — done.
2. **`macro_rules!` parser** → rule table (patterns, fragment specifiers, arms)
   — done.
3. **Matcher** — token tree against a pattern arm; bindings + repetitions —
   done (greedy `expr`/`ty`; no general backtracking).
4. **Transcriber** — substitute bindings, handle `##` and repetitions — done
   except hygiene.
5. **Expansion driver** — recognize `name!( … )` in item/statement/expression
   position; expand; re-parse — done.
6. **Diagnostics + `cx expand`** — done; errors point at the invocation.
   `#line` mapping through expansion is approximate.
7. **Imports** — definitions collected from imported files recursively — done.
8. **LSP** — invocation/generated origins done; macro definition indexing and
   expansion preview not done.
9. **Stdlib** — the `std_*.hx` container modules are in place.

## 9. Open questions

- Concat `##` vs a `concat` fragment — confirm `##` reads acceptably.
- Hygiene scope: local bindings only, or also item-level names?
- Expression-macro capture: `$x:expr` is greedy to a top-level `,` / `;`; a
  backtracking matcher is needed for richer patterns.
- Nested repetitions (`$( $( … )* )*`) and more fragments (`$x:block`,
  `$x:item`).
- Should macro definitions be indexed by the LSP, and should an expansion
  preview be offered?
