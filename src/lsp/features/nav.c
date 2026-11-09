#include "nav.h"

#include <stdlib.h>
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

/* See nav.h. Splits a `recv.member` / `recv->member` access around the cursor. */
int cx_lsp_dot_at(const char *text, int line, int col, char *recv, size_t rn,
                  char *member, size_t mn) {
    if (recv && rn) recv[0] = '\0';
    if (member && mn) member[0] = '\0';
    if (!text || line < 1 || col < 1 || !recv || !rn || !member || !mn)
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

    /* separator: optional spaces, `.` or `->`. */
    const char *q = ms;
    while (q > ls && (q[-1] == ' ' || q[-1] == '\t')) q--;
    if (q - ls >= 2 && q[-1] == '>' && q[-2] == '-') q -= 2;
    else if (q - ls >= 1 && q[-1] == '.') q -= 1;
    else return 0;
    while (q > ls && (q[-1] == ' ' || q[-1] == '\t')) q--;

    /* receiver: an identifier. */
    const char *qe = q;
    while (q > ls && is_ident_char(*(q - 1))) q--;
    if (qe == q) return 0;
    size_t rlen = (size_t)(qe - q);
    if (rlen >= rn) rlen = rn - 1;
    memcpy(recv, q, rlen);
    recv[rlen] = '\0';
    return 1;
}

void cx_lsp_base_type(const char *type, char *buf, size_t n) {
    if (!buf || n == 0) return;
    size_t i = 0;
    while (type && type[i] && type[i] != ' ' && type[i] != '*' &&
           type[i] != '[' && i + 1 < n) {
        buf[i] = type[i];
        i++;
    }
    buf[i] = '\0';
}

/* Map a byte offset in `text` to a 1-based (line, column). */
static void offset_to_linecol(const char *text, const char *at, int *line,
                              int *col) {
    int l = 1, c = 1;
    for (const char *p = text; p < at && *p; p++) {
        if (*p == '\n') {
            l++;
            c = 1;
        } else {
            c++;
        }
    }
    if (line) *line = l;
    if (col) *col = c;
}

/* See nav.h. Finds the call argument list around the cursor. */
int cx_lsp_call_open_at(const char *text, int line, int col, int *open_line,
                        int *open_col, int *arg_index) {
    if (arg_index) *arg_index = 0;
    if (!text || line < 1 || col < 1) return 0;

    const char *p = text;
    int ln = 1;
    while (*p && ln < line) {
        if (*p == '\n') ln++;
        p++;
    }
    if (ln != line) return 0;
    const char *at = p;
    for (int i = 1; i < col && *at && *at != '\n'; i++) at++;

    /* Forward scan, keeping the stack of open `(` offsets (skipping strings and
     * comments so a paren inside a literal does not fool us). */
    const char *stack[128];
    int sp = 0;
    for (const char *q = text; q < at;) {
        char c = *q;
        if (c == '"' || c == '\'') {
            char qc = c;
            q++;
            while (*q && *q != qc) {
                if (*q == '\\' && q[1]) q++;
                q++;
            }
            if (*q == qc) q++;
            continue;
        }
        if (c == '/' && q[1] == '/') {
            while (*q && *q != '\n') q++;
            continue;
        }
        if (c == '/' && q[1] == '*') {
            q += 2;
            while (*q && !(*q == '*' && q[1] == '/')) q++;
            if (*q) q += 2;
            continue;
        }
        if (c == '(') {
            if (sp < 128) stack[sp++] = q;
        } else if (c == ')') {
            if (sp > 0) sp--;
        }
        q++;
    }
    if (sp == 0) return 0;
    const char *open = stack[sp - 1];

    /* Count top-level commas between the `(` and the cursor. */
    int arg = 0, d = 0;
    for (const char *q = open + 1; q < at;) {
        char c = *q;
        if (c == '"' || c == '\'') {
            char qc = c;
            q++;
            while (*q && *q != qc) {
                if (*q == '\\' && q[1]) q++;
                q++;
            }
            if (*q == qc) q++;
            continue;
        }
        if (c == '(' || c == '[' || c == '{') d++;
        else if (c == ')' || c == ']' || c == '}') {
            if (d > 0) d--;
        } else if (c == ',' && d == 0) arg++;
        q++;
    }
    if (arg_index) *arg_index = arg;
    offset_to_linecol(text, open, open_line, open_col);
    return 1;
}

