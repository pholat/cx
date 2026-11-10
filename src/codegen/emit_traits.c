/* Cx code generation — `implements` methods, static vtables and trait install. */
#include "cx/cx.h"
#include "c_emitter.h"

#include <string.h>

static int is_receiver(const CxNode *p) {
    return p && p->name && strcmp(p->name, "self") == 0;
}

int struct_has_implements(const CxNode *s) {
    for (int i = 0; s && i < s->children.len; i++)
        if (s->children.items[i]->kind == CX_NODE_IMPLEMENTS) return 1;
    return 0;
}

/* `struct <Trait>__vtable { ret (*m)(params); … };` — one static table type per
 * trait, shared by every implementing struct. */
void emit_vtable_struct(Emit *e, CxNode *trait, const char *ns) {
    char *tm = cx_mangle(e->arena, ns, trait->name);
    cx_emit_line(e->out, trait->span.first_line, e->file);
    ind(e);
    cx_buf_printf(e->out, "struct %s__vtable {\n", tm);
    e->indent++;
    for (int i = 0; i < trait->children.len; i++) {
        CxNode *m = trait->children.items[i];
        if (m->kind != CX_NODE_TRAIT_METHOD) continue;
        ind(e);
        emit_type(e, m->type);
        cx_buf_printf(e->out, " (*%s)(", m->name);
        if (m->params.len == 0) {
            cx_buf_puts(e->out, "void");
        } else {
            for (int k = 0; k < m->params.len; k++) {
                CxNode *p = m->params.items[k];
                if (k) cx_buf_puts(e->out, ", ");
                if (is_receiver(p)) cx_buf_puts(e->out, "void *");
                else emit_type(e, p->type);
            }
        }
        cx_buf_puts(e->out, ");\n");
    }
    e->indent--;
    ind(e);
    cx_buf_puts(e->out, "};\n\n");
}

/* Signature of `Struct__Trait__method`; the receiver becomes `void *self_`. */
static void emit_impl_sig(Emit *e, CxNode *s, const char *ns,
                          const char *trait_name, CxNode *m, int proto) {
    char *sm = cx_mangle(e->arena, ns, s->name);
    cx_buf_puts(e->out, "static ");
    emit_type(e, m->type);
    cx_buf_printf(e->out, " %s__%s__%s(", sm, trait_name, m->name);
    if (m->params.len == 0) {
        cx_buf_puts(e->out, "void");
    } else {
        for (int i = 0; i < m->params.len; i++) {
            CxNode *p = m->params.items[i];
            if (i) cx_buf_puts(e->out, ", ");
            if (is_receiver(p)) {
                cx_buf_puts(e->out, "void *self_");
            } else {
                emit_type(e, p->type);
                cx_buf_putc(e->out, ' ');
                cx_buf_puts(e->out, p->name ? p->name : "");
            }
        }
    }
    cx_buf_putc(e->out, ')');
    cx_buf_puts(e->out, proto ? ";\n" : "\n");
}

static void emit_impl_method(Emit *e, CxNode *s, const char *ns,
                             const char *trait_name, CxNode *m) {
    cx_emit_line(e->out, m->span.first_line, e->file);
    ind(e);
    emit_impl_sig(e, s, ns, trait_name, m, 0);

    const CxNode *saved = e->self_struct;
    e->self_struct = s;
    cx_buf_puts(e->out, "{\n");
    e->indent++;
    if (m->params.len > 0 && is_receiver(m->params.items[0])) {
        char *sm = cx_mangle(e->arena, ns, s->name);
        ind(e);
        cx_buf_printf(e->out, "struct %s *self = self_; (void)self;\n", sm);
    }
    for (int i = 0; m->body && i < m->body->children.len; i++)
        emit_stmt(e, m->body->children.items[i]);
    e->indent--;
    ind(e);
    cx_buf_puts(e->out, "}\n\n");
    e->self_struct = saved;
}

