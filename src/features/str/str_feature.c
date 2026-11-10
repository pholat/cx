/* Cx feature: `#[str]` name helpers.
 *
 * `#[str] enum E { … }` adds `const char *E__c_str(E v)` (a switch over the
 * enumerators), and `#[str] enum Machine::State;` / `#[str] enum Machine::Event;`
 * adds the equivalent helper for a state machine's generated enums. */
#include "cx/cx.h"
#include "str_feature.h"
#include "../fsm/fsm_feature.h"

#include <string.h>

/* The first attribute named `name` on `n`, or NULL. */
static CxNode *find_attr(const CxNode *n, const char *name) {
    if (!n || !name) return NULL;
    for (int i = 0; i < n->attrs.len; i++)
        if (n->attrs.items[i]->name && !strcmp(n->attrs.items[i]->name, name))
            return n->attrs.items[i];
    return NULL;
}

/* `#[str] enum E { … }` generates `const char *E__c_str(E v)`, a switch that
 * returns each enumerator's name with a `""` fallback. */
void emit_enum_str_proto(Emit *e, CxNode *n, const char *ns) {
    if (!find_attr(n, "str")) return;
    char *q = cx_mangle(e->arena, ns, n->name);
    ind(e);
    cx_buf_printf(e->out, "const char *%s__c_str(%s v);\n", n->name, q);
}

void emit_enum_str_def(Emit *e, CxNode *n, const char *ns) {
    if (!find_attr(n, "str")) return;
    char *q = cx_mangle(e->arena, ns, n->name);
    cx_emit_line(e->out, n->span.first_line, e->file);
    ind(e);
    cx_buf_printf(e->out, "const char *%s__c_str(%s v) {\n", n->name, q);
    e->indent++;
    ind(e);
    cx_buf_puts(e->out, "switch (v) {\n");
    e->indent++;
    for (int i = 0; i < n->children.len; i++) {
        CxNode *m = n->children.items[i];
        ind(e);
        cx_buf_printf(e->out, "case %s__%s: return \"%s\";\n", n->name, m->name,
                      m->name);
    }
    ind(e);
    cx_buf_puts(e->out, "default: return \"\";\n");
    e->indent--;
    ind(e);
    cx_buf_puts(e->out, "}\n");
    e->indent--;
    ind(e);
    cx_buf_puts(e->out, "}\n\n");
}

/* `<M>__State__c_str` / `<M>__Event__c_str` — generated on demand by an
 * explicit `#[str] enum Machine::State;` marker, so the enum-name helpers are
 * opt-in and emitted where the user asked (after the machine declaration). */
void emit_fsm_cstr_proto(Emit *e, CxNode *n) {
    ind(e);
    cx_buf_printf(e->out, "const char *%s__%s__c_str(%s__%s v);\n", n->name,
                  n->type, n->name, n->type);
}

void emit_fsm_cstr_def(Emit *e, CxNode *n) {
    CxSymbol *fs = cx_sema_find_fsm(e->sema, n->name);
    if (!fs || !fs->decl) return;
    Fsm f;
    fsm_parse(e->arena, fs->decl->type, &f);
    const char *m = n->name;
    int is_state = n->type && strcmp(n->type, "State") == 0;

    ind(e);
    cx_buf_printf(e->out, "const char *%s__%s__c_str(%s__%s v) {\n", m, n->type,
                  m, n->type);
    e->indent++;
    ind(e);
    cx_buf_puts(e->out, "switch (v) {\n");
    e->indent++;
    if (is_state) {
        ind(e);
        cx_buf_printf(e->out, "case %s__state__entry: return \"entry\";\n", m);
        for (int i = 0; i < f.states.len; i++) {
            ind(e);
            cx_buf_printf(e->out, "case %s__state__%s: return \"%s\";\n", m,
                          f.states.names[i], f.states.names[i]);
        }
    } else {
        for (int i = 0; i < f.events.len; i++) {
            ind(e);
            cx_buf_printf(e->out, "case %s__event__%s: return \"%s\";\n", m,
                          f.events.names[i], f.events.names[i]);
        }
    }
    ind(e);
    cx_buf_puts(e->out, "default: return \"\";\n");
    e->indent--;
    ind(e);
    cx_buf_puts(e->out, "}\n");
    e->indent--;
    ind(e);
    cx_buf_puts(e->out, "}\n\n");
}

/* Codegen: the enum prototype is phase 2, the definition phase 4; the machine
 * helpers share the `CX_NODE_ENUM_STR` marker node. */
static int str_emit(Emit *e, CxNode *n, const char *ns, int phase) {
    if (n->kind == CX_NODE_ENUM) {
        if (phase == 2) { emit_enum_str_proto(e, n, ns); return 1; }
        if (phase == 4) { emit_enum_str_def(e, n, ns); return 1; }
    } else if (n->kind == CX_NODE_ENUM_STR) {
        if (phase == 2) { emit_fsm_cstr_proto(e, n); return 1; }
        if (phase == 4) { emit_fsm_cstr_def(e, n); return 1; }
    }
    return 0;
}

const CxFeature cx_feature_str = {"#[str]", NULL, str_emit};