/* Copy `text` with the cursor's line completed: everything before the cursor is
 * kept, the rest of the line is replaced by a placeholder member access (`__cx;`)
 * and the newline (and all following lines) preserved. This makes an incomplete
 * expression at the cursor parseable, so the enclosing function still reduces and
 * declarations before the cursor are visible. Returns malloc'd text or NULL. */
static char *repair_at_cursor(const char *text, int line, int col) {
    const char *p = text;
    int ln = 1;
    while (*p && ln < line) {
        if (*p == '\n') ln++;
        p++;
    }
    if (ln != line) return NULL;
    const char *ls = p;
    const char *le = p;
    while (*le && *le != '\n') le++;

    const char *at = ls + (col - 1);
    if (at < ls) at = ls;
    if (at > le) at = le;

    static const char suffix[] = "__cx;";
    size_t pre = (size_t)(at - text);
    size_t mid = sizeof suffix - 1;
    size_t post = strlen(le);
    char *out = (char *)malloc(pre + mid + post + 1);
    if (!out) return NULL;
    memcpy(out, text, pre);
    memcpy(out + pre, suffix, mid);
    memcpy(out + pre + mid, le, post + 1);
    return out;
}

/* See nav.h. */
const char *cx_lsp_receiver_base(CxArena *arena, CxNode *tu, CxSymbolTable *sema,
                                 const char *text, const char *uri, int line,
                                 int col, const char *name, char *buf, size_t n) {
    if (!buf || n == 0 || !name || !name[0]) return NULL;
    buf[0] = '\0';

    const char *t =
        tu ? cx_sema_receiver_type(arena, tu, line, col, name) : NULL;
    if (!t && sema) {
        CxSymbol *g = cx_sema_find_symbol(sema, name);
        if (g && g->type) t = g->type;
    }
    if (t) {
        cx_lsp_base_type(t, buf, n);
        return buf;
    }

    /* The cursor is likely in an incomplete expression, so the enclosing
     * function is not in the AST: reparse a repaired copy. */
    if (!text || !uri || !arena) return NULL;
    char *fixed = repair_at_cursor(text, line, col);
    if (!fixed) return NULL;
    CxDiagList diags;
    cx_diaglist_init(&diags, uri);
    CxNode *tu2 = cx_parse(arena, fixed, strlen(fixed), uri, &diags);
    CxSymbolTable *sema2 = tu2 ? cx_sema_build(arena, tu2, &diags) : NULL;
    const char *t2 =
        tu2 ? cx_sema_receiver_type(arena, tu2, line, col, name) : NULL;
    if (!t2 && sema2) {
        CxSymbol *g = cx_sema_find_symbol(sema2, name);
        if (g && g->type) t2 = g->type;
    }
    int ok = t2 != NULL;
    if (ok) cx_lsp_base_type(t2, buf, n);
    cx_diaglist_free(&diags);
    free(fixed);
    return ok ? buf : NULL;
}

