/* Cx `macro_rules!` — matcher: pattern + token tree -> bindings (`MEnv`).
 *
 * Pattern parsing (`parse_pat_group`) and the backtracking matcher
 * (`match_seq`/`match_rep`) are self-contained and independent of transcription
 * and of the driver, so they are unit-testable on their own. */
#include "cx/cx.h"
#include "macro_internal.h"

#include <string.h>

/* ================================================================ patterns */

typedef enum { P_TOKEN, P_FRAG, P_REP, P_GROUP } PKind;

struct MPat {
    PKind   kind;
    char   *text;  /* literal token text, or fragment metavariable name */
    char   *frag;  /* fragment specifier for P_FRAG */
    char    open;  /* P_GROUP / P_REP delimiters */
    char    close;
    MPat  **kids;
    int     nkids;
    char    sep;    /* repetition separator, if has_sep */
    int     has_sep;
    char    op;     /* '*' or '+' */
};

static MPat *pat_new(CxArena *a, PKind kind) {
    MPat *p = (MPat *)cx_arena_alloc(a, sizeof(MPat));
    if (p) p->kind = kind;
    return p;
}

static void pat_push(CxArena *a, PatVec *v, MPat *p) {
    if (v->len == v->cap) {
        int cap = v->cap ? v->cap * 2 : 8;
        MPat **items = (MPat **)cx_arena_alloc(a, sizeof(MPat *) * (size_t)cap);
        if (!items) return;
        if (v->items && v->len)
            memcpy(items, v->items, sizeof(MPat *) * (size_t)v->len);
        v->items = items;
        v->cap = cap;
    }
    v->items[v->len++] = p;
}

/* ------------------------------------------------------- parsing patterns */

static PatVec parse_pat_tokens(CxArena *a, CxDiagList *diags, MToken **toks,
                               int n);

PatVec parse_pat_group(CxArena *a, CxDiagList *diags, MToken *g) {
    return parse_pat_tokens(a, diags, g->kids, g->nkids);
}

static PatVec parse_pat_tokens(CxArena *a, CxDiagList *diags, MToken **toks,
                               int n) {
    PatVec out = {0};
    for (int i = 0; i < n;) {
        MToken *t = toks[i];
        if (tok_is_punct(t, "$")) {
            if (i + 1 < n && toks[i + 1]->kind == MT_GROUP) {
                MToken *g = toks[i + 1];
                i += 2;
                MPat *r = pat_new(a, P_REP);
                if (!r) return out;
                r->open = g->open;
                r->close = g->close;
                PatVec sub = parse_pat_group(a, diags, g);
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
                pat_push(a, &out, r);
                continue;
            }
            if (i + 3 < n && toks[i + 1]->kind == MT_IDENT &&
                tok_is_punct(toks[i + 2], ":") &&
                toks[i + 3]->kind == MT_IDENT) {
                MPat *f = pat_new(a, P_FRAG);
                if (!f) return out;
                f->text = toks[i + 1]->text;
                f->frag = toks[i + 3]->text;
                pat_push(a, &out, f);
                i += 4;
                continue;
            }
            cx_error(diags, tok_span(t), "malformed macro fragment or repetition");
            i++;
            continue;
        }
        if (t->kind == MT_GROUP) {
            MPat *g = pat_new(a, P_GROUP);
            if (!g) return out;
            g->open = t->open;
            g->close = t->close;
            PatVec sub = parse_pat_group(a, diags, t);
            g->kids = sub.items;
            g->nkids = sub.len;
            pat_push(a, &out, g);
            i++;
            continue;
        }
        MPat *p = pat_new(a, P_TOKEN);
        if (!p) return out;
        p->text = t->text;
        pat_push(a, &out, p);
        i++;
    }
    return out;
}

/* ================================================================== macros */

void req_push(CxArena *a, ReqVec *v, MReq req) {
    if (v->len == v->cap) {
        int cap = v->cap ? v->cap * 2 : 4;
        MReq *items = (MReq *)cx_arena_alloc(a, sizeof(MReq) * (size_t)cap);
        if (!items) return;
        if (v->items && v->len)
            memcpy(items, v->items, sizeof(MReq) * (size_t)v->len);
        v->items = items;
        v->cap = cap;
    }
    v->items[v->len++] = req;
}

