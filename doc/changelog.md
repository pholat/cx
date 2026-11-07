# Cx — Changelog

Session log. Append a dated entry per session with the headline changes.
(Moved here from `TECHNICAL.md` §9.)

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
* **Trait requirements in macros.** A rule may carry an optional `requires`
  clause between its pattern and `=>`, binding traits to metavariables:
  `($x:expr) requires $x: Iterable => { … }` or
  `requires $k: Hashable + Equatable, $v: Clone`. The clause is parsed,
  validated (the metavariable must exist in the pattern) and recorded on the
  rule; conformance checking is deferred. Trait members are reachable in the
  body through the field named after the trait. Test:
  `tests/golden/macro_requires.cx` (two traits, `requires $x: A + B`); the two
  trait tests also use the clause.
* Not yet: checked `requires` conformance; hygiene; LSP symbols/hover for macro
  definitions; richer fragment capture (current `expr`/`ty` are greedy to the
  next `,`/`;`).
* **Roadmap:** syntax/recording of traits-in-macros is done (moved to the Done
  list). Remaining order: (1) klib-backed containers; (2) **checked `requires`**
  after containers (deferred conformance check, so it targets real container
  requirements and associated types); (3) paired `.hx`/`.cx`.
* **Docs:** moved the roadmap and the generics/macros design note out of
  `TECHNICAL.md` §8 into [`ROADMAP.md`](./ROADMAP.md); §8 is now a pointer.
* **Containers (start):** added `tests/golden/klib_list.cx`, a macro-defined
  `IntList` over klib's `klist.h`. The macro emits `KLIST_INIT!` (C passthrough)
  and wrappers around `kl_init_<Name>` / `kl_pushp_<Name>` / `kl_shift_<Name>` /
  `kl_destroy_<Name>`; the wrapper stores the list as `void *` because Cx cannot
  spell `klist_t(<Name>)`. `tests/run.sh` adds `-Isubmodules/klib`.
* **Associated calls**: added `Type::func(args)` in expression position (lowered
  to a function named `Type__func`), so container entry points read as
  `IntList::new()`, `IntList::push(&l, 10)`, etc. `.`-method calls still need
  receiver-type tracking; `::` with an explicit receiver is the current form.
  Tests: `tests/golden/assoc.cx` (plain `Point::new`/`Point::get`) and
  `tests/golden/klib_list.cx` (container API).
* **Macro method spelling:** a transcription may write `$name::method`; the
  expander rewrites it to the internal `Name__method` name, so macros no longer
  spell `##__`. `klib_list.cx` now declares `$name::new` / `::push` / `::pop` /
  `::size` / `::free`.
* **LSP macro origins (partial):** `CxMacroMap` on the TU records generated-name
  → macro-definition spans (for `$name::method`, generated `$name(params) { … }`
  functions and `struct $name`). `definition` is wired to prefer that origin, but
  **jump-to-definition for the macro-generated `IntList` methods in
  `klib_list.cx` is still broken** and remains an open LSP issue (see
  `ROADMAP.md`).

### Session 2026-11-07 (checked `requires` + klib containers)

* **Roadmap 2 — checked `requires`.** The macro expander now records, for each
  invocation, the concrete type every `requires`-constrained metavariable was
  bound to (`CxMacroReq` in `CxMacroMap`, attached to the TU; `src/macro.c`).
  `cx_sema_build` runs `check_macro_requires`: the required trait must exist and,
  when the bound type resolves to a struct, that struct must have a matching
  `implements` block. Built-in/unknown types are skipped (first cut). Positive
  coverage: `traits_make_total.cx` (`make_total!(total, Row)` → `Row: Iterable`).
  Negative: new `tests/golden/macro_requires_fail.fail.cx`; `tests/run.sh` now
  runs `*.fail.cx` and requires `cx build` to fail.
* **Roadmap 1 — klib-backed containers.** Added `tests/golden/klib_map.cx`: a
  macro-defined `IntMap` over khash (`KHASH_MAP_INIT_INT!` passthrough). Because
  khash values live behind a typed `vals` array that Cx cannot spell
  (`khash_t(<Name>)` is not expressible), a per-instantiation C macro
  (`CX_KHASH_MAP_HELPERS`) emits typed accessors over a `void *` handle; the
  wrapper tracks its own count. API: `IntMap::new` / `::put` / `::get` / `::has`
  / `::del` / `::size` / `::free`. Added `tests/golden/klib_bag.cx`, an
  element-typed container over `klist.h`
  (`define_bag!($name, $t) requires $t: Drop`) that stores `$t *` in `void *`
  slots, so the concrete element type appears only in Cx-generated code — this
  exercises a checked `requires` bound on a struct element type.
