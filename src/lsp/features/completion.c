#include "cx/cx.h"

#include "../json.h"
#include "../lsp_internal.h"
#include "nav.h"

#include <string.h>

/* textDocument/completion. LSP CompletionItemKind: Function=3, Variable=6,
 * Keyword=14, Struct=22, Interface=8. When `label` and `insert` differ, the
 * client shows `label` but inserts `insert` (used to show `Type::method` while
 * inserting only the member after a `::`). */

static void add_item2(CxBuf *out, int *count, const char *label, int kind,
                      const char *detail, const char *insert) {
    if (*count) cx_buf_putc(out, ',');
    (*count)++;
    cx_buf_puts(out, "{\"label\":");
    cx_json_write_string(out, label);
    cx_buf_printf(out, ",\"kind\":%d", kind);
    if (insert && (!label || strcmp(insert, label) != 0)) {
        cx_buf_puts(out, ",\"insertText\":");
        cx_json_write_string(out, insert);
    }
    if (detail && *detail) {
        cx_buf_puts(out, ",\"detail\":");
        cx_json_write_string(out, detail);
    }
    cx_buf_putc(out, '}');
}

static void add_item(CxBuf *out, int *count, const char *label, int kind,
                     const char *detail) {
    add_item2(out, count, label, kind, detail, NULL);
}

/* A macro-generated `A__B` name is shown as the source spelling `A::B`. */
static const char *macro_display(CxMacroMap *map, const char *name, char *buf,
                                 size_t n) {
    if (map && name && cx_macro_map_find(map, name, NULL)) {
        char *dd = strstr(name, "__");
        if (dd) {
            snprintf(buf, n, "%.*s::%s", (int)(dd - name), name, dd + 2);
            return buf;
        }
    }
    return name;
}

