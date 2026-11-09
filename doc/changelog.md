# Cx — Changelog

Session log. Append a dated entry per session with the headline changes.
(Moved here from `technical.md` §9.)

### Session 2026-11-06 (initial)

* Evaluated approaches; chose **Flex/Bison transpiler**; wrote the initial
  decision notes (later folded into `readme.md` and `technical.md`).
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
  wrappers and `required-features.md`.
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
* **Planning**: added [`macro-rules.md`](./macro-rules.md) (Rust-style
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
  `technical.md` §8 into [`roadmap.md`](./roadmap.md); §8 is now a pointer.
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
  `roadmap.md`).

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
  `technical.md`. Added a root `readme.md` with an architecture diagram pointing
  at `doc/`, and split the original approach decision into a separate note
  (later folded into `readme.md` / `technical.md`).
* Removed the hand-written klib bridge example (`examples/klib/`); the
  macro-based container examples in `tests/golden/klib_*.cx` supersede it.

### Session 2026-11-08 (paired `.hx`/`.cx` + `auto` return types)

* **Roadmap item 2 — paired `.hx`/`.cx` model.** A `.cx` built by the CLI now
  detects a sibling `.hx` (`sibling_hx`/`replace_ext`/`file_exists` in
  `src/cli/main.c`) and records the generated header's include name on the
  translation unit (`CxNode.paired_header`). `cx_emit_c` then prefixes the
  output with `#include "foo.h"` and skips phases 0–1 (preamble/trait vtable
  types, typedefs, struct definitions) while still emitting phase 2 (prototypes,
  including the `.cx`'s own helpers — a duplicate prototype is legal C) and
  phases 3–4 (impl methods/tables, ctor/dtor helpers, function bodies, globals).
  The 1:1 basename mapping is `foo.cx` ⇄ `foo.hx` ⇄ `foo.h`/`foo.c`; a `.cx`
  without a sibling `.hx` emits the full translation unit as before.
* **Roadmap item 3 — `auto` return types.** `infer_auto` (sema) now also
  handles an `auto` **function return type**: it infers from the first
  `return expr;` in the body and rewrites the function's symbol, so callers
  (including `auto` at the call site) see the resolved type. `infer_type` was
  widened to resolve function parameters (looked up in the AST, since they are
  not in the symbol table), binary comparison/logical operands (`int`), and the
  left operand of a binary arithmetic/bitwise expression. The element-creation
  half of the item (`auto x = …`) already worked.
* Tests: `tests/golden/paired.hx` + `tests/golden/paired.cx` (namespace + trait +
  ctor/dtor + a vtable call) and `tests/golden/auto_return.cx` (`auto` return
  from a call, a literal, a parameter expression, and a comparison).
* Docs: `technical.md` §4/§5/§7 and `roadmap.md` (both items moved to Done).

### Session 2026-11-08 (receiver types → `.`-method calls)

* **Roadmap — `.`/`->` method calls.** Added `expr '.' IDENT '(' arg_list ')'`
  and `expr ARROW IDENT '(' arg_list ')'` to the grammar, building a
  `CX_NODE_EXPR_CALL` that carries the receiver in `lhs` and the `.`/`->` spelling
  in `type` (the `(` after a member is what tells a call from the existing field
  access; there is no `expr '(' …` rule, so no conflict).
* **Sema.** New `resolve_receivers` pass (`src/sema/symbols.c`) with a flat
  per-function scope (`Scope`: parameters plus locals as encountered, with a
  global fallback). For `recv.method(args)` whose receiver has a known struct
  type it emits the associated call `Type__method(&recv, args)`; a pointer
  receiver is passed through (`p->m(x)` → `Type__m(p, x)`). An unresolved
  receiver is left as written.
* **`auto` interplay.** `infer_auto` gained a `report` flag and now runs twice:
  a silent pass first (so an `auto` local gets a concrete type and can be a
  receiver), then `resolve_receivers`, then a reporting pass (so
  `auto x = l.pop();` infers once the method call is lowered) — no duplicate
  diagnostics.
* **Emitter.** `emit_expr`'s `CX_NODE_EXPR_CALL` branch emits an unresolved
  method call (`lhs` set) back as `recv.method(args)`, so a call through a C
  function-pointer field keeps working.
* Test: `tests/golden/method.cx` (`.`-method, `auto` receiver, `->`-method
  through a pointer, associated `::` calls side by side).
* Docs: `technical.md` §4/§6/§7 and `roadmap.md` (item moved to Done; the
  remaining work is the LSP tooling it unlocks).

### Session 2026-11-08 (receiver-type LSP tooling)

* **Dot-completion.** After `recv.`/`recv->`, `textDocument/completion` offers
  the receiver type's methods (functions named `Type__member`) as bare member
  names. New `cx_sema_receiver_type` (sema) types the receiver from the enclosing
  function's locals/parameters (else a global); new `cx_lsp_dot_at` (`nav.c`)
  splits a `recv.member` access around the cursor, and `cx_lsp_base_type` strips
  pointer decoration. If the receiver cannot be typed, the general list is
  offered instead.
* **Hover.** `recv.method` resolves (via the receiver type) to `Type__method` and
  renders its signature; hovering a local/parameter now shows `<type> <name>`.
* **Go-to-definition.** `recv.method` jumps to `Type__method`'s declaration,
  preferring the macro origin (so `gd` on `l.push` in a klib container lands in
  the `macro_rules!` body).
* **Server.** `>` added to the completion `triggerCharacters` (for `->`).
* No new golden tests (the LSP has no automated harness); the behavior is
  documented in `technical.md` §3 and exercised manually.

### Session 2026-11-08 (LSP signature help)

* **`textDocument/signatureHelp`.** New `src/lsp/features/signature.c`
  (`cx_lsp_signature_help`). `cx_lsp_call_open_at` (`nav.c`) finds the `(` that
  opens the argument list around the cursor (a forward scan keeps a stack of open
  parens, skipping strings and comments) and the 0-based argument index
  (top-level commas). The callee is then resolved at the `(` via
  `cx_lsp_scope_at` (`Type::method`), `cx_lsp_dot_at` (`recv.method`), or
  `cx_lsp_prefix_at` (a plain `f`), and the resulting `Type__method` function's
  parameter list is rendered. A `.`-method's hidden receiver is dropped so the
  `activeParameter` maps to the arguments the user typed.
* **Server.** Advertised `signatureHelpProvider` with trigger characters `(`/`,`
  and dispatched `textDocument/signatureHelp` (`handle_query` kind 4).

### Session 2026-11-08 (`list.push()` example + dot-completion on incomplete code)

* **Example.** Added `tests/golden/example.cx`: a klib-backed `IntList` filled
  element by element with the `.`-method spelling
  (`list.push(1); … list.size(); list.free();`), i.e. `list.push(…)` lowers to
  `IntList__push(&list, …)`.
* **LSP dot-completion fix.** While typing `list.` the document is an incomplete
  expression, so the enclosing function fails to reduce and the local's
  declaration is absent from the AST — `list.` therefore fell back to the general
  list (showing `IntList::push`, not `push`). New `cx_lsp_receiver_base`
  (`nav.c`): it uses `cx_sema_receiver_type` (locals/params, else a global) and,
  on failure, reparses a copy of the document with the cursor's line completed by
  `__cx;` (everything before the cursor is preserved), recovering `list`'s
  declared type. `completion`, `hover`, `definition`, and `signatureHelp` now go
  through it, so `push`/`pop`/`size`/… are offered right after `list.`.
* Docs: `technical.md` §3 (resolution + caveats).

### Session 2026-11-08 (`method_with_macro.cx`)

* Added `tests/golden/method_with_macro.cx`: the same "fill a container with
  `.push()`" example, but the container is macro-defined
  (`define_vec!($name, $t) requires $t: Drop`) and its element type is
  constrained by an **interface** (the `Drop` trait). `Box implements Drop`;
  `BoxVec vec` is filled with `vec.push(&a)` / `vec.push(&b)` (each lowering to
  `BoxVec__push(&vec, &item)`), sized with `vec.size()`, and freed with
  `vec.free()`; the interface method is reached through the installed vtable
  (`a.Drop->drop`). Exercises the macro + trait + `.`-method paths together.

### Session 2026-11-08 (struct-field completion)

* **LSP struct-member completion.** After `recv.`/`recv->`, `textDocument/
  completion` now also offers the receiver struct's **fields** and its **trait
  vtable pointers** (read straight from the struct declaration's AST, so no
  symbol-table change was needed) — e.g. after `a.` where `a` is a `Box`, `v`
  and `Drop` are offered. Previously only macro-generated `Type__member`
  functions appeared (`vec.push`), so a plain struct showed nothing.
* Also: `recv.Trait->` now offers the trait's required methods (e.g.
  `a.Drop->drop`), and dot-methods are filtered to **instance** methods (the
  first parameter must be the receiver type), so a static `Type__new()` no longer
  appears after `recv.`.
