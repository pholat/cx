#ifndef CX_PARSER_H
#define CX_PARSER_H

#include <stddef.h>

#include "cx/ast.h"
#include "cx/buf.h"
#include "cx/diag.h"

/* Identifiers known to be type names. The lexer emits TYPE_NAME for these so a
 * declarator `T *x` stays unambiguous from the expression `a * b` (the classic
 * C "typedef-name" lexer hack). Seeded with built-ins and extended whenever a
 * struct is declared. Names are owned elsewhere (string literals or the arena);
 * only the backing array is owned by the set. */
typedef struct {
    const char **items;
    int len;
    int cap;
} CxTypeSet;

int  cx_typeset_has(const CxTypeSet *ts, const char *name);
void cx_typeset_add(CxTypeSet *ts, const char *name);
void cx_typeset_free(CxTypeSet *ts);

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
    CxBuf       prelude;   /* verbatim preprocessor lines (#include, #define) */
    CxBuf       raw;       /* in-progress `raw { … }` capture (balanced braces) */
    int         raw_depth; /* open-brace depth while capturing a raw block */
    int         raw_line;  /* start of the current raw block (for its span) */
    int         raw_col;
    CxBuf       macro_args;/* in-progress `name!( … )` argument capture */
    char       *macro_name;/* identifier of the current macro invocation */
    int         macro_depth;/* open-paren depth while capturing macro args */
    int         macro_line;/* start of the current macro invocation */
    int         macro_col;
    CxTypeSet   types;     /* known type names for the lexer */
    CxVec       imports;   /* imported TU roots (declarations to merge) */
    const char **active;   /* import paths currently in progress (cycle guard) */
    int         active_len;
    int         active_cap;
} CxParseCtx;

/* Parse `path` (relative to the current file) and record it as an import. Called
 * from the grammar when an `import "…";` is reduced. */
void cx_import(CxParseCtx *ctx, const char *path);

/* Parse `len` bytes of `src` into a fresh AST. Syntax errors are reported via
 * `diags`; a best-effort partial tree is still returned through the start rule.
 * Returns NULL only on allocation failure. */
CxNode *cx_parse(CxArena *arena, const char *src, size_t len,
                 const char *filename, CxDiagList *diags);

#endif /* CX_PARSER_H */
