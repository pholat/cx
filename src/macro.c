/* Cx `macro_rules!` — token-level definition, matching and transcription.
 *
 * This is intentionally a *textual* pre-parse pass:
 *
 *   1. the whole source is tokenised into a small token-tree stream,
 *   2. every `macro_rules! name { … }` definition is collected and removed,
 *   3. every invocation `name!( … )` of a known macro is matched against the
 *      rules and replaced by the transcribed token stream,
 *   4. the result is fed back to the normal Cx parser.
 *
 * Unknown `NAME!( … )` invocations are left untouched, so the C-macro
 * passthrough implemented by the lexer keeps working.
 *
 * Implemented: fragment specifiers `ident`, `ty`, `expr`, `stmt`, `tt`, `lit`,
 * `path`, `field`; repetitions `$( … )*` / `$( … )+` with an optional single
 * separator (`$( … ),*`); and `##` identifier concatenation. Nested macro
 * invocations are expanded by repeated passes.
 *
 * Hygiene is not implemented yet: identifiers introduced by a macro body are
 * emitted verbatim (the `$…` fragments are, of course, substituted).
 */
#include "cx/macro.h"
#include "cx/cx.h"

#include <stdlib.h>
#include <string.h>

/* =================================================================== tokens */

typedef enum {
    MT_IDENT,
    MT_NUM,
    MT_STRING,
    MT_PUNCT,
    MT_GROUP,
    MT_OPAQUE /* preprocessor line, `raw { … }`, `#[…]` */
} MTKind;

typedef struct MToken MToken;

struct MToken {
    MTKind  kind;
    char   *text;  /* leaf text (ident/punct/literal/opaque) */
    char    open;  /* group delimiters */
    char    close;
    MToken **kids;
    int     nkids;
    int     line;
    int     col;
    size_t  start; /* byte offsets in the source buffer */
    size_t  end;
};

typedef struct {
    MToken **items;
    int      len;
    int      cap;
} MTVec;

static void mtv_push(CxArena *a, MTVec *v, MToken *t) {
    if (v->len == v->cap) {
        int cap = v->cap ? v->cap * 2 : 8;
        MToken **items =
            (MToken **)cx_arena_alloc(a, sizeof(MToken *) * (size_t)cap);
        if (!items) return;
        if (v->items && v->len)
            memcpy(items, v->items, sizeof(MToken *) * (size_t)v->len);
        v->items = items;
        v->cap = cap;
    }
    v->items[v->len++] = t;
}

/* --------------------------------------------------------------- tokenizer */

typedef struct {
    CxArena    *arena;
    CxDiagList *diags;
    const char *src;
    size_t      len;
    size_t      pos;
    int         line;
    int         col;
} LexCtx;

static char lx_peek(LexCtx *l, size_t k) {
    return (l->pos + k < l->len) ? l->src[l->pos + k] : '\0';
}

static void lx_adv(LexCtx *l) {
    if (l->pos >= l->len) return;
    char c = l->src[l->pos++];
    if (c == '\n') {
        l->line++;
        l->col = 1;
    } else {
        l->col++;
    }
}

static int is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_ident_char(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static int is_digit(char c) { return c >= '0' && c <= '9'; }

static void lx_skip_ws(LexCtx *l) {
    for (;;) {
        char c = lx_peek(l, 0);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            lx_adv(l);
            continue;
        }
        if (c == '/' && lx_peek(l, 1) == '/') {
            while (l->pos < l->len && lx_peek(l, 0) != '\n') lx_adv(l);
            continue;
        }
        if (c == '/' && lx_peek(l, 1) == '*') {
            lx_adv(l);
            lx_adv(l);
            while (l->pos < l->len) {
                if (lx_peek(l, 0) == '*' && lx_peek(l, 1) == '/') {
                    lx_adv(l);
                    lx_adv(l);
                    break;
                }
                lx_adv(l);
            }
            continue;
        }
        break;
    }
}

static void lx_skip_string(LexCtx *l, char quote) {
    lx_adv(l); /* opening quote */
    while (l->pos < l->len) {
        char c = lx_peek(l, 0);
        if (c == '\\') {
            lx_adv(l);
            if (l->pos < l->len) lx_adv(l);
            continue;
        }
        lx_adv(l);
        if (c == quote) break;
    }
}

/* Capture a balanced-brace body; `l->pos` is at the opening `{`. */
static size_t lx_capture_braces(LexCtx *l) {
    int depth = 0;
    while (l->pos < l->len) {
        char c = lx_peek(l, 0);
        if (c == '"' || c == '\'') {
            lx_skip_string(l, c);
            continue;
        }
        if (c == '/' && lx_peek(l, 1) == '/') {
            while (l->pos < l->len && lx_peek(l, 0) != '\n') lx_adv(l);
            continue;
        }
        if (c == '/' && lx_peek(l, 1) == '*') {
            lx_adv(l);
            lx_adv(l);
            while (l->pos < l->len) {
                if (lx_peek(l, 0) == '*' && lx_peek(l, 1) == '/') {
                    lx_adv(l);
                    lx_adv(l);
                    break;
                }
                lx_adv(l);
            }
            continue;
        }
        if (c == '{') depth++;
        else if (c == '}') {
            depth--;
            lx_adv(l);
            if (depth == 0) return l->pos;
            continue;
        }
        lx_adv(l);
    }
    return l->pos;
}

/* Capture `#[ … ]` (balanced brackets); `l->pos` is at `#`. */
static size_t lx_capture_attr(LexCtx *l) {
    int depth = 0;
    while (l->pos < l->len) {
        char c = lx_peek(l, 0);
        if (c == '"' || c == '\'') {
            lx_skip_string(l, c);
            continue;
        }
        if (c == '[') depth++;
        else if (c == ']') {
            depth--;
            lx_adv(l);
            if (depth <= 0) return l->pos;
            continue;
        }
        lx_adv(l);
    }
    return l->pos;
}

