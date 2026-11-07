#include "cx/cx.h"

#include <string.h>

#include "../json.h"
#include "../lsp_internal.h"
#include "nav.h"

/* Render `ret name(params)` for a function symbol (the parameter list makes the
 * call's inputs visible in the hover). */
static void render_function(CxBuf *md, CxSymbol *sym, const char *display) {
    cx_buf_printf(md, "%s %s(", sym->type ? sym->type : "void", display);
    const CxNode *decl = sym->decl;
    for (int i = 0; decl && i < decl->params.len; i++) {
        CxNode *p = decl->params.items[i];
        if (i) cx_buf_puts(md, ", ");
        cx_buf_printf(md, "%s%s%s", p->type ? p->type : "?",
                      p->name ? " " : "", p->name ? p->name : "");
    }
    cx_buf_putc(md, ')');
}

/* A macro-generated `A__B` name is shown as the source spelling `A::B`. */
static const char *display_name(CxMacroMap *map, CxSymbol *sym, char *buf,
                                size_t n) {
    if (map && sym->name && cx_macro_map_find(map, sym->name)) {
        char *dd = strstr(sym->name, "__");
        if (dd) {
            snprintf(buf, n, "%.*s::%s", (int)(dd - sym->name), sym->name,
                     dd + 2);
            return buf;
        }
    }
    return sym->name;
}

/* Render one symbol (optionally under an explicit `display` name, used for a
 * `Type::method` hover). */
static void render_symbol(CxBuf *md, CxMacroMap *map, CxSymbol *sym,
                          const char *display_override) {
    if (sym->decl && sym->decl->attrs.len > 0) {
        for (int i = 0; i < sym->decl->attrs.len; i++) {
            CxNode *a = sym->decl->attrs.items[i];
            cx_buf_printf(md, "`#[%s%s%s%s]` ", a->name ? a->name : "",
                          a->type ? "(" : "", a->type ? a->type : "",
                          a->type ? ")" : "");
        }
        cx_buf_puts(md, "\n\n");
    }

    char nm[256];
    const char *display = display_override
                              ? display_override
                              : display_name(map, sym, nm, sizeof nm);

    if (sym->kind == CX_NODE_FUNCTION) {
        render_function(md, sym, display);
    } else if (sym->kind == CX_NODE_STRUCT) {
        cx_buf_printf(md, "struct %s", sym->qualified ? sym->qualified : display);
    } else if (sym->kind == CX_NODE_TRAIT) {
        cx_buf_printf(md, "trait %s", sym->qualified ? sym->qualified : display);
    } else {
        cx_buf_printf(md, "%s %s", sym->type ? sym->type : "?", display);
    }
}

/* Hover: render a short signature for the symbol under the cursor. Handles a
 * `Type::method` pair by resolving the generated `Type__method` function. */
int cx_lsp_hover(CxBuf *out, const char *uri, const char *text, size_t len,
                 int line, int character) {
    CxArena *arena = cx_arena_new();
    CxDiagList diags;
    cx_diaglist_init(&diags, uri);

    CxNode *tu = cx_parse(arena, text, len, uri, &diags);
    CxSymbolTable *sema = tu ? cx_sema_build(arena, tu, &diags) : NULL;
    CxMacroMap *map = tu ? tu->macro_map : NULL;

    CxSymbol *sym = NULL;
    CxNode *tm = NULL; /* trait method (`Trait::member`) */
    char override[260];
    override[0] = '\0';
    char tqual[128];
    tqual[0] = '\0';

    char word[128];
    if (sema && cx_lsp_word_at(text, line + 1, character + 1, word, sizeof word))
        sym = cx_sema_find_symbol(sema, word);

    if (!sym && sema) {
        char qual[128], member[128];
        if (cx_lsp_scope_at(text, line + 1, character + 1, qual, sizeof qual,
                            member, sizeof member)) {
            char full[260];
            snprintf(full, sizeof full, "%s__%s", qual, member);
            CxSymbol *fn = cx_sema_find_function(sema, full);
            if (fn) {
                sym = fn;
                snprintf(override, sizeof override, "%s::%s", qual, member);
            } else {
                tm = cx_sema_find_trait_method(sema, qual, member);
                if (tm) snprintf(tqual, sizeof tqual, "%s", qual);
            }
        }
    }

    if (!sym && !tm) {
        cx_buf_puts(out, "null");
    } else {
        CxBuf md;
        cx_buf_init(&md);
        if (tm) {
            /* Lead with the `//` doc comment above the declaration, then the
             * signature (`Trait::member`). */
            char doc[1024];
            if (cx_lsp_doc_above(text, tm->span.first_line, doc, sizeof doc) &&
                doc[0])
                cx_buf_printf(&md, "%s\n\n", doc);
            cx_buf_printf(&md, "%s %s::%s(", tm->type ? tm->type : "void",
                          tqual, tm->name ? tm->name : "");
            for (int i = 0; i < tm->params.len; i++) {
                CxNode *pm = tm->params.items[i];
                if (i) cx_buf_puts(&md, ", ");
                if (pm->type) cx_buf_printf(&md, "%s ", pm->type);
                cx_buf_puts(&md, pm->name ? pm->name : "");
            }
            cx_buf_putc(&md, ')');
        } else {
            render_symbol(&md, map, sym, override[0] ? override : NULL);
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
