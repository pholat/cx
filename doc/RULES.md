# Cx rules — the extensions over C

> **What this is.** A rule-by-rule summary of the constructs Cx adds on top of C.
> Plain C is not repeated here, and the **klib-backed container layer is
> deliberately excluded** — it is an intermediate building block, not part of
> the extension (see [`TECHNICAL.md`](./TECHNICAL.md) §5 and
> [`ROADMAP.md`](./ROADMAP.md)). [`TECHNICAL.md`](./TECHNICAL.md) stays the
> authoritative, implemented map; this file is the compact "what are the rules"
> reference. Related: [`GENERICS.md`](./GENERICS.md) (traits/attributes/generics
> design), [`MACRO_RULES.md`](./MACRO_RULES.md) (`macro_rules!`),
> [`plan.md`](./plan.md) (architecture), [`integration.md`](./integration.md)
> (using Cx from a C build).

Every rule below is stated as **syntax → lowers to**, with notes. Lowercased
identifiers (`Point`, `Row`, `f`) stand for whatever the source names.

## 1. `namespace` — qualified names

```cxc
namespace aa::bb {
    int f() { return 0; }
}
```

→ `int aa__bb__f(void) { return 0; }`

* A namespace body may hold any declaration. Declared names are mangled by
  joining the namespace path and the name with `__`: the language-level `::`
  becomes `__` in C, so `aa::bb` + `f` is `aa__bb__f`.
* A namespace is a name-mangling device rather than a scope object: a path may
  be written more than once and the names simply accumulate in the same
  `aa__bb__…` space.
* Names at file scope are not mangled.

## 2. Constructors and destructors (RAII)

```cxc
struct Buf {
    char *p;
    int n;
    Buf(int n_) { p = alloc(n_); n = n_; }
    ~Buf()      { release(p); }
};
```

→

```c
struct Buf { char *p; int n; };
void Buf__ctor(struct Buf *self, int n_);
void Buf__dtor(struct Buf *self);
/* … `Buf__ctor`/`Buf__dtor` bodies … */
```

* A constructor is `Type(params) { … }` and a destructor is `~Type() { … }`,
  declared **inside** the struct; both names must equal the struct's.
* They lower to `void Type__ctor(struct Type *self, params)` and
  `void Type__dtor(struct Type *self)`.
* Any local of a struct type with a destructor is emitted with
  `__attribute__((cleanup(Type__dtor)))`, and a constructor call is emitted
  right after the declaration, so the value is torn down on every scope exit
  (`return`, `break`, `continue`, `goto`). This is Cx's RAII.
* `#[cleanup(fn)] struct T { … }` makes a struct RAII-managed by calling `fn`
  at scope exit instead of the generated `Type__dtor`.
* The RAII cleanup is **suppressed** where C already guarantees correctness —
  notably returning such a struct **by value** (see `TECHNICAL.md` §7).
* `#[ctor]`/`#[dtor]` attributes are rejected; the inline syntax is the only
  spelling.

## 3. Associated calls — `Type::method(…)`

```cxc
Point p = Point::new(3, 4);
int x = Point::get_x(&p);
```

→ `struct Point p = Point__new(3, 4);`
  `int x = Point__get_x(&p);`

* `Type::name(args)` in expression position lowers to a call of the plain
  function `Type__name` (resolved and mangled like any other call).
* This is the explicit spelling of an associated function. The `.`/`->` form
  (rule 4) is sugar over it when the receiver's type is known.

## 4. Receiver methods — `recv.method(…)`

```cxc
p.get_x();     // p is a Point
q->get_y();    // q is a Point *
```

→ `Point__get_x(&p);`
  `Point__get_y(q);`

* `recv.method(args)` and `recv->method(args)` resolve `recv`'s type from a
  **block-scoped** environment (function parameters and locals; also globals)
  and lower to `Type__method(&recv, args)`. A **pointer** receiver is passed
  through unchanged.
* Inside a constructor, destructor, or `implements` method body, `self` is bound
  to the enclosing struct as a pointer (`<Struct> *`), so `self.method(x)`
  lowers to `Type__method(self, x)`.
* An unresolved receiver — a C function-pointer field, an unknown type — is
  emitted verbatim as `recv.method(args)`, so it still compiles as a C call.
