#include "cx/codegen.h"
#include "cx/symbols.h"

/* Return the mangled C name of the destructor helper for `type`, e.g.
 * "io::File" -> "io__File__dtor". NULL when the type has no destructor. */
char *cx_raii_dtor_name(CxArena *arena, const CxSymbolTable *table,
                        const char *type) {
    CxSymbol *s = cx_sema_dtor_for(table, type);
    if (!s) return NULL;
    char *leaf = cx_arena_printf(arena, "%s__dtor", s->name);
    return cx_mangle(arena, s->ns, leaf);
}
