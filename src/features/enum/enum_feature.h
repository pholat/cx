#ifndef CX_ENUM_FEATURE_H
#define CX_ENUM_FEATURE_H

#include "../feature.h"

/* `typedef enum <tag> { … } <tag>;` */
void emit_enum_def(Emit *e, CxNode *n, const char *ns);

extern const CxFeature cx_feature_enum;

#endif /* CX_ENUM_FEATURE_H */
