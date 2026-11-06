#include "cx/cx.h"

#include "../json.h"
#include "../lsp_internal.h"

/* Reuses the exact same parser as the transpiler, so editor diagnostics can
 * never diverge from what `cx build` accepts. */

static int severity_code(CxSeverity s) {
    switch (s) {
    case CX_DIAG_ERROR:   return 1; /* LSP: Error */
    case CX_DIAG_WARNING: return 2; /* LSP: Warning */
    case CX_DIAG_NOTE:    return 3; /* LSP: Information */
    }
    return 3;
}

int cx_lsp_diagnostics_notification(CxBuf *out, const char *uri,
                                    const char *text, size_t len) {
    CxArena *arena = cx_arena_new();
    CxDiagList diags;
    cx_diaglist_init(&diags, uri);

    cx_parse(arena, text, len, uri, &diags);

    cx_buf_puts(out,
                "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\","
                "\"params\":{\"uri\":");
    cx_json_write_string(out, uri ? uri : "");
    cx_buf_puts(out, ",\"diagnostics\":[");

    for (int i = 0; i < diags.len; i++) {
        const CxDiagnostic *d = &diags.items[i];

        /* CxSpan is 1-based inclusive; LSP ranges are 0-based, end-exclusive. */
        int sl = d->span.first_line > 0 ? d->span.first_line - 1 : 0;
        int sc = d->span.first_column > 0 ? d->span.first_column - 1 : 0;
        int el = d->span.last_line > 0 ? d->span.last_line - 1 : sl;
        int ec = d->span.last_column > 0 ? d->span.last_column : sc + 1;
        if (el == sl && ec <= sc) ec = sc + 1;

        if (i) cx_buf_putc(out, ',');
        cx_buf_printf(out,
                      "{\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
                      "\"end\":{\"line\":%d,\"character\":%d}},"
                      "\"severity\":%d,\"source\":\"cx\",\"message\":",
                      sl, sc, el, ec, severity_code(d->severity));
        cx_json_write_string(out, d->message);
        cx_buf_putc(out, '}');
    }

    cx_buf_puts(out, "]}}");

    int count = diags.len;
    cx_diaglist_free(&diags);
    cx_arena_free(arena);
    return count;
}