int pat_has_var(MPat **pats, int n, const char *name) {
    for (int i = 0; i < n; i++) {
        MPat *p = pats[i];
        if (p->kind == P_FRAG && p->text && !strcmp(p->text, name)) return 1;
        if ((p->kind == P_GROUP || p->kind == P_REP) && p->kids &&
            pat_has_var(p->kids, p->nkids, name))
            return 1;
    }
    return 0;
}

/* The fragment specifier (`ident`, `ty`, `expr`, …) a metavariable is bound
 * with in a pattern, or NULL. */
const char *pat_find_frag(MPat **pats, int n, const char *name) {
    for (int i = 0; i < n; i++) {
        MPat *p = pats[i];
        if (p->kind == P_FRAG && p->text && !strcmp(p->text, name)) return p->frag;
        if ((p->kind == P_GROUP || p->kind == P_REP) && p->kids) {
            const char *f = pat_find_frag(p->kids, p->nkids, name);
            if (f) return f;
        }
    }
    return NULL;
}

/* ================================================================= matching */

/* Push an iteration index onto a (matching) environment's current path. */
static void env_push_path(CxArena *a, MEnv *e, int idx) {
    if (e->npath == e->pathcap) {
        int cap = e->pathcap ? e->pathcap * 2 : 8;
        int *items = (int *)cx_arena_alloc(a, sizeof(int) * (size_t)cap);
        if (!items) return;
        if (e->path && e->npath)
            memcpy(items, e->path, sizeof(int) * (size_t)e->npath);
        e->path = items;
        e->pathcap = cap;
    }
    e->path[e->npath++] = idx;
}

MBind *env_get(MEnv *e, const char *name) {
    for (int i = 0; i < e->len; i++)
        if (e->items[i].name && !strcmp(e->items[i].name, name))
            return &e->items[i];
    return NULL;
}

static MBind *env_get_or_add(CxArena *a, MEnv *e, const char *name) {
    MBind *b = env_get(e, name);
    if (b) return b;
    if (e->len == e->cap) {
        int cap = e->cap ? e->cap * 2 : 8;
        MBind *items = (MBind *)cx_arena_alloc(a, sizeof(MBind) * (size_t)cap);
        if (!items) return NULL;
        if (e->items && e->len)
            memcpy(items, e->items, sizeof(MBind) * (size_t)e->len);
        e->items = items;
        e->cap = cap;
    }
    b = &e->items[e->len++];
    memset(b, 0, sizeof *b);
    b->name = (char *)name;
    return b;
}

static void bind_add(CxArena *a, MBind *b, MToken **tokens, int n,
                     const int *path, int npath) {
    if (b->ncaps == b->cap) {
        int cap = b->cap ? b->cap * 2 : 4;
        MToken ***caps =
            (MToken ***)cx_arena_alloc(a, sizeof(MToken **) * (size_t)cap);
        int *lens = (int *)cx_arena_alloc(a, sizeof(int) * (size_t)cap);
        int **paths = (int **)cx_arena_alloc(a, sizeof(int *) * (size_t)cap);
        int *depths = (int *)cx_arena_alloc(a, sizeof(int) * (size_t)cap);
        if (!caps || !lens || !paths || !depths) return;
        if (b->caps && b->ncaps)
            memcpy(caps, b->caps, sizeof(MToken **) * (size_t)b->ncaps);
        if (b->lens && b->ncaps)
            memcpy(lens, b->lens, sizeof(int) * (size_t)b->ncaps);
        if (b->paths && b->ncaps)
            memcpy(paths, b->paths, sizeof(int *) * (size_t)b->ncaps);
        if (b->depths && b->ncaps)
            memcpy(depths, b->depths, sizeof(int) * (size_t)b->ncaps);
        b->caps = caps;
        b->lens = lens;
        b->paths = paths;
        b->depths = depths;
        b->cap = cap;
    }
    int *p = NULL;
    if (npath > 0) {
        p = (int *)cx_arena_alloc(a, sizeof(int) * (size_t)npath);
        if (p) memcpy(p, path, sizeof(int) * (size_t)npath);
    }
    b->caps[b->ncaps] = tokens;
    b->lens[b->ncaps] = n;
    b->paths[b->ncaps] = p;
    b->depths[b->ncaps] = npath;
    b->ncaps++;
}

