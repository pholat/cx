#include "cx/symbols.h"

/* Reserved for block-level lexical scoping (roadmap phase 4). The scaffold
 * resolves names through the global symbol table, which is sufficient for the
 * minimal grammar and keeps codegen and the LSP on the same lookup path. */
CxSymbol *cx_scope_lookup_local(const CxSymbolTable *table, const char *name) {
    return cx_sema_find_symbol(table, name);
}