/* Start of the 1-based numbered `line` in `text`. */
static const char *line_start_at(const char *text, int line) {
    const char *p = text;
    for (int ln = 1; *p && ln < line; ln++) {
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return p;
}

/* Find the call node named `name` (recursively). */
static CxNode *nav_find_call(CxNode *n, const char *name) {
    if (!n) return NULL;
    if (n->kind == CX_NODE_EXPR_CALL && n->name && !strcmp(n->name, name))
        return n;
    CxNode *kids[8] = {n->lhs, n->rhs,  n->init, n->body,
                       n->cond, n->then, n->els, n->incr};
    for (int i = 0; i < 8; i++) {
        CxNode *r = nav_find_call(kids[i], name);
        if (r) return r;
    }
    for (int i = 0; i < n->children.len; i++) {
        CxNode *r = nav_find_call(n->children.items[i], name);
        if (r) return r;
    }
    for (int i = 0; i < n->params.len; i++) {
        CxNode *r = nav_find_call(n->params.items[i], name);
        if (r) return r;
    }
    return NULL;
}

/* See nav.h. Types a computed receiver by parsing `recv.__cx_comp();`. */
const char *cx_lsp_computed_receiver_base(CxArena *arena, const char *text,
                                          const char *uri, int line, int column,
                                          int prefix_len, char *buf, size_t n) {
    if (!arena || !text || !buf || n == 0) return NULL;
    buf[0] = '\0';
    if (line < 1 || column < 1) return NULL;

    const char *ls = line_start_at(text, line);
    const char *le = ls;
    while (*le && *le != '\n') le++;
    const char *at = ls + (column - 1);
    if (at < ls) at = ls;
    if (at > le) at = le;
    const char *ps = at - prefix_len; /* start of the typed member prefix */
    if (ps < ls) ps = ls;

    /* Only when the member follows a `.` or `->`. */
    const char *sep = ps;
    while (sep > ls && (sep[-1] == ' ' || sep[-1] == '\t')) sep--;
    int dot = (sep - ls >= 1 && sep[-1] == '.');
    int arrow = (sep - ls >= 2 && sep[-1] == '>' && sep[-2] == '-');
    if (!dot && !arrow) return NULL;

    static const char marker[] = "__cx_comp();";
    size_t pre = (size_t)(ps - text);
    size_t mid = sizeof marker - 1;
    size_t post = strlen(le);
    char *fixed = (char *)malloc(pre + mid + post + 1);
    if (!fixed) return NULL;
    memcpy(fixed, text, pre);
    memcpy(fixed + pre, marker, mid);
    memcpy(fixed + pre + mid, le, post + 1);

    CxDiagList diags;
    cx_diaglist_init(&diags, uri ? uri : "<input>");
    CxNode *tu = cx_parse(arena, fixed, strlen(fixed), uri, &diags);
    CxSymbolTable *sema = tu ? cx_sema_build(arena, tu, &diags) : NULL;
    const char *res = NULL;
    if (sema) {
        CxNode *call = nav_find_call(tu, "__cx_comp");
        if (call && call->lhs) {
            const char *t = cx_sema_expr_type(arena, sema, call->lhs);
            if (t) {
                cx_lsp_base_type(t, buf, n);
                res = buf;
            }
        }
    }
    cx_diaglist_free(&diags);
    free(fixed);
    return res;
}

static int is_comment_line(const char *ls) {
    while (*ls == ' ' || *ls == '\t') ls++;
    return ls[0] == '/' && ls[1] == '/';
}

/* See nav.h. Collects the run of `//` lines directly above `line`. */
int cx_lsp_doc_above(const char *text, int line, char *buf, size_t n) {
    if (buf && n) buf[0] = '\0';
    if (!text || line <= 1) return 0;

    int j = line - 1;
    while (j >= 1 && is_comment_line(line_start_at(text, j))) j--;
    int first = j + 1;
    if (first >= line) return 0;

    for (int k = first; k < line; k++) {
        const char *ls = line_start_at(text, k);
        while (*ls == ' ' || *ls == '\t') ls++;
        if (ls[0] == '/' && ls[1] == '/') ls += 2;
        if (*ls == ' ') ls++;
        const char *le = ls;
        while (*le && *le != '\n') le++;
        if (buf) {
            size_t len = strlen(buf);
            for (const char *c = ls; c < le && len + 1 < n; c++) buf[len++] = *c;
            buf[len] = '\0';
            if (k + 1 < line && len + 1 < n) {
                buf[len++] = '\n';
                buf[len] = '\0';
            }
        }
    }
    return first;
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
