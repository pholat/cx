/* Cx code generation — function prototypes and struct/ctor/dtor definitions. */
#include "cx/cx.h"
#include "c_emitter.h"

#include <string.h>

static CxNode *find_member(const CxNode *s, CxNodeKind kind) {
    if (!s) return NULL;
    for (int i = 0; i < s->children.len; i++)
        if (s->children.items[i]->kind == kind) return s->children.items[i];
    return NULL;
}

/* The emitted C name for a function: its `#[export("…")]` name if any, else the
 * namespace-mangled name. */
static const char *fn_c_name(Emit *e, const char *ns, const char *name) {
    CxSymbol *fn = cx_sema_find_function(e->sema, name);
    if (fn) {
        if (fn->export_name) return fn->export_name;
        return cx_mangle(e->arena, fn->ns, fn->name);
    }
    return cx_mangle(e->arena, ns, name);
}

void emit_function_proto(Emit *e, CxNode *n, const char *ns) {
    ind(e);
    emit_attr_leading(e, n); /* `#[inline]`, `#[noreturn]` */
    emit_type(e, n->type);
    cx_buf_putc(e->out, ' ');
    cx_buf_puts(e->out, fn_c_name(e, ns, n->name));
    cx_buf_putc(e->out, '(');
    if (n->params.len == 0) {
        cx_buf_puts(e->out, "void");
    } else {
        for (int i = 0; i < n->params.len; i++) {
            if (i) cx_buf_puts(e->out, ", ");
            emit_decl(e, n->params.items[i]);
        }
    }
    cx_buf_putc(e->out, ')');
    emit_attr_trailing(e, n);
}

static void emit_struct_def(Emit *e, CxNode *n, const char *ns) {
    char *q = cx_mangle(e->arena, ns, n->name);
    ind(e);
    cx_buf_printf(e->out, "struct %s {\n", q);
    e->indent++;
    for (int i = 0; i < n->children.len; i++) {
        CxNode *m = n->children.items[i];
        if (m->kind == CX_NODE_RAW) {
            emit_raw(e, m->name); /* verbatim member (e.g. a macro-typed field) */
            continue;
        }
        if (m->kind != CX_NODE_VAR_DECL) continue;
        for (int k = 0; k < m->children.len; k++) {
            ind(e);
            emit_declarator(e, m->type, m->children.items[k]);
            emit_attr_trailing(e, m);
            cx_buf_puts(e->out, ";\n");
        }
    }
    /* One vtable pointer per implemented trait. */
    for (int i = 0; i < n->children.len; i++) {
        CxNode *m = n->children.items[i];
        if (m->kind != CX_NODE_IMPLEMENTS) continue;
        CxSymbol *tr = cx_sema_find_trait(e->sema, m->name);
        if (!tr) continue;
        char *tm = cx_mangle(e->arena, tr->ns, tr->name);
        ind(e);
        cx_buf_printf(e->out, "const struct %s__vtable *%s;\n", tm, tr->name);
    }
    e->indent--;
    ind(e);
    cx_buf_puts(e->out, "}");
    emit_attr_trailing(e, n); /* `#[packed] struct T { … }` */
    cx_buf_puts(e->out, ";\n");
}

/* Emit `T__ctor(&self->field);` for each embedded field whose struct type
 * declares a zero-argument constructor (constructed in declaration order). */
static void emit_member_ctor_calls(Emit *e, CxNode *s) {
    for (int i = 0; i < s->children.len; i++) {
        CxNode *m = s->children.items[i];
        if (m->kind != CX_NODE_VAR_DECL) continue;
        int ptr = 0;
        CxSymbol *st = struct_of(e, m->type, &ptr);
        if (!st || ptr || !st->ctor || st->ctor->params.len != 0) continue;
        for (int k = 0; k < m->children.len; k++) {
            ind(e);
            cx_buf_printf(e->out, "%s__ctor(&self->%s);\n",
                          cx_mangle(e->arena, st->ns, st->name),
                          m->children.items[k]->name);
        }
    }
}

/* Emit `T__dtor(&self->field);` for each embedded field whose struct type has a
 * destructor, in reverse declaration order. */
static void emit_member_dtor_calls(Emit *e, CxNode *s) {
    for (int i = s->children.len - 1; i >= 0; i--) {
        CxNode *m = s->children.items[i];
        if (m->kind != CX_NODE_VAR_DECL) continue;
        int ptr = 0;
        CxSymbol *st = struct_of(e, m->type, &ptr);
        if (!st || ptr || !st->dtor) continue;
        for (int k = m->children.len - 1; k >= 0; k--) {
            ind(e);
            cx_buf_printf(e->out, "%s__dtor(&self->%s);\n",
                          cx_mangle(e->arena, st->ns, st->name),
                          m->children.items[k]->name);
        }
    }
}