static MToken **cap_range(CxArena *a, MToken **in, int start, int count) {
    if (count <= 0) return NULL;
    MToken **out =
        (MToken **)cx_arena_alloc(a, sizeof(MToken *) * (size_t)count);
    if (!out) return NULL;
    memcpy(out, in + start, sizeof(MToken *) * (size_t)count);
    return out;
}

static void env_merge(CxArena *a, MEnv *dst, MEnv *src) {
    for (int i = 0; i < src->len; i++) {
        MBind *b = &src->items[i];
        for (int k = 0; k < b->ncaps; k++) {
            MBind *d = env_get_or_add(a, dst, b->name);
            if (d) bind_add(a, d, b->caps[k], b->lens[k], b->paths[k],
                            b->depths[k]);
        }
    }
}

static int frag_consume(MToken **in, int nin, int pos, const char *frag,
                        int *count) {
    *count = 0;
    if (!frag) return 0;
    if (!strcmp(frag, "ident") || !strcmp(frag, "field")) {
        if (pos < nin && in[pos]->kind == MT_IDENT) {
            *count = 1;
            return 1;
        }
        return 0;
    }
    if (!strcmp(frag, "tt")) {
        if (pos < nin) {
            *count = 1;
            return 1;
        }
        return 0;
    }
    /* `$x:item` matches a whole item: a balanced group (plus an optional `;`)
     * or a run of tokens terminated by a top-level `;`. */
    if (!strcmp(frag, "item")) {
        int p = pos;
        if (p < nin && in[p]->kind == MT_GROUP && in[p]->open == '{') {
            p++;
            if (p < nin && tok_is_punct(in[p], ";")) p++;
        } else {
            while (p < nin && !tok_is_punct(in[p], ";")) p++;
            if (p < nin && tok_is_punct(in[p], ";")) p++;
        }
        if (p == pos) return 0;
        *count = p - pos;
        return 1;
    }
    /* `$x:block` matches a single balanced `{ … }` group. */
    if (!strcmp(frag, "block")) {
        if (pos < nin && in[pos]->kind == MT_GROUP && in[pos]->open == '{') {
            *count = 1;
            return 1;
        }
        return 0;
    }
    if (!strcmp(frag, "lit")) {
        if (pos < nin && (in[pos]->kind == MT_NUM ||
                          in[pos]->kind == MT_STRING)) {
            *count = 1;
            return 1;
        }
        return 0;
    }
    if (!strcmp(frag, "path")) {
        int p = pos;
        if (p < nin && in[p]->kind == MT_IDENT) {
            p++;
            while (p + 1 < nin && tok_is_punct(in[p], "::") &&
                   in[p + 1]->kind == MT_IDENT)
                p += 2;
        }
        *count = p - pos;
        return *count > 0;
    }
    /* `Trait::member` — a qualified trait method reference (the sugar used to
     * say where a function comes from). */
    if (!strcmp(frag, "method")) {
        if (pos + 2 < nin && in[pos]->kind == MT_IDENT &&
            tok_is_punct(in[pos + 1], "::") && in[pos + 2]->kind == MT_IDENT) {
            *count = 3;
            return 1;
        }
        return 0;
    }
    if (!strcmp(frag, "ty")) {
        int p = pos;
        int any = 0;
        while (p < nin) {
            if (in[p]->kind == MT_IDENT) {
                p++;
                any = 1;
            } else if (tok_is_punct(in[p], "*")) {
                p++;
            } else {
                break;
            }
        }
        if (!any) return 0;
        *count = p - pos;
        return 1;
    }
    if (!strcmp(frag, "expr")) {
        int p = pos;
        while (p < nin && !tok_is_punct(in[p], ",") &&
               !tok_is_punct(in[p], ";"))
            p++;
        if (p == pos) return 0;
        *count = p - pos;
        return 1;
    }
    if (!strcmp(frag, "stmt")) {
        int p = pos;
        while (p < nin && !tok_is_punct(in[p], ";")) p++;
        if (p == pos) return 0;
        *count = p - pos;
        return 1;
    }
    return 0;
}

