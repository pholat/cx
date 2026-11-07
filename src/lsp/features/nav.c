#include "nav.h"

#include <string.h>

static int is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

int cx_lsp_word_at(const char *text, int line, int col, char *buf, size_t n) {
    if (!text || line < 1 || col < 1) return 0;

    const char *p = text;
    int ln = 1;
    while (*p && ln < line) {
        if (*p == '\n') ln++;
        p++;
    }
    if (ln != line) return 0;
    const char *line_start = p;
    const char *line_end = p;
    while (*line_end && *line_end != '\n') line_end++;

    const char *at = line_start + (col - 1);
    if (at < line_start) at = line_start;
    if (at > line_end) at = line_end;

    const char *s = at;
    if (s >= line_end || !is_ident_char(*s)) {
        if (at > line_start && is_ident_char(*(at - 1))) s = at - 1;
        else return 0;
    }
    const char *e = s;
    while (s > line_start && is_ident_char(*(s - 1))) s--;
    while (e < line_end && is_ident_char(*e)) e++;

    size_t len = (size_t)(e - s);
    if (len == 0 || len >= n) return 0;
    memcpy(buf, s, len);
    buf[len] = '\0';
    return 1;
}

int cx_lsp_prefix_at(const char *text, int line, int col, char *buf, size_t n) {
    if (buf && n) buf[0] = '\0';
    if (!text || line < 1 || col < 1 || !buf || n == 0) return 0;

    const char *p = text;
    int ln = 1;
    while (*p && ln < line) {
        if (*p == '\n') ln++;
        p++;
    }
    if (ln != line) return 0;
    const char *line_start = p;
    const char *line_end = p;
    while (*line_end && *line_end != '\n') line_end++;

    const char *at = line_start + (col - 1);
    if (at < line_start) at = line_start;
    if (at > line_end) at = line_end;

    const char *s = at;
    while (s > line_start && is_ident_char(*(s - 1))) s--;

    size_t len = (size_t)(at - s);
    if (len >= n) len = n - 1;
    memcpy(buf, s, len);
    buf[len] = '\0';
    return (int)len;
}

/* See nav.h. Splits a `Qualifier::member` pair around the cursor. */
int cx_lsp_scope_at(const char *text, int line, int col, char *qual,
                    size_t qn, char *member, size_t mn) {
    if (qual && qn) qual[0] = '\0';
    if (member && mn) member[0] = '\0';
    if (!text || line < 1 || col < 1 || !qual || !qn || !member || !mn)
        return 0;

    const char *p = text;
    int ln = 1;
    while (*p && ln < line) {
        if (*p == '\n') ln++;
        p++;
    }
    if (ln != line) return 0;
    const char *ls = p;
    const char *le = p;
    while (*le && *le != '\n') le++;

    const char *at = ls + (col - 1);
    if (at < ls) at = ls;
    if (at > le) at = le;

    /* member: identifier covering (or ending just before) the cursor. */
    const char *ms = at;
    if ((ms >= le || !is_ident_char(*ms)) && at > ls && is_ident_char(*(at - 1)))
        ms = at - 1;
    const char *me = ms;
    while (ms > ls && is_ident_char(*(ms - 1))) ms--;
    while (me < le && is_ident_char(*me)) me++;
    size_t mlen = (size_t)(me - ms);
    if (mlen >= mn) mlen = mn - 1;
    memcpy(member, ms, mlen);
    member[mlen] = '\0';

    /* qualifier: optional spaces, `::`, optional spaces, an identifier. */
    const char *q = ms;
    while (q > ls && (q[-1] == ' ' || q[-1] == '\t')) q--;
    if (q - ls < 2 || q[-1] != ':' || q[-2] != ':') return 0;
    q -= 2;
    while (q > ls && (q[-1] == ' ' || q[-1] == '\t')) q--;
    const char *qe = q;
    while (q > ls && is_ident_char(*(q - 1))) q--;
    if (qe == q) return 0;
    size_t qlen = (size_t)(qe - q);
    if (qlen >= qn) qlen = qn - 1;
    memcpy(qual, q, qlen);
    qual[qlen] = '\0';
    return 1;
}

static int span_contains(CxSpan s, int line, int col) {
    if (s.first_line <= 0) return 0;
    if (line < s.first_line || line > s.last_line) return 0;
    if (line == s.first_line && col < s.first_column) return 0;
    if (line == s.last_line && col > s.last_column) return 0;
    return 1;
}

/* Innermost named node containing the position. The tree is walked
 * unconditionally (a parent's span may be narrower than its children's — e.g.
 * a declarator whose initializer extends past the name); only nodes whose own
 * span actually contains the position are candidates, so a too-small ancestor
 * span never hides a matching descendant. */
static CxNode *search(CxNode *n, int line, int col) {
    if (!n) return NULL;

    CxNode *kids[8] = {n->lhs, n->rhs, n->init, n->body,
                       n->cond, n->then, n->els, n->incr};
    for (int i = 0; i < 8; i++) {
        CxNode *r = search(kids[i], line, col);
        if (r) return r;
    }
    for (int i = 0; i < n->children.len; i++) {
        CxNode *r = search(n->children.items[i], line, col);
        if (r) return r;
    }
    for (int i = 0; i < n->params.len; i++) {
        CxNode *r = search(n->params.items[i], line, col);
        if (r) return r;
    }
    if (!span_contains(n->span, line, col)) return NULL;
    return (n->name && n->name[0]) ? n : NULL;
}

CxNode *cx_lsp_node_at(CxNode *root, int line, int column) {
    return search(root, line, column);
}

void cx_lsp_write_range(CxBuf *out, CxSpan s) {
    int sl = s.first_line > 0 ? s.first_line - 1 : 0;
    int sc = s.first_column > 0 ? s.first_column - 1 : 0;
    int el = s.last_line > 0 ? s.last_line - 1 : sl;
    int ec = s.last_column > 0 ? s.last_column : sc + 1;
    if (el == sl && ec <= sc) ec = sc + 1;
    cx_buf_printf(out,
                  "{\"start\":{\"line\":%d,\"character\":%d},"
                  "\"end\":{\"line\":%d,\"character\":%d}}",
                  sl, sc, el, ec);
}