static void emit_dtor_helper(Emit *e, CxNode *s, const char *ns) {
    CxNode *dtor = find_member(s, CX_NODE_DESTRUCTOR);
    /* A body-less `~T();` is a declaration only: emit no definition (the paired
     * `.cx` provides it). */
    if (!dtor || !dtor->body) return;

    char *q = cx_mangle(e->arena, ns, s->name);
    cx_emit_line(e->out, dtor->span.first_line, e->file);
    ind(e);
    cx_buf_printf(e->out, "void %s__dtor(struct %s *self)\n", q, q);

    const CxNode *saved = e->self_struct;
    e->self_struct = s;
    cx_buf_puts(e->out, "{\n");
    e->indent++;
    for (int i = 0; dtor->body && i < dtor->body->children.len; i++)
        emit_stmt(e, dtor->body->children.items[i]);
    emit_member_dtor_calls(e, s); /* members are destroyed after the body */
    e->indent--;
    ind(e);
    cx_buf_puts(e->out, "}\n\n");
    e->self_struct = saved;
}

/* Prototype + definition of `void Struct__ctor(struct Struct *self, params)`. */
static void emit_ctor_signature(Emit *e, CxNode *ctor, const char *ns,
                                CxNode *s, int proto) {
    char *q = cx_mangle(e->arena, ns, s->name);
    cx_emit_line(e->out, ctor->span.first_line, e->file);
    ind(e);
    cx_buf_printf(e->out, "void %s__ctor(struct %s *self", q, q);
    for (int i = 0; i < ctor->params.len; i++) {
        CxNode *p = ctor->params.items[i];
        cx_buf_puts(e->out, ", ");
        emit_type(e, p->type);
        cx_buf_putc(e->out, ' ');
        cx_buf_puts(e->out, p->name);
    }
    cx_buf_puts(e->out, proto ? ");\n" : ")\n");
}

static void emit_ctor_proto(Emit *e, CxNode *s, const char *ns) {
    CxNode *ctor = find_member(s, CX_NODE_CONSTRUCTOR);
    if (ctor) emit_ctor_signature(e, ctor, ns, s, 1);
}

/* Destructor forward declaration, so an enclosing destructor may call an
 * embedded member's destructor before its definition. */
static void emit_dtor_proto(Emit *e, CxNode *s, const char *ns) {
    if (!find_member(s, CX_NODE_DESTRUCTOR)) return;
    char *q = cx_mangle(e->arena, ns, s->name);
    ind(e);
    cx_buf_printf(e->out, "void %s__dtor(struct %s *self);\n", q, q);
}

static void emit_ctor_helper(Emit *e, CxNode *s, const char *ns) {
    CxNode *ctor = find_member(s, CX_NODE_CONSTRUCTOR);
    if (!ctor || !ctor->body) return; /* declaration only (no body to emit) */

    emit_ctor_signature(e, ctor, ns, s, 0); /* ends with ")\n" */

    const CxNode *saved = e->self_struct;
    e->self_struct = s;
    cx_buf_puts(e->out, "{\n");
    e->indent++;
    emit_member_ctor_calls(e, s); /* members are constructed before the body */
    emit_trait_installs_self(e, s, ns); /* install trait vtables on `self` */
    for (int i = 0; ctor->body && i < ctor->body->children.len; i++)
        emit_stmt(e, ctor->body->children.items[i]);
    e->indent--;
    ind(e);
    cx_buf_puts(e->out, "}\n\n");
    e->self_struct = saved;
}

const char *child_ns(Emit *e, const char *ns, CxNode *child) {
    if (!ns || !*ns) return cx_arena_strdup(e->arena, child->name);
    return cx_arena_printf(e->arena, "%s::%s", ns, child->name);
}

/* Emit a struct and (recursively) any nested struct declarations. Phase 1 emits
 * nested types first so the embedding struct sees a complete type. */
void emit_struct_pass(Emit *e, CxNode *s, const char *ns, int phase) {
    const char *inner_ns = child_ns(e, ns, s);

    if (phase == 1) {
        for (int i = 0; i < s->children.len; i++) {
            CxNode *m = s->children.items[i];
            if (m->kind == CX_NODE_STRUCT) emit_struct_pass(e, m, inner_ns, 1);
        }
        cx_emit_line(e->out, s->span.first_line, e->file);
        emit_struct_def(e, s, ns);
        cx_buf_puts(e->out, "\n");
        return;
    }

    if (phase == 2) {
        emit_ctor_proto(e, s, ns);
        emit_dtor_proto(e, s, ns);
        if (!e->header) emit_impl_protos(e, s, ns); /* static: .c only */
    } else {
        emit_impl_defs(e, s, ns); /* methods + static tables, before ctors */
        emit_dtor_helper(e, s, ns);
        emit_ctor_helper(e, s, ns);
    }

    for (int i = 0; i < s->children.len; i++) {
        CxNode *m = s->children.items[i];
        if (m->kind == CX_NODE_STRUCT) emit_struct_pass(e, m, inner_ns, phase);
    }
}
