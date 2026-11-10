/* Cx `macro_rules!` — transcriber: transcription + bindings -> source text.
 *
 * The transcriber walks the transcribed token tree and, for each `$name`
 * metavariable, emits the tokens captured for the current repetition path. It
 * is independent of pattern matching (it only reads bindings), so it is
 * unit-testable in isolation. */
#include "cx/cx.h"
#include "macro_internal.h"

#include <string.h>

/* =========================================================== transcription */

typedef enum { T_TOKEN, T_VAR, T_REP, T_GROUP } TKind;

struct MTrans {
    TKind   kind;
    char   *text;
    char    open;
    char    close;
    MTrans **kids;
    int     nkids;
    char    sep;
    int     has_sep;
    char    op;
    CxSpan  span; /* source span in the macro definition */
};

static MTrans *tr_new(CxArena *a, TKind kind) {
    MTrans *t = (MTrans *)cx_arena_alloc(a, sizeof(MTrans));
    if (t) t->kind = kind;
    return t;
}

static void tr_push(CxArena *a, TrVec *v, MTrans *t) {
    if (v->len == v->cap) {
        int cap = v->cap ? v->cap * 2 : 8;
        MTrans **items =
            (MTrans **)cx_arena_alloc(a, sizeof(MTrans *) * (size_t)cap);
        if (!items) return;
        if (v->items && v->len)
            memcpy(items, v->items, sizeof(MTrans *) * (size_t)v->len);
        v->items = items;
        v->cap = cap;
    }
    v->items[v->len++] = t;
}

/* ---------------------------------------------- parsing transcriptions */

static TrVec parse_tr_tokens(CxArena *a, CxDiagList *diags, MToken **toks,
                             int n);

TrVec parse_tr_group(CxArena *a, CxDiagList *diags, MToken *g) {
    return parse_tr_tokens(a, diags, g->kids, g->nkids);
}

static TrVec parse_tr_tokens(CxArena *a, CxDiagList *diags, MToken **toks,
                             int n) {
    TrVec out = {0};
    for (int i = 0; i < n;) {
        MToken *t = toks[i];
        if (tok_is_punct(t, "$")) {
            if (i + 1 < n && toks[i + 1]->kind == MT_GROUP) {
                MToken *g = toks[i + 1];
                i += 2;
                MTrans *r = tr_new(a, T_REP);
                if (!r) return out;
                r->open = g->open;
                r->close = g->close;
                r->span = tok_span(t);
                TrVec sub = parse_tr_group(a, diags, g);
                r->kids = sub.items;
                r->nkids = sub.len;
                if (i < n && (tok_is_punct(toks[i], "*") ||
                              tok_is_punct(toks[i], "+"))) {
                    r->op = toks[i]->text[0];
                    i++;
                } else if (i + 1 < n && toks[i]->kind == MT_PUNCT &&
                           (tok_is_punct(toks[i + 1], "*") ||
                            tok_is_punct(toks[i + 1], "+"))) {
                    r->has_sep = 1;
                    r->sep = toks[i]->text[0];
                    r->op = toks[i + 1]->text[0];
                    i += 2;
                } else {
                    cx_error(diags, tok_span(t),
                             "expected repetition operator '*' or '+'");
                    r->op = '*';
                }
                tr_push(a, &out, r);
                continue;
            }
            if (i + 1 < n && toks[i + 1]->kind == MT_IDENT) {
                MTrans *v = tr_new(a, T_VAR);
                if (!v) return out;
                v->text = toks[i + 1]->text;
                v->span = tok_span(toks[i + 1]);
                tr_push(a, &out, v);
                i += 2;
                continue;
            }
            cx_error(diags, tok_span(t), "malformed macro metavariable");
            i++;
            continue;
        }
        if (t->kind == MT_GROUP) {
            MTrans *g = tr_new(a, T_GROUP);
            if (!g) return out;
            g->open = t->open;
            g->close = t->close;
            g->span = tok_span(t);
            TrVec sub = parse_tr_group(a, diags, t);
            g->kids = sub.items;
            g->nkids = sub.len;
            tr_push(a, &out, g);
            i++;
            continue;
        }
        MTrans *tok = tr_new(a, T_TOKEN);
        if (!tok) return out;
        tok->text = t->text;
        tok->span = tok_span_full(t);
        tr_push(a, &out, tok);
        i++;
    }
    return out;
}

