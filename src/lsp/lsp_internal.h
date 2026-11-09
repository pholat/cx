#ifndef CX_LSP_INTERNAL_H
#define CX_LSP_INTERNAL_H

#include <stddef.h>

#include "cx/buf.h"

/* ---------------------------------------------------------------- transport */
/* Read one JSON-RPC message (Content-Length framed) from stdin. The returned
 * buffer is heap-allocated and must be freed by the caller. NULL on EOF. */
char *cx_lsp_read_message(size_t *len_out);
int   cx_lsp_write_message(const char *json);

/* --------------------------------------------------------------- documents */
typedef struct CxDocument {
    char               *uri;
    char               *text;
    size_t              len;
    int                 version;
    struct CxDocument  *next;
} CxDocument;

CxDocument *cx_doc_store_get(const char *uri);
CxDocument *cx_doc_store_put(const char *uri, const char *text, size_t len,
                             int version);
void        cx_doc_store_remove(const char *uri);

/* The head of the list of all open documents (for workspace-wide features such
 * as cross-file `references`). */
CxDocument *cx_doc_store_all(void);

/* ---------------------------------------------------------------- features */
/* Build a textDocument/publishDiagnostics notification for `uri`/`text` into
 * `out`. Returns the number of diagnostics found. */
int cx_lsp_diagnostics_notification(CxBuf *out, const char *uri,
                                    const char *text, size_t len);

/* Each builds the JSON `result` value for the request into `out`. */
int cx_lsp_document_symbols(CxBuf *out, const char *uri, const char *text,
                            size_t len);
int cx_lsp_definition(CxBuf *out, const char *uri, const char *text, size_t len,
                      int line, int character);
int cx_lsp_hover(CxBuf *out, const char *uri, const char *text, size_t len,
                 int line, int character);
int cx_lsp_completion(CxBuf *out, const char *uri, const char *text, size_t len,
                      int line, int character);
int cx_lsp_signature_help(CxBuf *out, const char *uri, const char *text,
                          size_t len, int line, int character);
int cx_lsp_document_highlight(CxBuf *out, const char *uri, const char *text,
                              size_t len, int line, int character);
int cx_lsp_references(CxBuf *out, const char *uri, const char *text, size_t len,
                      int line, int character);
int cx_lsp_rename(CxBuf *out, const char *uri, const char *text, size_t len,
                  int line, int character, const char *new_name);

#endif /* CX_LSP_INTERNAL_H */
