# Cx — remaining low-level C gaps

> **Status.** Most of the original "wrap klib directly" checklist is implemented:
> `#include` / `#define` passthrough, pointers/`void *`, `typedef`, `const`,
> function pointers, multiple declarators, `sizeof`, all common operators,
> control flow (`if`/`else`, loops, `switch`, `goto`), `raw { … }`,
> `NAME!( … )` C-macro passthrough, static traits, attributes, and
> `macro_rules!` (including macro definitions crossing `import`).
>
> The klib container layer is built with `macro_rules!` and ships as the
> `std_*.hx` modules (`std_list.hx`, `std_map.hx`, `std_strmap.hx`,
> `std_set.hx`, `std_strset.hx`, `std_vec.hx`, `std_drop.hx`), with checked
> `requires` bounds and element teardown. See
> [`macro-rules.md`](./macro-rules.md) §7.
>
> This file keeps only the **remaining** gaps; the ordered backlog and the
> not-committed extensions are in [`roadmap.md`](./roadmap.md).

## 1. Remaining C interop gaps

| Gap | Why it matters |
| :-- | :-- |
| **C macros cannot name a mangled Cx struct type** | klib instantiations such as `klist_t(IntList)` / `khash_t(IntMap)` cannot take a Cx struct; containers therefore store `void *` and use per-instantiation C helpers. Element/value types stay primitive or `void *`. |
| **No `static` / `extern` / `inline` storage-class keywords** | `#[inline]` covers inlining; `static` helpers and `extern` declarations in hand-written-C style need `raw { … }` or generated linkage. |
| **No array types / array parameters** | `T x[N]`, `T x[]` and array parameters cannot be declared; use pointers in Cx and arrays in `raw` blocks. |
| **No comma operator** | Some C macro expansions and dense expressions cannot be written directly. |
| **No declaration-in-`for`-init** | `for (int i = 0; …)` must be rewritten as a declaration before the loop. |
| **No Cx stringizing (`#`) in `macro_rules!`** | `##` concatenation exists; `#` stringizing does not. Use a C macro through `NAME!( … )` when needed. |

## 2. Design points still worth remembering

- **RAII vs manual memory.** klib `malloc`s/`free`s explicitly. Returning an
  RAII handle by value runs its destructor (no move semantics), so factories
  returning destructible handles are unsafe. klib wrappers avoid this by not
  using a destructor (manual `free`).
- **Declaration vs expression ambiguity.** `T *x` vs `a * b` is solved by the
  type-name lexer hack (`CxTypeSet`); this is why types are single tokens and a
  shadowing identifier can be mislexed.
- **Macros vs generics.** klib is generative: its API only exists after macro
  expansion. Cx uses `macro_rules!` for the container layer; template-like
  generics remain deferred (see [`generics.md`](./generics.md)).
- **Lvalue semantics** (`*p = v`, `arr[i] = v`) and output parameters
  (`int *ret`) are supported.
- **Multiple translation units** are supported through `.hx` headers and
  `import "mod.hx";` (see [`language.md`](./language.md) §12).