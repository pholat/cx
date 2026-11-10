/* Cx `macro_rules!` — tokenizer: source text -> `MToken` token tree.
 *
 * The token tree preserves group nesting and, for every leaf, the byte range
 * and (line, column) in the source buffer, which the expander needs to splice
 * replacements and the LSP needs for position queries. */
#include "cx/cx.h"
#include "macro_internal.h"

#include <stdlib.h>
#include <string.h>

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

int is_ident_start(char c) {
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

void lex_tokens(LexCtx *l, char stop, MTVec *out) {
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

/* ---------------------------------------------------------- token helpers */

int tok_is_punct(const MToken *t, const char *s) {
    return t && t->kind == MT_PUNCT && t->text && strcmp(t->text, s) == 0;
}

int tok_is(const MToken *t, const char *s) {
    return t && t->text && strcmp(t->text, s) == 0;
}

CxSpan tok_span(const MToken *t) {
    CxSpan s = {t->line, t->col, t->line, t->col};
    return s;
}

/* Like `tok_span` but spanning the whole token, so position queries (the LSP's
 * "is the cursor on this invocation?") match any column of the identifier. */
CxSpan tok_span_full(const MToken *t) {
    int n = (int)(t->end > t->start ? t->end - t->start : 1);
    CxSpan s = {t->line, t->col, t->line, t->col + (n > 1 ? n - 1 : 0)};
    return s;
}
