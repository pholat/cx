#ifndef CX_EMITTER_INTERNAL_H
#define CX_EMITTER_INTERNAL_H

/* Private interface shared by the `src/codegen/` translation units.
 *
 * The five-phase `walk` (in c_emitter.c) drives a set of per-node emitters that
 * share one `Emit` context:
 *
 *   emit_types.c    typedefs, struct/ctor/dtor definitions and prototypes
 *   emit_expr.c     types, declarators, attributes and expressions
 *   emit_stmt.c     statements, blocks and local/global declarations
 *   emit_traits.c   `implements` methods, vtables and trait installation
 *
 * Feature syntaxes (enums, `sm`, `#[str]`, `cast`) live in `src/features/` and
 * plug into the phase walk through `cx_feature_emit` (see `feature.h`).
 *
 * Only the public surface in `cx/codegen.h` is visible outside this directory. */

#include "cx/ast.h"
#include "cx/buf.h"
#include "cx/symbols.h"

typedef struct {
    CxArena             *arena;
    CxBuf               *out;
    const CxSymbolTable *sema;
    const char          *file;
    int                  indent;
    int                  header;      /* declaration-only (.hx) output */
    int                  line_map;    /* `#line` before every statement (cx -g) */
    const CxNode        *self_struct; /* set while emitting a destructor body */
} Emit;

/* --------------------------------------------------------------- emit_expr.c */

void       ind(Emit *e);
CxSymbol  *struct_of(Emit *e, const char *type, int *is_pointer);
char      *mangled_struct(Emit *e, const char *type);
void       emit_type(Emit *e, const char *type);
void       emit_declarator(Emit *e, const char *type, const CxNode *d);
void       emit_decl(Emit *e, CxNode *n);
void       emit_raw(Emit *e, const char *text);
void       emit_macro_call(Emit *e, CxNode *n);
void       emit_attr_leading(Emit *e, CxNode *n);
void       emit_attr_trailing(Emit *e, CxNode *n);
void       emit_expr(Emit *e, CxNode *n);

/* --------------------------------------------------------------- emit_stmt.c */

void emit_var_decl(Emit *e, CxNode *n, int is_local);
void emit_stmt(Emit *e, CxNode *n);
void emit_block(Emit *e, CxNode *body);

/* -------------------------------------------------------------- emit_types.c */

void        emit_function_proto(Emit *e, CxNode *n, const char *ns);
void        emit_struct_pass(Emit *e, CxNode *s, const char *ns, int phase);
const char *child_ns(Emit *e, const char *ns, CxNode *child);

/* ------------------------------------------------------------- emit_traits.c */

int  struct_has_implements(const CxNode *s);
void emit_vtable_struct(Emit *e, CxNode *trait, const char *ns);
void emit_impl_protos(Emit *e, CxNode *s, const char *ns);
void emit_impl_defs(Emit *e, CxNode *s, const char *ns);
void emit_trait_installs_self(Emit *e, CxNode *s, const char *ns);
void emit_trait_installs_var(Emit *e, CxNode *s, const char *ns,
                             const char *var);
void emit_trait_global_init(Emit *e, CxNode *s, const char *ns);

#endif /* CX_EMITTER_INTERNAL_H */