static char *lx_strndup(CxArena *a, const char *s, size_t n) {
    return cx_arena_strndup(a, s, n);
}

static MToken *tok_new(CxArena *a, MTKind kind, int line, int col,
                       size_t start) {
    MToken *t = (MToken *)cx_arena_alloc(a, sizeof(MToken));
    if (!t) return NULL;
    t->kind = kind;
    t->line = line;
    t->col = col;
    t->start = start;
    t->end = start;
    return t;
}

static const char *multi_punct[] = {
    "=>", "##", "::", "->", "++", "--", "+=", "-=", "*=", "/=", "%=",
    "<<", ">>", "<=", ">=", "==", "!=", "&&", "||", NULL
};

static void lex_tokens(LexCtx *l, char stop, MTVec *out) {
    for (;;) {
        lx_skip_ws(l);
        if (l->pos >= l->len) return;

        char c = lx_peek(l, 0);
        if (stop && c == stop) {
            lx_adv(l);
            return;
        }

        size_t start = l->pos;
        int sline = l->line;
        int scol = l->col;

        if (c == '#' && lx_peek(l, 1) == '[') {
            size_t end = lx_capture_attr(l);
            MToken *t = tok_new(l->arena, MT_OPAQUE, sline, scol, start);
            if (!t) return;
            t->text = lx_strndup(l->arena, l->src + start, end - start);
            t->end = end;
            mtv_push(l->arena, out, t);
            continue;
        }
        if (c == '#' && lx_peek(l, 1) == '#') {
            lx_adv(l);
            lx_adv(l);
            MToken *t = tok_new(l->arena, MT_PUNCT, sline, scol, start);
            if (!t) return;
            t->text = "##";
            t->end = l->pos;
            mtv_push(l->arena, out, t);
            continue;
        }
        if (c == '#') {
            /* Preprocessor line (verbatim). */
            while (l->pos < l->len && lx_peek(l, 0) != '\n') lx_adv(l);
            MToken *t = tok_new(l->arena, MT_OPAQUE, sline, scol, start);
            if (!t) return;
            t->text = lx_strndup(l->arena, l->src + start, l->pos - start);
            t->end = l->pos;
            mtv_push(l->arena, out, t);
            continue;
        }
        if (is_ident_start(c)) {
            while (l->pos < l->len && is_ident_char(lx_peek(l, 0))) lx_adv(l);
            char *text = lx_strndup(l->arena, l->src + start, l->pos - start);
            if (text && strcmp(text, "raw") == 0) {
                /* `raw { … }` is verbatim C: keep it opaque. */
                size_t save_pos = l->pos;
                int save_line = l->line;
                int save_col = l->col;
                lx_skip_ws(l);
                if (lx_peek(l, 0) == '{') {
                    size_t end = lx_capture_braces(l);
                    MToken *t = tok_new(l->arena, MT_OPAQUE, sline, scol, start);
                    if (!t) return;
                    t->text = lx_strndup(l->arena, l->src + start, end - start);
                    t->end = end;
                    mtv_push(l->arena, out, t);
                    continue;
                }
                l->pos = save_pos;
                l->line = save_line;
                l->col = save_col;
            }
            MToken *t = tok_new(l->arena, MT_IDENT, sline, scol, start);
            if (!t) return;
            t->text = text;
            t->end = l->pos;
            mtv_push(l->arena, out, t);
            continue;
        }
        if (c == '"' || c == '\'') {
            lx_skip_string(l, c);
            MToken *t = tok_new(l->arena, MT_STRING, sline, scol, start);
            if (!t) return;
            t->text = lx_strndup(l->arena, l->src + start, l->pos - start);
            t->end = l->pos;
            mtv_push(l->arena, out, t);
            continue;
        }
        if (is_digit(c)) {
            while (l->pos < l->len) {
                char d = lx_peek(l, 0);
                if (is_ident_char(d) || d == '.') lx_adv(l);
                else break;
            }
            MToken *t = tok_new(l->arena, MT_NUM, sline, scol, start);
            if (!t) return;
            t->text = lx_strndup(l->arena, l->src + start, l->pos - start);
            t->end = l->pos;
            mtv_push(l->arena, out, t);
            continue;
        }
        if (c == '(' || c == '[' || c == '{') {
            char close = c == '(' ? ')' : (c == '[' ? ']' : '}');
            lx_adv(l);
            MTVec kids = {0};
            lex_tokens(l, close, &kids);
            MToken *t = tok_new(l->arena, MT_GROUP, sline, scol, start);
            if (!t) return;
            t->open = c;
            t->close = close;
            t->kids = kids.items;
            t->nkids = kids.len;
            t->end = l->pos;
            mtv_push(l->arena, out, t);
            continue;
        }
        if (c == ')' || c == ']' || c == '}') {
            lx_adv(l);
            MToken *t = tok_new(l->arena, MT_PUNCT, sline, scol, start);
            if (!t) return;
            char *txt = cx_arena_alloc(l->arena, 2);
            if (!txt) return;
            txt[0] = c;
            txt[1] = '\0';
            t->text = txt;
            t->end = l->pos;
            mtv_push(l->arena, out, t);
            continue;
        }

        /* Longest multi-character punctuator, else a single character. */
        const char *picked = NULL;
        for (int i = 0; multi_punct[i]; i++) {
            size_t n = strlen(multi_punct[i]);
            if (l->pos + n <= l->len &&
                memcmp(l->src + l->pos, multi_punct[i], n) == 0) {
                picked = multi_punct[i];
                break;
            }
        }
        MToken *t = tok_new(l->arena, MT_PUNCT, sline, scol, start);
        if (!t) return;
        if (picked) {
            size_t n = strlen(picked);
            for (size_t i = 0; i < n; i++) lx_adv(l);
            t->text = lx_strndup(l->arena, picked, n);
        } else {
            lx_adv(l);
            char *txt = cx_arena_alloc(l->arena, 2);
            if (!txt) return;
            txt[0] = c;
            txt[1] = '\0';
            t->text = txt;
        }
        t->end = l->pos;
        mtv_push(l->arena, out, t);
    }
}