* The grammar distinguishes a method call from a field access by the `(` after
  the member; a `.`/`->` member with no `(` is always a field access.

## 5. Static traits — `trait` / `implements`

```cxc
trait Iterable {
    int len(self);
    int at(self, int i);
};

struct Row {
    int *cells;
    int n;

    implements Iterable {
        int len(self)        { return self->n; }
        int at(self, int i)  { return self->cells[i]; }
    }
};
```

→

```c
struct Iterable__vtable { int (*len)(void *); int (*at)(void *, int); };

struct Row { int *cells; int n; const struct Iterable__vtable *Iterable; };

static int Row__Iterable__len(void *self_);
static int Row__Iterable__at(void *self_, int i);
static const struct Iterable__vtable Row__Iterable__table = { … };
/* installed by `Row__ctor`: `self->Iterable = &Row__Iterable__table;` */
```

* One **static vtable type** per trait (`struct <Trait>__vtable`), shared by all
  implementers; the receiver slot is `void *`.
* One **static table** and one **static impl method per member**, named
  `Type__Trait__method`, per implementing struct.
* The struct carries a **pointer field named after the trait**
  (`Iterable`), installed by its constructor — so dispatch is explicit:
  `r.Iterable->len(&r)`. There is no hidden global type id.
* `self` is a keyword receiver; a struct may implement several traits.
* `implements` is checked: the trait must exist. Macro `requires` bounds are
  checked separately (rule 8).

## 6. Attributes — `#[…]`

```cxc
#[inline]  int fast(void);
#[noreturn] void die(void);
#[export("c_api_name")] int api(void);
#[repr(packed)] struct P { char c; int i; };
```

* Leading specifiers: `#[inline]` → `inline`, `#[noreturn]` → `_Noreturn`.
* `#[export("name")]` — the function is emitted *and called* under the plain C
  name `name` (no namespace mangling).
* `#[cleanup(fn)]` on a struct — see rule 2.
* `#[packed]` / `#[repr(packed)]` → `__attribute__((packed))`; **any other
  attribute name passes through** as `__attribute__((name))` /
  `__attribute__((name(args)))` (e.g. `#[aligned(n)]`, `#[deprecated]`,
  `#[unused]`, `#[section(…)]`), so GCC/Clang attributes are usable directly.
* `#[test]` is ignored in the C output; `#[ctor]`/`#[dtor]` are errors
  (rule 2).
* Attributes attach to a declaration (an external declaration or a struct
  member) and are stacked before it, one per `#[…]`.

## 7. `import` and the paired `.hx`/`.cx` model

```cxc
import "mod.hx";
```

→ `#include "mod.h"` in the generated C; the module's declarations are merged
into the symbol table for resolution only (never re-emitted).

* An imported `.hx` is parsed (sharing the type-name set, so its types are
  visible to the importer) and its declarations are merged so calls are mangled
  and types resolve.
* A `.cx` is **paired** with a sibling `.hx` of the same basename: building
  `foo.cx` emits a `foo.c` that begins with `#include "foo.h"` and skips the
  declarations the header already provides (preamble, typedefs, struct
  definitions), emitting only the definitions. The header is the single source
  of the type shape.
* **Macro definitions cross `import`**: before a source is expanded, the macro
  pass collects `macro_rules!` definitions from the files it imports
  (recursively) — so a `.hx` can export a macro (rule 8).
* Paths resolve relative to the importing file.

## 8. Declarative macros — `macro_rules!`

```cxc
macro_rules! define_pair {
    ($name:ident, $t:ty) => {
        struct $name { $t a; $t b; };
    };
};

define_pair!(Point2, int);
```

→ `struct Point2 { int a; int b; };`

* A macro has one or more `(pattern) => { transcription }` rules; a pattern
  matches a token tree with metavariables and repetition.
* **Fragments**: `$x:ident`, `$x:field`, `$x:tt`, `$x:lit`, `$x:path`,
  `$x:ty`, `$x:expr`, `$x:stmt`, and `$x:method` (a qualified `Trait::member`
  reference). `$x:expr`/`$x:ty` capture to the next top-level `,`/`;`.
* **Repetition**: single-level `$( … )*` / `$( … )+`, optionally with a
  separator (`$( … ),*`). `##` concatenates tokens.
