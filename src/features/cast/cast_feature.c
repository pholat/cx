/* Cx feature: `cast[T](expr)`.
 *
 * A parser-only sugar that builds the existing `CX_NODE_EXPR_CAST`; there is no
 * semantic pass, and codegen is an ordinary C cast. */
#include "cx/cx.h"
#include "cast_feature.h"
#include "cast_parser.h"

/* -------------------------------------------------------------- parser hook */

/* Both `(T)expr` and `cast[T](expr)` build the same `CX_NODE_EXPR_CAST`. */
CxNode *cx_cast_expr(CxParseCtx *ctx, const char *type, CxNode *expr,
                     CxSpan span) {
    CxNode *n = cx_node_new(ctx->arena, CX_NODE_EXPR_CAST, span);
    n->type = (char *)type;
    n->lhs = expr;
    return n;
}

void cx_cast_emit(Emit *e, CxNode *n) {
    cx_buf_putc(e->out, '(');
    emit_type(e, n->type);
    cx_buf_putc(e->out, ')');
    emit_expr(e, n->lhs);
}

/* `cast` has no walk-phase emission (it is an expression); the emitter calls
 * `cx_cast_emit` directly from `emit_expr`. */
const CxFeature cx_feature_cast = {"cast", NULL, NULL};
