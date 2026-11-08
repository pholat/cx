#include "cx/symbols.h"

/* Reserved for a shared lexical environment. The receiver-type environment that
 * `.`-method resolution uses (block-scoped: a function's parameters plus locals
 * pushed/popped per block) lives in `resolve_receivers` in
 * `src/sema/symbols.c`; this scaffold keeps the global-symbol-table lookup path
 * that codegen and the LSP share, and is where a single shared scope model would
 * move next. */
CxSymbol *cx_scope_lookup_local(const CxSymbolTable *table, const char *name) {
    return cx_sema_find_symbol(table, name);
}
