# Cx — Language reference

> **Single source for the implemented language surface.** Every rule below is
> stated as *syntax → lowers to*, with notes. Plain C is not repeated; this file
> documents what Cx adds on top of it.
>
> Related docs: [`technical.md`](./technical.md) (implementation map, codegen
> passes, gotchas), [`macro-rules.md`](./macro-rules.md) (full `macro_rules!`
> reference), [`generics.md`](./generics.md) (trait design rationale),
> [`roadmap.md`](./roadmap.md) (pending work),
> [`required-features.md`](./required-features.md) (remaining low-level C gaps).

Doc filenames are **lowercase kebab-case**.

## 1. `namespace` — qualified names

```cxc
namespace aa::bb {
    int f() { return 0; }
}
```

→ `int aa__bb__f(void) { return 0; }`

- A namespace body may hold any declaration. Declared names are mangled by
  joining the namespace path and the name with `__`: the language-level `::`
  becomes `__`, so `aa::bb` + `f` is `aa__bb__f`.
- A namespace is a name-mangling device rather than a scope object: a path may
  be written more than once and the names simply accumulate in the same
  `aa__bb__…` space.
- Names at file scope are not mangled.

## 2. Structs, constructors and destructors

```cxc
struct Buf {
    char *p;
    int n;

    Buf(int n_) {
        p = alloc(n_);
        n = n_;
    }

    ~Buf() {
        release(p);
    }
};
```

→

```c
struct Buf { char *p; int n; };
void Buf__ctor(struct Buf *self, int n_);
void Buf__dtor(struct Buf *self);
/* … `Buf__ctor` / `Buf__dtor` bodies … */
```

- A constructor is `Type(params) { … }` and a destructor is `~Type() { … }`,
  declared **inside** the struct; both names must equal the struct's.
- They lower to `void Type__ctor(struct Type *self, params)` and
  `void Type__dtor(struct Type *self)`.
- Nested struct definitions are supported:
  `struct Outer { struct Inner { … }; … };`. Nested types are mangled through
  the enclosing path (`Outer::Inner` → `Outer__Inner`) and emitted before the
  embedder.
- Embedded (non-pointer) members of a struct type are wired into the enclosing
  ctor/dtor:
  - the constructor calls `<Field>__ctor(&self->field)` for members with a
    **zero-arg** constructor, before the body (declaration order);
  - the destructor calls `<Field>__dtor(&self->field)` for members with a
    destructor, after the body (reverse order).
- Pointer fields are never auto-constructed/destructed.

## 3. RAII

- A local whose type has a destructor is emitted with
  `__attribute__((cleanup(Type__dtor)))`, so the destructor runs on every scope
  exit (`return`, `break`, `continue`, `goto`, or falling off the block).
- Constructor calls: `T v(args);` and `T v = T(args);` both lower to
  `T v [cleanup]; T__ctor(&v, args);`.
- `#[cleanup(fn)] struct T { … }` makes a struct RAII-managed by calling `fn`
  at scope exit instead of the generated `Type__dtor`.
- `T v;` does **not** call a zero-arg constructor. Only `T v(args);` /
  `T v = T(args);` construct.
- There is **no move semantics**: returning an RAII local by value runs its
  destructor, so factories returning destructible handles are unsafe.
- If a struct embeds a destructible member but declares no `~T()`, the member
  destructors are not called (there is no hook).
- `#[ctor]` / `#[dtor]` attributes are rejected; the inline syntax is the only
  spelling.

## 4. Types and declarations

- A type is a **single token**: a built-in (`void char short int long float
  double unsigned signed _Bool bool size_t ptrdiff_t`) or any struct name.
- Pointers: `T *`. Function pointers: `T (*name)(params)` — the parameter list
  may be empty or unnamed, e.g. `int (*cmp)(int, int)`.
- Multiple words concatenate into one type (`unsigned int`, `long long`,
  `const char *`); `const` / `volatile` are seeded as type words.
- `typedef <type> <declarator>;` defines an alias (including function pointers),
  registers the name as a type, and emits the alias verbatim.
- Multiple declarators share one type: `int a, b = 2, c;` (each with its own
  optional initializer). A struct field list may do the same: `int x, y;`.
