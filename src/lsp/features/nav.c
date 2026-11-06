#include "nav.h"

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
