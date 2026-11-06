#ifndef CX_LSP_FEATURES_NAV_H
#define CX_LSP_FEATURES_NAV_H

#include "cx/cx.h"
#include "cx/buf.h"

/* Innermost named AST node containing the 1-based position, or NULL. */
CxNode *cx_lsp_node_at(CxNode *root, int line, int column);

/* Write an LSP `range` object ({start,end}) for a CxSpan (converted from the
 * 1-based, inclusive CxSpan to 0-based, end-exclusive LSP form). */
void cx_lsp_write_range(CxBuf *out, CxSpan span);

#endif /* CX_LSP_FEATURES_NAV_H */
