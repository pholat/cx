#include "cx/cx.h"
#include "../lsp_internal.h"

/* TODO(phase 5): resolve the symbol under the cursor via the shared symbol
 * table (cx_sema_find_symbol) and return textDocument/definition. */
int cx_lsp_definition(const char *uri, int line, int character) {
    (void)uri;
    (void)line;
    (void)character;
    return 0;
}
