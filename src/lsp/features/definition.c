#include "cx/cx.h"

#include <string.h>

#include "../json.h"
#include "../lsp_internal.h"
#include "nav.h"

/* Normalize `decl_file` to a URI: the current `uri`, an already-file:// URI, or
 * an absolute path turned into file://. */
static void write_location(CxBuf *out, const char *uri, const char *decl_file,
                           CxSpan span) {
    if (!decl_file) decl_file = uri;

    CxBuf target;
    cx_buf_init(&target);
    if (strcmp(decl_file, uri) == 0 || strncmp(decl_file, "file://", 7) == 0)
        cx_buf_puts(&target, decl_file);
    else if (decl_file[0] == '/') {
        cx_buf_puts(&target, "file://");
        cx_buf_puts(&target, decl_file);
    } else {
        cx_buf_puts(&target, decl_file);
    }

    cx_buf_puts(out, "{\"uri\":");
    cx_json_write_string(out, target.data ? target.data : uri);
    cx_buf_puts(out, ",\"range\":");
    cx_lsp_write_range(out, span);
    cx_buf_putc(out, '}');
    cx_buf_free(&target);
}

/* Go-to-definition. Resolution order:
 *   1. the identifier under the cursor, via the symbol table (preferring a
 *      macro-definition origin for a macro-generated name);
 *   2. a `Type::method` pair around the cursor (lowers to `Type__method`);
 *   3. a `macro_rules!` invocation at the cursor (jumps to the definition);
 *   4. the innermost named AST node at the cursor. */
