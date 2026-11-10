#ifndef CX_FEATURE_H
#define CX_FEATURE_H

/* Cx feature modules.
 *
 * A *feature* is one piece of Cx-introduced syntax — an enum, an `sm` state
 * machine, the `#[str]` name helpers, `cast[T]` — that touches more than one
 * layer of the compiler. Instead of scattering a feature across the grammar,
 * the sema passes and the emitter, each lives under `src/features/<name>/` and
 * plugs into the shared tree through this small interface:
 *
 *   collect   add the feature's symbols to the semantic table (sema pass)
 *   emit      emit the feature's code for one node in a codegen phase
 *
 * (The lexer/parser hook — registering the feature's type names in the lexer
 * type set — is driven by the grammar's mid-rule actions; see §6 of the
 * roadmap for grouping those per feature.)
 *
 * Both dispatchers walk the registry in declaration order and stop at the first
 * hook that claims the node, so a feature only has to recognize its own nodes. */

#include "../codegen/c_emitter.h"
#include "../sema/sema_internal.h"

typedef struct CxFeature {
    const char *name;
    /* Add symbols for `node` to `c->table`; return 1 when it handled `node`. */
    int (*collect)(BuildCtx *c, CxNode *node, const char *ns, const char *file);
    /* Emit `node` in codegen `phase`; return 1 when it handled `node`. */
    int (*emit)(Emit *e, CxNode *node, const char *ns, int phase);
} CxFeature;

/* Run every feature's `collect` hook for `node` (first match wins). */
int cx_feature_collect(BuildCtx *c, CxNode *node, const char *ns,
                       const char *file);

/* Run every feature's `emit` hook for `node` (first match wins). */
int cx_feature_emit(Emit *e, CxNode *node, const char *ns, int phase);

#endif /* CX_FEATURE_H */