- `auto x = expr;` infers from the initializer: a call's return type (including
  an `auto` function), a constructor call, a literal, a string, a paren, a cast,
  a function parameter, a comparison/logical operand (`int`), a binary operand,
  or a global's type.
- `auto` may be a **function return type**: `auto f(params) { return …; }`
  infers from the first `return expr;` and rewrites the function's symbol, so
  callers — including `auto` at the call site — see the resolved type. Inference
  is conservative: a returned local cannot be inferred (locals are not in the
  symbol table).
- A bare identifier that names a function is emitted with its mangled name, so
  Cx functions can be passed as callbacks.

## 5. Expressions

Supported operators: `+ - * / %`, comparisons `== != < > <= >=`, logical
`&& || !`, bitwise `& | ^ ~ << >>`, increment/decrement `++ --` (prefix and
postfix), assignment `=` and compound `+= -= *= /= %=`, the conditional
`cond ? a : b` (right-associative; binds tighter than `=` and looser than
`||`), unary `-`, casts `(T)expr`, `sizeof(...)` / `sizeof expr`, field access
`.` and `->`, subscript `[]`, address-of `&`, dereference `*`, calls, and
parentheses.

### Associated calls — `Type::method(…)`

```cxc
Point p = Point::new(3, 4);
int x = Point::get_x(&p);
```

→ `struct Point p = Point__new(3, 4);`
  `int x = Point__get_x(&p);`

- `Type::name(args)` in expression position lowers to a call of the plain
  function `Type__name` (resolved and mangled like any other call).
- This is the explicit spelling of an associated function. The `.`/`->` form is
  sugar over it when the receiver's type is known.

### Receiver methods — `recv.method(…)`

```cxc
p.get_x();     // p is a Point
q->get_y();    // q is a Point *
```

→ `Point__get_x(&p);`
  `Point__get_y(q);`

- `recv.method(args)` and `recv->method(args)` resolve `recv`'s type from a
  **block-scoped** environment (function parameters and locals; also globals)
  and lower to `Type__method(&recv, args)`. A **pointer** receiver is passed
  through unchanged.
- Inside a constructor, destructor, or `implements` method body, `self` is bound
  to the enclosing struct as a pointer (`<Struct> *`), so `self.method(x)`
  lowers to `Type__method(self, x)`.
- An unresolved receiver — a C function-pointer field, an unknown type — is
  emitted verbatim as `recv.method(args)`, so it still compiles as a C call.
- The grammar distinguishes a method call from a field access by the `(` after
  the member; a `.`/`->` member with no `(` is always a field access.
- **Chained and computed receivers** lower too: the receiver is resolved first,
  so `a.b().c()` uses the return type of `A__b`, and `make().m()` /
  `arr[i].m()` / `(expr).m()` resolve through the call return type, struct
  member type, element type, parentheses or cast. When the receiver type cannot
  be inferred the call is left verbatim.

## 6. Statements

Expression statements, `return`, blocks, `if`/`else`, `while`,
`do … while (cond);` (a single-statement body is emitted wrapped in braces),
`for (init; cond; incr)` (each clause optional; **no declaration-in-init**),
`break`, `continue`, `switch`/`case`/`default`, `goto` and labels (`name:`).

## 7. Preprocessor and `#line`

- Cx does **not** run the C preprocessor. A line beginning with `#` (`#include`,
  `#define`, …) is captured as the **prelude** and emitted verbatim at the top
  of the generated C (before the include guard, for a `.hx`).
- Generated C carries `#line N "file.cx"` directives so compiler diagnostics
  and GDB backtraces point back at the Cx source.

## 8. C interop

### `raw { … }`

A verbatim C block, captured with balanced braces and emitted unchanged.
Allowed at file scope, as a struct member, and as a statement. Use it for
macro-generated types (`klist_t(IntList)`), instantiations, or any C the Cx
parser cannot read.

```cxc
raw {
    #define CX_NOFREE(p) ((void)(p))
}
```

### `NAME!( … )` — C-macro invocation passthrough

```cxc
KLIST_INIT!(IntList, int, cx_nofree);
```

