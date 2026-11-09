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
    if (map && sym->name && cx_macro_map_find(map, sym->name, NULL)) {
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
    char member_md[512]; /* a struct field / trait pointer rendered directly */
    member_md[0] = '\0';

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

    if (!sym && !tm && sema) {
        char recv[128], dmem[128];
        if (cx_lsp_dot_at(text, line + 1, character + 1, recv, sizeof recv,
                          dmem, sizeof dmem) && dmem[0]) {
            char base[128];
            const char *bt =
                cx_lsp_receiver_base(arena, tu, sema, text, uri, line + 1,
                                     character + 1, recv, base, sizeof base);
            if (bt) {
                char full[260];
                snprintf(full, sizeof full, "%s__%s", bt, dmem);
                CxSymbol *fn = cx_sema_find_function(sema, full);
                if (fn) {
                    sym = fn;
                    snprintf(override, sizeof override, "%s::%s", bt, dmem);
                } else {
                    /* A struct field or a trait vtable pointer. */
                    CxSymbol *st = cx_sema_find_struct(sema, bt);
                    CxNode *decl = st ? st->decl : NULL;
                    for (int i = 0; decl && i < decl->children.len; i++) {
                        CxNode *m = decl->children.items[i];
                        if (m->kind == CX_NODE_VAR_DECL) {
                            for (int k = 0; k < m->children.len; k++) {
                                CxNode *d = m->children.items[k];
                                if (d->name && !strcmp(d->name, dmem))
                                    snprintf(member_md, sizeof member_md,
                                             "%s %s", m->type ? m->type : "?",
                                             d->name);
                            }
                        } else if (m->kind == CX_NODE_IMPLEMENTS && m->name &&
                                   !strcmp(m->name, dmem)) {
                            snprintf(member_md, sizeof member_md,
                                     "trait `%s` (vtable pointer on `%s`)",
                                     m->name, bt);
                        }
                    }
                }
            }
            /* A trait method reached through a pointer (`recv.Trait->method`):
             * `recv` then names the trait itself. */
            if (!sym && !tm && !member_md[0]) {
                CxNode *tmd = cx_sema_find_trait_method(sema, recv, dmem);
                if (tmd) {
                    tm = tmd;
                    snprintf(tqual, sizeof tqual, "%s", recv);
                }
            }
        }
    }

    CxBuf md;
    cx_buf_init(&md);
    int have = 0;

    if (tm) {
        /* Lead with the `//` doc comment above the declaration, then the
         * signature (`Trait::member`). */
        char doc[1024];
        if (cx_lsp_doc_above(text, tm->span.first_line, doc, sizeof doc) &&
            doc[0])
            cx_buf_printf(&md, "%s\n\n", doc);
        cx_buf_printf(&md, "%s %s::%s(", tm->type ? tm->type : "void", tqual,
                      tm->name ? tm->name : "");
        for (int i = 0; i < tm->params.len; i++) {
            CxNode *pm = tm->params.items[i];
            if (i) cx_buf_puts(&md, ", ");
            if (pm->type) cx_buf_printf(&md, "%s ", pm->type);
            cx_buf_puts(&md, pm->name ? pm->name : "");
        }
        cx_buf_putc(&md, ')');
        have = 1;
    } else if (sym) {
        /* Lead every hover target with its `//` doc comment, not just trait
         * methods (symbols, structs, functions, globals). */
        char symdoc[1024];
        if (cx_lsp_doc_above(text, sym->span.first_line, symdoc,
                             sizeof symdoc) &&
            symdoc[0])
            cx_buf_printf(&md, "%s\n\n", symdoc);
        render_symbol(&md, map, sym, override[0] ? override : NULL);
        have = 1;
    } else if (member_md[0]) {
        cx_buf_puts(&md, member_md);
        have = 1;
    } else if (sema && tu) {
        /* A local/parameter receiver: show `<type> <name>`. */
        char w[128];
        if (cx_lsp_word_at(text, line + 1, character + 1, w, sizeof w)) {
            const char *lt =
                cx_sema_receiver_type(arena, tu, line + 1, character + 1, w);
            if (lt) {
                cx_buf_printf(&md, "%s %s", lt, w);
                have = 1;
            }
        }
    }

    if (!have) {
        cx_buf_puts(out, "null");
    } else {
        cx_buf_puts(out, "{\"contents\":{\"kind\":\"markdown\",\"value\":");
        cx_json_write_string(out, md.data ? md.data : "");
        cx_buf_puts(out, "}}");
    }
    cx_buf_free(&md);

    cx_diaglist_free(&diags);
    cx_arena_free(arena);
    return 0;
}
