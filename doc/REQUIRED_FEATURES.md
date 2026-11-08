# Cx features required to wrap klib directly

> **Status.** Implemented since this list was written: `#include`/`#define`
> passthrough, comparisons/logical/bitwise operators, `++`/`--`, compound
> assignment, the conditional `?:`, `if`/`else`/`while`/`do…while`/`for`/
> `break`/`continue`, pointer types, `->`, `[]`, `&`/`*`, casts `(T)expr`,
> `sizeof`, `switch`/`case`/`default`, `goto`, function pointers
> `T (*name)(params)`, `auto`, multi-word types and `const`, typedefs, multiple
> declarators, declaration-only headers, Cx-to-Cx `import`, the **`raw { … }`
> escape hatch**, **`NAME!( … )` macro-invocation passthrough**, **static
> traits** (`trait`/`implements`), and **`macro_rules!`** (item +
> expression-position expansion with repetitions and `##`; definitions cross
> `import` boundaries). A **macro_rules-based container layer** over klib now
> ships as the `std_*.hx` modules (`std_list.hx`, `std_map.hx`, `std_strmap.hx`,
> `std_set.hx`, `std_strset.hx`, `std_vec.hx`, `std_drop.hx`), with **checked
> `requires`** bounds and an **element free/RAII policy** (`free` drops each
> element through the installed vtable). Remaining gaps: klib's C macros cannot
> name a mangled Cx struct type (so element/value types stay primitive or
> `void *`), plus some C-preprocessor/expression gaps listed below.
>
> The hand-written bridge this list originally motivated
> (`examples/klib/klib_bridge.{c,h}` + `list.cx`/`map.cx`) has been **removed**;
> the working container examples live in `tests/golden/`.

klib (`submodules/klib`) is header-only and macro/pointer based. Writing a
list/map wrapper **without** the C bridge requires the language features
below. `[L]` = needed by `klist.h`, `[M]` = needed by `khash.h`.

Reference:

```c
#include "khash.h"
KHASH_MAP_INIT_INT(32, char)
khash_t(32) *h = kh_init(32);
int ret; khiter_t k = kh_put(32, h, 5, &ret); kh_value(h, k) = 10;
k = kh_get(32, h, 5);
if (k == kh_end(h)) { /* missing */ }
for (k = kh_begin(h); k != kh_end(h); ++k) if (kh_exist(h, k)) { ... }
kh_destroy(32, h);
```

## 1. Preprocessing & genericity

| Feature | Notes |
| :-- | :-- |
| **`#include` passthrough** `[L][M]` | The lexer currently discards every `#…` line, so klib headers can't be pulled in. |
| **Macro definition / expansion** `[L][M]` | **Cx `macro_rules!` is now implemented** for generative Cx (types + functions, repetitions, `##`); C function-like macros remain available via `NAME!( … )` passthrough. The klib container layer is written on top (`tests/golden/klib_*.cx`). |
| **Generic/parameterized types** `[L][M]` | klib instantiates a type + functions per element type. Cx needs generics, or must delegate expansion to the C preprocessor. |
| **Token pasting `##` / stringizing `#`** `[L][M]` | Only if expansion happens in Cx rather than in the C preprocessor. |

This block is the deepest gap: without it, klib's API exists only after macro
expansion.

## 2. Types & declarations

| Feature | Notes |
| :-- | :-- |
| **Pointer types** `[L][M]` | `khash_t(x) *h`, `klist_t(x) *l`, `khint32_t *flags`. Today `type` is a single identifier. |
| **`void *`** `[M]` | `kmalloc`, `krealloc`, `kfree((void*)…)`. |
| **Struct forward declarations / self-reference** `[L][M]` | `struct __kl1_x *next;`, `struct kh_x_s *…`. |
| **`typedef`** `[L][M]` | `khash_t(name)`, `klist_t(name)`, `kliter_t(name)`, `khiter_t`, `khint32_t`. |
| **`const` qualifier** `[M]` | `const kh_x_t *h`, `kh_cstr_t`. |
| **`static` / `extern` / `inline`** `[L][M]` | klib generates `static inline` instantiations and `extern` prototypes. |
| **Multiple declarators per declaration** `[M]` | `khint_t k, i, last, mask, step = 0;`. |
| **`sizeof`** `[L][M]` | `calloc(1, sizeof(kh_x_t))`. |
| **Array types / array parameters** `[M]` | `khint32_t *flags` used with `[]`. |

