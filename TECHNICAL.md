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
│   ├── symbols.h codegen.h lsp.h cx.h (umbrella)
├── src/
│   ├── ast.c buf.c diag.c typeset.c
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
| `cx dump <file.cx>` | print the AST |
| `cx lsp` | run the language server on stdio |
| `cx --version` | print version |

### LSP

`cx lsp` speaks JSON-RPC 2.0 over stdio (Content-Length framing). Implemented:
`initialize`/`initialized`/`shutdown`/`exit`; `textDocument/publishDiagnostics`
on `didOpen`/`didChange`/`didClose` (full sync, `textDocumentSync: 1`); and
`textDocument/documentSymbol`, `textDocument/definition`, `textDocument/hover`
(each reparses the stored document through the shared parser + symbol table).
`definition` follows symbols into imported files (the URI comes from the
symbol's source file). `positionEncoding` is not negotiated (columns are byte
offsets).

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
* Multi-word types (`unsigned int`, `long long`) are **not** supported.

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

`__` is the reserved separator.

---

## 5. Code generation

`src/codegen/c_emitter.c` emits a translation unit in **four passes** so the
dependency order is valid C:

1. **struct definitions** (nested types first — the embedder needs a complete
   type);
2. **function + constructor + destructor prototypes** (so struct-by-value
   returns/params are valid, and a dtor can call an embedded dtor defined later);
3. **destructor + constructor helpers**;
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
* Headers use `cx_emit_h` (phases 1–2 only) wrapped in an include guard.
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
* **Imports** (`import "mod.hx";`) are parsed with a nested `parse_source` that
  shares the same `CxParseCtx` (so the type set is shared) but saves/restores the
  per-parse fields (line/column/result/filename/prelude). Paths resolve relative
  to the importing file; cycles are skipped via `ctx->active`. Imported
  declarations are merged in sema for resolution only (never emitted); the output
  is a single `#include "mod.h"`. Diagnostics raised while parsing an import are
  attributed to the importing file. Import declaration-only `.hx` modules — a
  `.cx` has no generated header to `#include`.
* Bison blocks are written for **GNU Bison 3.8**; the parser is pure/reentrant
  and the scanner is `reentrant bison-bridge bison-locations`.
* In the Neovim syntax file, `~` must be escaped (`\~`) — `~` is the
  "previous substitute string" atom.

---

## 8. Roadmap (not yet implemented)

Ordered roughly by value for wrapping C libraries (see
`examples/klib/REQUIRED_FEATURES.md` for the full matrix):

1. **Generics / macros** (or rely on the C preprocessor).
2. klib wrappers: currently a hand-written bridge + int handles
   (`examples/klib/`); direct Cx wrappers need item 1.
3. A paired `.hx`/`.cx` model (the `.c` includes its header and skips duplicate
   declarations).

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
* Tests added: `nested.cx`, `ctor.cx`, `nested_struct.cx`, `control.cx`,
  `operators.cx`, `prelude.cx`, `pointers.cx`, `casts.cx`, `switch.cx`,
  `funcptr.cx`, `auto.cx`, `types.cx`, `multi.cx`, `header.hx`.

---

## 10. Maintaining this document

At the end of a session:

1. Append a dated entry to §9 with the headline changes.
2. Move finished items out of §8, and add any new gaps/gotchas to §7.
3. Update §4 (language surface) and, if affected, §5 (codegen) and §6
   (semantics).
4. Keep `plan.md` and `examples/klib/REQUIRED_FEATURES.md` consistent.
