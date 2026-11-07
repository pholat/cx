# Cx — Implementation Plan: `.cx` / `.hx` → Generic C

This is the working plan for **Cx**, the C-extension language whose origin and
approach decision are recorded in [`origin.md`](./origin.md). It transpiles `.hx`
(headers) and `.cx` (sources) into plain, portable C that any C compiler (`gcc`,
`clang`, `arm-none-eabi-gcc`, …) can consume, while keeping a clean seam for a
first-class LSP server.

---

## 1. Goals & Constraints

| Goal | Requirement |
| :--- | :--- |
| Output | Generic C (compilable by gcc/clang/embedded toolchains, no compiler lock-in) |
| Input | `.hx` headers, `.cx` sources |
| Language features | Namespaces + RAII (struct destructors) |
| Tooling | An LSP server must be cheap to add and share the *same* language definition |
| Forbidden | Modifying/patching GCC or forking a C frontend (no "plain GCC route") |

---

## 2. Decision

**Approach 3 — Source-to-Source Transpiler (Flex/Bison)** is chosen.

The decisive factor is **LSP reuse**. An LSP server needs, on every edit:

1. A parser that can be invoked on a string buffer, repeatedly and cheaply.
2. Precise source locations attached to every node (for diagnostic ranges).
3. Error recovery that still produces a usable (partial) AST.

Bison gives all three (`%locations`, `%define parse.lac full`,
`%define api.pure full` reentrant parsers, `error` productions). Clang
LibTooling does *not* parse the Cx syntax at all — its C grammar has no
`namespace` or `~Dtor`; supporting them means editing Clang's parser, which
reintroduces the exact maintenance burden we are avoiding.

Rationale in full:

* **New syntax, not C syntax.** Cx adds keywords/markers Clang cannot accept.
  Layering Cx *as* C (macros/attributes) to satisfy Clang would cripple the
  ergonomics that motivate the language.
* **One grammar, one AST.** The lexer, parser, and AST are shared verbatim
  between the codegen backend and the LSP frontend. There is no second, subtly
  diverging language definition (a classic transpiler/LSP drift bug).
* **LSP-native parser properties.** Reentrancy + location tracking + LAC map
  directly onto `textDocument/publishDiagnostics`, `documentSymbol`, and
  `definition`.
* **Portable output.** Codegen emits standard C using
  `__attribute__((cleanup(fn)))` (supported by gcc/clang/embedded GCC), so the
  result is not tied to any compiler version.
* **Clang stays optional.** Clang is retained *after* transpilation as an
  optional semantic verifier / second diagnostic source — never as the
  frontend.

---

## 3. Pipeline Architecture

```
 .cx / .hx
    |
    v
+--------+     +--------+     +---------+     +----------+
| Lexer  | --> | Parser | --> |   AST   | --> |  Sema    |
|(Flex)  |     |(Bison) |     |(shared) |     |(scopes,  |
+--------+     +--------+     +---------+     | symbols, |
    ^               |             |           | RAII)    |
    |               |             |           +----------+
    |          diagnostics      +----+--------+     |
    |                           |             |     |
    |                           v             v     v
    |                    +------------+  +--------+ +----------+
    |                    |  Codegen   |  |  LSP   | | #line    |
    |                    | (C output) |  | Server | | mapping  |
    |                    +------------+  +--------+ +----------+
    |                          |             |
    |                          v             v
    |                     generic .c      diagnostics,
    |                     / .h           symbols, hover
    |                                        |
    +----------------------------------------+
         same lexer/parser/AST source files
```

The **AST + Sema** layer is the contract. Codegen and the LSP are two consumers
of the same tree; neither owns the grammar.

---

## 4. Proposed Directory Layout

