# Cx — Generics, traits & attributes (design)

> Status: **partly implemented / under discussion**. Implemented: **static traits**
> (§4: `trait`/`implements`, static per-type tables, one pointer per trait,
> conformance checking) and **C interop** (`raw { … }` + `NAME!(…)` invocation
> passthrough). Still under discussion: attributes (§5) and `macro!` (§6).
> `macro_rules!` (see [`MACRO_RULES.md`](./MACRO_RULES.md)) was picked mainly
> because C++-style templates are hard to *read* — not a strong reason by itself.
> Linked from `TECHNICAL.md` §8.

## 1. The problem

We want (some combination of):

1. **Parameterized containers** — `List<T>`, `Map<K,V>`.
2. **Capabilities on structs/functions** — "this type is ordered / hashable /
   printable", "this function is the destructor / is exported / is inline".
3. **Reusable abstraction** — one definition, many concrete types.
4. **Codegen control** — directives that today would be keywords/attributes.

These are three different axes (type parameters, behaviour interfaces, metadata)
and they don't have to share one mechanism. The mistake would be to force one.

## 2. Candidate building blocks

| Block | Axis | Notes |
| :-- | :-- | :-- |
| **Templates** (monomorphize `List<T>` → `List__int`) | type parameters | typed, toolable; verbose to read in C++ |
| **`macro_rules!`** | generativity | token-level; generates types *and* functions; poor tooling inside bodies |
| **Traits** | static capability sets | **no inheritance, no vtables**; `trait`/`impl`; queried by macros/generics |
| **Attributes `#[…]`** | metadata / codegen | annotate structs, fields, functions with directives |
| **Expression macros `name!(…)`** | generativity in expressions | `max!(a, b)` |

## 3. Rust syntax pieces worth borrowing

* **`#[ … ]` attributes** — metadata on items/fields/params. Very cheap to add and
  immediately useful (see §5), independent of the generics decision.
* **`macro!` invocation** — function-like macros usable in *expression* position
  (in addition to item/statement position). Rust's `!` disambiguates a macro call
  from a function call unambiguously at the syntax level.
* **`trait` / `impl`** — the interface/abstraction mechanism.

C++ `template<…>` is deliberately *not* proposed as syntax — it's the readability
problem that started this.

## 4. Traits (static, `implements` inside the struct)

> **Implemented** (see `TECHNICAL.md` §4/§5). The design below matches the
> lowering; one difference: because a call through the table needs a callee
> expression and Cx has no postfix-call syntax yet, reach the table via a
> function-pointer local (`int (*len)(void *) = r.T; len(&r);`).

Cx has **no inheritance and no class vtables**. A trait is a named set of
required member signatures. A struct opts in with an `implements` block **inside**
the struct:

```cxc
trait Iterable {
    int len(self);
    int at(self, int i);
}

struct Row {
    int *cells;
    int  n;

    implements Iterable {
        int len(self)       { return self->n; }
        int at(self, int i) { return self->cells[i]; }
    }
}
```

The `implements` block provides the methods and **registers** them under the
trait. Each trait gets a **static per-type table** (a `const` struct of function
pointers), and the implementing struct stores **one pointer** to its table — per
trait, not per method — so a macro/generic can call the methods without knowing
the concrete type:

```c
struct Iterable__vtable {
    int (*len)(void *self);
    int (*at)(void *self, int index);
};

static int Row__Iterable__len(struct Row *self) { return self->n; }
static int Row__Iterable__at(struct Row *self, int index) { return self->cells[index]; }

static const struct Iterable__vtable Row__Iterable__table = {
    (int (*)(void *))      Row__Iterable__len,
    (int (*)(void *, int)) Row__Iterable__at,
};

struct Row {
    int *cells;
    int  n;
    const struct Iterable__vtable *Iterable; /* set by the ctor */
};
```

and the generated constructor installs the single table pointer:

```c
self->Iterable = &Row__Iterable__table;
```

A struct implementing several traits carries one table pointer per trait.

The compiler **statically checks** that the `implements` block supplies every
member the trait requires (matching name and signature).

### Conformance is explicit

Conformance is **explicit** (`implements Trait { … }` in the struct). There is
**no structural / duck-typed** conformance — merely having members named
`len`/`at` is not enough. This keeps intent explicit, makes the check
unambiguous, and lets the LSP index "what type implements which trait".

### Using it from a macro

A macro requires the trait and calls through the registered (pointer) members:

```cxc
macro_rules! sum_over {
    ($x:expr) requires trait Iterable => {
        int total = 0;
        int i = 0;
        for (i = 0; i < $x->Iterable->len($x); i = i + 1) {
            total += $x->Iterable->at($x, i);
        }
        total
    };
}
```

`requires trait Iterable` restricts the macro to values whose type `implements
Iterable`, and gives the expansion access to the trait's members. It is resolved
**statically** — the macro body is only well-formed for conforming types.

### Why this covers list/map