* **LSP jump-to-definition fix.** An initializer's declarator node now carries
  the whole `name = init` span (`SETSPAN` in the grammar), so
  `cx_lsp_node_at` can descend into the initializer; the identifier under the
  cursor resolves to the `Type__method` call node and is looked up in the macro
  origin map. Jump-to-definition for `IntList::new`/`::push`/… now lands in
  `macro_rules! define_list`.
* **Not yet:** a container free/RAII policy that calls the trait on teardown
  (`klib_bag.cx` checks the `Drop` bound but does not call `drop` from `free`);
  primitive element/key bounds (built-ins can't implement a trait, so the
  checker skips them); `.`-method calls (receiver local types are not in the
  symbol table); namespaced types in a `requires` fragment are not resolved (the
  recorder joins `::` with spaces, so only a bare type name is checked).

### Session 2026-11-07 (LSP: `gd`/hover/completion for macro containers)

* **`gd` on `Type::method`.** Added `cx_lsp_scope_at` (`features/nav.c`): it
  splits a `Qualifier::member` pair around the cursor, so both `gd` and hover can
  form `Type__method` and look it up. In `klib_map.cx`, `gd` on `new` in
  `IntMap::new()` now jumps to the `new` token in `macro_rules! define_int_map`
  (the `$name::method` origin is recorded at the method token; previously the
  `$name`).
* **`gd` on a macro invocation.** `CxMacroMap` gained `CxMacroInvoke`
  (`site` → `def`); the expander records every `macro_rules!` invocation.
  `cx_macro_map_find_invocation` resolves the cursor to its definition, so `gd`
  on `define_int_map!` jumps to `macro_rules! define_int_map`.
* **`cx_lsp_node_at` fix.** `search` no longer early-returns on a parent span
  that misses the position (it walks the tree and matches on each node's own
  span); the TU/declarator spans could hide a matching descendant.
* **Hover (`K`).** Resolves the `Type::method` pair and renders the full
  signature with parameters (`ret Type::method(params)`), and shows macro-
  generated `A__B` names as `A::B`.
* **Completion.** After `Type::`, offers the generated members as
  `Type::method` (label) with `insertText` = the bare member; the general list
  also shows macro-generated `A__B` names as `A::B` (no more `IntMap__new`).
* **Not yet:** `.`/`->` member completion and `Type::` signature help; hover
  does not show the macro's doc comment; `gd` on a local variable is not tracked.

### Session 2026-11-07 (`requires` on function metavariables)

* **`requires` on a function.** An `ident`/`path` constraint that names no struct
  is now checked against the trait's methods: `requires $a: A` requires the
  function `a` to be one of `A`'s required methods. `CxMacroReq` gained the
  metavariable's `frag`; `check_macro_requires` routes struct bindings to the
  `implements` check and function bindings to the method check. New negative
  test `tests/golden/macro_requires_method_fail.fail.cx`; `run.sh` accepts either
  diagnosis.

### Session 2026-11-07 (`Trait::member` references + LSP)

* **`$x:method` fragment / `Trait::member`.** New metavariable fragment matching
  a qualified method reference (`A::a`). In a transcription it expands to the
  vtable access `Trait->member` (`($x).$a` → `($x).A->a`), and the trait/member
  pair is checked (`a` must be a method of `A`). `tests/golden/macro_requires.cx`
  now has `both_total!($x, A::a, B::b)` (called from `require_both!`); the
  functions no longer need a `requires` clause — the qualification is the
  requirement.
* **LSP for `Trait::method`.** `cx_sema_find_trait_method` resolves a
  `Trait::method` pair to the trait's method node; `definition` jumps to it and
  hover shows the method signature, preceded by the `//` doc comment above its
  declaration (`cx_lsp_doc_above`).

### Session 2026-11-07 (docs reorganisation)

* Moved all docs except `readme.md` into `doc/`; the session log moved here from
  `TECHNICAL.md`. Added a root `readme.md` with an architecture diagram pointing
  at `doc/`, and split the original approach decision into `doc/origin.md`.
* Removed the hand-written klib bridge example (`examples/klib/`); the
  macro-based container examples in `tests/golden/klib_*.cx` supersede it.