static void out_text(Out *o, const char *text) {
    if (!text || !*text) return;
    if (!o->join && o->out->len) {
        char last = o->out->data[o->out->len - 1];
        char first = text[0];
        int no_space =
            (last == '\n' || last == ' ' || last == '\t' || last == '\r' ||
             last == '(' || last == '[' || last == '{' || first == ')' ||
             first == ']' || first == '}' || first == ',' || first == ';' ||
             first == '!' || first == '.' || first == ':' || first == '=' ||
             first == '<');
        if (!no_space) cx_buf_putc(o->out, ' ');
    }
    cx_buf_puts(o->out, text);
    o->join = 0;
}

static void out_opaque(Out *o, const char *text) {
    if (!text || !*text) return;
    if (text[0] == '#') {
        if (o->out->len && o->out->data[o->out->len - 1] != '\n')
            cx_buf_putc(o->out, '\n');
        cx_buf_puts(o->out, text);
        if (text[1] != '[') cx_buf_putc(o->out, '\n');
        o->join = 0;
    } else {
        out_text(o, text);
    }
}

static void emit_tokens(Out *o, MToken **toks, int n) {
    for (int i = 0; i < n; i++) {
        MToken *t = toks[i];
        if (t->kind == MT_GROUP) {
            char open[2] = {t->open, '\0'};
            char close[2] = {t->close, '\0'};
            out_text(o, open);
            o->join = 1;
            emit_tokens(o, t->kids, t->nkids);
            out_text(o, close);
        } else if (t->kind == MT_OPAQUE) {
            out_opaque(o, t->text);
        } else {
            out_text(o, t->text);
        }
    }
}

/* Number of iterations a repetition at `kids` runs for the current transcription
 * path (the index path `path[0..npath)`). Counts the distinct values the first
 * metavariable found inside the repetition records at index `npath`; supports
 * nested repetitions. Returns -1 when no metavariable is found. */
static int trans_count(MTrans **kids, int n, MEnv *env, const int *path,
                       int npath) {
    for (int i = 0; i < n; i++) {
        MTrans *t = kids[i];
        if (t->kind == T_VAR) {
            MBind *b = env_get(env, t->text);
            if (!b) continue;
            int mx = -1;
            for (int k = 0; k < b->ncaps; k++) {
                if (b->depths[k] <= npath) continue;
                if (npath > 0 && b->paths[k] &&
                    memcmp(b->paths[k], path, sizeof(int) * (size_t)npath) != 0)
                    continue;
                int idx = b->paths[k][npath];
                if (idx > mx) mx = idx;
            }
            /* A metavariable bound *outside* this repetition (its captures are
             * shallower than `npath`) does not drive the count — skip it and
             * look for the variable that does. Only a variable with captures at
             * this depth gives the iteration count. */
            if (mx >= 0) return mx + 1;
            continue;
        } else if (t->kind == T_GROUP || t->kind == T_REP) {
            int c = trans_count(t->kids, t->nkids, env, path, npath);
            if (c >= 0) return c;
        }
    }
    return -1;
}

static void transcribe_var(Out *o, MBind *b, const int *path, int npath) {
    if (!b) return;
    int ci = bind_find(b, path, npath);
    if (ci >= 0)
        emit_tokens(o, b->caps[ci], b->lens[ci]);
    else if (b->ncaps == 1)
        emit_tokens(o, b->caps[0], b->lens[0]);
    /* used outside its repetition, or no matching iteration: emit nothing */
}