## 3. Expressions & operators

| Feature | Notes |
| :-- | :-- |
| **`->` member access** `[L][M]` | `h->keys[x]`, `it->data`, `mp->buf[k]`. |
| **`[]` subscript** `[L][M]` | `h->keys[i]`, `mp->buf[k]`. |
| **`&` address-of** `[M]` | `kh_put(..., &ret)`, `kl_shift(..., &d)`. |
| **unary `*` deref (as lvalue)** `[L]` | `*kl_pushp(m, l) = value;`. |
| **comparisons** `== != < > <= >=` `[L][M]` | `it != kl_end(l)`, `k == kh_end(h)`, `ret >= 0`. |
| **logical** `&& || !` `[L][M]` | `!__ac_isempty(flag, i)`. |
| **bitwise** `& | ^ ~ << >>` `[M]` | `(i>>4)`, `(i&0xfU)<<1`, `x |= x>>1`, `~(key<<15)`. |
| **compound assignment** `+= -= |= &= ^= <<= >>=` `[M]` | `x |= x >> 1`. |
| **increment / decrement** `++ --` `[L][M]` | `++step`, `++h->size`. |
| **cast** `(T)expr` `[L][M]` | `(khint_t)(…)`, `(void *)h->keys`. |
| **ternary `?:`** `[M]` | `((m) < 16 ? 1 : (m) >> 4)`. |
| **comma operator** `[M]` | `i = (i + (++step)) & mask` style expressions. |
| **`NULL` / null pointer literal** `[L][M]` | `p->next = 0`. |

## 4. Statements & control flow

| Feature | Notes |
| :-- | :-- |
| **`if` / `else`** `[L][M]` | pervasive. |
| **`while`** `[M]` | `while (1) { … }`. |
| **`for`** `[L][M]` | iteration + rehash loops. |
| **`break` / `continue`** `[L][M]` | inside loops. |
| **`do…while`** | common in klib macros (`do { … } while (0)`). |
| **`switch` / `case`**, **`goto`** | used by other klib headers (ksw, kseq, kexpr); not by klist/khash. |

Today Cx has only `return`, blocks and expression statements.

## 5. Program structure

| Feature | Notes |
| :-- | :-- |
| **Function pointers** `[M]` | custom free/destructor and hash/equal functions (`kmpfree_t`, `kh_str_hash_func`). |
| **Output parameters** `[M]` | `int *ret` out-params. |
| **Multiple translation units / headers / imports** | to expose the wrapper to other files (no `.hx`/import support yet). |

## 6. Design issues (not just "add a keyword")

* **Declaration vs expression ambiguity.** `T *x` vs `a * b` cannot be resolved
  with plain LALR(1); C solves it with symbol-table feedback into the lexer (the
  "typedef-name" hack). Introduced by adding pointer types. (This is why pointers
  were deferred earlier.)
* **Macros vs generics.** klib is *generative*: the API only exists after macro
  expansion. Either run the C preprocessor (pass macros through verbatim) or add
  a real generics facility.
* **RAII vs manual memory.** klib `malloc`s/`free`s explicitly. Returning an RAII
  handle by value runs its destructor (no move semantics), so a factory like
  `List list_new() { List l(...); return l; }` would free the handle it returns.
* **Lvalue semantics.** `*p = v` and `arr[i] = v` need dereference/subscript to
  produce lvalues, not just rvalues.
* **Statement vs declaration in `for` init.** `for (khint_t i = 0; …)` mixes a
  declaration into the initializer.

## 7. Shortcut (now available)

The **raw-C escape hatch** is implemented: `#include` passthrough, verbatim
`raw { … }` blocks (file scope, struct member, statement), and `NAME!( … )`
macro-invocation passthrough (e.g. `KLIST_INIT!(IntList, int, 0);` →
`KLIST_INIT(IntList, int, 0);`). This unlocks klib today without the generics
layer: put the klib glue in `raw` blocks and call macro-generated functions via
named invocations.
