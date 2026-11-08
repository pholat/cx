# Cx — Roadmap

> Living document. Update **this file** (not `TECHNICAL.md`) when priorities or
> the design direction change. `TECHNICAL.md` §8 is just a pointer here.
>
> Related: [`TECHNICAL.md`](./TECHNICAL.md) (implemented language/codegen/sema
> details), [`RULES.md`](./RULES.md) (rule-by-rule language extensions),
> [`GENERICS.md`](./GENERICS.md) (traits/attributes/generics design),
> [`MACRO_RULES.md`](./MACRO_RULES.md) (`macro_rules!` design),
> [`REQUIRED_FEATURES.md`](./REQUIRED_FEATURES.md) (C/klib gaps),
> [`changelog.md`](./changelog.md) (history).

## Status

Done: **C interop** (`raw { … }` + `#include` + `NAME!(…)` macro invocation
passthrough), **static traits** (`trait`/`implements`), **attributes** (`#[…]`),
**`macro_rules!`** (item + expression-position expansion, see `TECHNICAL.md` §4),
**checked trait requirements in macros** (`requires`, for structs **and
primitives**, plus the `$x:method` `Trait::member` fragment), the **paired
`.hx`/`.cx` model** (`.c` includes the
matching header and emits definitions only), **`auto` return types**,
**`.`/`->` method calls** (receiver-type tracking), **container variants +
teardown** (list/vec/bag + int/string maps + set, with `drop` on teardown),
**macros across `import` boundaries** (`std_*.hx` container modules), the
**conditional `?:` and `do…while`**, and the **LSP navigation/hover/completion**
work for those (macro origins,
`Type::method`, `Trait::method` + docs, receiver-type completion / hover / `gd` /
signature help, and same-file `documentHighlight` / `references` / `rename`).
The language rules are written up in [`RULES.md`](./RULES.md). See the Done list
below.

## Ordered items

All ordered items are complete — see the Done list below; the **Refinement**
backlog holds what can be added next.

## Done

* **Traits in macros — checked `requires`**: after expansion/parse/sema, each
  `requires` binding recorded at an invocation site is verified. A **type** bound
  to the metavariable must implement every required trait (the trait must exist);
  an **`ident`/`path`** binding that names no struct is a function and must name
  one of the trait's methods (`requires $a: A` for the function `a` passed as
  A's method). Built-ins/unknown types are skipped. Positive tests:
  `tests/golden/traits_make_total.cx` (`make_total!(total, Row)`; `Row: Iterable`)
  and `tests/golden/macro_requires.cx` (`both_total!($x, A::a, B::b)`;
  `$a: A, $b: B`). Negative tests: `tests/golden/macro_requires_fail.fail.cx`
  (struct) and `tests/golden/macro_requires_method_fail.fail.cx` (method); `cx
  build` must fail on `*.fail.cx`.
* **`Trait::member` references / LSP for macros.** The `$x:method` fragment
  matches `Trait::member`, is checked, and expands to `Trait->member`. The LSP
  resolves `gd`/hover on a `Type::method` or `Trait::method` pair, on a macro
  invocation, and shows the `//` doc comment above a declaration.
* **Paired `.hx`/`.cx` model.** Building `foo.cx` detects a sibling `foo.hx`
  (CLI `sibling_hx`/`replace_ext`) and emits `foo.c` as `#include "foo.h"` plus
  the definitions only: the preamble, typedefs and struct definitions the header
  already declares are skipped, while the `.cx`'s own helper prototypes, impl
  methods/tables, ctor/dtor helpers, function bodies and globals are still
  emitted. The pair shares its declarations (`foo.hx` = interface, `foo.cx` =
  interface + bodies), so the generated `.c` compiles against exactly what
  consumers see (`CxNode.paired_header` carries the include name into
  `cx_emit_c`). Test: `tests/golden/paired.{hx,cx}` (namespace, trait,
  ctor/dtor, and an associated method through the vtable).
