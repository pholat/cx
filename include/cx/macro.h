#ifndef CX_MACRO_H
#define CX_MACRO_H

#include <stddef.h>

#include "cx/ast.h"
#include "cx/diag.h"

/* Token-level expansion of Cx `macro_rules!` definitions and invocations.
 *
 * The expander runs before the Cx parser: it removes `macro_rules!` items,
 * expands every known `name!( … )` invocation to source text, and feeds the
 * result back into the normal lexer/parser. Unknown `NAME!( … )` invocations
 * are left untouched (they are C macro passthrough handled by the lexer).
 *
 * Returns an arena-allocated NUL-terminated expanded source. Definitions are
 * removed; their line count is preserved with blank lines so later diagnostics
 * keep pointing at roughly the right place.
 */
char *cx_macro_expand(CxArena *arena, const char *src, size_t len,
                      const char *filename, CxDiagList *diags);

#endif /* CX_MACRO_H */
