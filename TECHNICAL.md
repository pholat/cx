# Cx — Technical Notes
> **Living document.** Read this first when starting a session. At the end of
> every session, update §4 "Language surface", §8 "Roadmap", and §9
> "Session log". Keep it accurate — it is the map of the codebase.

Related docs: `readme.md` (the approach decision), `plan.md` (design & roadmap),
`integration.md` (using Cx from a C build + editor setup),
`examples/klib/REQUIRED_FEATURES.md` (what Cx still lacks for low-level C).

---

## 1. Overview

Cx is a thin extension over C adding **namespaces** and **RAII** (struct
constructors/destructors). It is transpiled to **portable C** that any normal
toolchain (GCC, Clang, `arm-none-eabi-gcc`) compiles. The chosen architecture is
a **Flex/Bison source-to-source transpiler** with a single shared
AST + semantic layer consumed by **both** the C code generator and the LSP
server (so editor diagnostics cannot diverge from the transpiler).

Output relies on `__attribute__((cleanup(fn)))` (GCC/Clang).

---

## 2. Repository layout

```
cx/
├── readme.md, plan.md, integration.md, TECHNICAL.md
├── Makefile, .gitignore, .clangd
├── grammar/
│   ├── lexer.l                 # Flex scanner (reentrant, bison-bridge/locations)
│   └── parser.y                # Bison grammar (pure, LAC, AMBIGUITY notes)
├── include/cx/                 # public headers
│   ├── span.h   ast.h   buf.h   diag.h
│   ├── parser.h                # CxParseCtx, CxTypeSet, cx_parse
│   ├── macro.h                 # macro_rules! expansion (pre-parse pass)
│   ├── symbols.h codegen.h lsp.h cx.h (umbrella)
├── src/
│   ├── ast.c buf.c diag.c typeset.c macro.c
│   ├── sema/   symbols.c scope.c raii.c
│   ├── codegen/ mangle.c line_map.c c_emitter.c
│   ├── lsp/    json.{h,c} lsp_internal.h transport.c documents.c server.c
│   │           features/{diagnostics,symbols,definition,hover}.c
│   └── cli/    main.c
├── tests/ run.sh  golden/*.cx
├── examples/ hello.cx vec.cx  klib/{list.cx,map.cx,klib_bridge.{c,h},...}
└── submodules/klib/            # git submodule (header-only C containers)
```

Generated C and objects go to `build/` (git-ignored).

---

## 3. Build & test

Requires `cc`, `flex`, `bison`.

| Command | Effect |
| :-- | :-- |
| `make` | build `./cx` |
| `make test` | transpile every `tests/golden/*.cx` and compile the result |
| `make clean` | remove `build/` and `cx` |

The Makefile regenerates `build/parser.{c,h}` and `build/lexer.c` from the
grammar, and uses `-MMD -MP` so editing `include/cx/*.h` triggers rebuilds.

### CLI

| Command | Effect |
| :-- | :-- |
| `cx build <file.cx> [-o <out.c>]` | transpile to C (stdout without `-o`) |
| `cx expand <file.cx>` | print the source after `macro_rules!` expansion |
| `cx dump <file.cx>` | print the AST |
| `cx lsp` | run the language server on stdio |
| `cx --version` | print version |

### LSP

`cx lsp` speaks JSON-RPC 2.0 over stdio (Content-Length framing). Implemented:
`initialize`/`initialized`/`shutdown`/`exit`; `textDocument/publishDiagnostics`
on `didOpen`/`didChange`/`didClose` (full sync, `textDocumentSync: 1`); and
`documentSymbol`, `definition`, `declaration`, `hover`, and `completion` (each
reparses the stored document through the shared parser + symbol table).
`definition` follows symbols into imported files (the URI comes from the
symbol's source file); `completion` offers symbol-table names (functions,
structs, globals) plus keywords, filtered by the typed prefix.
`positionEncoding` is not negotiated (columns are byte offsets).

Completion caveats: plain **prefix** match (no fuzzy matching or auto-import);
only symbol-table entries are offered — **locals/parameters aren't in the
table**, so they don't appear; no snippet/trigger characters beyond `.`/`:`; and
struct members aren't completed after a `.`/`->` yet.

