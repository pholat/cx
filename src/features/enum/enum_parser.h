#ifndef CX_ENUM_PARSER_H
#define CX_ENUM_PARSER_H

#include "cx/ast.h"
#include "cx/parser.h"

/* Parser hooks for the `enum` feature (called from `grammar/parser.y`). The
 * mid-rule `cx_typeset_add` that registers the enum name before its body stays
 * in the grammar; these build the nodes. */
CxNode *cx_enum_decl(CxParseCtx *ctx, const char *name, CxVec members,
                     CxSpan span);
CxNode *cx_enum_str_decl(CxParseCtx *ctx, const char *machine, const char *which,
                         CxSpan span);

#endif /* CX_ENUM_PARSER_H */
