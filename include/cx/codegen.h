#ifndef CX_CODEGEN_H
#define CX_CODEGEN_H

#include "cx/ast.h"
#include "cx/buf.h"
#include "cx/symbols.h"

/* Mangle `name` inside the namespace path `ns` (NULL/"" for global scope).
 * "io::net" + "Socket" -> "io__net__Socket". */
char *cx_mangle(CxArena *arena, const char *ns, const char *name);

/* Emit a `#line` directive so downstream compiler errors map back to source. */
void cx_emit_line(CxBuf *buf, int line, const char *file);

/* Emit a complete C translation unit for `tu`. */
void cx_emit_c(CxArena *arena, CxBuf *buf, CxNode *tu,
               const CxSymbolTable *sema, const char *source_name);

#endif /* CX_CODEGEN_H */