/* `static const struct <Trait>__vtable <Struct>__<Trait>__table = { … };` */
static void emit_vtable(Emit *e, CxNode *s, const char *ns, CxSymbol *trait) {
    char *sm = cx_mangle(e->arena, ns, s->name);
    char *tm = cx_mangle(e->arena, trait->ns, trait->name);
    cx_buf_printf(e->out, "static const struct %s__vtable %s__%s__table = {\n",
                  tm, sm, trait->name);
    e->indent++;
    CxNode *decl = trait->decl;
    for (int i = 0; decl && i < decl->children.len; i++) {
        CxNode *m = decl->children.items[i];
        if (m->kind != CX_NODE_TRAIT_METHOD) continue;
        ind(e);
        cx_buf_printf(e->out, "%s__%s__%s,\n", sm, trait->name, m->name);
    }
    e->indent--;
    ind(e);
    cx_buf_puts(e->out, "};\n\n");
}

void emit_impl_protos(Emit *e, CxNode *s, const char *ns) {
    for (int i = 0; i < s->children.len; i++) {
        CxNode *imp = s->children.items[i];
        if (imp->kind != CX_NODE_IMPLEMENTS) continue;
        for (int k = 0; k < imp->children.len; k++) {
            CxNode *m = imp->children.items[k];
            if (m->kind != CX_NODE_IMPL_METHOD) continue;
            ind(e);
            emit_impl_sig(e, s, ns, imp->name, m, 1);
        }
    }
}

void emit_impl_defs(Emit *e, CxNode *s, const char *ns) {
    for (int i = 0; i < s->children.len; i++) {
        CxNode *imp = s->children.items[i];
        if (imp->kind != CX_NODE_IMPLEMENTS) continue;
        for (int k = 0; k < imp->children.len; k++) {
            CxNode *m = imp->children.items[k];
            if (m->kind != CX_NODE_IMPL_METHOD) continue;
            emit_impl_method(e, s, ns, imp->name, m);
        }
        CxSymbol *tr = cx_sema_find_trait(e->sema, imp->name);
        if (tr) emit_vtable(e, s, ns, tr);
    }
}

/* `self->Trait = &Struct__Trait__table;` (used inside constructors). */
void emit_trait_installs_self(Emit *e, CxNode *s, const char *ns) {
    for (int i = 0; i < s->children.len; i++) {
        CxNode *m = s->children.items[i];
        if (m->kind != CX_NODE_IMPLEMENTS) continue;
        CxSymbol *tr = cx_sema_find_trait(e->sema, m->name);
        if (!tr) continue;
        char *sm = cx_mangle(e->arena, ns, s->name);
        ind(e);
        cx_buf_printf(e->out, "self->%s = &%s__%s__table;\n", tr->name, sm,
                      tr->name);
    }
}

/* `var.Trait = &Struct__Trait__table;` after a default local declaration. */
void emit_trait_installs_var(Emit *e, CxNode *s, const char *ns,
                             const char *var) {
    for (int i = 0; i < s->children.len; i++) {
        CxNode *m = s->children.items[i];
        if (m->kind != CX_NODE_IMPLEMENTS) continue;
        CxSymbol *tr = cx_sema_find_trait(e->sema, m->name);
        if (!tr) continue;
        char *sm = cx_mangle(e->arena, ns, s->name);
        ind(e);
        cx_buf_printf(e->out, "%s.%s = &%s__%s__table;\n", var, tr->name, sm,
                      tr->name);
    }
}

/* `= { .Trait = &Struct__Trait__table, … }` for a global default declaration. */
void emit_trait_global_init(Emit *e, CxNode *s, const char *ns) {
    cx_buf_puts(e->out, " = {");
    for (int i = 0; i < s->children.len; i++) {
        CxNode *m = s->children.items[i];
        if (m->kind != CX_NODE_IMPLEMENTS) continue;
        CxSymbol *tr = cx_sema_find_trait(e->sema, m->name);
        if (!tr) continue;
        char *sm = cx_mangle(e->arena, ns, s->name);
        cx_buf_printf(e->out, " .%s = &%s__%s__table,", tr->name, sm, tr->name);
    }
    cx_buf_puts(e->out, " }");
}