→ `KLIST_INIT(IntList, int, cx_nofree);`

- `NAME!(…)` whose `NAME` is **not** a `macro_rules!` macro is a **C macro**
  invocation. The balanced argument list is captured textually and emitted
  verbatim as `NAME(args)`.
- The arguments may be types or concatenated identifiers, which Cx expressions
  cannot spell.
- Allowed at file scope and statement position (not in expressions yet).
- Without a trailing `!(…)`, `NAME` stays a normal identifier.

## 9. Static traits — `trait` / `implements`

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

- One **static vtable type** per trait (`struct <Trait>__vtable`), shared by all
  implementers; the receiver slot is `void *`.
- One **static table** and one **static impl method per member**, named
  `Type__Trait__method`, per implementing struct.
- The struct carries a **pointer field named after the trait** (`Iterable`),
  installed by its constructor — so dispatch is explicit: `r.Iterable->len(&r)`.
  There is no hidden global type id.
- A default-constructed local `T v;` gets the table pointer by an assignment
  emitted right after the declaration; globals use a designated initializer.
- `self` is a keyword receiver; a struct may implement several traits.
- Conformance is **explicit and static**: sema checks that every required method
  is present with a matching signature (the `self` receiver is ignored). No
  structural/duck typing, no inheritance, no class vtables.
- There is no postfix-call syntax yet, so reaching the table uses a
  function-pointer local: `int (*len)(void *) = r.T; len(&r);`.

Design rationale and open questions: [`generics.md`](./generics.md).

## 10. Attributes — `#[…]`

```cxc
#[inline]  int fast(void);
#[noreturn] void die(void);
#[export("c_api_name")] int api(void);
#[repr(packed)] struct P { char c; int i; };
```

- `#[name]` / `#[name(args)]` may precede an external declaration or a struct
  member; they stack, one per `#[…]`, and the argument text is captured
  verbatim.
- **Leading specifiers**: `#[inline]` → `inline`, `#[noreturn]` → `_Noreturn`.
- **`#[export("name")]`** — the function is emitted *and called* under the plain
  C name `name` (no namespace mangling).
- **`#[cleanup(fn)]`** on a struct — see §3.
- **Passed through** as `__attribute__((…))`: `#[packed]` / `#[repr(packed)]`,
  `#[aligned(n)]`, `#[deprecated]`, `#[unused]`, `#[warn_unused_result]`,
  `#[naked]`, `#[section(…)]`, and any other name (unknown names become a raw C
  attribute).
- `#[test]` is ignored in the C output.
- `#[ctor]` / `#[dtor]` are errors — see §3.
- Attributes attach to a declaration (an external declaration or a struct
  member), not to statements.

## 11. Declarative macros — `macro_rules!`

```cxc
macro_rules! define_pair {
    ($name:ident, $t:ty) => {
        struct $name { $t a; $t b; };
    };
};

define_pair!(Point2, int);
```

→ `struct Point2 { int a; int b; };`

- A macro has one or more `(pattern) => { transcription }` rules; a pattern
  matches a token tree with metavariables and repetition.
- **Fragments**: `$x:ident`, `$x:field`, `$x:tt`, `$x:item`, `$x:block`, `$x:lit`,
  `$x:path`, `$x:ty`, `$x:expr`, `$x:stmt`, and `$x:method` (a qualified
  `Trait::member` reference). `$x:expr` / `$x:ty` capture to the next top-level
  `,` / `;`; `$x:item` captures a balanced group or a `;`-terminated run;
  `$x:block` captures a `{ … }`.
- **Repetition**: `$( … )*` / `$( … )+`, optionally with a separator
  (`$( … ),*`). Repetitions nest (`$( $( … )* )*`); each metavariable capture
  records the index path that reaches it. `##` concatenates tokens.
- `$name::method` in a transcription is the public spelling of the internal
  `Name__method` name; the expander records the origin so the LSP can jump from
  a generated method to the macro body.