Neovim (this machine, 0.12): config lives in `~/.config/nvim/init.lua`
(`vim.lsp.config`/`vim.lsp.enable`, prefers the dev build `~/workspace/cx/cx`),
with `~/.config/nvim/syntax/cx.vim` + `~/.config/nvim/ftdetect/cx.vim`. After
rebuilding `cx`, restart the client (`:LspRestart`) — the running server keeps
the old binary in memory.

---

## 4. Language surface (implemented)

### Namespaces
* `namespace a { namespace b { … } }` and path form `namespace a::b { … }`.
* Mangled by joining the path with `__`: `a::b::Foo` → `a__b__Foo`.

### Structs
* Fields; **nested struct definitions** (`struct Outer { struct Inner { … }; … };`).
* C++-style **constructor** — a member named after the struct:
  `Counter(int v) { value = v; }`.
* **Destructor** — `~Counter() { … }`. The name must match the struct (enforced).
* Embedded (non-pointer) members of a struct type are wired into the enclosing
  ctor/dtor:
  * ctor calls `<Field>__ctor(&self->field)` for members with a **zero-arg**
    ctor, before the body (declaration order);
  * dtor calls `<Field>__dtor(&self->field)` for members with a dtor, after the
    body (reverse order).

### RAII
* A local whose type has a destructor gets
  `__attribute__((cleanup(Type__dtor)))`, so it runs on every scope exit.
* Constructor calls: `T v(args);` and `T v = T(args);` both lower to
  `T v [cleanup]; T__ctor(&v, args);`.

### Types
* A type is a **single token**: a built-in (`void char short int long float
  double unsigned signed _Bool bool size_t ptrdiff_t`) or any struct name.
* Pointers: `T *`. Function pointers: `T (*name)(params)` (the parameter list
  may be empty or unnamed, e.g. `int (*cmp)(int, int)`).
* Multi-word types are supported (`unsigned int`, `long long`, `const char *`)
  by concatenating successive type words; `const`/`volatile` are seeded as type
  words. `typedef <type> <declarator>;` defines an alias (including function
  pointers) and registers the name as a type; the alias is emitted verbatim.
* Multiple declarators share one type: `int a, b = 2, c;` (each with its own
  optional initializer). A struct field list may do the same: `int x, y;`.