```
cx/
├── readme.md                  # entry point + architecture diagram
├── doc/                       # all documentation
│   ├── TECHNICAL.md           # living map of the codebase (read first)
│   ├── ROADMAP.md             # priorities + refinement backlog
│   ├── changelog.md           # session log
│   ├── GENERICS.md
│   ├── MACRO_RULES.md
│   ├── plan.md                # this document
│   ├── integration.md
│   ├── origin.md              # origin/approach decision
│   └── REQUIRED_FEATURES.md
├── Makefile
├── .gitignore
├── grammar/
│   ├── lexer.l                # Flex lexer (shared, reentrant)
│   └── parser.y               # Bison grammar (shared, reentrant, LAC)
├── include/cx/
│   ├── span.h                 # source spans (line/col, offsets)
│   ├── ast.h                  # AST node definitions (shared)
│   ├── diag.h                 # diagnostic + severity types
│   ├── symbols.h              # symbol/scope model
│   └── cx.h                   # umbrella public header
├── src/
│   ├── ast.c                  # AST constructors / arena / free
│   ├── diag.c                 # diagnostic collection + rendering
│   ├── sema/
│   │   ├── scope.c            # namespace + block scope resolution
│   │   ├── symbols.c          # symbol table (codegen + LSP)
│   │   └── raii.c             # destructor discovery, scope-exit analysis
│   ├── codegen/
│   │   ├── c_emitter.c        # AST -> generic C text
│   │   ├── mangle.c           # namespace name mangling
│   │   └── line_map.c         # #line directive injection
│   ├── lsp/
│   │   ├── server.c           # JSON-RPC + lifecycle (initialize/…)
│   │   ├── transport.c        # stdio framing (Content-Length)
│   │   ├── documents.c        # document store + text sync
│   │   └── features/
│   │       ├── diagnostics.c  # → publishDiagnostics
│   │       ├── symbols.c      # → documentSymbol
│   │       ├── definition.c   # → definition (via symbol table)
│   │       └── hover.c        # → hover
│   └── cli/
│       └── main.c             # `cx build`, `cx lsp`, `cx dump`, `cx expand`
├── tests/
│   ├── golden/                # input .cx/.hx, compiled after transpilation
│   └── run.sh                 # transpile + compile every golden file
├── examples/
│   ├── hello.cx
│   └── vec.cx
└── submodules/klib/           # header-only C containers (git submodule)
```

*(This is the plan as written; the actual tree evolved — see `TECHNICAL.md` §2
for the current layout.)*

---

## 5. Language → C Translation Rules

### 5.1 Namespaces → symbol mangling

```cxc
namespace io {
    struct File { int fd; };
    void open_file(File *f);
}
```

```c
/* io__File, io__open_file */
struct io__File { int fd; };
void io__open_file(struct io__File *f);
```

* Separator: `__` (double underscore).
* Nested namespaces join: `a::b::f` → `a__b__f`. A namespace can be declared
  either by nesting (`namespace a { namespace b { … } }`) or directly with a
  path (`namespace a::b { … }`).
* Global scope stays unmangled.

### 5.2 RAII / struct destructors → `cleanup`

```cxc
struct File {
    int fd;
    ~File() { close(fd); }
};

void use() {
    File f = open_file("x");
}   /* ~File() runs here */
```

```c
struct File { int fd; };

static inline void File__dtor(struct File *self) {
    close(self->fd);
}

void use(void) {
    struct File f __attribute__((cleanup(File__dtor))) = open_file("x");
}
```

* Each destructor becomes a function receiving a pointer to the variable.
* Every local with a destructible type gets the `cleanup` attribute.
* Scope exit, `break`, `continue`, `return`, and `goto` all trigger cleanup
  automatically — matching normal C++ RAII semantics.
* Order: destructors run in reverse declaration order (guaranteed by C's
  `cleanup` on block exit).

### 5.2b Constructors (C++-style)

A constructor is a struct member named after the struct (mirroring the
destructor rule, which the semantic pass enforces):

```cxc
struct File {
    int fd;
    File(int fd_) { fd = fd_; }   /* constructor */
    ~File() { close(fd); }
};
```

It lowers to a helper plus an explicit call, so declaration and construction are
separate but always paired:

```c
void File__ctor(struct File *self, int fd_);
...
void File__ctor(struct File *self, int fd_) { self->fd = fd_; }

void use(void) {
    struct File f __attribute__((cleanup(File__dtor)));
    File__ctor(&f, 3);
}
```

Two declaration forms are accepted and both lower to the same code:

```cxc
File f(3);        /* direct construction */
File f = File(3); /* initializer form */
```

Because constructors can be used before their definition, the emitter orders the
output as: struct definitions -> function/constructor prototypes -> destructor
and constructor helpers -> function bodies. This also makes struct-by-value
returns (e.g. factory functions) valid, since the struct type is complete before
any prototype that uses it.

### 5.2c Nested structs

A struct may declare nested types, which are mangled with the enclosing path
(`demo::Outer` + `Inner` -> `demo__Outer__Inner`) and emitted before the struct
that embeds them:

```cxc
struct Outer {
    struct Inner {
        int x;
        ~Inner() { reset(x); }
    };

    Inner inner;   /* embedded by value: struct demo__Outer__Inner inner; */
    int y;
};
```

