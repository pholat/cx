# Cx — Implementation Plan: `.cx` / `.hx` → Generic C

This is the working plan for **Cx**, the C-extension language described in
`readme.md`. It transpiles `.hx` (headers) and `.cx` (sources) into plain,
portable C that any C compiler (`gcc`, `clang`, `arm-none-eabi-gcc`, …) can
consume, while keeping a clean seam for a first-class LSP server.

---

## 1. Goals & Constraints

| Goal | Requirement |
| :--- | :--- |
| Output | Generic C (compilable by gcc/clang/embedded toolchains, no compiler lock-in) |
| Input | `.hx` / `.hx` headers, `.cx` sources |
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
  Layouting Cx *as* C (macros/attributes) to satisfy Clang would cripple the
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
├── readme.md                  # decision + overview
├── plan.md                    # this document
├── Makefile                   # build: transpiler + LSP + tests
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
│       └── main.c             # `cx build`, `cx lsp`, `cx fmt`
├── tests/
│   ├── golden/                # input .cx/.hx + expected .c/.h
│   ├── compile/               # compile generated C with gcc/clang
│   └── lsp/                   # JSON-RPC request/response fixtures
├── examples/
│   ├── hello.cx
│   └── shape.hx
└── third_party/
    └── yyjson/                # small, embeddable JSON for the LSP
```

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
* Nested namespaces join: `a::b::f` → `a__b__f`.
* Global scope stays unmangled.
* Optional: emit a per-namespace enumerator/`typedef` block for ergonomics.

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

* Each destructor becomes a `static inline` (or `static`) function receiving a
  pointer to the variable.
* Every local with a destructible type gets the `cleanup` attribute.
* Scope exit, `break`, `continue`, `return`, and `goto` all trigger cleanup
  automatically — matching normal C++ RAII semantics.
* Order: destructors run in reverse declaration order (guaranteed by C's
  `cleanup` on block exit).

### 5.3 Headers (`.hx`)

* Transpile to `.h` with an include guard or `#pragma once`.
* Declarations stay declarations; destructor helpers are emitted as `static
  inline` so multiple translation units can include the header safely.
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
    int  line, col;        /* 1-based start */
    int  end_line, end_col;/* 1-based end   */
    int  offset, end_offset;
} CxSpan;

/* include/cx/ast.h */
typedef enum {
    CX_NODE_TRANSLATION_UNIT,
    CX_NODE_NAMESPACE,
    CX_NODE_STRUCT,
    CX_NODE_DESTRUCTOR,
    CX_NODE_FUNCTION,
    CX_NODE_VAR_DECL,
    CX_NODE_ERROR            /* Bison error-recovery node */
} CxNodeKind;

typedef struct CxNode {
    CxNodeKind kind;
    CxSpan     span;
    /* kind-tagged union omitted for brevity */
} CxNode;
```

Design rules that make the LSP easy to add:

1. **Everything is a span.** No node lacks location info; diagnostics and
   ranges are derived, never guessed from text offsets.
2. **Error nodes are first-class.** Bison `error` productions produce
   `CX_NODE_ERROR` nodes, so an incomplete edit still yields a tree.
3. **Reentrant parser.** `%define api.pure full` with `%parse-param`; the LSP
   can run parses on any buffer without global lexer/parser state.
4. **Arena lifecycle.** `cx_parser_parse(buffer, len)` returns an arena that the
   caller frees wholesale — one allocation pool per LSP request.
5. **Sema is pure.** `sema_build(ast) -> symbols` takes only the AST and returns
   a symbol table, consumed identically by codegen and LSP features.

---

## 7. LSP Server Design

* **Transport:** newline-delimited JSON-RPC 2.0 over stdin/stdout using the
  standard `Content-Length` header framing.
* **JSON:** a small embedded library (`yyjson`) to avoid a heavy dependency.
* **Lifecycle:** `initialize` → `initialized` → `shutdown` → `exit`.
* **Sync:** `textDocument/didOpen`, `didChange` (incremental where feasible),
  `didClose`. Documents held in an in-memory store keyed by URI.
* **Features (phased):**
  1. `textDocument/publishDiagnostics` — from parser `error` nodes + Sema checks.
  2. `textDocument/documentSymbol` — namespaces, structs, functions, vars.
  3. `textDocument/definition` — resolve via the shared symbol table.
  4. `textDocument/hover` — type/signature from Sema annotations.

Because the parser is reentrant, each request parses the current buffer on
demand; a simple LRU cache avoids reparsing unchanged documents.

---

## 8. Diagnostics Strategy

* Enable in `parser.y`:
  ```yacc
  %locations
  %define parse.error verbose
  %define parse.lac full
  ```
* `yyerror` override takes `YYLTYPE *locp` and prints
  `line:column` plus the offending token; the LSP wraps this into a
  `Diagnostic` with a `range`.
* **Codegen** injects `#line N "foo.cx"` before each emitted construct so that
  downstream compiler errors and GDB backtraces map back to the original
  `.cx`/`.hx` source.

