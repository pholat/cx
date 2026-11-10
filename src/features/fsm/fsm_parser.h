#ifndef CX_FSM_PARSER_H
#define CX_FSM_PARSER_H

#include "cx/ast.h"
#include "cx/parser.h"

/* Parser hook for the `sm` feature (called from `grammar/parser.y`): register
 * the machine name as a type and build the `CX_NODE_FSM` node. */
CxNode *cx_fsm_decl(CxParseCtx *ctx, const char *name, const char *body,
                    CxSpan span);

#endif /* CX_FSM_PARSER_H */