* `$name::method` in a transcription is the public spelling of the internal
  `Name__method` name; the expander records the origin so the LSP can jump from
  a generated method to the macro body.
* **`requires`**: an optional clause between the pattern and `=>` binds traits to
  metavariables — `($x:expr) requires $x: Iterable => { … }`, or several
  `requires $k: Hashable + Equatable, $v: Clone`. It is **checked** after the
  AST is built:
  * a **struct** binding must `implements` the trait;
  * a **primitive** binding (`int`, `unsigned int`, `const char`, …) satisfies
    the built-in capability traits `Hashable`, `Equatable`, `Comparable`,
    `Ordered` (and is an error otherwise);
  * an `ident`/`path` binding that is not a struct must name a method of the
    trait;
  * anything else (a C type Cx does not know) is not checkable and is skipped.
* Expansion is **textual, before parsing** (up to 64 passes), so a macro can
  generate types *and* functions; a definition is hoisted (an invocation may
  precede its definition), and `cx expand` prints the result for debugging.
* Trait members are reached in a transcription through the field named after the
  trait: `($x).Iterable->len`.

## 9. `raw { … }` and the `#…` prelude — verbatim C

```cxc
#include "plat.h"          // prelude: `^#…` lines, emitted at the very top

raw {
    #define CX_NOFREE(p) ((void)(p))
}
```

* A `^[ \t]*#…` line is captured as the **prelude** and emitted verbatim at the
  top of the generated file (before the include guard, for a `.hx`).
* `raw { … }` captures a body with balanced braces verbatim and emits it as a
  preamble block (or in the header for a `.hx`). It may contain preprocessor
  lines and braces.
* These are escape hatches: Cx does not interpret their contents.

## 10. `NAME!( … )` — C-macro invocation passthrough

```cxc
KLIST_INIT!(IntList, int, cx_nofree);
```

→ `KLIST_INIT(IntList, int, cx_nofree);`

* `NAME!(…)` whose `NAME` is **not** a `macro_rules!` macro is a **C macro**
  invocation. The balanced argument list is captured textually and emitted
  verbatim as `NAME(args)`.
* Unlike an expression, the arguments may be types or concatenated identifiers,
  which Cx expressions cannot spell.
* Allowed at file scope and at statement position (not in expressions, yet).

## 11. `auto` — inferred types

```cxc
auto n = 5;                      // int
auto p = make_point(1, 2);       // the callee's return type
auto f(int x) { return x + 1; }  // int, from the first `return`
```

* `auto x = expr;` infers from the initializer: a call's return type (including
  an `auto` function), a constructor call, a literal, a string, a paren, a cast,
  a function parameter, a comparison/logical operand (`int`), a binary operand,
  or a global's type.
* `auto` as a **function return type** infers from the first `return expr;` and
  rewrites the function's symbol, so callers — including `auto` at the call site
  — see the resolved type.
* Inference is conservative: an initializer (or a return expression) it cannot
  type is an error (`cannot infer type for 'auto'` / `'auto' function`).

## 12. Preprocessor and `#line`

* Cx does **not** run the C preprocessor. `^#…` lines are passed through as the
  prelude (rule 9); everything else is parsed as Cx.
* Generated C carries `#line N "file.cx"` directives so compiler diagnostics
  point back at the Cx source.

## 13. What is *not* an extension (differences to keep in mind)

Cx accepts plain C declarations, statements and expressions (structs, typedefs,
functions, `if`/`else`, `while`, `do … while`, `for`, `switch`/`case`/`default`,
`goto`/labels, the conditional `?:`, casts, `sizeof`, pointer types, subscript
`[]`, function-pointer declarators, …) without changing them. A few structural
points:

* A `struct` / `typedef` declaration requires a trailing `;`.
* A constructor/destructor is declared inside the struct (rule 2); `#[ctor]` /
  `#[dtor]` are rejected.
* There are no `static` / `extern` / `inline` storage-class keywords yet
  (`#[inline]` covers inlining); see the "Language surface" list in
  [`ROADMAP.md`](./ROADMAP.md).
* Names of locals/parameters are not offered by the *general* completion list —
  they are not in the symbol table — but they do drive receiver-type features
  (rule 4).
