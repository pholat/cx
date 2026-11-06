#include "cx/cx.h"
#include "../lsp_internal.h"

/* TODO(phase 5): parse the stored document, walk the AST and return
 * textDocument/documentSymbol results (namespaces, structs, functions, vars). */
int cx_lsp_document_symbols(const char *uri) {
    (void)uri;
    return 0;
}
