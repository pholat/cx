#ifndef CX_FSM_FEATURE_H
#define CX_FSM_FEATURE_H

#include "../feature.h"

#define FSM_MAX 16

typedef struct {
    const char *src;   /* state name, or "*"/"any"/"current" */
    const char *dst;
    const char *event; /* NULL: unconditional */
    const char *guards[FSM_MAX];
    int         nguards;
    const char *actions[FSM_MAX];
    int         nactions;
} FsmTrans;

typedef struct {
    const char **names;
    int          len;
    int          cap;
} FsmNames;

typedef struct {
    CxArena  *arena;
    FsmNames  states;
    FsmNames  events;
    FsmNames  guards;
    FsmNames  actions;
    FsmTrans *trans;
    int       ntrans;
    int       tcap;
} Fsm;

/* Parse the captured `sm` body into `f` (sub-state blocks are flattened). */
void fsm_parse(CxArena *a, const char *body, Fsm *f);

/* The machine's `<M>__State`/`<M>__Event` enums, struct/typedefs and API. */
void emit_fsm_def(Emit *e, CxNode *n, const char *ns);
void emit_fsm_protos(Emit *e, CxNode *n, const char *ns);
void emit_fsm_funcs(Emit *e, CxNode *n, const char *ns);

extern const CxFeature cx_feature_fsm;

#endif /* CX_FSM_FEATURE_H */