Nested types are referenced by their bare name (`Inner`), consistent with the
current bare-name type resolution. Nested structs may themselves be nested, and
their constructors/destructors are emitted like any other struct's.

Embedded members are wired into the enclosing struct's constructor and
destructor:

* the enclosing constructor calls `<Field>__ctor(&self->field)` for every
  embedded field whose type declares a **zero-argument** constructor, before its
  own body (declaration order);
* the enclosing destructor calls `<Field>__dtor(&self->field)` for every
  embedded field whose type has a destructor, after its own body (reverse
  declaration order).

Fields whose constructor requires arguments are not auto-constructed; pointer
fields are never auto-constructed/destructed. If a struct embeds a destructible
member but declares no destructor of its own, the member's destructor is not
called — declare a `~Type() {}` (even empty) to get one.

### 5.2d Expressions and control flow

* Operators: `+ - * / %`, comparisons `== != < > <= >=`, logical `&& || !`,
  bitwise `& | ^ ~ << >>`, increment/decrement `++ --` (prefix and postfix),
  assignment `=` with compound `+= -= *= /= %=`, unary `-`, field access `.` and
  `->`, subscript `[]`, address-of `&`, dereference `*`, calls, parentheses.
* Types: a type token — built-ins from a fixed set, or any struct name — plus
  `T *` pointers. The lexer tracks known type names (the C "typedef-name" lexer
  hack), so `T *x` stays unambiguous from the expression `a * b`. Multi-word
  types are concatenated (`unsigned int`, `const char *`).
* Statements: expression statements, `return`, blocks, `if`/`else`, `while`,
  `for`, `switch`, `break`, `continue`, `goto`.
* `for` takes the C form `for (init; cond; incr)` where each clause is optional
  (`for (;;)`); declaration-in-init is not supported yet.
* Preprocessor lines (`#include`, `#define`, …) at the start of a line are
  captured and emitted verbatim at the top of the generated C.

Still missing for heavy C code (see [`REQUIRED_FEATURES.md`](./REQUIRED_FEATURES.md)):
array types, `do…while`, ternary `?:`, the comma operator, and a fully
backtracking macro matcher.

### 5.2e `macro_rules!` (implemented)

`macro_rules!` definitions are expanded by a token-level pre-parse pass
(`src/macro.c`) before the Cx parser runs. Definitions produce no C; invocations
in item, statement or expression position are matched and replaced by the
transcribed source, then re-parsed. Fragments (`ident`, `ty`, `expr`, `stmt`,
`tt`, `lit`, `path`, `field`, `method`), repetitions (`$( … )*`/`$( … )+`) and
`##` concatenation are supported. Unknown `NAME!(…)` invocations stay available
as C-macro passthrough. See `TECHNICAL.md` §4/§7 and `MACRO_RULES.md`.

### 5.3 Headers (`.hx`)

* Transpile to `.h` with an include guard.
* Declarations stay declarations; destructor helpers are emitted with external
  linkage so a consumer TU can link the `.c`.
* `#line` mapping is applied so diagnostics point at `.hx`, not the output.

### 5.4 `.cx` / `.hx` pairing

* `.cx` → `.c` includes the generated header of the matching `.hx`.
* The CLI keeps a 1:1 basename mapping (`foo.cx` ⇄ `foo.hx` ⇄ `foo.c`).

---

## 6. Shared AST Design (the LSP seam)

Nodes carry a span and are arena-allocated so a full document parse is cheap
and discardable (ideal for an LSP request cycle).

```c
/* include/cx/span.h */
typedef struct {
    int first_line, first_column;   /* 1-based, inclusive */
    int last_line,  last_column;
} CxSpan;

/* include/cx/ast.h */
typedef enum {
    CX_NODE_TRANSLATION_UNIT,
    CX_NODE_NAMESPACE,
    CX_NODE_STRUCT,
    CX_NODE_DESTRUCTOR,
    CX_NODE_CONSTRUCTOR,
    CX_NODE_FUNCTION,
    CX_NODE_VAR_DECL,
    CX_NODE_ERROR            /* Bison error-recovery node */
    /* … */
} CxNodeKind;

typedef struct CxNode {
    CxNodeKind kind;
    CxSpan     span;
    /* kind-tagged fields */
} CxNode;
```

Design rules that make the LSP easy to add:

1. **Everything is a span.** No node lacks location info; diagnostics and
   ranges are derived, never guessed from text offsets.