int match_seq(CxArena *a, CxDiagList *diags, MPat **pats, int np,
              MToken **in, int nin, int *pos, MEnv *env);

static int match_rep(CxArena *a, CxDiagList *diags, MPat *p, MToken **in,
                     int nin, int *pos, MEnv *env) {
    int count = 0;
    int sep_at = -1;
    for (;;) {
        int iter_start = *pos;
        MEnv iter = {0};
        /* Each iteration runs with the outer path extended by its index, so a
         * metavariable captured by a nested repetition records the index path
         * that reaches it. */
        for (int i = 0; i < env->npath; i++) env_push_path(a, &iter, env->path[i]);
        env_push_path(a, &iter, count);
        if (!match_seq(a, diags, p->kids, p->nkids, in, nin, pos, &iter)) {
            *pos = iter_start;
            if (sep_at >= 0) {
                /* A trailing separator (`$( … ),*` on `a, b,`) is allowed. */
                *pos = (sep_at + 1 == nin) ? sep_at + 1 : sep_at;
            }
            break;
        }
        if (*pos == iter_start) break;
        env_merge(a, env, &iter);
        count++;
        sep_at = -1;
        if (p->has_sep) {
            if (*pos < nin && in[*pos]->kind == MT_PUNCT && in[*pos]->text &&
                in[*pos]->text[0] == p->sep) {
                sep_at = *pos;
                (*pos)++;
            } else {
                break;
            }
        }
    }
    if (p->op == '+' && count == 0) return 0;
    return 1;
}

int match_seq(CxArena *a, CxDiagList *diags, MPat **pats, int np,
              MToken **in, int nin, int *pos, MEnv *env) {
    for (int i = 0; i < np; i++) {
        MPat *p = pats[i];
        switch (p->kind) {
        case P_TOKEN:
            if (*pos >= nin) return 0;
            if (!tok_is(in[*pos], p->text)) return 0;
            (*pos)++;
            break;
        case P_GROUP: {
            if (*pos >= nin || in[*pos]->kind != MT_GROUP) return 0;
            MToken *g = in[*pos];
            if (g->open != p->open || g->close != p->close) return 0;
            int gp = 0;
            if (!match_seq(a, diags, p->kids, p->nkids, g->kids, g->nkids, &gp,
                           env))
                return 0;
            if (gp != g->nkids) return 0;
            (*pos)++;
            break;
        }
        case P_FRAG: {
            int count = 0;
            if (!frag_consume(in, nin, *pos, p->frag, &count)) return 0;
            MToken **cap = cap_range(a, in, *pos, count);
            MBind *b = env_get_or_add(a, env, p->text);
            if (b) bind_add(a, b, cap, count, env->path, env->npath);
            *pos += count;
            break;
        }
        case P_REP:
            if (!match_rep(a, diags, p, in, nin, pos, env)) return 0;
            break;
        }
    }
    return 1;
}

/* The capture whose repetition index path matches the current
 * `path[0..npath)` at the capture's own depth, or -1. A capture belongs to the
 * current position when its depth equals `npath` and its path is a prefix of
 * `path`. */
int bind_find(const MBind *b, const int *path, int npath) {
    if (!b) return -1;
    for (int k = 0; k < b->ncaps; k++) {
        if (b->depths[k] != npath) continue;
        if (npath > 0 && b->paths[k] &&
            memcmp(b->paths[k], path, sizeof(int) * (size_t)npath) != 0)
            continue;
        return k;
    }
    return -1;
}

/* The single token captured by `$x` at the current path when it is an
 * identifier, or NULL. Used to form `$x::method` names and to record
 * macro-definition origins. */
MToken *single_ident_cap(MBind *b, const int *path, int npath) {
    int ci = bind_find(b, path, npath);
    if (!b || ci < 0 || b->lens[ci] != 1) return NULL;
    MToken *t = b->caps[ci][0];
    return (t && t->kind == MT_IDENT) ? t : NULL;
}
