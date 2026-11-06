#ifndef CX_PARSER_H
#define CX_PARSER_H

#include <stddef.h>

#include "cx/ast.h"
#include "cx/diag.h"

/* Rides along on the Flex scanner (`extra`) and is also passed to Bison as a
 * parse parameter, so actions and `yyerror` can reach the arena and diags. */
typedef struct CxParseCtx {
    CxArena    *arena;
    CxDiagList *diags;
    const char *filename;
    CxNode     *result;
    int         line;      /* current 1-based lexer line */
    int         column;    /* current 1-based lexer column */
    CxSpan      last_span; /* last token location */
} CxParseCtx;

/* Parse `len` bytes of `src` into a fresh AST. Syntax errors are reported via
 * `diags`; a best-effort partial tree is still returned through the start rule.
 * Returns NULL only on allocation failure. */
CxNode *cx_parse(CxArena *arena, const char *src, size_t len,
                 const char *filename, CxDiagList *diags);

#endif /* CX_PARSER_H */