/* ================================================================ patterns */

typedef enum { P_TOKEN, P_FRAG, P_REP, P_GROUP } PKind;

typedef struct MPat MPat;
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

typedef struct {
    MPat **items;
    int    len;
    int    cap;
} PatVec;

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

typedef enum { T_TOKEN, T_VAR, T_REP, T_GROUP } TKind;

typedef struct MTrans MTrans;
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

typedef struct {
    MTrans **items;
    int      len;
    int      cap;
} TrVec;

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

static int tok_is_punct(const MToken *t, const char *s) {
    return t && t->kind == MT_PUNCT && t->text && strcmp(t->text, s) == 0;
}

static int tok_is(const MToken *t, const char *s) {
    return t && t->text && strcmp(t->text, s) == 0;
}

static CxSpan tok_span(const MToken *t) {
    CxSpan s = {t->line, t->col, t->line, t->col};
    return s;
}

/* Like `tok_span` but spanning the whole token, so position queries (the LSP's
 * "is the cursor on this invocation?") match any column of the identifier. */
static CxSpan tok_span_full(const MToken *t) {
    int n = (int)(t->end > t->start ? t->end - t->start : 1);
    CxSpan s = {t->line, t->col, t->line, t->col + (n > 1 ? n - 1 : 0)};
    return s;
}

/* ------------------------------------------------------- parsing patterns */

static PatVec parse_pat_tokens(CxArena *a, CxDiagList *diags, MToken **toks,
                               int n);

