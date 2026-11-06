#include "cx/codegen.h"
#include "cx/symbols.h"

/* Return the mangled C name of the destructor helper for `type`, e.g.
 * "io::File" -> "io__File__dtor". NULL when the type has no destructor. */
char *cx_raii_dtor_name(CxArena *arena, const CxSymbolTable *table,
                        const char *type) {
    CxSymbol *s = cx_sema_dtor_for(table, type);
    if (!s) return NULL;
    /* `#[cleanup(fn)] struct T { … }`: call `fn` on scope exit. The name is
     * mangled if it is a Cx function, else emitted verbatim (C function). */
    if (s->cleanup_fn) {
        CxSymbol *fn = cx_sema_find_function(table, s->cleanup_fn);
        if (fn)
            return cx_mangle(arena, fn->ns,
                             fn->export_name ? fn->export_name : fn->name);
        return cx_arena_strdup(arena, s->cleanup_fn);
    }
    char *leaf = cx_arena_printf(arena, "%s__dtor", s->name);
    return cx_mangle(arena, s->ns, leaf);
}
