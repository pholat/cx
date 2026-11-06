#include "cx/cx.h"

#include "../json.h"
#include "../lsp_internal.h"
#include "nav.h"

/* Hover: render a short signature for the symbol under the cursor. */
int cx_lsp_hover(CxBuf *out, const char *uri, const char *text, size_t len,
                 int line, int character) {
    CxArena *arena = cx_arena_new();
    CxDiagList diags;
    cx_diaglist_init(&diags, uri);

    CxNode *tu = cx_parse(arena, text, len, uri, &diags);
    CxSymbolTable *sema = tu ? cx_sema_build(arena, tu, &diags) : NULL;

    char word[128];
    CxSymbol *sym =
        (sema && cx_lsp_word_at(text, line + 1, character + 1, word, sizeof word))
            ? cx_sema_find_symbol(sema, word)
            : NULL;

    if (!sym) {
        cx_buf_puts(out, "null");
    } else {
        CxBuf md;
        cx_buf_init(&md);
        /* Lead with any `#[…]` attributes on the declaration. */
        if (sym->decl && sym->decl->attrs.len > 0) {
            for (int i = 0; i < sym->decl->attrs.len; i++) {
                CxNode *a = sym->decl->attrs.items[i];
                cx_buf_printf(&md, "`#[%s%s%s%s]` ", a->name ? a->name : "",
                              a->type ? "(" : "", a->type ? a->type : "",
                              a->type ? ")" : "");
            }
            cx_buf_puts(&md, "\n\n");
        }
        if (sym->kind == CX_NODE_FUNCTION) {
            cx_buf_printf(&md, "%s %s(", sym->type ? sym->type : "void", sym->name);
            const CxNode *decl = sym->decl;
            for (int i = 0; decl && i < decl->params.len; i++) {
                CxNode *p = decl->params.items[i];
                if (i) cx_buf_puts(&md, ", ");
                cx_buf_printf(&md, "%s %s", p->type ? p->type : "?",
                              p->name ? p->name : "");
            }
            cx_buf_putc(&md, ')');
        } else if (sym->kind == CX_NODE_STRUCT) {
            cx_buf_printf(&md, "struct %s",
                          sym->qualified ? sym->qualified : sym->name);
        } else if (sym->kind == CX_NODE_TRAIT) {
            cx_buf_printf(&md, "trait %s",
                          sym->qualified ? sym->qualified : sym->name);
        } else {
            cx_buf_printf(&md, "%s %s", sym->type ? sym->type : "?",
                          sym->name ? sym->name : "");
        }

        cx_buf_puts(out, "{\"contents\":{\"kind\":\"markdown\",\"value\":");
        cx_json_write_string(out, md.data ? md.data : "");
        cx_buf_puts(out, "}}");
        cx_buf_free(&md);
    }

    cx_diaglist_free(&diags);
    cx_arena_free(arena);
    return 0;
}
