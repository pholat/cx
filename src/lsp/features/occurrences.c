#include "cx/cx.h"

#include <string.h>

#include "../json.h"
#include "../lsp_internal.h"
#include "nav.h"

/* textDocument/documentHighlight, textDocument/references and
 * textDocument/rename, all same-file. They share one scanner: collect whole-word
 * occurrences of the identifier under the cursor, skipping line and block
 * comments and string / char literals. It is a lexical match (no binding
 * analysis), which fits Cx's unmangled identifiers. */

static int occ_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int occ_ident_char(char c) {
    return occ_ident_start(c) || (c >= '0' && c <= '9');
}

/* Append comma-separated `{…}` items for whole-word occurrences of the
 * identifier under the cursor. `mode` picks the item shape:
 *   0 = documentHighlight  -> {range, kind}
 *   1 = references         -> {uri, range}
 *   2 = rename             -> {range, newText}
 * Returns the occurrence count. */
static int occurrences_body(CxBuf *body, const char *uri, const char *text,
                            size_t len, int line, int character, int mode,
                            const char *new_name) {
    char word[128];
    if (!cx_lsp_word_at(text, line + 1, character + 1, word, sizeof word))
        return 0;
    size_t wl = strlen(word);

    int count = 0;
    int l = 1, c = 1;
    size_t i = 0;
    while (i < len) {
        char ch = text[i];
        if (ch == '/' && i + 1 < len && text[i + 1] == '/') {
            while (i < len && text[i] != '\n') {
                i++;
                c++;
            }
            continue;
        }
        if (ch == '/' && i + 1 < len && text[i + 1] == '*') {
            i += 2;
            c += 2;
            while (i < len &&
                   !(text[i] == '*' && i + 1 < len && text[i + 1] == '/')) {
                if (text[i] == '\n') {
                    l++;
                    c = 1;
                } else {
                    c++;
                }
                i++;
            }
            if (i < len) {
                i += 2;
                c += 2;
            }
            continue;
        }
        if (ch == '"' || ch == '\'') {
            char q = ch;
            i++;
            c++;
            while (i < len && text[i] != q) {
                if (text[i] == '\\' && i + 1 < len) {
                    i += 2;
                    c += 2;
                    continue;
                }
                if (text[i] == '\n') {
                    l++;
                    c = 1;
                } else {
                    c++;
                }
                i++;
            }
            if (i < len) {
                i++;
                c++;
            }
            continue;
        }
        if (occ_ident_start(ch)) {
            size_t s = i;
            int sl = l, sc = c;
            while (i < len && occ_ident_char(text[i])) {
                i++;
                c++;
            }
            size_t n = i - s;
            if (n == wl && strncmp(text + s, word, n) == 0) {
                CxSpan sp;
                sp.first_line = sl;
                sp.first_column = sc;
                sp.last_line = sl;
                sp.last_column = sc + (int)n - 1;
                if (count) cx_buf_putc(body, ',');
                cx_buf_putc(body, '{');
                if (mode == 1) {
                    cx_buf_puts(body, "\"uri\":");
                    cx_json_write_string(body, uri);
                    cx_buf_puts(body, ",\"range\":");
                } else {
                    cx_buf_puts(body, "\"range\":");
                }
                cx_lsp_write_range(body, sp);
                if (mode == 0) {
                    cx_buf_puts(body, ",\"kind\":1");
                } else if (mode == 2) {
                    cx_buf_puts(body, ",\"newText\":");
                    cx_json_write_string(body, new_name ? new_name : "");
                }
                cx_buf_putc(body, '}');
                count++;
            }
            continue;
        }
        if (ch == '\n') {
            l++;
            c = 1;
            i++;
            continue;
        }
        i++;
        c++;
    }
    return count;
}

/* Wrap `body` in an array and put it in `out`. */
static void put_array(CxBuf *out, const CxBuf *body) {
    cx_buf_putc(out, '[');
    if (body->len) cx_buf_puts(out, body->data);
    cx_buf_putc(out, ']');
}

int cx_lsp_document_highlight(CxBuf *out, const char *uri, const char *text,
                              size_t len, int line, int character) {
    CxBuf body;
    cx_buf_init(&body);
    occurrences_body(&body, uri, text, len, line, character, 0, NULL);
    put_array(out, &body);
    cx_buf_free(&body);
    return 0;
}

int cx_lsp_references(CxBuf *out, const char *uri, const char *text, size_t len,
                      int line, int character) {
    CxBuf body;
    cx_buf_init(&body);
    occurrences_body(&body, uri, text, len, line, character, 1, NULL);
    put_array(out, &body);
    cx_buf_free(&body);
    return 0;
}

int cx_lsp_rename(CxBuf *out, const char *uri, const char *text, size_t len,
                  int line, int character, const char *new_name) {
    char word[128];
    if (!new_name || !new_name[0] ||
        !cx_lsp_word_at(text, line + 1, character + 1, word, sizeof word)) {
        cx_buf_puts(out, "null");
        return 0;
    }
    CxBuf body;
    cx_buf_init(&body);
    int count =
        occurrences_body(&body, uri, text, len, line, character, 2, new_name);
    if (count == 0) {
        cx_buf_puts(out, "null");
        cx_buf_free(&body);
        return 0;
    }
    /* A WorkspaceEdit with a single-file `changes` map. */
    cx_buf_puts(out, "{\"changes\":{");
    cx_json_write_string(out, uri);
    cx_buf_puts(out, ":[");
    if (body.len) cx_buf_puts(out, body.data);
    cx_buf_puts(out, "]}}");
    cx_buf_free(&body);
    return count;
}
