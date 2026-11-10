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

/* Debug-info polish: when `on` (the `cx build -g` flag), the emitter also
 * places a `#line` directive before every statement, so compiler errors and GDB
 * frames inside a body map to the exact Cx line rather than the enclosing
 * declaration. Off by default (the output stays declaration-level). This is a
 * process-global codegen option. */
void cx_codegen_set_line_map(int on);

/* Emit a complete C translation unit for `tu`. */
void cx_emit_c(CxArena *arena, CxBuf *buf, CxNode *tu,
               const CxSymbolTable *sema, const char *source_name);

/* Emit a declaration-only header (include guard + typedefs + struct definitions
 * + prototypes). */
void cx_emit_h(CxArena *arena, CxBuf *buf, CxNode *tu,
               const CxSymbolTable *sema, const char *source_name);

#endif /* CX_CODEGEN_H */
