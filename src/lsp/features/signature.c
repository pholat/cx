#include "cx/cx.h"

#include <string.h>

#include "../json.h"
#include "../lsp_internal.h"
#include "nav.h"

/* textDocument/signatureHelp. Finds the call whose argument list holds the
 * cursor (`cx_lsp_call_open_at`), resolves the callee — `f`, `Type::method`, or
 * `recv.method`/`recv->method` — through the symbol table, and renders its
 * parameter list with the active parameter. A `.`-method's hidden receiver (the
 * injected first argument) is dropped from the user-facing parameter list, so
 * `activeParameter` maps straight to the arguments the user typed. */
int cx_lsp_signature_help(CxBuf *out, const char *uri, const char *text,
                          size_t len, int line, int character) {
    CxArena *arena = cx_arena_new();
    CxDiagList diags;
    cx_diaglist_init(&diags, uri);
    CxNode *tu = cx_parse(arena, text, len, uri, &diags);
    CxSymbolTable *sema = tu ? cx_sema_build(arena, tu, &diags) : NULL;

    int oline = 0, ocol = 0, arg = 0;
    if (!sema || !cx_lsp_call_open_at(text, line + 1, character + 1, &oline,
                                      &ocol, &arg)) {
        cx_buf_puts(out, "null");
        cx_diaglist_free(&diags);
        cx_arena_free(arena);
        return 0;
    }

    CxSymbol *fn = NULL;
    char disp[260];
    disp[0] = '\0';
    int via_dot = 0;

    char qual[128], member[128], recv[128], dmem[128];
    int scoped = cx_lsp_scope_at(text, oline, ocol, qual, sizeof qual, member,
                                 sizeof member);
    int dotted = cx_lsp_dot_at(text, oline, ocol, recv, sizeof recv, dmem,
                               sizeof dmem);

    if (scoped) {
        char full[260];
        snprintf(full, sizeof full, "%s__%s", qual, member);
        fn = cx_sema_find_function(sema, full);
        if (fn) snprintf(disp, sizeof disp, "%s::%s", qual, member);
    } else if (dotted && dmem[0]) {
        char base[128];
        const char *bt =
            cx_lsp_receiver_base(arena, tu, sema, text, uri, oline, ocol, recv,
                                 base, sizeof base);
        if (bt) {
            char full[260];
            snprintf(full, sizeof full, "%s__%s", bt, dmem);
            fn = cx_sema_find_function(sema, full);
            if (fn) {
                snprintf(disp, sizeof disp, "%s::%s", bt, dmem);
                via_dot = 1;
            }
        }
    } else {
        char name[128];
        if (cx_lsp_prefix_at(text, oline, ocol, name, sizeof name) > 0) {
            fn = cx_sema_find_function(sema, name);
            if (fn) snprintf(disp, sizeof disp, "%s", name);
        }
    }

    if (!fn) {
        cx_buf_puts(out, "null");
        cx_diaglist_free(&diags);
        cx_arena_free(arena);
        return 0;
    }

    CxBuf lbl, params;
    cx_buf_init(&lbl);
    cx_buf_init(&params);
    cx_buf_printf(&lbl, "%s %s(", fn->type ? fn->type : "void", disp);

    const CxNode *decl = fn->decl;
    int nparam = 0, first = 1;
    for (int i = via_dot ? 1 : 0; decl && i < decl->params.len; i++) {
        CxNode *p = decl->params.items[i];
        char piece[256];
        snprintf(piece, sizeof piece, "%s%s%s", p->type ? p->type : "?",
                 p->name ? " " : "", p->name ? p->name : "");
        if (!first) cx_buf_puts(&lbl, ", ");
        cx_buf_puts(&lbl, piece);
        if (nparam) cx_buf_putc(&params, ',');
        cx_buf_puts(&params, "{\"label\":");
        cx_json_write_string(&params, piece);
        cx_buf_putc(&params, '}');
        nparam++;
        first = 0;
    }
    cx_buf_putc(&lbl, ')');

    if (arg < 0) arg = 0;
    if (nparam > 0 && arg >= nparam) arg = nparam - 1;

    cx_buf_puts(out, "{\"signatures\":[{\"label\":");
    cx_json_write_string(out, lbl.data ? lbl.data : "");
    cx_buf_puts(out, ",\"parameters\":[");
    cx_buf_puts(out, params.data ? params.data : "");
    cx_buf_puts(out, "]}],\"activeSignature\":0");
    if (nparam > 0) cx_buf_printf(out, ",\"activeParameter\":%d", arg);
    cx_buf_putc(out, '}');

    cx_buf_free(&lbl);
    cx_buf_free(&params);
    cx_diaglist_free(&diags);
    cx_arena_free(arena);
    return 0;
}