void transcribe(CxArena *a, CxDiagList *diags, CxSpan span, MPat **pat,
                int npat, MTrans **tr, int ntr, MEnv *env, const int *path,
                int npath, Out *o, CxMacroMap *map, const char *def_file) {
    for (int i = 0; i < ntr; i++) {
        MTrans *t = tr[i];
        switch (t->kind) {
        case T_TOKEN:
            if (t->text && !strcmp(t->text, "##")) o->join = 1;
            else if (t->text && t->text[0] == '#') out_opaque(o, t->text);
            else out_text(o, t->text);
            break;
        case T_VAR: {
            MBind *b = env_get(env, t->text);
            /* `$a:method` bound to `Trait::member`: emit the vtable member access
             * `Trait->member` and require `member` to be a method of `Trait`. */
            const char *frag = pat ? pat_find_frag(pat, npat, t->text) : NULL;
            if (frag && !strcmp(frag, "method")) {
                int ci = bind_find(b, path, npath);
                if (b && ci >= 0 && b->lens[ci] == 3 && b->caps[ci][1] &&
                    tok_is_punct(b->caps[ci][1], "::")) {
                    MToken *tr_tok = b->caps[ci][0];
                    MToken *me_tok = b->caps[ci][2];
                    if (tr_tok && me_tok) {
                        macro_req_add(a, map, me_tok->text, tr_tok->text,
                                      "ident", span);
                        char *s = cx_arena_printf(a, "%s->%s", tr_tok->text,
                                                  me_tok->text);
                        out_text(o, s);
                        break;
                    }
                }
            }
            MToken *bt = single_ident_cap(b, path, npath);
            /* `struct $name` defines a generated type: let definition on the
             * type name jump back to the `$name` in the macro. */
            if (bt && i > 0 && tr[i - 1]->kind == T_TOKEN &&
                tr[i - 1]->text && !strcmp(tr[i - 1]->text, "struct"))
                macro_map_add(a, map, bt->text, t->span, def_file);
            /* `$name(params) { … }` is a generated function/constructor: map the
             * name so LSP definition jumps to the macro. */
            if (bt && i + 2 < ntr && tr[i + 1]->kind == T_GROUP &&
                tr[i + 1]->open == '(' && tr[i + 2]->kind == T_GROUP &&
                tr[i + 2]->open == '{')
                macro_map_add(a, map, bt->text, t->span, def_file);
            /* `$name::method` in a transcription is the public spelling of the
             * internal `Name__method` name. Emit the joined name and record the
             * `method` token so the LSP can jump to it in the macro body. */
            if (bt && i + 2 < ntr && tr[i + 1]->kind == T_TOKEN &&
                tr[i + 1]->text && !strcmp(tr[i + 1]->text, "::") &&
                tr[i + 2]->kind == T_TOKEN && tr[i + 2]->text &&
                is_ident_start(tr[i + 2]->text[0])) {
                char *name = cx_arena_printf(a, "%s__%s", bt->text,
                                             tr[i + 2]->text);
                macro_map_add(a, map, name, tr[i + 2]->span, def_file);
                out_text(o, name);
                i += 2;
                break;
            }
            transcribe_var(o, b, path, npath);
            break;
        }
        case T_GROUP: {
            char open[2] = {t->open, '\0'};
            char close[2] = {t->close, '\0'};
            out_text(o, open);
            o->join = 1;
            transcribe(a, diags, span, pat, npat, t->kids, t->nkids, env, path,
                       npath, o, map, def_file);
            out_text(o, close);
            break;
        }
        case T_REP: {
            int count = trans_count(t->kids, t->nkids, env, path, npath);
            if (count < 0) count = 0;
            for (int k = 0; k < count; k++) {
                if (k > 0 && t->has_sep) {
                    char sep[2] = {t->sep, '\0'};
                    out_text(o, sep);
                }
                /* Extend the transcription path with this iteration's index so a
                 * nested repetition transcribes the right captures. */
                int *np = (int *)cx_arena_alloc(a,
                                                sizeof(int) * (size_t)(npath + 1));
                if (!np) break;
                for (int j = 0; j < npath; j++) np[j] = path[j];
                np[npath] = k;
                transcribe(a, diags, span, pat, npat, t->kids, t->nkids, env, np,
                           npath + 1, o, map, def_file);
            }
            break;
        }
        }
    }
    (void)a;
    (void)diags;
    (void)span;
}

/* Render a captured token slice as source text (used to recover the concrete
 * type bound to a `requires` metavariable). Groups are re-delimited. */
void render_tokens(CxBuf *b, MToken **toks, int n) {
    for (int i = 0; i < n; i++) {
        MToken *t = toks[i];
        if (i) cx_buf_putc(b, ' ');
        if (t->kind == MT_GROUP) {
            cx_buf_putc(b, t->open);
            render_tokens(b, t->kids, t->nkids);
            cx_buf_putc(b, t->close);
        } else if (t->text) {
            cx_buf_puts(b, t->text);
        }
    }
}
