#ifndef CX_STR_FEATURE_H
#define CX_STR_FEATURE_H

#include "../feature.h"

/* `#[str]`: `const char *Name__c_str(v)` name helpers for enums and machines. */
void emit_enum_str_proto(Emit *e, CxNode *n, const char *ns);
void emit_enum_str_def(Emit *e, CxNode *n, const char *ns);
void emit_fsm_cstr_proto(Emit *e, CxNode *n);
void emit_fsm_cstr_def(Emit *e, CxNode *n);

extern const CxFeature cx_feature_str;

#endif /* CX_STR_FEATURE_H */
