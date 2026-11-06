#include "cx/codegen.h"

/* Emit a `#line` directive so downstream compiler errors and debugger
 * backtraces point at the original .cx/.hx file rather than the generated C. */
void cx_emit_line(CxBuf *buf, int line, const char *file) {
    if (line > 0 && file)
        cx_buf_printf(buf, "#line %d \"%s\"\n", line, file);
}
