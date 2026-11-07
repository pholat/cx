# Cx — Origin & approach decision

> **Historical / context.** This document records the *initial assumptions* and
> the architectural comparison that led to Cx being a Flex/Bison
> source-to-source transpiler. For the current entry point see
> [`../readme.md`](../readme.md); for the living technical map see
> [`TECHNICAL.md`](./TECHNICAL.md).

This document evaluates the architectural approaches for extending the C
language with native Namespaces and RAII Semantics (Struct Destructors). It
covers compiler-level hacking, source-to-source transpilation via Flex/Bison, and
modern AST tooling options.

---

## Architectural Comparison Matrix

| Approach | Architecture | Development Effort | Portability | Code Complexity |
| :--- | :--- | :--- | :--- | :--- |
| 1. GCC Core Patch | C Frontend modification | 🔴 Extremely High | 🔴 Locked to specific GCC host target | 🔴 Massive C codebase |
| 2. GCC Plugin | Dynamic Hooks (`.so`) | 🟡 High | 🟡 Portable code, hard version lock | 🟡 Moderate C/C++ API |
| 3. Flex/Bison Transpiler ✅ | Source-to-Source Text | 🟢 Low | 🟢 Emits generic C, any platform | 🟢 Highly isolated |
| 4. Clang Plugin / LibTooling | AST Frontend Actions | 🟡 Medium-High | 🟢 Broad target support | 🟡 Structured C++ AST API |

---

## Decision

> **Chosen: (3) Source-to-Source Transpiler (Flex/Bison).**
> Implemented with a *single shared AST + semantic layer* that is consumed by **both** the C code generator and the LSP server.

**Why Flex/Bison over Clang LibTooling:**

* **Cx is not C.** The language adds `namespace Foo {}` and `~Dtor()` syntax that
  Clang's C grammar cannot parse. Reading Cx via LibTooling would require forking
  Clang's frontend parser — which collapses straight back into the
  maintenance-heavy "compiler hack" this project explicitly rejects.
* **The parser is the LSP.** An LSP needs a reentrant parser, exact source
  locations on every node, and error recovery that yields a partial AST on every
  keystroke. Bison provides all three directly (`%locations`,
  `%define api.pure full`, `%define parse.lac full`, `error` productions).
* **One grammar, no drift.** Lexer, parser, AST, and semantic analysis are shared
  verbatim by codegen and the LSP, so diagnostics can never diverge from what the
  transpiler actually accepts.
* **Portable output.** Codegen emits standard C using
  `__attribute__((cleanup(fn)))`, accepted by `gcc`, `clang`, and
  `arm-none-eabi-gcc`, so the result stays compiler-version agnostic.
* **Clang remains optional** — used *after* transpilation as a semantic verifier,
  never as the frontend.

The full architecture, translation rules, directory layout, and a phased roadmap
are in [`plan.md`](./plan.md). For wiring Cx into an existing C build (Makefile /
CMake) and editor setup, see [`integration.md`](./integration.md).
[`TECHNICAL.md`](./TECHNICAL.md) is the living technical map of the codebase —
read it first before starting a session, and update it at the end of one.

---

## Evaluation of Approaches

### 1. Hacking the Core GCC Source Tree
Modifying `gcc/c/c-parser.cc` and `gcc/c/c-decl.cc` to add keywords and manipulate internal scopes.

*   Pros:
    *   True Language Integration: Clean syntax (`namespace Foo {}`, `~MyStruct()`) without any performance or compilation abstractions.
    *   Native Control Flow Mapping: Seamless interaction with GCC's backend optimizations and intermediate representations (GIMPLE/RTL).
*   Cons:
    *   Monolithic Compilation: Building GCC from source takes a massive amount of time, resulting in incredibly slow iteration loops.
    *   Upstream Fragility: Merging or upgrading your fork with upstream GNU releases is an ongoing maintenance nightmare.
*   Mitigations:
    *   Isolate modifications exclusively to the C frontend folder (`gcc/c/`).
    *   Configure builds using `--enable-languages=c --disable-bootstrap` to bypass unnecessary targets and compilation validation passes during testing.

### 2. Creating a GCC Plugin
Writing a dynamic library (`.so`) that hooks into GCC's internal AST parsing and tree-lowering pipeline.