static PatVec parse_pat_group(CxArena *a, CxDiagList *diags, MToken *g) {
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

static TrVec parse_tr_tokens(CxArena *a, CxDiagList *diags, MToken **toks,
                             int n);

static TrVec parse_tr_group(CxArena *a, CxDiagList *diags, MToken *g) {
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

/* ================================================================== macros */

/* A `requires $x: TraitA + TraitB` constraint attached to one metavariable.
 * The requirement is parsed and recorded on the rule; it is not statically
 * checked yet (that is a later roadmap item). */
typedef struct {
    char  *var;
    char **traits;
    int    ntraits;
} MReq;

typedef struct {
    MReq *items;
    int   len;
    int   cap;
} ReqVec;

typedef struct {
    MPat  **pat;
    int     npat;
    MTrans **tr;
    int     ntr;
    MReq   *reqs;
    int     nreqs;
} MRule;

static void req_push(CxArena *a, ReqVec *v, MReq req) {
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

static int pat_has_var(MPat **pats, int n, const char *name) {
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
static const char *pat_find_frag(MPat **pats, int n, const char *name) {
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

typedef struct {
    char      *name;
    MRule     *rules;
    int        nrules;
    int        cap;
    CxSpan     span;
} Macro;

typedef struct {
    Macro *items;
    int    len;
    int    cap;
} MacroTable;

static Macro *find_macro(MacroTable *t, const char *name) {
    for (int i = 0; i < t->len; i++)
        if (t->items[i].name && !strcmp(t->items[i].name, name))
            return &t->items[i];
    return NULL;
}

static Macro *table_add(CxArena *a, MacroTable *t, const char *name,
                        CxSpan span) {
    if (t->len == t->cap) {
        int cap = t->cap ? t->cap * 2 : 8;
        Macro *items = (Macro *)cx_arena_alloc(a, sizeof(Macro) * (size_t)cap);
        if (!items) return NULL;
        if (t->items && t->len)
            memcpy(items, t->items, sizeof(Macro) * (size_t)t->len);
        t->items = items;
        t->cap = cap;
    }
    Macro *m = &t->items[t->len++];
    memset(m, 0, sizeof *m);
    m->name = (char *)name;
    m->span = span;
    return m;
}

static void macro_add_rule(CxArena *a, Macro *m, MRule r) {
    if (m->nrules == m->cap) {
        int cap = m->cap ? m->cap * 2 : 4;
        MRule *rules = (MRule *)cx_arena_alloc(a, sizeof(MRule) * (size_t)cap);
        if (!rules) return;
        if (m->rules && m->nrules)
            memcpy(rules, m->rules, sizeof(MRule) * (size_t)m->nrules);
        m->rules = rules;
        m->cap = cap;
    }
    m->rules[m->nrules++] = r;
}

/* ================================================================= matching */

typedef struct {
    char     *name;
    MToken ***caps; /* one token list per repetition iteration */
    int      *lens;
    int       ncaps;
    int       cap;
} MBind;

typedef struct {
    MBind *items;
    int    len;
    int    cap;
} MEnv;

static MBind *env_get(MEnv *e, const char *name) {
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

static void bind_add(CxArena *a, MBind *b, MToken **tokens, int n) {
    if (b->ncaps == b->cap) {
        int cap = b->cap ? b->cap * 2 : 4;
        MToken ***caps =
            (MToken ***)cx_arena_alloc(a, sizeof(MToken **) * (size_t)cap);
        int *lens = (int *)cx_arena_alloc(a, sizeof(int) * (size_t)cap);
        if (!caps || !lens) return;
        if (b->caps && b->ncaps)
            memcpy(caps, b->caps, sizeof(MToken **) * (size_t)b->ncaps);
        if (b->lens && b->ncaps)
            memcpy(lens, b->lens, sizeof(int) * (size_t)b->ncaps);
        b->caps = caps;
        b->lens = lens;
        b->cap = cap;
    }
    b->caps[b->ncaps] = tokens;
    b->lens[b->ncaps] = n;
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
            if (d) bind_add(a, d, b->caps[k], b->lens[k]);
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

static int match_seq(CxArena *a, CxDiagList *diags, MPat **pats, int np,
                     MToken **in, int nin, int *pos, MEnv *env);

static int match_rep(CxArena *a, CxDiagList *diags, MPat *p, MToken **in,
                     int nin, int *pos, MEnv *env) {
    int count = 0;
    int sep_at = -1;
    for (;;) {
        int iter_start = *pos;
        MEnv iter = {0};
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

static int match_seq(CxArena *a, CxDiagList *diags, MPat **pats, int np,
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
            if (b) bind_add(a, b, cap, count);
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

/* =========================================================== transcription */

typedef struct {
    CxBuf *out;
    int    join; /* next token is concatenated (`##`) */
} Out;

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

static int trans_count(MTrans **kids, int n, MEnv *env);

static int trans_kids_count(MTrans **kids, int nk, MEnv *env) {
    for (int i = 0; i < nk; i++) {
        MTrans *t = kids[i];
        if (t->kind == T_VAR) {
            MBind *b = env_get(env, t->text);
            if (b) return b->ncaps;
        } else if (t->kind == T_GROUP || t->kind == T_REP) {
            int c = trans_kids_count(t->kids, t->nkids, env);
            if (c >= 0) return c;
        }
    }
    return -1;
}

static int trans_count(MTrans **kids, int n, MEnv *env) {
    int c = trans_kids_count(kids, n, env);
    return c < 0 ? 0 : c;
}

static void macro_map_add(CxArena *a, CxMacroMap *map, const char *name,
                          CxSpan span) {
    if (!map || !name) return;
    for (int i = 0; i < map->len; i++)
        if (map->items[i].name && !strcmp(map->items[i].name, name)) return;
    if (map->len == map->cap) {
        int cap = map->cap ? map->cap * 2 : 8;
        CxMacroOrigin *items =
            (CxMacroOrigin *)cx_arena_alloc(a, sizeof(CxMacroOrigin) * (size_t)cap);
        if (!items) return;
        if (map->items && map->len)
            memcpy(items, map->items, sizeof(CxMacroOrigin) * (size_t)map->len);
        map->items = items;
        map->cap = cap;
    }
    map->items[map->len].name = cx_arena_strdup(a, name);
    map->items[map->len].span = span;
    map->len++;
}

const CxSpan *cx_macro_map_find(const CxMacroMap *map, const char *name) {
    if (!map || !name) return NULL;
    for (int i = 0; i < map->len; i++)
        if (map->items[i].name && !strcmp(map->items[i].name, name))
            return &map->items[i].span;
    return NULL;
}

/* Record a `requires` binding: `type_text` was bound to a metavariable that must
 * implement `trait` (or, for an `ident`/`path` fragment, name one of its
 * methods). Duplicates (same type + trait) are ignored. */
static void macro_req_add(CxArena *a, CxMacroMap *map, const char *type_text,
                          const char *trait, const char *frag, CxSpan span) {
    if (!map || !type_text || !trait) return;
    for (int i = 0; i < map->nreqs; i++)
        if (map->reqs[i].type && map->reqs[i].trait &&
            !strcmp(map->reqs[i].type, type_text) &&
            !strcmp(map->reqs[i].trait, trait))
            return;
    if (map->nreqs == map->reqcap) {
        int cap = map->reqcap ? map->reqcap * 2 : 8;
        CxMacroReq *items =
            (CxMacroReq *)cx_arena_alloc(a, sizeof(CxMacroReq) * (size_t)cap);
        if (!items) return;
        if (map->reqs && map->nreqs)
            memcpy(items, map->reqs, sizeof(CxMacroReq) * (size_t)map->nreqs);
        map->reqs = items;
        map->reqcap = cap;
    }
    map->reqs[map->nreqs].type = cx_arena_strdup(a, type_text);
    map->reqs[map->nreqs].trait = cx_arena_strdup(a, trait);
    map->reqs[map->nreqs].frag = frag ? cx_arena_strdup(a, frag) : NULL;
    map->reqs[map->nreqs].span = span;
    map->nreqs++;
}

/* Record a `macro_rules!` invocation: the name token's site and the macro's
 * definition span. Deduplicated by (name, site). */
static void macro_invoke_add(CxArena *a, CxMacroMap *map, const char *name,
                             CxSpan site, CxSpan def) {
    if (!map || !name) return;
    for (int i = 0; i < map->ninvokes; i++)
        if (map->invokes[i].name && !strcmp(map->invokes[i].name, name) &&
            map->invokes[i].site.first_line == site.first_line &&
            map->invokes[i].site.first_column == site.first_column)
            return;
    if (map->ninvokes == map->invcap) {
        int cap = map->invcap ? map->invcap * 2 : 8;
        CxMacroInvoke *items =
            (CxMacroInvoke *)cx_arena_alloc(a, sizeof(CxMacroInvoke) * (size_t)cap);
        if (!items) return;
        if (map->invokes && map->ninvokes)
            memcpy(items, map->invokes,
                   sizeof(CxMacroInvoke) * (size_t)map->ninvokes);
        map->invokes = items;
        map->invcap = cap;
    }
    map->invokes[map->ninvokes].name = cx_arena_strdup(a, name);
    map->invokes[map->ninvokes].site = site;
    map->invokes[map->ninvokes].def = def;
    map->ninvokes++;
}

const CxSpan *cx_macro_map_find_invocation(const CxMacroMap *map, int line,
                                           int column) {
    if (!map) return NULL;
    for (int i = 0; i < map->ninvokes; i++) {
        CxSpan s = map->invokes[i].site;
        if (line < s.first_line || line > s.last_line) continue;
        if (line == s.first_line && column < s.first_column) continue;
        if (line == s.last_line && column > s.last_column) continue;
        return &map->invokes[i].def;
    }
    return NULL;
}

/* Render a captured token slice as source text (used to recover the concrete
 * type bound to a `requires` metavariable). Groups are re-delimited. */
static void render_tokens(CxBuf *b, MToken **toks, int n) {
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

static void transcribe(CxArena *a, CxDiagList *diags, CxSpan span, MPat **pat,
                       int npat, MTrans **tr, int ntr, MEnv *env, int index,
                       Out *o, CxMacroMap *map);

/* The single token captured by `$x` when it is an identifier, or NULL. Used to
 * form `$x::method` names and to record macro-definition origins. */
static MToken *single_ident_cap(MBind *b, int index) {
    if (!b) return NULL;
    int ci = index >= 0 ? index : 0;
    if (ci >= b->ncaps || b->lens[ci] != 1) return NULL;
    MToken *t = b->caps[ci][0];
    return (t && t->kind == MT_IDENT) ? t : NULL;
}

static void transcribe_var(Out *o, MBind *b, int index) {
    if (!b) return;
    if (index >= 0) {
        if (index < b->ncaps) emit_tokens(o, b->caps[index], b->lens[index]);
    } else if (b->ncaps == 1) {
        emit_tokens(o, b->caps[0], b->lens[0]);
    } else if (b->ncaps == 0) {
        /* used outside its repetition; emit nothing */
    } else {
        /* ambiguous: repeated metavariable used outside a repetition */
        emit_tokens(o, b->caps[0], b->lens[0]);
    }
}

static void transcribe(CxArena *a, CxDiagList *diags, CxSpan span, MPat **pat,
                       int npat, MTrans **tr, int ntr, MEnv *env, int index,
                       Out *o, CxMacroMap *map) {
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
                int ci = index >= 0 ? index : 0;
                if (b && ci < b->ncaps && b->lens[ci] == 3 &&
                    b->caps[ci][1] && tok_is_punct(b->caps[ci][1], "::")) {
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
            MToken *bt = single_ident_cap(b, index);
            /* `struct $name` defines a generated type: let definition on the
             * type name jump back to the `$name` in the macro. */
            if (bt && i > 0 && tr[i - 1]->kind == T_TOKEN &&
                tr[i - 1]->text && !strcmp(tr[i - 1]->text, "struct"))
                macro_map_add(a, map, bt->text, t->span);
            /* `$name(params) { … }` is a generated function/constructor: map the
             * name so LSP definition jumps to the macro. */
            if (bt && i + 2 < ntr && tr[i + 1]->kind == T_GROUP &&
                tr[i + 1]->open == '(' && tr[i + 2]->kind == T_GROUP &&
                tr[i + 2]->open == '{')
                macro_map_add(a, map, bt->text, t->span);
            /* `$name::method` in a transcription is the public spelling of the
             * internal `Name__method` name. Emit the joined name and record the
             * `method` token so the LSP can jump to it in the macro body. */
            if (bt && i + 2 < ntr && tr[i + 1]->kind == T_TOKEN &&
                tr[i + 1]->text && !strcmp(tr[i + 1]->text, "::") &&
                tr[i + 2]->kind == T_TOKEN && tr[i + 2]->text &&
                is_ident_start(tr[i + 2]->text[0])) {
                char *name = cx_arena_printf(a, "%s__%s", bt->text,
                                             tr[i + 2]->text);
                macro_map_add(a, map, name, tr[i + 2]->span);
                out_text(o, name);
                i += 2;
                break;
            }
            transcribe_var(o, b, index);
            break;
        }
        case T_GROUP: {
            char open[2] = {t->open, '\0'};
            char close[2] = {t->close, '\0'};
            out_text(o, open);
            o->join = 1;
            transcribe(a, diags, span, pat, npat, t->kids, t->nkids, env, index,
                       o, map);
            out_text(o, close);
            break;
        }
        case T_REP: {
            int count = trans_count(t->kids, t->nkids, env);
            for (int k = 0; k < count; k++) {
                if (k > 0 && t->has_sep) {
                    char sep[2] = {t->sep, '\0'};
                    out_text(o, sep);
                }
                transcribe(a, diags, span, pat, npat, t->kids, t->nkids, env, k,
                           o, map);
            }
            break;
        }
        }
    }
    (void)a;
    (void)diags;
    (void)span;
}

/* ============================================================== definitions */

typedef struct {
    char **items;
    int    len;
    int    cap;
} NameVec;

static void name_push(CxArena *a, NameVec *v, char *name) {
    if (v->len == v->cap) {
        int cap = v->cap ? v->cap * 2 : 4;
        char **items = (char **)cx_arena_alloc(a, sizeof(char *) * (size_t)cap);
        if (!items) return;
        if (v->items && v->len)
            memcpy(items, v->items, sizeof(char *) * (size_t)v->len);
        v->items = items;
        v->cap = cap;
    }
    v->items[v->len++] = name;
}

/* Optional `requires $x: TraitA + TraitB, $y: TraitC` between the pattern group
 * and `=>`. The requirements are attached to metavariables and recorded on the
 * rule; at each invocation the concrete types bound to those metavariables are
 * recorded on the `CxMacroMap` so sema can check conformance. */
static int parse_requires(CxArena *a, CxDiagList *diags, MToken **toks, int n,
                          int *pi, MRule *rule) {
    int i = *pi;
    if (!(i < n && toks[i]->kind == MT_IDENT && toks[i]->text &&
          !strcmp(toks[i]->text, "requires")))
        return 1; /* no clause */

    MToken *kw = toks[i];
    i++;

    if (i < n && toks[i]->kind == MT_IDENT && toks[i]->text &&
        (!strcmp(toks[i]->text, "trait") || !strcmp(toks[i]->text, "traits"))) {
        cx_error(diags, tok_span(toks[i]),
                 "attach a requirement to a metavariable, e.g. "
                 "`requires $x: Trait`");
        return 0;
    }

    ReqVec reqs = {0};
    for (;;) {
        if (!(i + 2 < n && tok_is_punct(toks[i], "$") &&
              toks[i + 1]->kind == MT_IDENT && tok_is_punct(toks[i + 2], ":"))) {
            cx_error(diags, tok_span(i < n ? toks[i] : kw),
                     "expected `$metavariable: Trait` in requires clause");
            return 0;
        }
        MReq req;
        memset(&req, 0, sizeof req);
        req.var = toks[i + 1]->text;
        i += 3;

        if (i >= n || toks[i]->kind != MT_IDENT) {
            cx_error(diags, tok_span(i < n ? toks[i] : kw),
                     "expected a trait name after ':' in requires clause");
            return 0;
        }

        NameVec tr = {0};
        name_push(a, &tr, toks[i]->text);
        i++;
        while (i + 1 < n && tok_is_punct(toks[i], "+") &&
               toks[i + 1]->kind == MT_IDENT) {
            name_push(a, &tr, toks[i + 1]->text);
            i += 2;
        }
        req.traits = tr.items;
        req.ntraits = tr.len;
        req_push(a, &reqs, req);

        if (i < n && tok_is_punct(toks[i], ",")) {
            i++;
            if (i < n && tok_is_punct(toks[i], "=>")) break; /* trailing comma */
            continue;
        }
        break;
    }

    rule->reqs = reqs.items;
    rule->nreqs = reqs.len;
    *pi = i;
    return 1;
}

static int parse_macro_body(CxArena *a, CxDiagList *diags, Macro *mac,
                            MToken *body) {
    int i = 0;
    while (i < body->nkids) {
        while (i < body->nkids &&
               (tok_is_punct(body->kids[i], ";") ||
                tok_is_punct(body->kids[i], ",")))
            i++;
        if (i >= body->nkids) break;

        if (body->kids[i]->kind != MT_GROUP) {
            cx_error(diags, tok_span(body->kids[i]),
                     "macro '%s': expected a pattern group", mac->name);
            return 0;
        }
        MToken *patgrp = body->kids[i++];
        PatVec pat = parse_pat_group(a, diags, patgrp);

        MRule r;
        memset(&r, 0, sizeof r);
        r.pat = pat.items;
        r.npat = pat.len;

        if (!parse_requires(a, diags, body->kids, body->nkids, &i, &r))
            return 0;

        if (i >= body->nkids || !tok_is_punct(body->kids[i], "=>")) {
            cx_error(diags, tok_span(patgrp),
                     "macro '%s': expected '=>' after pattern", mac->name);
            return 0;
        }
        i++;

        if (i >= body->nkids || body->kids[i]->kind != MT_GROUP) {
            cx_error(diags, tok_span(patgrp),
                     "macro '%s': expected a transcription group", mac->name);
            return 0;
        }
        MToken *trgrp = body->kids[i++];

        TrVec tr = parse_tr_group(a, diags, trgrp);
        r.tr = tr.items;
        r.ntr = tr.len;

        for (int k = 0; k < r.nreqs; k++) {
            if (!pat_has_var(r.pat, r.npat, r.reqs[k].var))
                cx_error(diags, tok_span(patgrp),
                         "requires names unknown metavariable '$%s'",
                         r.reqs[k].var);
        }

        macro_add_rule(a, mac, r);
    }
    return mac->nrules > 0;
}

/* ================================================================== scanning */

typedef struct {
    size_t start;
    size_t end;
    char  *text; /* replacement, NULL means "remove" */
} Repl;

typedef struct {
    Repl *items;
    int   len;
    int   cap;
} ReplVec;

static void repl_push(CxArena *a, ReplVec *v, size_t start, size_t end,
                      char *text) {
    if (v->len == v->cap) {
        int cap = v->cap ? v->cap * 2 : 16;
        Repl *items = (Repl *)cx_arena_alloc(a, sizeof(Repl) * (size_t)cap);
        if (!items) return;
        if (v->items && v->len)
            memcpy(items, v->items, sizeof(Repl) * (size_t)v->len);
        v->items = items;
        v->cap = cap;
    }
    v->items[v->len].start = start;
    v->items[v->len].end = end;
    v->items[v->len].text = text;
    v->len++;
}

/* Replace a removed definition with the same number of newlines so subsequent
 * source lines keep their original line numbers. */
static char *blank_lines(CxArena *a, const char *src, size_t start, size_t end) {
    size_t len = strlen(src);
    int n = 0;
    for (size_t i = start; i < end && i < len; i++)
        if (src[i] == '\n') n++;
    if (n <= 0) return NULL;
    char *s = (char *)cx_arena_alloc(a, (size_t)n + 1);
    if (!s) return NULL;
    for (int i = 0; i < n; i++) s[i] = '\n';
    s[n] = '\0';
    return s;
}

static int is_macro_def_start(MToken **t, int i, int n) {
    return i + 3 < n && t[i]->kind == MT_IDENT &&
           t[i]->text && !strcmp(t[i]->text, "macro_rules") &&
           tok_is_punct(t[i + 1], "!") && t[i + 2]->kind == MT_IDENT &&
           t[i + 3]->kind == MT_GROUP && t[i + 3]->open == '{';
}

/* Collect all `macro_rules!` definitions (recursively, at any block scope). */
static void scan_defs(CxArena *a, CxDiagList *diags, const char *src,
                      MToken **toks, int n, MacroTable *tab, ReplVec *reps,
                      int *found) {
    for (int i = 0; i < n; i++) {
        if (is_macro_def_start(toks, i, n)) {
            MToken *name = toks[i + 2];
            MToken *body = toks[i + 3];
            CxSpan span = tok_span(toks[i]);
            Macro *mac = find_macro(tab, name->text);
            if (mac) {
                /* Redefinition: the newest rule set replaces the old one. */
                mac->nrules = 0;
                mac->rules = NULL;
                mac->cap = 0;
                mac->span = span;
            } else {
                mac = table_add(a, tab, name->text, span);
            }
            if (!mac) return;
            if (!parse_macro_body(a, diags, mac, body)) {
                cx_error(diags, span, "macro '%s' has no rules", name->text);
            }
            /* An attribute immediately before the definition belongs to it. */
            size_t start = toks[i]->start;
            for (int k = i - 1; k >= 0; k--) {
                if (toks[k]->kind == MT_OPAQUE && toks[k]->text &&
                    toks[k]->text[0] == '#' && toks[k]->text[1] == '[')
                    start = toks[k]->start;
                else
                    break;
            }
            /* Consume an optional trailing `;` so it does not become a stray
             * empty statement at file scope. */
            size_t end = body->end;
            int skip = 3;
            if (i + 4 < n && tok_is_punct(toks[i + 4], ";")) {
                end = toks[i + 4]->end;
                skip = 4;
            }
            repl_push(a, reps, start, end, blank_lines(a, src, start, end));
            *found = 1;
            i += skip;
            continue;
        }
        if (toks[i]->kind == MT_GROUP)
            scan_defs(a, diags, src, toks[i]->kids, toks[i]->nkids, tab, reps,
                      found);
    }
}

/* ============================================== macros across import boundaries */

typedef struct {
    char **items;
    int    len;
    int    cap;
} PathVec;

static void path_push(CxArena *a, PathVec *v, const char *s) {
    if (v->len == v->cap) {
        int cap = v->cap ? v->cap * 2 : 8;
        char **items = (char **)cx_arena_alloc(a, sizeof(char *) * (size_t)cap);
        if (!items) return;
        if (v->items && v->len)
            memcpy(items, v->items, sizeof(char *) * (size_t)v->len);
        v->items = items;
        v->cap = cap;
    }
    v->items[v->len++] = (char *)s;
}

/* Strip the surrounding quotes from a string token. */
static const char *str_unquote(CxArena *a, const char *s) {
    size_t n = s ? strlen(s) : 0;
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"')
        return cx_arena_strndup(a, s + 1, n - 2);
    return s;
}

/* Collect the paths of every `import "…"` in a token tree (recursively). */
static void find_imports(CxArena *a, MToken **toks, int n, PathVec *out) {
    for (int i = 0; i < n; i++) {
        MToken *t = toks[i];
        if (t->kind == MT_IDENT && t->text && !strcmp(t->text, "import") &&
            i + 1 < n && toks[i + 1]->kind == MT_STRING) {
            path_push(a, out, str_unquote(a, toks[i + 1]->text));
        }
        if (t->kind == MT_GROUP) find_imports(a, t->kids, t->nkids, out);
    }
}

/* Resolve `path` relative to `base_file` (a plain path or a `file://` URI). */
static void resolve_rel(const char *base_file, const char *path, char *out,
                        size_t n) {
    const char *bf = base_file ? base_file : "";
    int is_uri = strncmp(bf, "file://", 7) == 0;
    const char *dir_file = is_uri ? bf + 7 : bf;
    if (path[0] == '/') {
        snprintf(out, n, "%s", path);
        return;
    }
    const char *slash = strrchr(dir_file, '/');
    if (slash)
        snprintf(out, n, "%.*s/%s", (int)(slash - dir_file), dir_file, path);
    else
        snprintf(out, n, "%s", path);
}

/* Read a whole file into malloc'd storage (NUL-terminated), or NULL. */
static char *read_file(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    if (len_out) *len_out = got;
    return buf;
}

/* Collect `macro_rules!` definitions from `src` (when `collect_own`) and,
 * recursively, from the files it imports, into `tab`. This is what lets a `.hx`
 * export a macro to its importer. `depth` bounds import cycles. */
static void collect_imports_from(CxArena *a, CxDiagList *diags, const char *src,
                                 size_t len, const char *filename, MacroTable *tab,
                                 int depth, int collect_own) {
    if (!src || !filename || depth > 16) return;

    LexCtx lx;
    memset(&lx, 0, sizeof lx);
    lx.arena = a;
    lx.diags = diags;
    lx.src = src;
    lx.len = len;
    lx.line = 1;
    lx.col = 1;
    MTVec toks = {0};
    lex_tokens(&lx, '\0', &toks);

    if (collect_own) {
        ReplVec reps = {0};
        int found = 0;
        scan_defs(a, diags, src, toks.items, toks.len, tab, &reps, &found);
    }

    PathVec paths = {0};
    find_imports(a, toks.items, toks.len, &paths);
    for (int i = 0; i < paths.len; i++) {
        char resolved[1024];
        resolve_rel(filename, paths.items[i], resolved, sizeof resolved);
        size_t n = 0;
        char *buf = read_file(resolved, &n);
        if (!buf) continue;
        collect_imports_from(a, diags, buf, n, resolved, tab, depth + 1, 1);
        free(buf);
    }
}

static char *expand_invocation(CxArena *a, CxDiagList *diags, Macro *mac,
                               MToken *args, CxSpan span, CxMacroMap *map) {
    for (int r = 0; r < mac->nrules; r++) {
        MRule *rule = &mac->rules[r];
        MEnv env = {0};
        int pos = 0;
        if (match_seq(a, diags, rule->pat, rule->npat, args->kids, args->nkids,
                      &pos, &env) &&
            pos == args->nkids) {
            /* Record the invocation itself so "go to definition" on its name
             * lands in the `macro_rules!` definition. */
            if (map) macro_invoke_add(a, map, mac->name, span, mac->span);
            /* Checked `requires`: record the concrete type each constrained
             * metavariable was bound to, so the semantic pass can verify that
             * the type implements the required trait(s). */
            if (map) {
                for (int q = 0; q < rule->nreqs; q++) {
                    MReq *rq = &rule->reqs[q];
                    MBind *b = env_get(&env, rq->var);
                    if (!b) continue;
                    const char *frag =
                        pat_find_frag(rule->pat, rule->npat, rq->var);
                    for (int ci = 0; ci < b->ncaps; ci++) {
                        CxBuf tb;
                        cx_buf_init(&tb);
                        render_tokens(&tb, b->caps[ci], b->lens[ci]);
                        for (int t = 0; t < rq->ntraits; t++)
                            macro_req_add(a, map, tb.data ? tb.data : "",
                                          rq->traits[t], frag, span);
                        cx_buf_free(&tb);
                    }
                }
            }
            CxBuf buf;
            cx_buf_init(&buf);
            Out out = {&buf, 0};
            transcribe(a, diags, span, rule->pat, rule->npat, rule->tr,
                       rule->ntr, &env, -1, &out, map);
            char *result = cx_arena_strdup(a, buf.data ? buf.data : "");
            cx_buf_free(&buf);
            return result;
        }
    }
    cx_error(diags, span, "no matching rule for macro '%s'", mac->name);
    return cx_arena_strdup(a, "");
}

/* Heuristic: does the `{ … }` group at `idx` introduce item declarations
 * (namespace/struct/trait/implements) rather than statements? Used only to
 * decide whether the `;` after an item-position macro invocation belongs to
 * the invocation (declarations already carry their own `;`). */
static int group_item_scope(MToken **toks, int idx) {
    for (int j = idx - 1; j >= 0; j--) {
        MToken *t = toks[j];
        if (t->kind == MT_GROUP && t->close == '}') break;
        if (t->kind == MT_PUNCT &&
            (tok_is_punct(t, ";") || tok_is_punct(t, "}") ||
             tok_is_punct(t, "{")))
            break;
        if (t->kind == MT_IDENT &&
            (!strcmp(t->text, "namespace") || !strcmp(t->text, "struct") ||
             !strcmp(t->text, "trait") || !strcmp(t->text, "implements")))
            return 1;
        if (tok_is_punct(t, ")")) return 0;
    }
    return 0;
}

static void scan_invocations(CxArena *a, CxDiagList *diags, MToken **toks,
                             int n, MacroTable *tab, ReplVec *reps, int *found,
                             int item_scope, CxMacroMap *map) {
    for (int i = 0; i < n; i++) {
        if (is_macro_def_start(toks, i, n)) {
            i += 3; /* skip the definition body */
            continue;
        }
        if (toks[i]->kind == MT_IDENT && i + 2 < n &&
            tok_is_punct(toks[i + 1], "!") && toks[i + 2]->kind == MT_GROUP) {
            Macro *mac = find_macro(tab, toks[i]->text);
            if (mac) {
                MToken *args = toks[i + 2];
                char *text = expand_invocation(a, diags, mac, args,
                                               tok_span_full(toks[i]), map);
                size_t end = args->end;
                int skip = 2;
                /* At item scope a bare `name!(…);` invocation owns its
                 * trailing semicolon: the expansion supplies complete
                 * declarations. In expression position the `;` is the
                 * statement terminator and must stay. */
                int at_item_start =
                    item_scope &&
                    (i == 0 || tok_is_punct(toks[i - 1], ";") ||
                     tok_is_punct(toks[i - 1], "}") ||
                     (toks[i - 1]->kind == MT_GROUP &&
                      toks[i - 1]->close == '}'));
                if (at_item_start && i + 3 < n &&
                    tok_is_punct(toks[i + 3], ";")) {
                    end = toks[i + 3]->end;
                    skip = 3;
                }
                repl_push(a, reps, toks[i]->start, end, text);
                *found = 1;
                i += skip;
                continue;
            }
        }
        if (toks[i]->kind == MT_GROUP) {
            int child_item = 0;
            if (toks[i]->open == '{')
                child_item = group_item_scope(toks, i);
            scan_invocations(a, diags, toks[i]->kids, toks[i]->nkids, tab, reps,
                             found, child_item, map);
        }
    }
}

/* ================================================================ assembling */

static int repl_cmp(const void *pa, const void *pb) {
    const Repl *a = (const Repl *)pa;
    const Repl *b = (const Repl *)pb;
    if (a->start < b->start) return -1;
    if (a->start > b->start) return 1;
    return 0;
}

static void buf_append(CxBuf *b, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) cx_buf_putc(b, s[i]);
}

static char *apply_repls(CxArena *a, const char *src, size_t len,
                         ReplVec *reps) {
    if (reps->len > 1)
        qsort(reps->items, (size_t)reps->len, sizeof(Repl), repl_cmp);

    CxBuf out;
    cx_buf_init(&out);
    size_t pos = 0;
    for (int i = 0; i < reps->len; i++) {
        Repl *r = &reps->items[i];
        if (r->start < pos) continue; /* overlapping replacement */
        if (r->start > len) break;
        if (r->start > pos) {
            buf_append(&out, src + pos, r->start - pos);
            pos = r->start;
        }
        if (r->text) cx_buf_puts(&out, r->text);
        pos = r->end > pos ? r->end : pos;
    }
    if (pos < len) buf_append(&out, src + pos, len - pos);

    char *result = cx_arena_strdup(a, out.data ? out.data : "");
    cx_buf_free(&out);
    return result;
}

/* ------------------------------------------------------------------ API */

char *cx_macro_expand(CxArena *arena, const char *src, size_t len,
                      const char *filename, CxDiagList *diags,
                      CxMacroMap *map) {
    if (!src) return cx_arena_strdup(arena, "");

    MacroTable table = {0};
    /* An imported `.hx` may export `macro_rules!`: collect definitions from the
     * files this one imports before expanding. The current file's own
     * definitions are collected by the loop below (so they take precedence). */
    collect_imports_from(arena, diags, src, len, filename, &table, 0, 0);

    char *cur = cx_arena_strndup(arena, src, len);

    for (int pass = 0; pass < 64; pass++) {
        size_t cur_len = strlen(cur);

        LexCtx lx;
        memset(&lx, 0, sizeof lx);
        lx.arena = arena;
        lx.diags = diags;
        lx.src = cur;
        lx.len = cur_len;
        lx.line = 1;
        lx.col = 1;

        MTVec toks = {0};
        lex_tokens(&lx, '\0', &toks);

        ReplVec reps = {0};
        int found = 0;

        scan_defs(arena, diags, cur, toks.items, toks.len, &table, &reps,
                  &found);
        scan_invocations(arena, diags, toks.items, toks.len, &table, &reps,
                         &found, 1, map);

        if (!found) return cur;

        char *next = apply_repls(arena, cur, cur_len, &reps);
        if (!next || strcmp(next, cur) == 0) return next ? next : cur;
        cur = next;
    }

    cx_error(diags, (CxSpan){1, 1, 1, 1},
             "macro expansion exceeded the maximum depth");
    return cur;
}