---

## 9. Build System

* `Flex` + `Bison` generate `lexer.c` / `parser.c` (checked into build dir, not
  the repo).
* `Makefile` targets:
  * `make` — build the `cx` CLI (transpiler + `cx lsp` subcommand).
  * `make test` — golden tests, compile tests, LSP fixtures.
  * `make install` — install the `cx` binary.
* C11, `-Wall -Wextra -Werror`, no external runtime deps beyond `yyjson`.

---

## 10. Phased Roadmap

| Phase | Deliverable | Exit criteria |
| :--- | :--- | :--- |
| 0 | Scaffolding, `Makefile`, empty `lexer.l`/`parser.y` | `cx --version` builds |
| 1 | Lexer + parser + AST for namespaces & destructors | Golden parse tests pass |
| 2 | Codegen: mangling + `cleanup` emission | Generated C compiles with gcc **and** clang |
| 3 | Diagnostics + `#line` mapping + `cx build` | Errors point at `.cx`/`.hx` lines |
| 4 | Sema: scopes, symbols, RAII analysis | Symbol table tests pass |
| 5 | `cx lsp`: transport, doc store, diagnostics, symbols, definition, hover | LSP fixtures pass; editor integration demo |
| 6 | Polish: incremental reparse, `cx fmt` | Usable daily driver |

---

## 11. Testing Strategy

* **Golden tests:** `tests/golden/<name>.cx` compared to `<name>.c`.
* **Compile tests:** every golden output is compiled by `gcc` and `clang`;
  a runtime harness verifies destructors fire on scope exit, `return`, and
  `break`.
* **LSP tests:** drive the server over pipes with recorded JSON-RPC
  request/response fixtures (diagnostics, symbols, definition, hover).
* **Fuzz/robustness:** feed malformed Cx and assert no crash + a diagnostic.

---

## 12. Risks & Mitigations

| Risk | Mitigation |
| :--- | :--- |
| Error desync between `.cx` and generated `.c` | `#line` directives (already mitigated) |
| Bison error recovery quality | `%define parse.lac full`, curated `error` productions, `CX_NODE_ERROR` |
| LSP/transpiler drift | Single shared lexer/parser/AST/Sema; no second grammar |
| `cleanup` on non-GNU compilers | Document GCC/Clang requirement; keep output behind the attribute (portable across the intended toolchains) |
| Grammar growth | Keep grammar small; push semantics into Sema |

---

## 13. Immediate Next Steps

1. Create the directory skeleton from §4.
2. Author `grammar/lexer.l` + `grammar/parser.y` with a minimal grammar
   (`namespace`, `struct`, `~Dtor`, functions, declarations).
3. Implement `ast.c` + `diag.c`.
4. Implement the first codegen path (namespaces → mangling) and a golden test.
5. Add the RAII → `cleanup` lowering.
6. Stand up the `cx lsp` skeleton (transport + initialize + diagnostics).