*   Pros:
    *   Zero Core Modifications: Your code lives entirely outside the GCC source repository.
    *   Middle-End Power: Can utilize native compiler mechanisms like `CLEANUP_STMT` to let the compiler inherently solve block-exit control flow graph cloning for RAII.
*   Cons:
    *   Syntactic Limitations: GCC plugins cannot easily inject arbitrary new raw keywords; you are mostly stuck intercepting pragmas or `__attribute__((...))` annotations.
    *   Version Lock-In: No stable API. A plugin compiled for GCC 14.2 will instantly fail on GCC 13.3 due to volatile internal data structures.
*   Mitigations:
    *   Build a dedicated CI pipeline that multi-targets and rebuilds your plugin matching the precise internal headers of your target cross-compilers (e.g. `arm-none-eabi-gcc`).

### 3. Source-to-Source Transpiler (Flex/Bison)
An independent processing tool that compiles custom code down into compliant standard GNU C code containing `__attribute__((cleanup))` markers and mangled names.

*   Pros:
    *   Total Target Freedom: The output is completely vanilla C code. It will work effortlessly with absolutely any compiler version (`gcc`, `clang`, `arm-none-eabi-gcc`).
    *   Simple Pipeline: Fully isolated grammar file (`parser.y`) and lexer (`lexer.l`).
*   Cons:
    *   Error Desynchronization: Out-of-the-box compiler warnings and debuggers (GDB) point to lines in the *generated* `.c` file, not your original source file.
    *   Double Parsing Pass: Introduces an extra build step which can marginally slow down massive project builds.
*   Mitigations:
    *   Instruct your transpiler to inject `#line [num] "[filename]"` preprocessor directives into the generated C file. This forces downstream GCC error messages and GDB backtraces to map seamlessly to your custom source layout.

---

## Robust Error Parsing in Flex/Bison

The primary drawback of custom-built transpilers is poor error diagnostic capability. By default, Bison gives a cryptic `"syntax error"`. You can drastically upgrade the parser's error tracking using the following implementations:

### 1. Track Exact Location Semantics
Enable the `%locations` directive in your Bison file. This instructs the Flex lexer to track precise columns and lines rather than just a global character stream.

```yacc
/* In parser.y */
%locations
%define parse.error verbose
%define parse.lac full
```
*   `parse.error verbose`: Generates context-rich error descriptions (e.g., *“syntax error, unexpected IDENTIFIER, expecting ';' or '}'”*).
*   `parse.lac full`: Lookahead Correction (LAC), an advanced Bison mechanism that resolves parsing context states to provide highly accurate expected-token lists.

### 2. Overriding the Error Handler
Replace the default signature of `yyerror` to print human-friendly diagnostics indicating the exact line context:

```c
void yyerror(YYLTYPE *locp, const char *s) {
    fprintf(stderr, "Error at line %d, column %d: %s\n",
            locp->first_line, locp->first_column, s);

    // Optional: read the file source line and print a caret indicator (^)
}
```

## Modern Alternative: Clang LibTooling Extensions

Instead of wrestling with GCC's legacy internals or building a text parser from scratch, a highly viable modern option is writing a tool using LLVM/Clang LibTooling.

### How it Works
Clang provides a production-grade C++ library interface (`LibTooling`) explicitly designed for building standalone source-to-source code refactoring tools and custom compilers.

[ Custom Code ] ──► [ Clang Frontend Parser ] ──► [ Full C/C++ AST ] ──► [ Your Custom AST Consumer ] ──► [ Standard GNU C ]

### Why it excels as an alternative:
*   **Flawless Diagnostic Engine:** You instantly inherit Clang’s world-class syntax checking, error reporting, and visual caret warnings out of the box.
*   **Real Semantic Understanding:** Instead of guessing text patterns like a Flex preprocessor, Clang constructs a complete, strongly-typed Abstract Syntax Tree (AST). Your extension can easily detect if a variable scope is ending and safely rewrite it.
*   **Target Agnostic:** Like the Bison transpiler, you emit standard C code, meaning the downstream output can still be fed straight into `arm-none-eabi-gcc` or any specialized embedded toolchain.