A `Container`/`Iterable`/`KeyValue`-style trait gives macros the indirection they
need to operate over *any* conforming type — which is most of what a generic
`List`/`Map` needs — without inheritance and without a class hierarchy.

### Keyword style

Cx-introduced keywords are **lowercase full words** — no abbreviations
(`implements`, not `impl`; `function`, not `func`). Existing **C keywords** keep
their C spelling (`return`, `struct`, `typedef`, `sizeof`, `const`, …), since C
compatibility is a goal.

### Open refinements

* **Decided**: a single pointer to a shared **static per-type table** per trait
  (not per-instance function pointers) — smaller structs, C-idiomatic.
* Table / member naming after mangling (`Iterable__vtable`, `Trait__method`).
* Whether `requires trait` is the final spelling.

## 5. Sketch: attributes

> **Implemented** (see `TECHNICAL.md` §4). Honoured: `#[inline]`, `#[noreturn]`,
> `#[export("c_name")]`, `#[cleanup(fn)]`, `#[test]`; everything else passes
> through as `__attribute__((…))`. Attributes are accepted before external
> declarations and struct members (not statements). **`#[ctor]`/`#[dtor]` are
> rejected** — ctor/dtor declaration keeps the Cx syntax.

Attributes are the cheapest win here and orthogonal to generics. Candidates:

```cxc
#[inline]                 fn f() { ... }
#[export("c_name")]       fn g() { ... }        /* keep a plain C symbol name */
#[cleanup(close)]         struct File { int fd; }
#[repr(packed)]           struct Header { ... }
#[deprecated]
#[test]
```

Constructor/destructor declaration deliberately stays on the Cx syntax
(`Type(args) { … }` / `~Type() { … }`). `#[ctor]`/`#[dtor]` were considered as
attribute forms and **rejected**: the existing rules are already readable, and
writing `#[ctor]`/`#[dtor]` now raises an error pointing at the Cx syntax.

Implementation is largely mechanical: attach a list of `(name, args)` to the AST
node; the emitter/back-end honours the ones it knows and passes others through as
C attributes/`__attribute__((...))`. The LSP can surface them in hover.

## 6. Sketch: `macro!` (expression macros)

Beyond item `macro_rules!` (`MACRO_RULES.md`), allow invocation in expressions:

```cxc
int m = max!(a, b);
```

Rust-style `!` makes `name!(...)` unambiguous: it's always a macro, never a call,
so it can appear anywhere a primary expression can. This is independent of traits;
it's the "generative in expressions" half of the macro story.

## 7. How the pieces could combine

A coherent story might be:

1. **Attributes `#[…]`** first — metadata/codegen, cheap, useful immediately.
2. **`macro!`** — token-level generativity for containers/boilerplate (`List<T>`
   via `macro_rules!` generating klib instantiations), per `MACRO_RULES.md`.
3. **Traits** — explicit `implements` blocks inside structs (static; one pointer
   per trait to a shared static per-type table) that macros can `requires trait`
   and call through.
4. **Templates** — *only if* tooling/type-error quality for generic containers
   turns out to be worth it; otherwise macro-generated containers suffice.

klib stays the container backend in all current cases, but that may change to instead use the metaprogramming toolkit - and instead use klib as a base for the features required.

## 8. Recommendation (draft)

* Do **not** treat `macro_rules!` as the whole plan; it's one axis (generativity).
* Adopt **attributes `#[…]`** early — low risk, immediate value, and it gives a
  place to express codegen directives (`#[dtor]`, `#[cleanup]`, `#[export]`).
* Add **`macro!`** (expression position) alongside item `macro_rules!`.
* Introduce **static traits** — an `implements Trait { … }` block inside a struct,
  lowering to one pointer per trait to a shared **static per-type table**; macros
  `requires trait` and call through it (no inheritance, no class vtables). All new
  keywords are lowercase full words.
* Defer the template-vs-macro decision for *containers* until traits/attributes are
  in place and we can judge readability/ergonomics with real code.

## 9. Open questions

* `requires trait` spelling; can a macro require several traits at once
  (`requires trait Iterable, Comparable`)?
* Conformance check: **exact signature match**, or member names only?
* Trait member visibility/mangling: `Trait__method` vs `Type__Trait__method`.
* Do traits carry **associated types/constants** (needed for real generic
  containers, e.g. `Iterable::Item`)?
* Does `self` in `implements` methods follow the existing field-shorthand
  (`self->`)?
* Can a struct `implements` the same trait twice / two traits with a same-named
  member?
* Attributes: fixed built-in set, or namespaced/extensible? Which lower to C
  `__attribute__((...))` vs which are Cx-only?
* `#[dtor]` vs the existing `~Type()` rule — keep both or unify?
* Does `macro!` need to share hygiene with `macro_rules!` (`MACRO_RULES.md` §4)?
* Ordering with the lexer type-name set: traits/attributes/macros can all
  introduce *type* names, so expansion/annotation must run before later tokens are
  lexed (same constraint as `import`).
