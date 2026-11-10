#ifndef CX_CAST_FEATURE_H
#define CX_CAST_FEATURE_H

#include "../feature.h"

/* `cast[T](expr)` — the parser sugar that builds `CX_NODE_EXPR_CAST`; codegen
 * emits the ordinary C cast `(T)expr`. */
void cx_cast_emit(Emit *e, CxNode *n);

extern const CxFeature cx_feature_cast;

#endif /* CX_CAST_FEATURE_H */
