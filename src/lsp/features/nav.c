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

static int span_contains(CxSpan s, int line, int col) {
    if (s.first_line <= 0) return 0;
    if (line < s.first_line || line > s.last_line) return 0;
    if (line == s.first_line && col < s.first_column) return 0;
    if (line == s.last_line && col > s.last_column) return 0;
    return 1;
}

static CxNode *search(CxNode *n, int line, int col) {
    if (!n || !span_contains(n->span, line, col)) return NULL;

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
