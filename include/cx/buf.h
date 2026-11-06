#ifndef CX_BUF_H
#define CX_BUF_H

#include <stddef.h>

/* Growable text buffer used by the code generator. */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} CxBuf;

void  cx_buf_init(CxBuf *buf);
void  cx_buf_free(CxBuf *buf);
void  cx_buf_putc(CxBuf *buf, char c);
void  cx_buf_puts(CxBuf *buf, const char *s);
void  cx_buf_printf(CxBuf *buf, const char *fmt, ...);

/* Transfers ownership of the NUL-terminated contents to the caller. */
char *cx_buf_take(CxBuf *buf);

#endif /* CX_BUF_H */
