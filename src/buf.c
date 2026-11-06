#include "cx/buf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cx_buf_init(CxBuf *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void cx_buf_free(CxBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void buf_reserve(CxBuf *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->len + extra + 1) cap *= 2;
    char *p = (char *)realloc(b->data, cap);
    if (!p) return;
    b->data = p;
    b->cap = cap;
}

void cx_buf_putc(CxBuf *b, char c) {
    buf_reserve(b, 1);
    if (!b->data) return;
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
}

void cx_buf_puts(CxBuf *b, const char *s) {
    if (!s) return;
    size_t n = strlen(s);
    buf_reserve(b, n);
    if (!b->data) return;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void cx_buf_printf(CxBuf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(ap2);
        return;
    }
    buf_reserve(b, (size_t)n);
    if (b->data) {
        vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap2);
        b->len += (size_t)n;
    }
    va_end(ap2);
}

char *cx_buf_take(CxBuf *b) {
    char *p = b->data;
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    return p;
}