* `auto x = expr;` infers the type from the initializer (call return type,
  constructor call, literal, paren, cast, or a known symbol's type).
* A bare identifier that names a function is emitted with its mangled name, so
  Cx functions can be passed as callbacks.

### Expressions
`+ - * / %`, comparisons `== != < > <= >=`, logical `&& || !`, bitwise
`& | ^ ~ << >>`, increment/decrement `++ --` (prefix and postfix), assignment
`=` and compound `+= -= *= /= %=`, unary `-`, casts `(T)expr`, `sizeof(...)` /
`sizeof expr`, field access `.` and `->`, subscript `[]`, address-of `&`,
dereference `*`, calls, parentheses.

### Statements
Expression statements, `return`, blocks, `if`/`else`, `while`,
`for (init; cond; incr)` (each clause optional; no declaration-in-init),
`break`, `continue`, `switch`/`case`/`default`, `goto` and labels (`name:`).

### Preprocessor
Lines beginning with `#` (`#include`, `#define`, …) are captured and emitted
verbatim at the top of the generated C.

### C interop

* `raw { … }` — a verbatim C block, captured with balanced braces and emitted
  unchanged. Allowed at file scope, as a struct member, and as a statement. Use
  it for macro-generated types (`klist_t(IntList)`), instantiations, or any C
  the Cx parser cannot read.
* `NAME!(args);` — a C function-like macro **invocation** passthrough, allowed at
  file scope and statement position (not in expressions yet). The lexer captures
  the balanced argument list verbatim (types and `##`-generated names are fine)
  and codegen emits `NAME(args)`, e.g. `KLIST_INIT!(IntList, int, 0);` lowers to
  `KLIST_INIT(IntList, int, 0);`. Without a trailing `!(…)`, `NAME` stays a
  normal identifier.

### Traits (static)

* `trait T { <ret> m(self, …); … };` declares a named set of required member
  signatures. The receiver is the `self` parameter; its type is supplied by the
  implementing struct.
* A struct opts in with an `implements T { <methods> }` block. Each method
  lowers to a `static` function `Struct__Trait__method(void *self_, …)` that
  casts `self_` back to the concrete struct.
* Each trait gets one `struct T__vtable` of function pointers; each implementing
  struct gets a `static const` per-type table and **one pointer field per
  trait** (`const struct T__vtable *T;`, field name = trait name).
* The pointer is installed by the constructor, or — for a default-constructed
  local `T v;` — by an assignment emitted right after the declaration. Globals
  use a designated initializer. (Consistent with RAII: `T v;` does not call a
  ctor.)
* Conformance is **explicit and static**: sema checks that every required method
  is present with a matching signature (the `self` receiver is ignored in the
  comparison). No structural/duck typing, no inheritance, no class vtables.
* There is no postfix-call syntax yet, so reaching the table uses a
  function-pointer local: `int (*len)(void *) = r.T; len(&r);`.

### Attributes

`#[name]` / `#[name(args)]` may precede an external declaration or a struct
member (stacked as needed, one per `#[…]`); the argument text is captured
verbatim.

* **Honoured**: `#[inline]` / `#[noreturn]` → leading C specifiers
  (`inline` / `_Noreturn`); `#[export("c_name")]` → the function is emitted *and
  called* under the plain C name (no namespace mangling); `#[cleanup(fn)]` on a
  struct → locals of that type get `__attribute__((cleanup(fn)))` (RAII without
  a `~Type()`); `#[test]` → ignored in C output.
* **Passed through** as `__attribute__((…))`: `#[packed]` / `#[repr(packed)]`,
  `#[aligned(n)]`, `#[deprecated]`, `#[unused]`, `#[warn_unused_result]`,
  `#[naked]`, `#[section(…)]`, and any other name (unknown names become a raw C
  attribute `__attribute__((name))`).
* **Rejected**: `#[ctor]` / `#[dtor]` are an error — constructor/destructor
  declaration keeps the Cx syntax (`Type(args) { … }` / `~Type() { … }`).

### Macros (`macro_rules!`)

Definitions and invocations are expanded by a **token-level pre-parse pass**
(`src/macro.c`), before the Cx parser runs. A definition produces no C:

```cxc
macro_rules! make_vec {
    ($name:ident, $t:ty) => {
        struct $name { $t *data; int len; };
        $t $name##_first($name *v) { return v->data[0]; }
    };
}
make_vec!(IntVec, int);          /* -> struct IntVec { … }; int IntVec_first( … ) */
```

* **Fragments**: `$x:ident`, `$x:ty`, `$x:expr`, `$x:stmt`, `$x:tt`, `$x:lit`,
  `$x:path`, `$x:field`.
* **Repetition**: `$( … )*` and `$( … )+`, with an optional single separator,
  e.g. `$( $x:expr ),*`. A trailing separator is accepted.
* **Concatenation**: `$name##_new` joins tokens into one identifier.
* **Invocation position**: any position the parser accepts after substitution —
  item/declaration, statement, or **expression** (`int m = max!(a, b);`).
* **Nested invocations** are expanded by repeated passes (a macro body may call
  another macro; the expansion is re-scanned).
* An invocation whose name is **not** a `macro_rules!` macro is left untouched,
  so the C-passthrough form `NAME!( … )` still works.
* Definitions are removed from the parsed source. Definitions are hoisted for
  resolution (an invocation may precede its definition). `cx expand` prints the
  expanded source for debugging.
* **Not yet**: hygiene (body-introduced identifiers are emitted verbatim); macro
  definitions do not cross `import` boundaries (each file is expanded on its
  own, so an imported `.hx` cannot export a macro to its importer); and macro
  definitions are not indexed by the LSP (they are gone before the AST is
  built).

### Headers and imports
`cx build foo.hx -o foo.h` emits a **declaration-only** header: an include guard,
typedefs, struct definitions, and prototypes (functions, constructors,
destructors) — no definitions. Pair it with a `.cx` that provides the `.c`
symbols, so hand-written C can `#include "foo.h"` and link `foo.c`.

A `.cx` can pull in another module with `import "mod.hx";`: the target is parsed
(sharing the type-name set, so its types are visible to the importer), its
declarations are merged into the symbol table (so calls are mangled and types
resolve), and the generated C gets `#include "mod.h"`. Imports are
declaration-only; a paired `.hx`/`.cx` model (where the `.c` includes the header
and skips duplicate declarations) is not implemented yet.

### Name mangling summary

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

`__` is the reserved separator.

---

## 5. Code generation

`src/codegen/c_emitter.c` emits a translation unit in **five passes** so the
dependency order is valid C:

0. **preamble** — trait vtable types (`struct T__vtable`), verbatim `raw { … }`
   blocks, and C macro invocations (anything a later struct/function depends on);
1. **struct definitions** (nested types first — the embedder needs a complete
   type) and typedefs;
2. **function + constructor + destructor prototypes** (so struct-by-value
   returns/params are valid, and a dtor can call an embedded dtor defined later),
   plus impl-method prototypes in `.c` output;
3. **destructor + constructor helpers**, preceded by impl methods and their
   `static const` vtables (the constructor installs the table pointers, so the
   tables must be defined first);
4. **function bodies + globals**.

Details:
* Struct tags are the mangled names (`struct ns__Foo`), so same-named structs in
  different namespaces never collide.
* In ctor/dtor bodies, a bare identifier that names a field is emitted as
  `self->field`.
* `#line N "file.cx"` is emitted before declarations so downstream compiler
  errors and GDB map back to the Cx source.
* Destructor helpers have **external linkage** (`void T__dtor(struct T *)`), so a
  generated header can declare them and a consumer TU can link the `.c`.
* Headers use `cx_emit_h` (phases 0–2) wrapped in an include guard. The vtable
  *types* and the struct's trait pointer fields are declared, but the `static`
  impl methods/tables are `.c`-only (a consumer TU only needs the layout + the
  vtable type to call through the pointer).
* `raw { … }` is emitted verbatim (first line indented; the rest kept as
  written). A top-level `raw`/macro line goes in phase 0.
* In an `implements` method body the receiver is a local
  `struct T *self = self_; (void)self;`, so `self->field` and bare field names
  both resolve.
* Attributes are emitted by `emit_attr_leading` (before the return type) and
  `emit_attr_trailing` (after the declarator / struct body). `#[export]` and
  `#[cleanup]` are resolved through the symbol table (`export_name`,
  `cleanup_fn`) rather than emitted.
* File-scope constructor calls are not supported (emitted as a comment).

---

## 6. Semantics & diagnostics

`src/sema/symbols.c` (`cx_sema_build`) walks the tree building a symbol table
(structs, fields, functions, globals) with namespace-qualified names, and:

* enforces **dtor/ctor name == struct name** and rejects duplicates;
* `check_calls` rejects a call whose callee is a struct with no constructor
  (`no constructor defined for struct 'T'`).

The LSP diagnostics path runs `cx_parse` **and** `cx_sema_build`, so editor
diagnostics match the transpiler. `CxSpan` is 1-based inclusive; the LSP
converts to 0-based, end-exclusive ranges.

---

## 7. Design decisions & gotchas

Read this before changing the grammar/lexer.

* **Type-name lexer hack** (`CxTypeSet`, `src/typeset.c`). To disambiguate
  `T *x` from `a * b`, the lexer returns `TYPE_NAME` for identifiers in a known
  set (built-ins + struct names). Struct names are registered by a **mid-rule
  action** in `struct_decl`, before the body, so self-references
  (`Node *next;` inside `struct Node`) work. Ripples: ctor/dtor member names and
  ctor-call expressions use `TYPE_NAME`; an identifier that shadows a type name
  is mislexed; types are single tokens.
* **Dangling `else`** is resolved with `%prec LOWER_THAN_ELSE` vs `%nonassoc
  ELSE`.
* **Unary vs binary** `*`/`&` and pointer `type '*'` live in different parser
  states (after `TYPE_NAME` vs after an expression), so no conflict; unary
  precedences are `UDEREF`/`UADDR`, postfix is `POSTFIX`.
* **Prelude lifecycle**: preprocessor lines are accumulated in
  `CxParseCtx.prelude` (`CxBuf`), attached to the TU node (`CxNode.prelude`), and
  emitted at the top; the buffer is freed in `cx_parse`.
* **Locations** are tracked explicitly in the lexer (`ctx.line`/`column`, newline
  counting in `YY_USER_ACTION`) — do not rely on Flex `yylineno`.
* **No move semantics.** Returning an RAII local by value runs its destructor, so
  factories returning destructible handles are unsafe. klib wrappers avoid this
  by not using a destructor (manual `free`).
* **Default construction.** `T v;` does **not** call a zero-arg constructor; only
  `T v(args);` / `T v = T(args);` do.
* **No implicit destructor.** If a struct embeds a destructible member but
  declares no `~T()`, member dtors are not called (no place to hook them).
* **`auto` inference** is a sema pass (`infer_auto`) that rewrites the var's
  type in place. It resolves call return types, constructor calls, literals,
  parens, casts, and identifiers of symbols in the table (globals/functions);
  anything else reports *cannot infer type for 'auto'*. Locals/params are not in
  the table.
* **Typedef aliases** are emitted verbatim (`typedef …`) and registered in the
  lexer type set, but struct-specific behaviour (cleanup, member ctor/dtor calls,
  `auto` inference) keys off the **struct name**, not the alias — so aliasing a
  destructible struct loses its RAII. Use the struct name directly for RAII
  types.
* **Multi-word types** come from `type TYPE_NAME` (concatenation), so any two
  type words combine (`int int` is accepted by the parser and left to the C
  compiler to reject).
* **Keyword style**: Cx-introduced keywords are **lowercase full words** — no
  abbreviations (`implements`, not `impl`; `function`, not `func`). C keywords
  keep their C spelling (`return`, `struct`, `typedef`, `sizeof`, `const`, …),
  since C compatibility is a goal. There is **no inheritance**; abstractions are
  built from an explicit `implements Trait { … }` block inside a struct (static
  traits; one pointer per trait to a static per-type table) — see
  [`GENERICS.md`](./GENERICS.md) §4.
* **Imports** (`import "mod.hx";`) are parsed with a nested `parse_source` that
  shares the same `CxParseCtx` (so the type set is shared) but saves/restores the
  per-parse fields (line/column/result/filename/prelude). Paths resolve relative
  to the importing file; cycles are skipped via `ctx->active`. Imported
  declarations are merged in sema for resolution only (never emitted); the output
  is a single `#include "mod.h"`. Diagnostics raised while parsing an import are
  attributed to the importing file. Import declaration-only `.hx` modules — a
  `.cx` has no generated header to `#include`.
* **Verbatim capture** (`raw { … }`, `NAME!( … )`) uses two Flex start conditions
  (`RAW`/`MACRO`) that copy balanced `{}`/`()` into a buffer in `CxParseCtx`; the
  text is copied into the arena when the token is produced, so it survives LAC
  lookahead (which may lex ahead without reducing). `raw` is a block only when
  followed by `{` (otherwise it stays an identifier), and `NAME!(…)` requires the
  `!` immediately after the identifier. Macro invocation is file/statement
  position only for now (no expression position).
* **Traits** are static and check-only: `implements T { … }` is verified against
  the trait's members (names + signatures; the `self` receiver is ignored). No
  postfix-call syntax exists, so a call *through* the vtable needs a
  function-pointer local. The table pointer is installed by the ctor or by a
  statement emitted after a default declaration `T v;` (and by a designated
  initializer for globals); a `malloc`'d struct is the user's responsibility.
* **Attributes** (`#[…]`) are captured by a Flex `ATTR` start condition; the
  preprocessor-line rule excludes `#[` so attributes are not swallowed. Leading
  (`inline`/`noreturn`) become specifiers; anything else becomes
  `__attribute__((…))` (unknown names pass through verbatim). `export`, `cleanup`
  and `test` are front-end only (never emitted as attributes); `ctor`/`dtor` are
  rejected with an error. Attributes are accepted before external declarations
  and struct members, not statements.
* **Macro expansion is a text pass before parsing** (`src/macro.c`). It tokenises
  the source into a small token tree, removes `macro_rules!` definitions, and
  substitutes matching invocations. Consequences: (a) definitions are not in the
  AST (no LSP macro symbols yet); (b) hygienic renaming is not implemented;
  (c) `$x:expr`/`$x:ty` capture greedily to the next top-level `,`/`;`, so a
  pattern cannot put another literal fragment after them unambiguously; (d) a
  trailing `;` after an item-position invocation is consumed (the expansion
  supplies the declarations), while an expression-position `;` is preserved;
  (e) `raw { … }`, preprocessor lines and `#[…]` attributes are opaque to the
  expander and are passed through verbatim. Expansion is iterative (max 64
  passes), so macros may invoke other macros. Nested repetitions are not fully
  modelled (the outer count is inferred from the first metavariable it finds);
  single-level `$( … )*`/`$( … )+` is the supported case.
* Bison blocks are written for **GNU Bison 3.8**; the parser is pure/reentrant
  and the scanner is `reentrant bison-bridge bison-locations`.
* In the Neovim syntax file, `~` must be escaped (`\~`) — `~` is the
  "previous substitute string" atom.

---

## 8. Roadmap (not yet implemented)

Ordered roughly by value (see `examples/klib/REQUIRED_FEATURES.md` for the full
matrix). Done: **C interop** (`raw { … }` + `#include` + `NAME!(…)` macro
invocation passthrough), **static traits** (`trait`/`implements`), **attributes**
(`#[…]`), and **`macro_rules!`** (item + expression-position expansion, see §4).

1. **klib-backed containers** — `List<T>` / `Map<K,V>` on klib, via macros or
   (later) templates — see the design note below. With `macro_rules!`, `raw`
   passthrough and static traits in place, a macro-defined container layer is the
   next step.
2. A paired `.hx`/`.cx` model (the `.c` includes its header and skips duplicate
   declarations).

Macro follow-ups (not blockers): hygiene for body-introduced identifiers,
LSP indexing/hover for macro definitions, and a richer fragment matcher
(backtracking beyond the current greedy `expr`/`ty` capture).

### Generics & macros (design note — under discussion)

> The type-abstraction direction is **not settled**. `macro_rules!` was chosen
> partly because C++ templates are hard to read; that rationale is being
> revisited in favour of a **traits + attributes + `macro!`** approach. See
> [`GENERICS.md`](./GENERICS.md) (traits/attributes/`macro!`) and
> [`MACRO_RULES.md`](./MACRO_RULES.md) (`macro_rules!`). These are different axes
> (type parameters, behaviour interfaces, metadata, generativity) and need not
> share one mechanism.

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

**Draft recommendation (see [`GENERICS.md`](./GENERICS.md) §8).** Treat the axes
separately: adopt **attributes `#[…]`** early (metadata/codegen); add **`macro!`**
(generativity) per [`MACRO_RULES.md`](./MACRO_RULES.md); **explicit static traits
are now implemented** (`implements` block inside a struct → one pointer per trait
to a shared static per-type table, statically checked) — macros will opt in with
`requires trait`; keep klib as the container backend. Option C (`macro_rules!`) is *a*
mechanism, not the whole plan; option B (template-like generics) is deferred
until tooling/type-error quality can be judged on real code. Exposing klib's
macros directly (A) stays confined to the `raw { … }` / invocation-passthrough
escape hatch — not the container API.

### Considered extensions (not committed)

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

---

## 9. Session log

Append a row per session with the headline changes.

### Session 2026-11-06 (initial)

* Evaluated approaches; chose **Flex/Bison transpiler**; wrote `readme.md`
  decision + `plan.md`.
* Scaffolded the project: headers, arena/AST, diag, buf, sema (symbols/scope/
  raii), codegen (mangle/line_map/c_emitter), LSP skeleton, CLI, Makefile,
  examples, golden tests.
* Fixed build/test issues: missing `%type` for `struct_member`; `yylex`/`yyerror`
  prototypes (`yyerror` takes `YYLTYPE *` first); empty-block shift/reduce;
  required `;` after structs; deterministic line/column tracking.
* Added `integration.md`.
* LSP: dependency-free JSON parser/serializer; real JSON-RPC dispatch;
  `publishDiagnostics` on open/change/close (runs parser+sema).
* Neovim: LSP config in `init.lua`; `syntax/cx.vim` + `ftdetect/cx.vim`.
* Language: nested namespaces (`namespace a::b`); destructor name == struct
  enforced; **C++-style constructors**; **nested structs**; **member ctor/dtor
  calls**; **preprocessor passthrough**; **control flow** (`if`/`else`/`while`/
  `for`/`break`/`continue`); **operators** (comparison/logical/bitwise,
  `++`/`--`, compound assignment); **pointers, `->`, `[]`, `&`/`*`** via the
  type-name lexer hack.
* Emission reordered to four passes (structs → protos → helpers → bodies) to
  enable struct-by-value returns and member ctor calls.
* klib (`submodules/klib`): added `klib_bridge.{c,h}` + `list.cx`/`map.cx`
  wrappers and `REQUIRED_FEATURES.md`.
* Roadmap item 1: **casts `(T)expr`** and **`sizeof`** (`CAST`/`SIZEOF` nodes;
  `casts.cx` test).
* Roadmap item 2: **`switch`/`case`/`default`**, **`goto`** and labels
  (`SWITCH`/`CASE`/`DEFAULT`/`LABEL`/`GOTO` nodes; `switch.cx` test).
* Roadmap item 3: **function pointers** — `T (*name)(params)` declarator
  (`declarator`/`fp_param` rules, `emit_decl`, `is_funcptr`); bare function
  identifiers emit their mangled name (`funcptr.cx` test).
* Roadmap item 4: **`auto`** type inference (`infer_auto` sema pass; `auto.cx`
  test).
* Roadmap item 5 (partial): **multi-word types** (`type TYPE_NAME`, `const`/
  `volatile`) and **`typedef`** (registered in the lexer type set; emitted in
  phase 1). `types.cx` test.
* **Multiple declarators** (`int a, b = 2;`): a declaration now holds a list of
  declarators (`one_decl_list`); codegen emits one line per declarator and sema
  iterates. `multi.cx` test.
* **Declaration-only headers**: `cx build foo.hx -o foo.h` (`cx_emit_h`, phases
  1–2 + include guard); destructors switched to external linkage so headers can
  declare them. `header.hx` test (`run.sh` now `-fsyntax-only`-checks `.hx`
  output).
* **LSP features**: `textDocument/documentSymbol`, `definition`, and `hover`
  (`features/nav.c` position→node + range helpers; `symbols.c`/`definition.c`/
  `hover.c`). The server dispatch handles them as requests.
* **Cx-to-Cx imports**: `import "mod.hx";` — nested parse (`parse_source`)
  sharing the type set, declaration merge in sema, and `#include "mod.h"` in the
  output. `run.sh` now transpiles headers first and compiles with `-I$tmp`.
  `imports.cx` + `imported.hx` test.
* **LSP completion**: `textDocument/completion` (symbol table + keywords,
  prefix-filtered; `completionProvider` advertised). `features/completion.c`,
  `cx_lsp_prefix_at`.
* **Planning**: added [`MACRO_RULES.md`](./MACRO_RULES.md) (Rust-style
  `macro_rules!` design) and linked it from the roadmap; set direction to
  C-interop (`raw { … }` + macro-invocation passthrough) + `macro_rules!`, with
  klib as the container backend.
* **LSP robustness**: imports resolve from `file://` URIs (and store the target
  as a URI); `definition`/`hover` resolve the **word under the cursor** (so type
  references work); each symbol records its source file, so `definition` jumps
  into imported files; `textDocument/declaration` is handled like `definition`
  and `declarationProvider` is advertised. (Neovim side: `gd` was not bound to
  LSP definition in the user config — added.)
* Tests added: `nested.cx`, `ctor.cx`, `nested_struct.cx`, `control.cx`,
  `operators.cx`, `prelude.cx`, `pointers.cx`, `casts.cx`, `switch.cx`,
  `funcptr.cx`, `auto.cx`, `types.cx`, `multi.cx`, `header.hx`.

### Session 2026-11-06 (C interop + static traits)

* **Roadmap 1 — C interop.** `raw { … }` verbatim C passthrough (file scope,
  struct member, statement) via a Flex `RAW` start condition capturing balanced
  braces; `NAME!( … );` C macro-invocation passthrough via a `MACRO` start
  condition capturing balanced parens, allowed at file/statement position.
  `#include`/`#define` passthrough already existed.
* **Roadmap 4 — static traits.** `trait T { <ret> m(self, …); … };` plus an
  `implements T { … }` block inside a struct. Lowers to one `struct T__vtable`
  per trait, a `static const` per-type table, and one pointer field per trait
  (field name = trait name). Sema checks conformance (method presence +
  signature; `self` receiver ignored). The pointer is installed by the ctor or,
  for `T v;`, by a statement after the declaration (globals via a designated
  initializer).
* Emitter gained **phase 0** (preamble: trait vtable types, `raw` blocks, macro
  invocations) and impl-method/vtable emission in phases 2–3; headers emit
  phases 0–2 (static impls are `.c`-only).
* AST: `CX_NODE_RAW`, `CX_NODE_MACRO`, `CX_NODE_TRAIT`, `CX_NODE_TRAIT_METHOD`,
  `CX_NODE_IMPLEMENTS`, `CX_NODE_IMPL_METHOD`; new keywords `trait`,
  `implements`, `self`; `CxParseCtx` gained capture buffers; `cx_buf_clear`.
* Tests: `raw.cx`, `traits.cx`.
* Not yet: expression-position macro invocation / a postfix call syntax (needed
  to call through a trait table without a function-pointer local); attributes
  `#[…]`; `macro_rules!`/`macro!`.

### Session 2026-11-06 (attributes)

* **Attributes `#[name]` / `#[name(args)]`** before external declarations and
  struct members. Honoured: `#[inline]`/`#[noreturn]` (leading specifiers);
  `#[export("c_name")]` (emitted *and* called under the plain C name, no
  mangling); `#[cleanup(fn)]` on a struct (locals get
  `__attribute__((cleanup(fn)))` — RAII without `~Type()`); `#[test]` (Cx-only).
  Everything else passes through as `__attribute__((…))` (`#[packed]`,
  `#[repr(packed)]`, `#[aligned(n)]`, `#[deprecated]`, `#[unused]`, …); unknown
  names become `__attribute__((name))`.
* Lexer: new `ATTR` start condition captures the attribute body; the
  preprocessor-line rule now excludes `#[` so attributes aren't swallowed.
* AST: `CX_NODE_ATTR` + a `CxVec attrs` on `CxNode`. Sema records `export_name`
  and `cleanup_fn` on symbols, and `cx_raii_dtor_name` prefers `#[cleanup]`.
  Emitter gained `emit_attr_leading`/`emit_attr_trailing` and `fn_c_name`; LSP
  hover shows the attributes. `cx dump` prints them.
* Tests: `attributes.cx`. Also fixed a pre-existing `-Wdiscarded-qualifiers`
  warning in `emit_struct_pass`.
* `#[ctor]` / `#[dtor]` raise an error — ctor/dtor declaration keeps `Type(args)`
  / `~Type()`.
* Not yet: attributes on statements.

### Session 2026-11-06 (`macro_rules!`)

* **Roadmap 1 — `macro_rules!`.** Added a token-level pre-parse expander
  (`include/cx/macro.h`, `src/macro.c`): tokenises into a token tree, collects
  and removes `macro_rules!` definitions, matches invocations, applies `##`
  concatenation, unrolls `$( … )*` / `$( … )+` repetitions with optional single
  separators, and substitutes fragments (`ident`, `ty`, `expr`, `stmt`, `tt`,
  `lit`, `path`, `field`). Expansion is iterative (nested invocations) and runs
  before `cx_parse` / `cx_import`, so generated types register in the lexer type
  set and generated functions in the symbol table.
* **Expression-position invocation** works because the expansion is textual; the
  result is re-parsed like ordinary Cx. Unknown `NAME!( … )` invocations are left
  alone, preserving C-macro passthrough.
* Item-position invocations own a trailing `;` (the expansion supplies complete
  declarations); expression-position invocations keep theirs.
* `cx expand <file>` prints the expanded source. `raw { … }`, preprocessor lines
  and `#[…]` attributes are opaque to the expander (passed through verbatim).
* Tests: `tests/golden/macro_rules.cx` (generated struct + function, `##`,
  repetition, expression position, nested macro invocation),
  `tests/golden/traits_make_total.cx` (`make_total!(total, Row)` generates the
  `total(Row)` function against the static vtable), and
  `tests/golden/traits_macro.cx` (`total!(r)` is an expression macro over a
  generic `iterable_total(void *, len, at)` helper). The two trait examples
  show the generated-function and expression-macro forms side by side.
* Not yet: hygiene; LSP symbols/hover for macro definitions; richer
  fragment capture (current `expr`/`ty` are greedy to the next `,`/`;`).

---

## 10. Maintaining this document

At the end of a session:

1. Append a dated entry to §9 with the headline changes.
2. Move finished items out of §8, and add any new gaps/gotchas to §7.
3. Update §4 (language surface) and, if affected, §5 (codegen) and §6
   (semantics).
4. Keep `plan.md` and `examples/klib/REQUIRED_FEATURES.md` consistent.