int cx_lsp_completion(CxBuf *out, const char *uri, const char *text, size_t len,
                      int line, int character) {
    char prefix[128];
    size_t plen = (size_t)cx_lsp_prefix_at(text, line + 1, character + 1, prefix,
                                           sizeof prefix);

    char qual[128], member[128];
    int scoped = cx_lsp_scope_at(text, line + 1, character + 1, qual,
                                 sizeof qual, member, sizeof member);

    char recv[128], dmem[128];
    int dotted = cx_lsp_dot_at(text, line + 1, character + 1, recv, sizeof recv,
                               dmem, sizeof dmem);

    CxArena *arena = cx_arena_new();
    CxDiagList diags;
    cx_diaglist_init(&diags, uri);
    CxNode *tu = cx_parse(arena, text, len, uri, &diags);
    CxSymbolTable *sema = tu ? cx_sema_build(arena, tu, &diags) : NULL;
    CxMacroMap *map = tu ? tu->macro_map : NULL;

    cx_buf_putc(out, '[');
    int count = 0;

    /* After `recv.` / `recv->`, type the receiver (locals/params, else a global,
     * with a repaired-reparse fallback for incomplete code); unresolved falls
     * through to the general list. */
    char recv_base[128];
    const char *recv_type = NULL;
    if (dotted && sema)
        recv_type = cx_lsp_receiver_base(arena, tu, sema, text, uri, line + 1,
                                         character + 1, recv, recv_base,
                                         sizeof recv_base);
    else if (sema)
        /* A computed receiver: `a.b().` / `f(x).` — type it by reparsing with
         * a marker method call. Returns NULL when the cursor is not after a
         * `.`/`->`. */
        recv_type = cx_lsp_computed_receiver_base(
            arena, text, uri, line + 1, character + 1, (int)plen, recv_base,
            sizeof recv_base);
    /* `recv.` -> a struct (fields + methods); `recv.` where `recv` names a trait
     * is the `x.Trait->method` step, so offer the trait's methods. */
    CxSymbol *recv_struct = (recv_type && sema)
                                ? cx_sema_find_struct(sema, recv_type)
                                : NULL;
    CxSymbol *recv_trait =
        (!recv_type && dotted && sema) ? cx_sema_find_trait(sema, recv) : NULL;

    if (scoped) {
        /* After `Type::`, offer that type's generated members (`Type__member`)
         * under the method spelling `Type::member`. */
        size_t qlen = strlen(qual);
        size_t mlen = strlen(member);
        for (int i = 0; sema && i < sema->len; i++) {
            CxSymbol *s = sema->items[i];
            if (!s->name || s->kind != CX_NODE_FUNCTION) continue;
            if (strlen(s->name) < qlen + 2) continue;
            if (strncmp(s->name, qual, qlen) != 0) continue;
            if (s->name[qlen] != '_' || s->name[qlen + 1] != '_') continue;
            const char *m = s->name + qlen + 2;
            if (mlen && strncmp(m, member, mlen) != 0) continue;
            char label[260];
            snprintf(label, sizeof label, "%s::%s", qual, m);
            add_item2(out, &count, label, 3, s->type, m);
        }
    } else if (recv_type || recv_trait) {
        /* Offer the receiver type's members as bare names (inserted after the
         * `.`/`->`): associated functions named `Type__member`, plus the
         * struct's own fields and its trait vtable pointers. For a trait
         * receiver (`x.Trait->`), offer the trait's methods. For a computed
         * receiver the typed member prefix comes from `prefix`. */
        const char *mfilter = dotted ? dmem : prefix;
        size_t mlen = strlen(mfilter);
        if (recv_type) {
            size_t blen = strlen(recv_type);
            for (int i = 0; i < sema->len; i++) {
                CxSymbol *s = sema->items[i];
                if (!s->name || s->kind != CX_NODE_FUNCTION) continue;
                if (strlen(s->name) < blen + 2) continue;
                if (strncmp(s->name, recv_type, blen) != 0) continue;
                if (s->name[blen] != '_' || s->name[blen + 1] != '_') continue;
                const char *m = s->name + blen + 2;
                /* Skip `Type__Trait__method` impl methods: those are reached
                 * through the trait pointer field (`x.Trait->method`). */
                if (strstr(m, "__")) continue;
                /* Only instance methods: the first parameter must be the
                 * receiver type (a static `Type__new()` has none). */
                if (!s->decl || s->decl->params.len == 0) continue;
                char pb[128];
                cx_lsp_base_type(s->decl->params.items[0]->type, pb, sizeof pb);
                if (strcmp(pb, recv_type) != 0) continue;
                if (mlen && strncmp(m, mfilter, mlen) != 0) continue;
                add_item2(out, &count, m, 2 /* Method */, s->type, m);
            }
        }

        CxNode *decl = recv_struct ? recv_struct->decl : NULL;
        for (int i = 0; decl && i < decl->children.len; i++) {
            CxNode *m = decl->children.items[i];
            if (m->kind == CX_NODE_VAR_DECL) {
                for (int k = 0; k < m->children.len; k++) {
                    CxNode *d = m->children.items[k];
                    if (!d->name) continue;
                    if (mlen && strncmp(d->name, mfilter, mlen) != 0) continue;
                    add_item2(out, &count, d->name, 5 /* Field */, m->type,
                              d->name);
                }
            } else if (m->kind == CX_NODE_IMPLEMENTS && m->name) {
                if (mlen && strncmp(m->name, mfilter, mlen) != 0) continue;
                add_item2(out, &count, m->name, 5 /* Field */, "trait vtable",
                          m->name);
            }
        }

        decl = recv_trait ? recv_trait->decl : NULL;
        for (int i = 0; decl && i < decl->children.len; i++) {
            CxNode *m = decl->children.items[i];
            if (m->kind != CX_NODE_TRAIT_METHOD || !m->name) continue;
            if (mlen && strncmp(m->name, dmem, mlen) != 0) continue;
            add_item2(out, &count, m->name, 2 /* Method */, m->type, m->name);
        }
    } else {
        for (int i = 0; sema && i < sema->len; i++) {
            CxSymbol *s = sema->items[i];
            if (!s->name) continue;
            if (plen && strncmp(s->name, prefix, plen) != 0) continue;
            int kind = 6; /* Variable */
            if (s->kind == CX_NODE_FUNCTION) kind = 3;
            else if (s->kind == CX_NODE_STRUCT) kind = 22;
            else if (s->kind == CX_NODE_TRAIT) kind = 8; /* Interface */

            char nm[256];
            const char *label = s->name;
            const char *insert = NULL;
            if (s->kind == CX_NODE_FUNCTION) {
                const char *disp = macro_display(map, s->name, nm, sizeof nm);
                if (disp != s->name) {
                    label = disp; /* show `Type::method`, insert the same */
                    insert = disp;
                }
            }
            add_item2(out, &count, label, kind, s->type, insert);
        }

        static const char *keywords[] = {
            "if", "else", "while", "for", "switch", "case", "default", "break",
            "continue", "return", "goto", "namespace", "struct", "typedef",
            "import", "trait", "implements", "self", "raw", "auto", "sizeof",
            "const", "void", "int", "char", "long", "unsigned", NULL};
        for (int i = 0; keywords[i]; i++) {
            if (plen && strncmp(keywords[i], prefix, plen) != 0) continue;
            add_item(out, &count, keywords[i], 14, NULL);
        }
    }

    cx_buf_putc(out, ']');

    cx_diaglist_free(&diags);
    cx_arena_free(arena);
    return 0;
}
