#include "cx/cx.h"
#include "../lsp_internal.h"

/* TODO(phase 5): return textDocument/hover with the declared type/signature of
 * the symbol under the cursor. */
int cx_lsp_hover(const char *uri, int line, int character) {
    (void)uri;
    (void)line;
    (void)character;
    return 0;
}
