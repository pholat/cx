#include "cx/cx.h"

#include "../json.h"
#include "../lsp_internal.h"
#include "nav.h"

/* textDocument/documentSymbol. LSP SymbolKind: Namespace=3, Field=8,
 * Function=12, Variable=13, Struct=23. */

static void emit_symbols_array(CxBuf *out, CxNode *container);

static void emit_children_array(CxBuf *out, CxNode *s) {
    /* struct members: fields (per declarator) and nested structs */
    int first = 1;
    for (int i = 0; i < s->children.len; i++) {
        CxNode *m = s->children.items[i];
        if (m->kind == CX_NODE_VAR_DECL) {
            for (int k = 0; k < m->children.len; k++) {
                CxNode *d = m->children.items[k];
                if (!first) cx_buf_putc(out, ',');
                first = 0;
                cx_buf_puts(out, "{\"name\":");
                cx_json_write_string(out, d->name);
                cx_buf_puts(out, ",\"kind\":8,\"range\":");
                cx_lsp_write_range(out, d->span);
                cx_buf_puts(out, ",\"selectionRange\":");
                cx_lsp_write_range(out, d->span);
                cx_buf_putc(out, '}');
            }
        } else if (m->kind == CX_NODE_STRUCT) {
            if (!first) cx_buf_putc(out, ',');
            first = 0;
            cx_buf_puts(out, "{\"name\":");
            cx_json_write_string(out, m->name);
            cx_buf_puts(out, ",\"kind\":23,\"range\":");
            cx_lsp_write_range(out, m->span);
            cx_buf_puts(out, ",\"selectionRange\":");
            cx_lsp_write_range(out, m->span);
            cx_buf_puts(out, ",\"children\":[");
            emit_children_array(out, m);
            cx_buf_puts(out, "]}");
        }
    }
}

static void emit_symbols_array(CxBuf *out, CxNode *container) {
    int first = 1;
    for (int i = 0; i < container->children.len; i++) {
        CxNode *n = container->children.items[i];
        if (n->kind == CX_NODE_NAMESPACE) {
            if (!first) cx_buf_putc(out, ',');
            first = 0;
            cx_buf_puts(out, "{\"name\":");
            cx_json_write_string(out, n->name);
            cx_buf_puts(out, ",\"kind\":3,\"range\":");
            cx_lsp_write_range(out, n->span);
            cx_buf_puts(out, ",\"selectionRange\":");
            cx_lsp_write_range(out, n->span);
            cx_buf_puts(out, ",\"children\":[");
            emit_symbols_array(out, n);
            cx_buf_puts(out, "]}");
        } else if (n->kind == CX_NODE_STRUCT) {
            if (!first) cx_buf_putc(out, ',');
            first = 0;
            cx_buf_puts(out, "{\"name\":");
            cx_json_write_string(out, n->name);
            cx_buf_puts(out, ",\"kind\":23,\"range\":");
            cx_lsp_write_range(out, n->span);
            cx_buf_puts(out, ",\"selectionRange\":");
            cx_lsp_write_range(out, n->span);
            cx_buf_puts(out, ",\"children\":[");
            emit_children_array(out, n);
            cx_buf_puts(out, "]}");
        } else if (n->kind == CX_NODE_FUNCTION) {
            if (!first) cx_buf_putc(out, ',');
            first = 0;
            cx_buf_puts(out, "{\"name\":");
            cx_json_write_string(out, n->name);
            cx_buf_puts(out, ",\"kind\":12,\"range\":");
            cx_lsp_write_range(out, n->span);
            cx_buf_puts(out, ",\"selectionRange\":");
            cx_lsp_write_range(out, n->span);
            cx_buf_putc(out, '}');
        } else if (n->kind == CX_NODE_VAR_DECL) {
            for (int k = 0; k < n->children.len; k++) {
                CxNode *d = n->children.items[k];
                if (!first) cx_buf_putc(out, ',');
                first = 0;
                cx_buf_puts(out, "{\"name\":");
                cx_json_write_string(out, d->name);
                cx_buf_puts(out, ",\"kind\":13,\"range\":");
                cx_lsp_write_range(out, d->span);
                cx_buf_puts(out, ",\"selectionRange\":");
                cx_lsp_write_range(out, d->span);
                cx_buf_putc(out, '}');
            }
        }
    }
}

int cx_lsp_document_symbols(CxBuf *out, const char *uri, const char *text,
                            size_t len) {
    CxArena *arena = cx_arena_new();
    CxDiagList diags;
    cx_diaglist_init(&diags, uri);

    CxNode *tu = cx_parse(arena, text, len, uri, &diags);

    cx_buf_putc(out, '[');
    if (tu) emit_symbols_array(out, tu);
    cx_buf_putc(out, ']');

    cx_diaglist_free(&diags);
    cx_arena_free(arena);
    return 0;
}
