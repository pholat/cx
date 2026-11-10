#ifndef CX_CAST_PARSER_H
#define CX_CAST_PARSER_H

#include "cx/ast.h"
#include "cx/parser.h"

/* Parser hook for the `cast` feature (called from `grammar/parser.y`): build a
 * `CX_NODE_EXPR_CAST` for both `(T)expr` and `cast[T](expr)`. */
CxNode *cx_cast_expr(CxParseCtx *ctx, const char *type, CxNode *expr,
                     CxSpan span);

#endif /* CX_CAST_PARSER_H */
