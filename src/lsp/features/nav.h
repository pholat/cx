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

/* If the 1-based (line, column) sits in a `recv.member` / `recv->member` access
 * (the cursor may be inside `member`, or just after the `.`/`->` with an empty
 * member, for completion), copy the receiver identifier into `recv` and the
 * member (possibly empty) into `member`, and return 1. Otherwise return 0. Used
 * to type a `.`/`->` receiver. */
int cx_lsp_dot_at(const char *text, int line, int column, char *recv, size_t rn,
                  char *member, size_t mn);

/* Strip trailing pointer/space/array decoration so "File *" -> "File". */
void cx_lsp_base_type(const char *type, char *buf, size_t n);

/* The base type name of the receiver `name` at the 1-based (line, column), or
 * NULL. Tries the AST (`cx_sema_receiver_type`, then a global symbol) and, when
 * the live text is an incomplete expression at the cursor (so the enclosing
 * function could not reduce and the declaration is invisible), reparses a copy
 * with the cursor's line completed by a placeholder. Copies the pointer-stripped
 * type into `buf`. Used for `.`/`->` completion/hover/definition/signature. */
const char *cx_lsp_receiver_base(CxArena *arena, CxNode *tu,
                                 CxSymbolTable *sema, const char *text,
                                 const char *uri, int line, int column,
                                 const char *name, char *buf, size_t n);

/* Like `cx_lsp_receiver_base`, but for a *computed* receiver: when the 1-based
 * (line, column) sits just after a `.`/`->` whose receiver is an expression
 * (`a.b().`, `f(x).`), resolve its base type by reparsing with a marker method
 * call (`__cx_comp()`). `prefix_len` is the length of the already-typed member
 * prefix after the dot. Copies the base type into `buf`; NULL if not applicable. */
const char *cx_lsp_computed_receiver_base(CxArena *arena, const char *text,
                                          const char *uri, int line, int column,
                                          int prefix_len, char *buf, size_t n);

/* If the 1-based (line, column) is inside a call's argument list, find the `(`
 * that opens it and the 0-based argument index at the cursor (top-level commas
 * between the `(` and the cursor). Returns 1 (writing the 1-based positions of
 * the `(` into *open_line and *open_col) on success. Used by signature help. */
int cx_lsp_call_open_at(const char *text, int line, int column, int *open_line,
                        int *open_col, int *arg_index);

/* Collect the run of `//` comment lines immediately above the 1-based `line`
 * into `buf` (comment markers and leading indentation stripped). Returns the
 * 1-based line of the first collected comment line, or 0 if there is none. */
int cx_lsp_doc_above(const char *text, int line, char *buf, size_t n);

/* Write an LSP `range` object ({start,end}) for a CxSpan (converted from the
 * 1-based, inclusive CxSpan to 0-based, end-exclusive LSP form). */
void cx_lsp_write_range(CxBuf *out, CxSpan span);

#endif /* CX_LSP_FEATURES_NAV_H */
