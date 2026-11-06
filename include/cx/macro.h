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
/* One generated-name -> macro-definition mapping. When a transcription writes
 * `$name::method`, the expander emits the internal `Name__method` identifier and
 * records where `$name` appears in the macro definition. The LSP uses this so
 * "go to definition" on a macro-generated symbol lands in the macro body. */
typedef struct {
    char   *name; /* generated Cx name, e.g. "IntList__new" */
    CxSpan  span; /* source span in the macro definition */
} CxMacroOrigin;

typedef struct CxMacroMap {
    CxMacroOrigin *items;
    int            len;
    int            cap;
} CxMacroMap;

const CxSpan *cx_macro_map_find(const CxMacroMap *map, const char *name);

/* `map` is optional; when provided it receives generated-name origins. */
char *cx_macro_expand(CxArena *arena, const char *src, size_t len,
                      const char *filename, CxDiagList *diags,
                      CxMacroMap *map);

#endif /* CX_MACRO_H */