* **`auto` return types.** `auto` may now be a function's return type:
  `infer_auto` (sema) infers it from the first `return expr;` and rewrites the
  function's symbol, so callers — including `auto` at the call site — see the
  resolved type. Inference was also widened: a function parameter, a
  comparison/logical operand (`int`), and a binary arithmetic/bitwise operand
  (the left operand's type) now resolve. The element-creation half of the item
  (`auto x = …`) already worked. Test: `tests/golden/auto_return.cx`.
* **Receiver types → `.`-method calls.** The grammar accepts `recv.method(args)`
  and `recv->method(args)` (distinguished from field access by the `(`; no
  `expr '(' …` rule exists, so there is no conflict). Sema's `resolve_receivers`
  keeps a flat per-function scope of parameters and locals and lowers the call to
  the associated `Type__method(&recv, args)` (a pointer receiver is passed
  through). An unresolved receiver is emitted verbatim, so a C function-pointer
  field call still works. It runs between a silent and a reporting `auto` pass so
  an `auto` local can be a receiver and `auto x = l.pop();` still infers. Inside a
  constructor/destructor/`implements` method body `self` is bound to the
  enclosing struct (as `<Struct> *`), and struct fields are kept out of the local
  scope. The environment is **block-scoped** (an inner block's locals shadow and
  are popped at its `}`). Tests: `tests/golden/method.cx`,
  `tests/golden/self_receiver.cx`, `tests/golden/block_scope.cx`. Still to come:
  chained `a.b().c()`.
* **Receiver-type tooling (LSP).** `completion` after `recv.`/`recv->` offers the
  receiver type's members — instance methods (`Type__member`, filtered to those
  whose first parameter is the receiver), the struct's **fields**, and its
  **trait vtable pointers**; `recv.Trait->` offers the trait's methods. `hover`
  resolves `recv.method` to `Type__method` (and shows `<type> <name>` for a
  local/parameter, `<type> <field>` for a field, the trait for a vtable pointer,
  and the trait method for `recv.Trait->method`); `definition` jumps to the
  method's declaration (preferring the macro origin), a field's declarator, the
  trait, or the trait method. New helpers:
  `cx_lsp_dot_at` (splits `recv.member`) and `cx_lsp_base_type` (strips pointer
  decoration) in `nav.c`, and `cx_sema_receiver_type` (types the receiver from
  the enclosing function's locals/params, else a global) in `symbols.c`; `>`
  added to the completion trigger characters. `cx_lsp_receiver_base` (`nav.c`)
  wraps the lookup and, when the live text is incomplete at the cursor (typing
  `list.` stops the enclosing function from reducing), reparses a copy with the
  cursor's line completed so the declaration is still found — so `push` is
  offered right after `list.`. Still to come: `references`/`rename`.
* **LSP signature help.** `textDocument/signatureHelp` (trigger characters `(`
  and `,`) finds the call's argument list (`cx_lsp_call_open_at` in `nav.c`),
  resolves the callee — `f`, `Type::method`, `recv.method`/`recv->method` — to
  `Type__method`, and renders the parameter list with the active parameter
  (dropping a `.`-method's hidden receiver). Advertised via
  `signatureHelpProvider`.
* **Container element teardown.** `define_bag!`/`define_vec!`'s `free` now pops
  each stored `$t *` and invokes `drop` through the installed vtable
  (`kl_shift_<Name>` in a loop), and `define_obj_map!` (`klib_map_drop.cx`)
  iterates a klist/khash of `void *` values via a per-instantiation C helper and
  drops each one. So a `requires $t: Drop` bound now has a matching teardown.
* **More container variants.** `StrMap` (`klib_strmap.cx`, `KHASH_MAP_INIT_STR`)
  and `IntSet` (`klib_set.cx`, `KHASH_SET_INIT_INT`) join the int map, with the
  same per-instantiation C helper pattern and `.`-method spelling. Struct-keyed
  maps (which need a user hash/equal pair) and a real capacity policy remain.
* **Macros across `import` boundaries.** `cx_macro_expand` now collects
  `macro_rules!` definitions from the files a source imports (recursively,
  cycle-bounded) before expanding it, so an imported `.hx` can export a macro;
  the importer's own definitions take precedence. Tests:
  `tests/golden/macro_lib.hx` + `macro_import.cx`. This unblocks a user-facing
  `std` module.
* **Container modules (`std_*.hx`).** `std_list.hx` (`define_list!`),
  `std_map.hx` / `std_strmap.hx` (`define_int_map!` / `define_str_map!`),
  `std_set.hx` / `std_strset.hx` (`define_int_set!` / `define_str_set!`) and
  `std_vec.hx` (`define_vec!`, element-typed with `requires $t: Drop` and
  `drop`-on-teardown) export the container macros as reusable modules, with
  `std_drop.hx` exporting the `Drop` interface. Each module carries its own C
  context (the `#include`s and helpers) in the generated header, so an importer
  needs nothing extra. New examples: `tests/golden/std_demo.cx` (list + int map),
  `std_owned_demo.cx` (owning vector + int set) and `std_str_demo.cx` (string map
  + string set).
* **LSP `documentHighlight` / `references` / `rename`.** Same-file and lexical:
  they scan the document for whole-word occurrences of the identifier under the
  cursor, skipping comments and string/char literals, and answer
  `textDocument/documentHighlight` (each `kind: 1`),
  `textDocument/references` (`{uri, range}`) and `textDocument/rename` (a
  `WorkspaceEdit` with a single-file `changes` map of `{range, newText}`). Cross-
  file `references` (and a binding-aware rename) remain.
* **`do…while` and the conditional `?:`.** The grammar gained
  `do stmt while (cond);` (`CX_NODE_DO`; a single-statement body is emitted
  wrapped in braces) and `cond ? a : b` (`CX_NODE_EXPR_TERNARY`,
  right-associative, binding tighter than `=` and looser than `||`), with the
  `do` keyword and the `?` token in the lexer; `infer_type` resolves a ternary
  through its operands. Tests: `tests/golden/do_while.cx`, `ternary.cx`.
* **`requires` bounds on primitives.** `check_macro_requires` no longer skips a
  primitive binding: `is_primitive_type` recognizes a built-in type (`int`,
  `unsigned int`, `const char`, `float *`, …) and the binding is checked against
  a fixed set of built-in capability traits (`Hashable`, `Equatable`,
  `Comparable`, `Ordered`) — so `requires $k: Hashable` on `int` passes and
  `requires $k: Drop` on `int` is a conformance error. Tests:
  `tests/golden/primitive_requires.cx`, `primitive_requires_fail.fail.cx`.
* **klib-backed containers (roadmap ordered item 1) — complete.** The container
  layer over klib covers klist-backed lists/vectors and khash-backed maps/sets
  (int/string keys, `Set`, value-dropping map), an element free/RAII policy
  (`free` drops each element through the vtable), reusable `std_*.hx` macro
  modules that carry their own C context, checked `requires` bounds (structs and
  primitives), and both the associated `Type::method(…)` and `.`-method
  `l.push(…)` spellings. See the per-file container bullets above; the remaining
  refinements live under "Containers / stdlib".
* **Cx rules written down (roadmap ordered item 2).** New
  [`RULES.md`](./RULES.md) — a rule-by-rule summary of the language extensions
  over C (namespace, ctors/dtors + RAII, associated calls, receiver methods,
  traits, attributes, `import`/pairing, `macro_rules!`, `raw`/prelude,
  `NAME!(…)` passthrough, `auto`, `#line`), each with what it lowers to. It
  excludes the klib container layer, as the item specified. That closes the
  ordered-items list.

## Refinement (what can be added next)

Themes are roughly prioritised; see "Suggested order" at the end.

### Receiver types → `.`-method calls

Landed: the transpiler lowering (a **block-scoped** environment, and `self`
bound in constructor/destructor/`implements` bodies) *and* the LSP tooling
(dot-completion for methods **and** fields, hover, `gd`, signature help). What
remains is chained `a.b().c()` and computed receivers. `src/sema/scope.c` is
still the scaffold; the current environment lives in `resolve_receivers`
(`src/sema/symbols.c`).

### Containers / stdlib

* **`Set<T>` / `Map` variants** — string keys and `Set` landed
  (`klib_strmap.cx`, `klib_set.cx`); remaining are struct keys (which need a
  user hash/equal pair) and a real capacity/growth policy. `requires` bounds on
  primitives are now checked against the built-in capability traits
  (`Hashable`, `Equatable`, `Comparable`, `Ordered`) — see Done.
* **Infer the trait from the object** — once receiver types exist, generate
  `($x).<Trait>::<member>` from a `requires $x: A + B` clause so the call site
  can be `both_total!($x)` again, with `A::a` reserved for the ambiguous cases.
* **`Map` variants** for string/struct keys (`KHASH_MAP_INIT_STR`), and a real
  capacity/size policy; `Set<T>`.
* **`for` over containers** — an iterator protocol lowering to `at`/`len` (or a
  `next` method) trait members.

### Macro system

* **Hygiene** — body-introduced identifiers are emitted verbatim; add fresh-name
  renaming (`__cx_m_<n>`) so a macro's `$t *tmp` can't capture a caller's name.
* **Non-greedy fragments** — `$x:expr`/`$x:ty` capture to the next top-level
  `,`/`;` (`TECHNICAL.md` §7c), so `($k:expr => $v:expr)` can't be written. A
  backtracking / balanced matcher (or a must-not-be lookahead) fixes composition.
* **Nested repetitions** — only single-level `$( … )*`/`$( … )+` is modelled;
  full recursion through `match_rep`/`transcribe` supports `$( $( … )* )*`.
* **More fragments** — `$x:block`, `$x:item`.
* **Doc comments on macro definitions** (`/// …`) captured by the expander and
  surfaced for hover; **LSP indexing of macro definitions** (currently gone
  before the AST, so `documentSymbol`/rename can't see them).

### Traits

* **Associated types/constants** — needed by real generic containers
  (`trait Iterable { type Item; … }`); `check_macro_requires` and the vtable
  emitter would carry them.
* **Supertraits and default method bodies**
  (`trait X: A + B { int f(self) { … } }`).
* **Postfix vtable calls** (`x.T->m(a)` / `x.m(a)`) — removes the
  function-pointer-local boilerplate (`TECHNICAL.md` §4).
* **Derived/blanket implementations** (e.g. auto-`Equatable` for POD structs).
* **Receiver forms other than `self`** (`&self`, `self_`), which interacts with
  move semantics.

### LSP

* Richer `documentHighlight` kinds (read/write), workspace-wide (cross-file)
  `references`; `semanticTokens`, inlay hints.
* Chained receiver calls (`a.b().c()`) and computed receivers.
* Doc comments (`//`, `///`) attached to **all** hover targets, not just trait
  methods.
* `codeAction` "implement missing trait method" from the trait's required set.
* `positionEncoding` negotiation (UTF-16 columns); workspace symbols; call
  hierarchy.

### Language surface (also see `REQUIRED_FEATURES.md`)

* The comma operator, `static`/`extern`/`inline` specifiers, array types.
* **Multiple value return/unpacking** (`(a, b) = f();`) — lowering to a struct
  return plus per-field unpacking (see "Considered extensions").
* **Scoped imports** (names only for a scope).
* `defer`.

### Tooling / infrastructure

* **Compile database** (`compile_commands.json`) emitted alongside generated C,
  so clangd resolves the transpiled output (see "Considered extensions").
* **Golden-output tests** (diff the generated C, not just "does it compile"),
  plus fuzzing `cx_macro_expand` / the parser.
* `#line`/debug-info polish and a CI target.
* **Paired `.hx`/`.cx` follow-ups**: an out-of-line constructor/destructor
  definition (so the `.hx` can declare `Counter(int);` / `~Counter();` without a
  body and the `.cx` provide it), and inter-procedural `.c`-only declaration
  handling (prototypes for helpers the header does not declare).

### Suggested order

1. Struct-keyed maps (user hash/equal) + a capacity/growth policy; then a
   `std` overview in the docs.
2. Chained receiver calls and computed receivers.
3. Trait associated types/constants, then checked `requires` for them.
4. Macro hygiene + non-greedy fragments.
5. Cross-file `references` / binding-aware `rename`.

> **Ordering rationale.** Traits in macros (syntax, recorded requirements,
> textual member access, **and checked conformance**) are implemented, so
> container macros are written against the final spelling. The checked half is a
> first cut: it verifies concrete types that resolve to a struct and skips
> built-ins/unknowns. Deeper work (associated types/constants, resolving
> namespaced types, bounds for primitive elements) is folded into the container
> items above.

## Generics & macros (design note — under discussion)

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
(generativity) per `MACRO_RULES.md`; **explicit static traits are implemented**
(`implements` block inside a struct → one pointer per trait to a shared static
per-type table, statically checked) — macros opt in with `requires`; keep klib as
the container backend. Option C (`macro_rules!`) is *a* mechanism, not the whole
plan; option B (template-like generics) is deferred until tooling/type-error
quality can be judged on real code. Exposing klib's macros directly (A) stays
confined to the `raw { … }` / invocation-passthrough escape hatch.

## Considered extensions (not committed)

* **Multiple value return and unpacking** — return and destructure several
  values, e.g. `(a, b) = f();` or `a, b := f();`. No grammar chosen yet; a
  natural lowering is a struct return plus per-field unpacking (leaning on the
  existing struct-by-value return support).
  For the struct, it could be nice to be able to write i.e. `a,b := struc_sub[field-name, field-name]`
  but this collides with standard `[]` unpacking so it's less than ideal
  maybe some other syntax like `a,b := struc_sub=>[field-name, field-name]`
  where `=>` would mean 'reach' or something similar would be nice
* **Scoped imports** — an `import` that introduces names only for a scope
  (block / function / namespace) instead of file-wide, so imported symbols do
  not pollute the whole translation unit. Would likely interact with the lexer
  type set and the symbol table's scope model.
* **Compile database (`compile_commands.json`)** — emit a compile database
  (include paths, flags, one entry per generated file) alongside the generated C,
  so clangd and other C tooling resolve the transpiled output; could also help
  the Cx LSP discover module roots and import paths.
* Static map - constatnt size map up to n elements that uses whole space required
  but is always use'able via index. With/without builtin len check - optional
  depending on use required.
  Static mapping should be usefull especially for typical switch-case scenarios
  which build table execution.
* avl or rb based map for anything that provides interface
* `#[cast]` as macro, or as new execution `cast[type]` - syntax sugar for c-cast 
  because standard c-cast is not very readable. Consider optional 2 parameter
  with compile time-check.
* fsm syntax that can be used with cx and creates a sm with basic uml syntax
  `StateA -> StateB : Event [Guard] / Action`
  which would generate:
  1. enum with states
  2. enum with events
  3. sm struct with
      1. guard function pointers to fill
      2. acton function pointers to fill
      3. context pointer to fill
  4. the sm struct will provide:
     1. process_event
     2. reset
     3. get_state
 * `#[str]` syntax that will automatically create a c_str function for enum
   that will return a static set of strings in switch case for each enum element
   ```
   const char* c_str(enum val)
   {
      switch(val):
         case enum_name_1:
            return "enum_name_1"
      ...
      return "";
   }
   ```
