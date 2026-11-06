#include "cx/cx.h"

#include "../json.h"
#include "../lsp_internal.h"
#include "nav.h"

#include <string.h>

/* textDocument/completion. LSP CompletionItemKind: Function=3, Variable=6,
 * Keyword=14, Struct=22. */

static void add_item(CxBuf *out, int *count, const char *label, int kind,
                     const char *detail) {
    if (*count) cx_buf_putc(out, ',');
    (*count)++;
    cx_buf_puts(out, "{\"label\":");
    cx_json_write_string(out, label);
    cx_buf_printf(out, ",\"kind\":%d", kind);
    if (detail && *detail) {
        cx_buf_puts(out, ",\"detail\":");
        cx_json_write_string(out, detail);
    }
    cx_buf_putc(out, '}');
}

int cx_lsp_completion(CxBuf *out, const char *uri, const char *text, size_t len,
                      int line, int character) {
    char prefix[128];
    size_t plen = (size_t)cx_lsp_prefix_at(text, line + 1, character + 1, prefix,
                                           sizeof prefix);

    CxArena *arena = cx_arena_new();
    CxDiagList diags;
    cx_diaglist_init(&diags, uri);
    CxNode *tu = cx_parse(arena, text, len, uri, &diags);
    CxSymbolTable *sema = tu ? cx_sema_build(arena, tu, &diags) : NULL;

    cx_buf_putc(out, '[');
    int count = 0;

    for (int i = 0; sema && i < sema->len; i++) {
        CxSymbol *s = sema->items[i];
        if (!s->name) continue;
        if (plen && strncmp(s->name, prefix, plen) != 0) continue;
        int kind = 6; /* Variable */
        if (s->kind == CX_NODE_FUNCTION) kind = 3;
        else if (s->kind == CX_NODE_STRUCT) kind = 22;
        else if (s->kind == CX_NODE_TRAIT) kind = 8; /* Interface */
        add_item(out, &count, s->name, kind, s->type);
    }

    static const char *keywords[] = {
        "if", "else", "while", "for", "switch", "case", "default", "break",
        "continue", "return", "goto", "namespace", "struct", "typedef", "import",
        "trait", "implements", "self", "raw", "auto", "sizeof", "const", "void",
        "int", "char", "long", "unsigned", NULL};
    for (int i = 0; keywords[i]; i++) {
        if (plen && strncmp(keywords[i], prefix, plen) != 0) continue;
        add_item(out, &count, keywords[i], 14, NULL);
    }

    cx_buf_putc(out, ']');

    cx_diaglist_free(&diags);
    cx_arena_free(arena);
    return 0;
}