int cx_lsp_definition(CxBuf *out, const char *uri, const char *text, size_t len,
                      int line, int character) {
    CxArena *arena = cx_arena_new();
    CxDiagList diags;
    cx_diaglist_init(&diags, uri);

    CxNode *tu = cx_parse(arena, text, len, uri, &diags);
    CxSymbolTable *sema = tu ? cx_sema_build(arena, tu, &diags) : NULL;
    CxMacroMap *map = tu ? tu->macro_map : NULL;

    CxSpan found = {0};
    const char *found_file = uri;
    int have = 0;

    char word[128];
    CxSymbol *sym = NULL;
    if (sema && cx_lsp_word_at(text, line + 1, character + 1, word, sizeof word))
        sym = cx_sema_find_symbol(sema, word);

    if (sym) {
        const char *origin_file = NULL;
        const CxSpan *origin =
            map ? cx_macro_map_find(map, sym->name, &origin_file) : NULL;
        if (origin) {
            found = *origin;
            found_file = origin_file ? origin_file : uri;
            have = 1;
        } else if (sym->decl) {
            found = sym->decl->span;
            found_file = sym->file ? sym->file : uri;
            have = 1;
        }
    }

    char qual[128], member[128];
    if (!have && sema && cx_lsp_scope_at(text, line + 1, character + 1, qual,
                                         sizeof qual, member, sizeof member)) {
        /* `Type::method` -> `Type__method`. Prefer the macro origin, else the
         * generated function's declaration. */
        char full[260];
        snprintf(full, sizeof full, "%s__%s", qual, member);
        const char *origin_file = NULL;
        const CxSpan *origin =
            map ? cx_macro_map_find(map, full, &origin_file) : NULL;
        CxSymbol *fn = cx_sema_find_function(sema, full);
        if (origin) {
            found = *origin;
            found_file = origin_file ? origin_file : uri;
            have = 1;
        } else if (fn && fn->decl) {
            found = fn->decl->span;
            found_file = fn->file ? fn->file : uri;
            have = 1;
        } else {
            /* `Trait::member`: jump to the trait method declaration (including a
             * preceding `//` doc comment, if any). */
            CxNode *tm = cx_sema_find_trait_method(sema, qual, member);
            if (tm) {
                found = tm->span;
                char doc[512];
                int dl = cx_lsp_doc_above(text, found.first_line, doc,
                                          sizeof doc);
                if (dl > 0) {
                    found.first_line = dl;
                    found.first_column = 1;
                }
                have = 1;
            }
        }
    }

    char recv[128], dmem[128];
    if (!have && sema &&
        cx_lsp_dot_at(text, line + 1, character + 1, recv, sizeof recv, dmem,
                      sizeof dmem) &&
        dmem[0]) {
        /* `recv.method` -> `Type__method` (the receiver's type resolves it);
         * otherwise the receiver may expose a field, a trait vtable pointer, or
         * a trait method through the pointer (`recv.Trait->method`). */
        char base[128];
        const char *bt =
            cx_lsp_receiver_base(arena, tu, sema, text, uri, line + 1,
                                 character + 1, recv, base, sizeof base);
        if (bt) {
            char full[260];
            snprintf(full, sizeof full, "%s__%s", bt, dmem);
            const char *origin_file = NULL;
            const CxSpan *origin =
                map ? cx_macro_map_find(map, full, &origin_file) : NULL;
            CxSymbol *fn = cx_sema_find_function(sema, full);
            if (origin) {
                found = *origin;
                found_file = origin_file ? origin_file : uri;
                have = 1;
            } else if (fn && fn->decl) {
                found = fn->decl->span;
                found_file = fn->file ? fn->file : uri;
                have = 1;
            }
        }
        if (!have && bt) {
            CxSymbol *st = cx_sema_find_struct(sema, bt);
            CxNode *decl = st ? st->decl : NULL;
            for (int i = 0; decl && i < decl->children.len && !have; i++) {
                CxNode *m = decl->children.items[i];
                if (m->kind == CX_NODE_VAR_DECL) {
                    for (int k = 0; k < m->children.len && !have; k++) {
                        CxNode *d = m->children.items[k];
                        if (d->name && !strcmp(d->name, dmem)) {
                            found = d->span;
                            found_file = st->file ? st->file : uri;
                            have = 1;
                        }
                    }
                } else if (m->kind == CX_NODE_IMPLEMENTS && m->name &&
                           !strcmp(m->name, dmem)) {
                    CxSymbol *tr = cx_sema_find_trait(sema, m->name);
                    if (tr && tr->decl) {
                        found = tr->decl->span;
                        found_file = tr->file ? tr->file : uri;
                        have = 1;
                    }
                }
            }
        }
        if (!have) {
            CxNode *tmd = cx_sema_find_trait_method(sema, recv, dmem);
            if (tmd) {
                found = tmd->span;
                char doc[512];
                int dl = cx_lsp_doc_above(text, found.first_line, doc,
                                          sizeof doc);
                if (dl > 0) {
                    found.first_line = dl;
                    found.first_column = 1;
                }
                have = 1;
            }
        }
    }

    if (!have && map) {
        const char *def_file = NULL;
        const CxSpan *def =
            cx_macro_map_find_invocation(map, line + 1, character + 1,
                                         &def_file);
        if (def) {
            found = *def;
            found_file = def_file ? def_file : uri;
            have = 1;
        }
    }

    if (!have && sema && tu) {
        CxNode *node = cx_lsp_node_at(tu, line + 1, character + 1);
        if (node && node->name) {
            CxSymbol *s2 = cx_sema_find_symbol(sema, node->name);
            if (s2) {
                const char *origin_file = NULL;
                const CxSpan *origin = map
                                          ? cx_macro_map_find(map, s2->name,
                                                              &origin_file)
                                          : NULL;
                if (origin) {
                    found = *origin;
                    found_file = origin_file ? origin_file : uri;
                    have = 1;
                } else if (s2->decl) {
                    found = s2->decl->span;
                    found_file = s2->file ? s2->file : uri;
                    have = 1;
                }
            }
        }
    }

    if (have) write_location(out, uri, found_file, found);
    else cx_buf_puts(out, "null");

    cx_diaglist_free(&diags);
    cx_arena_free(arena);
    return 0;
}