* Docs: `technical.md` §3, `roadmap.md` (tooling + LSP backlog).

### Session 2026-11-08 (field hover + definition)

* **LSP member hover/definition.** Complementing field completion, `hover` and
  `definition` now handle a struct member access `recv.field`:
  * hover shows `<type> <field>`; a trait vtable pointer shows
    ``trait `T` (vtable pointer on `S`)``; `recv.Trait->method` shows the trait
    method signature;
  * `gd` jumps to a field's declarator, to the trait declaration (for a vtable
    pointer), or to the trait method declaration (with a preceding `//` doc
    comment).
  Fields are read from the struct's AST declaration (the same source the
  completion uses), so no symbol-table change was needed.

### Session 2026-11-08 (`self` as a receiver)

* **`self` in method bodies.** `resolve_receivers` (`src/sema/symbols.c`) now
  walks a struct's constructor/destructor/`implements` method bodies with `self`
  bound to the enclosing struct as a pointer (`<Struct> *`), so `self.method(x)`
  lowers to `Type__method(self, x)` (the pointer is passed through, not `&self`).
  Struct fields are no longer pushed into the local scope (only method bodies are
  walked). `cx_sema_receiver_type` likewise types `self` — via a new
  `enclosing_struct_for_self` — so the LSP's completion / hover / `gd` work after
  `self.`/`self->` in those bodies too.
