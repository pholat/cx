#ifndef CX_SEMA_INTERNAL_H
#define CX_SEMA_INTERNAL_H

/* Private interface shared by the `src/sema/` translation units:
 *
 *   collect.c      AST -> symbol table, symbol lookups, cx_sema_build
 *   conform.c      trait conformance and checked `requires`
 *   receivers.c    `auto` inference + `.`/`->` receiver resolution (+ LSP)
 *   diagnostics.c  attribute / call diagnostics
 *
 * Only the public surface in `cx/symbols.h` is visible outside this directory. */

#include "cx/symbols.h"

#include <stddef.h>

/* Shared state threaded through the sema passes. */
typedef struct {
    CxArena       *arena;
    CxSymbolTable *table;
    CxDiagList    *diags;
} BuildCtx;

/* Strip trailing pointer/array decorations from a type so "File *" -> "File". */
void type_base(const char *type, char *buf, size_t n);

/* Symbol construction, shared with the feature modules (`src/features/`), whose
 * `collect` hooks add the symbols for their own syntax (enums, `sm`, …). */
CxSymbol *symbol_new(CxArena *a, CxNode *n, const char *ns, CxNodeKind kind,
                     const char *file);
void      table_push(CxSymbolTable *t, CxSymbol *s);

/* `auto` inference + `.`/`->` receiver resolution, run by `cx_sema_build`
 * between symbol collection and the check passes. */
void cx_sema_receivers_pass(BuildCtx *c, CxNode *tu);

/* Diagnostics / conformance passes (each walks the whole tree). */
void check_calls(BuildCtx *c, CxNode *n);
void check_implements(BuildCtx *c, CxNode *n);
void check_attrs(BuildCtx *c, CxNode *n);
void check_macro_requires(BuildCtx *c, CxNode *tu);

#endif /* CX_SEMA_INTERNAL_H */