2. **Error nodes are first-class.** Bison `error` productions produce
   `CX_NODE_ERROR` nodes, so an incomplete edit still yields a tree.
3. **Reentrant parser.** `%define api.pure full` with `%parse-param`; the LSP
   can run parses on any buffer without global lexer/parser state.
4. **Arena lifecycle.** `cx_parse(buffer, len)` returns an arena that the
   caller frees wholesale — one allocation pool per LSP request.
5. **Sema is pure.** `cx_sema_build(ast) -> symbols` takes only the AST and
   returns a symbol table, consumed identically by codegen and LSP features.

---

## 7. LSP Server Design

* **Transport:** JSON-RPC 2.0 over stdin/stdout using the standard
  `Content-Length` header framing.
* **JSON:** a small embedded parser/serializer to avoid a heavy dependency.
* **Lifecycle:** `initialize` → `initialized` → `shutdown` → `exit`.
* **Sync:** `textDocument/didOpen`, `didChange` (full sync), `didClose`.
  Documents held in an in-memory store keyed by URI.
* **Features:**
  1. `textDocument/publishDiagnostics` — from the parser + Sema checks.
  2. `textDocument/documentSymbol` — namespaces, structs, functions, vars.
  3. `textDocument/definition` — via the shared symbol table (+ macro origins).
  4. `textDocument/hover` — type/signature from the symbol table (+ doc comments).
  5. `textDocument/completion` — symbol table + keywords.

Because the parser is reentrant, each request parses the current buffer on
demand. See `TECHNICAL.md` §3 for the current state.

---

## 8. Diagnostics Strategy

* Enabled in `parser.y`:
  ```yacc
  %locations
  %define parse.error verbose
  %define parse.lac full
  ```
* `yyerror` override takes `YYLTYPE *locp` and reports `line:column`; the LSP
  wraps this into a `Diagnostic` with a `range`.
* **Codegen** injects `#line N "foo.cx"` before each emitted construct so that
  downstream compiler errors and GDB backtraces map back to the original
  `.cx`/`.hx` source.

---

## 9. Build System

* `Flex` + `Bison` generate `lexer.c` / `parser.c` into `build/`.
* `Makefile` targets:
  * `make` — build the `cx` CLI (transpiler + `cx lsp` subcommand).
  * `make test` — transpile + compile the golden tests.
  * `make clean`.
* C11, `-Wall -Wextra`, no external runtime deps.

---

## 10. Phased Roadmap

| Phase | Deliverable | Exit criteria |
| :--- | :--- | :--- |
| 0 | Scaffolding, `Makefile`, `lexer.l`/`parser.y` | `cx --version` builds |
| 1 | Lexer + parser + AST for namespaces & destructors | Golden parse tests pass |
| 2 | Codegen: mangling + `cleanup` emission | Generated C compiles with gcc **and** clang |
| 3 | Diagnostics + `#line` mapping + `cx build` | Errors point at `.cx`/`.hx` lines |
| 4 | Sema: scopes, symbols, RAII analysis | Symbol table tests pass |
| 5 | `cx lsp`: transport, doc store, diagnostics, symbols, definition, hover | Editor integration works |
| 6 | Polish: incremental reparse, more LSP features | Usable daily driver |

The live ordering is kept in [`ROADMAP.md`](./ROADMAP.md).

---

## 11. Testing Strategy

* **Golden tests:** `tests/golden/<name>.{cx,hx}` transpiled and then compiled
  by the host compiler; `.fail.cx` files must fail.
* **Compile tests:** every golden output is compiled; a runtime harness (TODO)
  verifies destructors fire on scope exit, `return`, and `break`.
* **LSP tests:** drive the server over pipes with recorded JSON-RPC
  request/response fixtures (diagnostics, symbols, definition, hover).
* **Fuzz/robustness:** feed malformed Cx and assert no crash + a diagnostic.

---

## 12. Risks & Mitigations

| Risk | Mitigation |
| :--- | :--- |
| Error desync between `.cx` and generated `.c` | `#line` directives |
| Bison error recovery quality | `%define parse.lac full`, curated `error` productions, `CX_NODE_ERROR` |
| LSP/transpiler drift | Single shared lexer/parser/AST/Sema; no second grammar |
| `cleanup` on non-GNU compilers | Document GCC/Clang requirement; keep output behind the attribute |
| Grammar growth | Keep grammar small; push semantics into Sema |

---

## 13. Immediate Next Steps

See [`ROADMAP.md`](./ROADMAP.md) — the "Refinement" section is the current
backlog.