* Test: `tests/golden/self_receiver.cx` (`show(self)` calls `self.get()` /
  `self.set(...)`, which lower to `Counter__get(self)` / `Counter__set(self, …)`).
* Docs: `technical.md` §3/§4/§6/§7, `roadmap.md`.

### Session 2026-11-08 (block-scoped receivers)

* **Transpiler.** `resolve_receivers` gained a `CX_NODE_BLOCK` case (save/restore
  the scope length around a block's statements), so a local declared in an inner
  block shadows an outer one and is popped at the block's `}`. Previously the
  environment was flat per function, so an inner declaration leaked past its
  block and could mis-resolve a later `x.m()`.
* **LSP.** `cx_sema_receiver_type` now walks the enclosing body with block scope:
  new `collect_visible` accumulates the bindings visible at the cursor (skipping
  statements that start after it, popping per block), and the lookup prefers the
  innermost/most-recent binding (params first, then locals). It also gained an
  `arena` parameter (for the scratch scope) and now finds the enclosing body
  owner via `innermost_body_owner` (function **or** constructor/destructor/
  `implements` method), so locals inside method bodies resolve too.
* Test: `tests/golden/block_scope.cx` (an inner `B x` shadows an outer `A x`;
  `x.m()` resolves to `B__m` inside the block and back to `A__m` after it).
* Docs: `technical.md` §3/§4/§6/§7, `roadmap.md`.

### Session 2026-11-08 (container element teardown)

* **List/bag RAII.** `define_bag!` (`klib_bag.cx`) and `define_vec!`
  (`method_with_macro.cx`) now give their `free` an element-teardown policy: it
  pops each stored `$t *` with `kl_shift_<Name>` and invokes `drop` through the
  installed vtable (`void (*dropfn)(void *) = p->Drop->drop; dropfn(p);`) before
  destroying the list. No C helper was needed — a `void *` converts implicitly to
  the generated `kl_<Name>_t *`.
* **Map RAII.** New `tests/golden/klib_map_drop.cx`: `define_obj_map!` stores
  `void *` values (so the `KHASH_MAP_INIT_INT` passthrough never names a Cx
  struct), with a per-instantiation C helper (`CX_KHASH_PTR_HELPERS`) providing a
  typed `put` and a `kh_foreach_value` iterator; `free` drops every stored value
  through its interface, then destroys the table.
* Docs: `roadmap.md` (container item, refinement, suggested order).

### Session 2026-11-08 (container variants: string map + set)

* **`StrMap`** (`tests/golden/klib_strmap.cx`): a string-keyed khash map
  (`KHASH_MAP_INIT_STR`) from `const char *` to `int`. Keys are not copied by
  klib, so the caller keeps them alive (string literals). A per-instantiation C
  helper (`CX_KHASH_STRMAP_HELPERS`) provides `put`/`get`/`has`/`del` over the
  `void *` handle; the Cx wrapper adds `size`/`free` and uses the `.`-method
  spelling (`m.put("alpha", 1)`).
* **`IntSet`** (`tests/golden/klib_set.cx`): an int khash set
  (`KHASH_SET_INIT_INT`) with `add`/`has`/`del`/`size`/`free`
  (`CX_KHASH_SET_HELPERS`); `add` returns whether the element was new.
* Both follow the existing `klib_map.cx` pattern (per-instantiation C helper +
  `void *` handle, since Cx cannot spell `khash_t(<Name>)`).
* Docs: `roadmap.md` (container Done list, suggested order). Remaining: a real
  capacity/growth policy, struct-keyed maps (user hash/equal), and the
  user-facing `std` module (blocked on macro definitions crossing `import`).

### Session 2026-11-08 (macros across `import` boundaries)

* **Feature.** `cx_macro_expand` (`src/macro.c`) now collects `macro_rules!`
  definitions from the files a source `import`s — recursively and cycle-bounded
  (`depth > 16`) — into its expander table **before** expanding the source, so a
  `.hx` can export a macro to its importer. The importer's own definitions are
  collected by the normal pass, so they take precedence over imported ones of the
  same name.
* New helpers in `macro.c`: `collect_imports_from` (tokenize, `scan_defs`,
  recurse), `find_imports` (a recursive token scan for `import "…"`),
  `str_unquote`, `resolve_rel` (path relative to a file path or `file://` URI)
  and `read_file`. A missing/unreadable import is skipped silently.
* Test: `tests/golden/macro_lib.hx` (the macro) + `macro_import.cx`
  (`import "macro_lib.hx";` then `define_pair!(IntPair, int)` and use of the
  generated struct/function).
* Docs: `technical.md` §4/§7, `roadmap.md`, `integration.md`. This unblocks a
  user-facing `std` module.

### Session 2026-11-08 (std container modules)

* **Modules.** Added `tests/golden/std_list.hx` (`define_list!`) and
  `tests/golden/std_map.hx` (`define_int_map!`) — the container macros packaged
  as reusable module files now that macros cross `import` boundaries. Each module
  carries the C context it needs (`#include "klist.h"` / `#include "khash.h"`,
  the `cx_nofree` pool callback, and the `CX_KHASH_MAP_HELPERS` accessors) in its
  generated header, so an importer needs nothing extra.
* **Example.** New `tests/golden/std_demo.cx` (existing examples left untouched):
  ```
  import "std_list.hx";
  import "std_map.hx";
  define_list!(IntList, int);
  define_int_map!(IntMap, int);
  ```
  then fills/reads through `.`-methods (`l.push(1)`, `l.pop(0)`, `m.put(1, x)`,
  `m.get(1, 0)`).
* Docs: `roadmap.md` (container item, Done list, suggested order).

### Session 2026-11-08 (std owning containers)

* **Modules.** Added `tests/golden/std_drop.hx` (the `Drop` interface),
  `std_vec.hx` (element-typed `define_vec!`, `requires $t: Drop`, with
  `drop`-on-teardown) and `std_set.hx` (`define_int_set!`), joining
  `std_list.hx`/`std_map.hx`. Each module carries its own C context in the
  generated header.
* **Example.** New `tests/golden/std_owned_demo.cx` (existing examples left
  untouched): imports `std_drop.hx`/`std_vec.hx`/`std_set.hx`, declares a `Box`
  that `implements Drop`, `define_vec!(BoxVec, Box)` + `define_int_set!(IntSet)`,
  and exercises `vec.push(&x)` / `vec.free()` (dropping the elements through the
  interface) and `s.add(1)` / `s.has(2)` / `s.free()`.
* Docs: `roadmap.md`.

### Session 2026-11-08 (std string containers)

* **Modules.** Added `tests/golden/std_strmap.hx` (`define_str_map!`,
  `KHASH_MAP_INIT_STR`, `const char *` keys → `$v`) and
  `tests/golden/std_strset.hx` (`define_str_set!`, `KHASH_SET_INIT_STR`), with
  their own accessor helpers (`CX_KHASH_STRMAP_HELPERS` /
  `CX_KHASH_STRSET_HELPERS`) over the `void *` handle.
* **Example.** New `tests/golden/std_str_demo.cx` (existing examples untouched):
  imports both modules, `define_str_map!(NameMap, int)` +
  `define_str_set!(NameSet)`, and exercises `m.put("alpha", 1)`,
  `m.get("alpha", 0)`, `m.has`/`m.del`/`m.size`/`m.free` and
  `s.add("x")` / `s.has("y")` / `s.free()`.
* Docs: `roadmap.md`.

### Session 2026-11-08 (LSP documentHighlight + references)

* **Feature.** New `src/lsp/features/occurrences.c`: `cx_lsp_document_highlight`
  and `cx_lsp_references` share one scanner that collects whole-word occurrences
  of the identifier under the cursor, skipping `//` / `/* */` comments and
  string/char literals (tracking line/column through them). It is lexical and
  same-file: `documentHighlight` answers `[{range, kind:1}, …]`, `references`
  answers `[{uri, range}, …]`.
* **Server.** Registered `documentHighlightProvider` / `referencesProvider` and
  dispatched `textDocument/documentHighlight` (`handle_query` kind 5) and
  `textDocument/references` (kind 6).
* Docs: `technical.md` §2/§3, `roadmap.md`. Cross-file `references` and `rename`
  remain.

### Session 2026-11-08 (LSP rename + self_receiver fix)

* **Fix.** `tests/golden/self_receiver.cx` declared its two associated functions
  (`Counter__get`/`Counter__set`) *inside* the struct body, which Cx rejects
  (functions are not struct members), so `make test` failed at that file. They
  now live at namespace scope; the test still exercises `self.get()` lowering to
  `Counter__get(self)`.
* **Feature.** `textDocument/rename`. The occurrence scanner was refactored into
  `occurrences_body` (mode 0 = `{range, kind}`, 1 = `{uri, range}`, 2 =
  `{range, newText}`), and `cx_lsp_rename` returns a `WorkspaceEdit` —
  `{"changes": {"<uri>": [ … ]}}` — with one edit per whole-word occurrence of
  the identifier under the cursor (same-file, lexical, like `references`),
  or `null` when the cursor is not on an identifier / there is nothing to rewrite.
* **Server.** Registered `renameProvider` and dispatched `textDocument/rename`
  (`handle_query` kind 7, reading `params.newName`).
* Docs: `technical.md` §3, `roadmap.md`, `integration.md`.

### Session 2026-11-08 (`do…while` + the conditional `?:`)

* **Grammar.** Added the `DO` token with the rule `DO stmt WHILE '(' expr ')' ';'`
  (`CX_NODE_DO`; `body` = the statement, `cond` = the condition) and the rule
  `expr '?' expr ':' expr %prec '?'` (`CX_NODE_EXPR_TERNARY`;
  `cond`/`lhs`/`rhs`), with `%right '?'` declared between assignment and `||`, so
  it is right-associative and binds tighter than `=` (like C).
* **Lexer.** Added the `do` keyword and the `?` punctuation (previously `?` was
  silently dropped by the catch-all rule).
* **Emitter.** `emit_stmt` emits `do { … }` followed by `while (cond);` (a
  single-statement body is wrapped in braces by `emit_branch`); `emit_expr`
  emits `cond ? a : b`. `infer_type` resolves a ternary through its operands (so
  `auto x = a ? b : c;` works).
* **Tests.** `tests/golden/do_while.cx`, `tests/golden/ternary.cx`.
* Docs: `language.md`, `roadmap.md`, `required-features.md`.

### Session 2026-11-08 (roadmap tidy + primitive `requires`)

* **Roadmap.** Moved the completed "klib-backed containers" ordered item into
  the Done list (the container layer is done; the remaining refinements —
  struct-keyed maps, a capacity policy — stay in the Refinement backlog), so
  "Write down cx rules" is now the only ordered item. Refreshed the
  receiver-types/fields prose that had gone stale.
* **`requires` bounds on primitives.** `check_macro_requires`
  (`src/sema/symbols.c`) no longer skips a primitive binding: new
  `is_primitive_type` recognizes a built-in type (`int`, `unsigned int`,
  `const char`, `float *`, …) and the binding is checked against a fixed set of
  built-in capability traits (`Hashable`, `Equatable`, `Comparable`, `Ordered`
  via `primitive_impls_trait`). So `requires $k: Hashable` on `int` passes and
  `requires $k: Drop` on `int` is a conformance error (both were silently
  skipped before). Unknown C types still skip.
* Tests: `tests/golden/primitive_requires.cx` (positive),
  `tests/golden/primitive_requires_fail.fail.cx` (negative).
* Docs: `technical.md` §4/§6, `roadmap.md`.

### Session 2026-11-08 (language reference — the last ordered item)

* **Doc.** New `rules.md` (now [`language.md`](./language.md)): a rule-by-rule summary of the language
  extensions over C, each stated as syntax → lowering, with notes —
  `namespace`, constructors/destructors + RAII (incl. `#[cleanup]`), associated
  calls `Type::method(…)`, receiver methods `recv.method(…)`, static traits
  (`trait`/`implements` + vtable layout), attributes, `import` and the paired
  `.hx`/`.cx` model, `macro_rules!` (fragments, repetition, `requires`, import
  collection), `raw { … }` / the `#…` prelude, the `NAME!(…)` C-macro
  passthrough, `auto`, and `#line` back-mapping — plus a short "not an
  extension / differences" section. It excludes the klib container layer, as
  the ordered item required.
* **Roadmap.** That was the last ordered item, so the `Ordered items` section
  now reads "all complete"; `language.md` is linked from `roadmap.md`,
  `readme.md`, and `technical.md`.

### Session 2026-11-08 (roadmap backlog becomes the new ordered items)

* **Roadmap.** Rebuilt the `## Ordered items` section into the next backlog:
  every leftover proposition from the Refinement themes and the "Considered
  extensions" list is now a flat, prioritised 14-item list — struct-keyed maps +
  capacity policy, chained receivers, trait associated types/constants, macro
  hygiene + non-greedy fragments, cross-file `references`/binding-aware
  `rename`, `for` over containers, infer-trait-from-object, more fragments +
  nested repetitions, macro-definition doc comments/indexing, more trait
  features, LSP polish, language surface, tooling, and the not-committed
  considered extensions. Each entry names its theme for the detail below.
* Folded the old "Suggested order" list into it (the Refinement section now
  points back at **Ordered items**), refreshed the ordering rationale, and
  dropped a stale `Map`/`Set` bullet (string keys and `Set` landed).

### Session 2026-11-08 (documentation cleanup)

* **Removed obsolete docs.** Deleted `plan.md` and `origin.md` (their still-valid
  rationale is folded into `readme.md` and `technical.md` §1) and the
  unlinked `FIX.md` scratchpad (its macro-`gd` issue is now under
  `roadmap.md` *Known issues*).
* **One naming format.** All doc filenames are now lowercase kebab-case:
  `technical.md`, `language.md`, `roadmap.md`, `changelog.md`, `generics.md`,
  `macro-rules.md`, `lsp.md`, `integration.md`, `required-features.md`.
* **One owner per topic.** The full language surface now lives in
  `language.md` (previously split between `RULES.md` and `technical.md` §4);
  `technical.md` keeps codegen/sema/gotchas and links out. `generics.md` is now
  design rationale only; `macro-rules.md` is the single macro reference;
  `lsp.md` owns LSP features and `integration.md` owns editor/build setup.
  `required-features.md` is reduced to the remaining C/klib gaps.
* **Roadmap trimmed.** The dated Done list now lives only in this changelog;
  `roadmap.md` keeps the ordered table, known issues, refinement themes and
  considered extensions.
* **Stale claims fixed.** Removed the false "RAII cleanup suppressed on return
  by value" note, corrected `MACRO_RULES.md`'s "definitions do not cross import"
  status, and documented that traits are exercised in
  `macro_requires.cx`, `traits_macro.cx`, `traits_make_total.cx` and
  `std_vec.hx`.

### Session 2026-11-09 (roadmap items 1, 4–6, 9-partial + macro-`gd` fix)

* **Macro `gd` in imported `.hx` (Known issue).** `CxMacroMap` now records the
  **definition file** for both macro invocations (`CxMacroInvoke.def_file`) and
  generated names (`CxMacroOrigin.file`). The expander threads the defining
  file through `collect_imports_from` → `scan_defs` → `Macro.file` →
  `macro_invoke_add` / `macro_map_add`, and `definition.c` uses it for the
  returned `Location.uri`. `Macro.file` is arena-owned (the import path lives in
  a stack buffer at collection time, so a raw pointer produced garbage URIs).
  `gd` on `define_list!` / `define_int_map!` in an importing `.cx` now jumps
  into `std_list.hx` / `std_map.hx`.
* **Chained and computed receivers (roadmap item 1).** `src/sema/symbols.c`
  gained `expr_type` (identifiers, calls, method-call return types,
  field/arrow members, subscripts, parens, casts, binary/ternary) and
  `resolve_receivers` now resolves the receiver **first**, so `a.b().c()`,
  `make().m()` and `arr[i].m()` lower to the associated calls.
* **Local receiver type in the LSP.** `cx_sema_receiver_type` now walks the
  enclosing body's top-level statements individually instead of passing the body
  block to `collect_visible`, which popped the body's own locals on exit. As a
  result `.`-completion / hover / `gd` resolve **locals** again (e.g. `out.`
  offers `get` rather than the general list showing `Outer__get`).
* **Computed-receiver completion.** `cx_lsp_computed_receiver_base` reparses a
  repaired copy (`recv.__cx_comp();`) and types the receiver expression via the
  new `cx_sema_expr_type` (a wrapper over the sema `expr_type`), so
  `out.get().` offers `value` (the members of `Inner`) instead of the general
  list.
* **Trait associated types (roadmap item 2) — paused.** A `type Item;` member
  was prototyped but removed again: it has little value while traits are
  method-only. The idea now lives under *Considered extensions* in the roadmap.
  Traits remain functions-only.
* **Cross-file references / rename (roadmap item 4).** `references` and
  `rename` now also scan every **open** document (`cx_doc_store_all`), producing
  a multi-file `changes` map. Scope is open documents only and the match stays
  lexical (binding-aware rename is still future work).
* **`for!` over containers (roadmap item 5).** New `tests/golden/std_iter.hx`
  ships a `for_each!` macro that lowers to an indexed loop over `.len()` /
  `.at(i)`. The element local is declared with **`auto`** (inferred from
  `.at`), so the invocation is `for_each!(x, list, { … })` with no type
  argument; `std_iter_demo.cx` exercises it.
* **More macro fragments + nested repetitions (roadmap item 6).** `$x:block`
  (a balanced `{ … }`) and `$x:item` (a balanced group, or a `;`-terminated
  run) are supported. Repetition captures now carry an **index path**, so
  `$( $( … )* )*` matches and transcribes with the right per-iteration binding
  (`MBind.paths`/`depths`, `MEnv.path`, path-aware `trans_count` / `bind_find`).
* **LSP polish (roadmap item 9, partial).** `documentHighlight` now reports
  read/write kinds (2/3, a lexical assignment-target heuristic) instead of a
  flat `1`; hover leads **every** target with its `//` doc comment, not just
  trait methods. `semanticTokens`, inlay hints, `positionEncoding`, workspace
  symbols, call hierarchy and the `codeAction` remain pending.

