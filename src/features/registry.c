/* Cx feature registry.
 *
 * The single place that lists the feature modules; both dispatch helpers walk
 * it in order and stop at the first hook that claims the node. Adding a feature
 * is a new directory under `src/features/<name>/` plus one line here. */
#include "feature.h"

#include "enum/enum_feature.h"
#include "fsm/fsm_feature.h"
#include "str/str_feature.h"
#include "cast/cast_feature.h"

static const CxFeature *const features[] = {
    &cx_feature_enum, &cx_feature_fsm, &cx_feature_str, &cx_feature_cast, NULL
};

int cx_feature_collect(BuildCtx *c, CxNode *node, const char *ns,
                       const char *file) {
    for (int i = 0; features[i]; i++)
        if (features[i]->collect && features[i]->collect(c, node, ns, file))
            return 1;
    return 0;
}

int cx_feature_emit(Emit *e, CxNode *node, const char *ns, int phase) {
    for (int i = 0; features[i]; i++)
        if (features[i]->emit && features[i]->emit(e, node, ns, phase))
            return 1;
    return 0;
}
