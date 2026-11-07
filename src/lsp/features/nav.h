#ifndef CX_LSP_FEATURES_NAV_H
#define CX_LSP_FEATURES_NAV_H

#include "cx/cx.h"
#include "cx/buf.h"

/* Innermost named AST node containing the 1-based position, or NULL. */
CxNode *cx_lsp_node_at(CxNode *root, int line, int column);

/* Copy the identifier covering the 1-based (line, column) from `text` into `buf`
 * (NUL-terminated). Returns 1 on success. */
int cx_lsp_word_at(const char *text, int line, int column, char *buf, size_t n);

/* Copy the identifier prefix ending just before the 1-based (line, column) —
 * used for completion. Returns the prefix length. */
int cx_lsp_prefix_at(const char *text, int line, int column, char *buf, size_t n);

/* If the 1-based (line, column) sits in a `Qualifier::member` pair (the cursor
 * may be inside `member`, or just after `Qualifier::` with an empty member, for
 * completion), copy the qualifier into `qual` and the member (possibly empty)
 * into `member`, and return 1. Otherwise return 0. Used to resolve
 * `Type::method` (which lowers to `Type__method`). */
int cx_lsp_scope_at(const char *text, int line, int column, char *qual,
                    size_t qn, char *member, size_t mn);

/* Collect the run of `//` comment lines immediately above the 1-based `line`
 * into `buf` (comment markers and leading indentation stripped). Returns the
 * 1-based line of the first collected comment line, or 0 if there is none. */
int cx_lsp_doc_above(const char *text, int line, char *buf, size_t n);

/* Write an LSP `range` object ({start,end}) for a CxSpan (converted from the
 * 1-based, inclusive CxSpan to 0-based, end-exclusive LSP form). */
void cx_lsp_write_range(CxBuf *out, CxSpan span);

#endif /* CX_LSP_FEATURES_NAV_H */