- **`requires`**: an optional clause between the pattern and `=>` binds traits to
  metavariables — `($x:expr) requires $x: Iterable => { … }`, or several
  `requires $k: Hashable + Equatable, $v: Clone`. It is **checked** after the
  AST is built:
  - a **struct** binding must `implements` the trait;
  - a **primitive** binding (`int`, `unsigned int`, `const char`, …) satisfies
    the built-in capability traits `Hashable`, `Equatable`, `Comparable`,
    `Ordered` (and is an error otherwise);
  - an `ident`/`path` binding that is not a struct must name a method of the
    trait;
  - anything else (a C type Cx does not know) is not checkable and is skipped.
- Expansion is **textual, before parsing** (up to 64 passes), so a macro can
  generate types *and* functions; definitions are hoisted (an invocation may
  precede its definition), and `cx expand` prints the result for debugging.
- Macros may cross `import` boundaries: before expanding a source, the macro
  pass collects `macro_rules!` definitions from the files it imports
  (recursively, cycle-bounded).
- Trait members are reached in a transcription through the field named after the
  trait: `($x).Iterable->len`.
- Not implemented: hygiene, a fully backtracking matcher, nested repetitions,
  `$x:block` / `$x:item`, and LSP indexing of macro definitions.

Full reference: [`macro-rules.md`](./macro-rules.md).

## 12. Headers and imports

`cx build foo.hx -o foo.h` emits a **declaration-only** header: an include
guard, typedefs, struct definitions, and prototypes (functions, constructors,
destructors) — no definitions. Pair it with a `.cx` that provides the `.c`
symbols, so hand-written C can `#include "foo.h"` and link `foo.c`.

A `.cx` can pull in another module with `import "mod.hx";`:

- the target is parsed, sharing the type-name set, so its types are visible to
  the importer;
- its declarations are merged into the symbol table for resolution only (never
  emitted);
- the generated C gets `#include "mod.h"`.

Imports are declaration-only. Paths resolve relative to the importing file;
cycles are skipped.

**Paired `.hx`/`.cx` model.** When a `.cx` is built and a sibling `.hx` with the
same basename exists (`foo.cx` ⇄ `foo.hx`), the transpiler pairs them: the
generated `foo.c` starts with `#include "foo.h"` and skips the declarations the
header already provides (preamble, typedefs, struct definitions). Only the
definitions are emitted. The header is the single source of the type shape.

## 13. Name mangling

| Cx | C |
| :-- | :-- |
| `namespace a::b { void f(); }` | `a__b__f` |
| `struct Foo` (global) | `struct Foo` |
| `struct Foo` in `ns` | `struct ns__Foo` |
| nested `Outer::Inner` | `Outer__Inner` |
| `~Foo()` / `Foo()` in `ns` | `ns__Foo__dtor` / `ns__Foo__ctor` |
| `implements T` method `m` in `Foo` | `Foo__T__m` (static) |
| per-type trait table | `Foo__T__table` (static const) |
| trait vtable type `trait T` | `struct T__vtable` |

`__` is the reserved separator: do not use `__` in your own Cx identifiers.

## 14. What is *not* an extension

Cx accepts plain C declarations, statements and expressions (structs, typedefs,
functions, `if`/`else`, `while`, `do … while`, `for`, `switch`/`case`/`default`,
`goto`/labels, the conditional `?:`, casts, `sizeof`, pointer types, subscript
`[]`, function-pointer declarators, …) without changing them. A few structural
points:

- A `struct` / `typedef` declaration requires a trailing `;`.
- A constructor/destructor is declared inside the struct; `#[ctor]` / `#[dtor]`
  are rejected.
- There are no `static` / `extern` / `inline` storage-class keywords yet
  (`#[inline]` covers inlining); array types and the comma operator are also
  missing. See [`required-features.md`](./required-features.md) and
  [`roadmap.md`](./roadmap.md).
- Names of locals/parameters are not offered by the *general* completion list —
  they are not in the symbol table — but they do drive receiver-type features.

## 15. Keyword style

Cx-introduced keywords are **lowercase full words** — no abbreviations
(`implements`, not `impl`; `function`, not `func`). Existing **C keywords** keep
their C spelling (`return`, `struct`, `typedef`, `sizeof`, `const`, …), since C
compatibility is a goal.