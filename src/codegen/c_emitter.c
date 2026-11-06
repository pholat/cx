#include "cx/cx.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    CxArena             *arena;
    CxBuf               *out;
    const CxSymbolTable *sema;
    const char          *file;
    int                  indent;
    const CxNode        *self_struct; /* set while emitting a destructor body */
} Emit;

static void ind(Emit *e) {
    for (int i = 0; i < e->indent; i++) cx_buf_puts(e->out, "    ");
}

/* Mangled C tag for a declared type, or NULL when the type is not a struct. */
static char *mangled_struct(Emit *e, const char *type) {
    if (!type || !*type) return NULL;
    size_t n = strlen(type);
    size_t base = n;
    while (base > 0 && (type[base - 1] == '*' || type[base - 1] == ' ')) base--;
    char name[128];
    size_t k = base < sizeof name - 1 ? base : sizeof name - 1;
    memcpy(name, type, k);
    name[k] = '\0';
    CxSymbol *st = cx_sema_find_struct(e->sema, name);
    if (!st) return NULL;
    return cx_mangle(e->arena, st->ns, st->name);
}

static CxNode *find_member(const CxNode *s, CxNodeKind kind) {
    if (!s) return NULL;
    for (int i = 0; i < s->children.len; i++)
        if (s->children.items[i]->kind == kind) return s->children.items[i];
    return NULL;
}

/* Emit a declared type. Struct names are qualified to their mangled C tag so
 * that same-named structs in different namespaces never collide. */
static void emit_type(Emit *e, const char *type) {
    if (!type || !*type) {
        cx_buf_puts(e->out, "void");
        return;
    }
    size_t n = strlen(type);
    size_t base = n;
    while (base > 0 && (type[base - 1] == '*' || type[base - 1] == ' ')) base--;

    char name[128];
    size_t k = base < sizeof name - 1 ? base : sizeof name - 1;
    memcpy(name, type, k);
    name[k] = '\0';

    CxSymbol *st = cx_sema_find_struct(e->sema, name);
    if (st)
        cx_buf_printf(e->out, "struct %s", cx_mangle(e->arena, st->ns, st->name));
    else
        cx_buf_puts(e->out, name);

    cx_buf_puts(e->out, type + base); /* re-attach " *" etc. */
}

static int struct_has_field(const CxNode *s, const char *name) {
    if (!s || !name) return 0;
    for (int i = 0; i < s->children.len; i++) {
        CxNode *m = s->children.items[i];
        if (m->kind == CX_NODE_VAR_DECL && m->name && !strcmp(m->name, name))
            return 1;
    }
    return 0;
}

static void emit_expr(Emit *e, CxNode *n) {
    if (!n) return;
    switch (n->kind) {
    case CX_NODE_EXPR_IDENT:
        if (e->self_struct && struct_has_field(e->self_struct, n->name))
            cx_buf_printf(e->out, "self->%s", n->name);
        else
            cx_buf_puts(e->out, n->name);
        break;
    case CX_NODE_EXPR_INT:
    case CX_NODE_EXPR_STRING:
        cx_buf_puts(e->out, n->name);
        break;
    case CX_NODE_EXPR_PAREN:
        cx_buf_putc(e->out, '(');
        emit_expr(e, n->lhs);
        cx_buf_putc(e->out, ')');
        break;
    case CX_NODE_EXPR_BINARY:
        emit_expr(e, n->lhs);
        cx_buf_printf(e->out, " %s ", n->name);
        emit_expr(e, n->rhs);
        break;
    case CX_NODE_EXPR_UNARY:
        cx_buf_puts(e->out, n->name);
        emit_expr(e, n->lhs);
        break;
    case CX_NODE_EXPR_ASSIGN:
        emit_expr(e, n->lhs);
        cx_buf_puts(e->out, " = ");
        emit_expr(e, n->rhs);
        break;
    case CX_NODE_EXPR_FIELD:
        emit_expr(e, n->lhs);
        cx_buf_putc(e->out, '.');
        cx_buf_puts(e->out, n->name);
        break;
    case CX_NODE_EXPR_CALL: {
        CxSymbol *fn = cx_sema_find_function(e->sema, n->name);
        if (fn)
            cx_buf_puts(e->out, cx_mangle(e->arena, fn->ns, fn->name));
        else
            cx_buf_puts(e->out, n->name);
        cx_buf_putc(e->out, '(');
        for (int i = 0; i < n->children.len; i++) {
            if (i) cx_buf_puts(e->out, ", ");
            emit_expr(e, n->children.items[i]);
        }
        cx_buf_putc(e->out, ')');
        break;
    }
    default:
        break;
    }
}

static void emit_block(Emit *e, CxNode *body);
static void emit_stmt(Emit *e, CxNode *n);
static void emit_var_decl(Emit *e, CxNode *n, int is_local);

/* A constructor call is any call whose callee names a struct that has a ctor. */
static int is_ctor_init(Emit *e, CxNode *init) {
    if (!init || init->kind != CX_NODE_EXPR_CALL || !init->name) return 0;
    CxSymbol *st = cx_sema_find_struct(e->sema, init->name);
    return st && st->ctor != NULL;
}

static void emit_var_decl(Emit *e, CxNode *n, int is_local) {
    int ctor = is_ctor_init(e, n->init);

    ind(e);
    emit_type(e, n->type);
    cx_buf_putc(e->out, ' ');
    cx_buf_puts(e->out, n->name);

    /* `cleanup` only applies to automatic (local) variables. */
    if (is_local) {
        char *dtor = cx_raii_dtor_name(e->arena, e->sema, n->type);
        if (dtor) cx_buf_printf(e->out, " __attribute__((cleanup(%s)))", dtor);
    }

    if (n->init && !ctor) {
        cx_buf_puts(e->out, " = ");
        emit_expr(e, n->init);
    }
    cx_buf_puts(e->out, ";\n");

    if (ctor && is_local) {
        /* T v(args);  ->  T v [cleanup];  T__ctor(&v, args); */
        char *q = mangled_struct(e, n->type);
        ind(e);
        cx_buf_printf(e->out, "%s__ctor(&%s", q ? q : n->name, n->name);
        for (int i = 0; i < n->init->children.len; i++) {
            cx_buf_puts(e->out, ", ");
            emit_expr(e, n->init->children.items[i]);
        }
        cx_buf_puts(e->out, ");\n");
    } else if (ctor) {
        ind(e);
        cx_buf_printf(e->out,
                      "/* constructor call for '%s' ignored at file scope */\n",
                      n->name);
    }
}

static void emit_stmt(Emit *e, CxNode *n) {
    switch (n->kind) {
    case CX_NODE_VAR_DECL:
        emit_var_decl(e, n, 1);
        break;
    case CX_NODE_EXPR_STMT:
        ind(e);
        emit_expr(e, n->lhs);
        cx_buf_puts(e->out, ";\n");
        break;
    case CX_NODE_RETURN:
        ind(e);
        if (n->init) {
            cx_buf_puts(e->out, "return ");
            emit_expr(e, n->init);
        } else {
            cx_buf_puts(e->out, "return");
        }
        cx_buf_puts(e->out, ";\n");
        break;
    case CX_NODE_BLOCK:
        ind(e);
        emit_block(e, n);
        break;
    case CX_NODE_EMPTY:
        ind(e);
        cx_buf_puts(e->out, ";\n");
        break;
    default:
        break;
    }
}

static void emit_block(Emit *e, CxNode *body) {
    cx_buf_puts(e->out, "{\n");
    e->indent++;
    for (int i = 0; body && i < body->children.len; i++)
        emit_stmt(e, body->children.items[i]);
    e->indent--;
    ind(e);
    cx_buf_puts(e->out, "}\n");
}

static void emit_function_proto(Emit *e, CxNode *n, const char *ns) {
    ind(e);
    emit_type(e, n->type);
    cx_buf_putc(e->out, ' ');
    cx_buf_puts(e->out, cx_mangle(e->arena, ns, n->name));
    cx_buf_putc(e->out, '(');
    if (n->params.len == 0) {
        cx_buf_puts(e->out, "void");
    } else {
        for (int i = 0; i < n->params.len; i++) {
            if (i) cx_buf_puts(e->out, ", ");
            CxNode *p = n->params.items[i];
            emit_type(e, p->type);
            cx_buf_putc(e->out, ' ');
            cx_buf_puts(e->out, p->name);
        }
    }
    cx_buf_putc(e->out, ')');
}

static void emit_struct_def(Emit *e, CxNode *n, const char *ns) {
    char *q = cx_mangle(e->arena, ns, n->name);
    ind(e);
    cx_buf_printf(e->out, "struct %s {\n", q);
    e->indent++;
    for (int i = 0; i < n->children.len; i++) {
        CxNode *m = n->children.items[i];
        if (m->kind != CX_NODE_VAR_DECL) continue;
        ind(e);
        emit_type(e, m->type);
        cx_buf_putc(e->out, ' ');
        cx_buf_puts(e->out, m->name);
        cx_buf_puts(e->out, ";\n");
    }
    e->indent--;
    ind(e);
    cx_buf_puts(e->out, "};\n");
}

static void emit_dtor_helper(Emit *e, CxNode *s, const char *ns) {
    CxNode *dtor = find_member(s, CX_NODE_DESTRUCTOR);
    if (!dtor) return;

    char *q = cx_mangle(e->arena, ns, s->name);
    cx_emit_line(e->out, dtor->span.first_line, e->file);
    ind(e);
    cx_buf_printf(e->out, "static inline void %s__dtor(struct %s *self)\n", q, q);

    const CxNode *saved = e->self_struct;
    e->self_struct = s;
    emit_block(e, dtor->body);
    e->self_struct = saved;
    cx_buf_puts(e->out, "\n");
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

static void emit_ctor_helper(Emit *e, CxNode *s, const char *ns) {
    CxNode *ctor = find_member(s, CX_NODE_CONSTRUCTOR);
    if (!ctor) return;

    emit_ctor_signature(e, ctor, ns, s, 0);
    const CxNode *saved = e->self_struct;
    e->self_struct = s;
    emit_block(e, ctor->body);
    e->self_struct = saved;
    cx_buf_puts(e->out, "\n");
}

static const char *child_ns(Emit *e, const char *ns, CxNode *child) {
    if (!ns || !*ns) return cx_arena_strdup(e->arena, child->name);
    return cx_arena_printf(e->arena, "%s::%s", ns, child->name);
}

/* Emit a struct and (recursively) any nested struct declarations. Phase 1 emits
 * nested types first so the embedding struct sees a complete type. */
static void emit_struct_pass(Emit *e, CxNode *s, const char *ns, int phase) {
    char *inner_ns = child_ns(e, ns, s);

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
    } else {
        emit_dtor_helper(e, s, ns);
        emit_ctor_helper(e, s, ns);
    }

    for (int i = 0; i < s->children.len; i++) {
        CxNode *m = s->children.items[i];
        if (m->kind == CX_NODE_STRUCT) emit_struct_pass(e, m, inner_ns, phase);
    }
}

/* Four passes so the emitted C has a valid dependency order:
 *   1. struct definitions             (complete types first)
 *   2. function + constructor protos  (may return/accept structs by value)
 *   3. destructor + constructor impls (call functions through the protos)
 *   4. function bodies + globals      (use ctors/dtors)
 */
static void walk(Emit *e, CxNode *node, const char *ns, int phase) {
    if (!node) return;

    if (node->kind == CX_NODE_TRANSLATION_UNIT ||
        node->kind == CX_NODE_NAMESPACE) {
        for (int i = 0; i < node->children.len; i++) {
            CxNode *ch = node->children.items[i];
            if (ch->kind == CX_NODE_NAMESPACE)
                walk(e, ch, child_ns(e, ns, ch), phase);
            else
                walk(e, ch, ns, phase);
        }
        return;
    }

    switch (phase) {
    case 1:
        if (node->kind == CX_NODE_STRUCT)
            emit_struct_pass(e, node, ns, 1);
        break;
    case 2:
        if (node->kind == CX_NODE_FUNCTION) {
            cx_emit_line(e->out, node->span.first_line, e->file);
            emit_function_proto(e, node, ns);
            cx_buf_puts(e->out, ";\n");
        } else if (node->kind == CX_NODE_STRUCT) {
            emit_struct_pass(e, node, ns, 2);
        }
        break;
    case 3:
        if (node->kind == CX_NODE_STRUCT)
            emit_struct_pass(e, node, ns, 3);
        break;
    default:
        if (node->kind == CX_NODE_FUNCTION && node->body) {
            cx_emit_line(e->out, node->span.first_line, e->file);
            emit_function_proto(e, node, ns);
            cx_buf_putc(e->out, ' ');
            emit_block(e, node->body);
            cx_buf_puts(e->out, "\n");
        } else if (node->kind == CX_NODE_VAR_DECL) {
            cx_emit_line(e->out, node->span.first_line, e->file);
            emit_var_decl(e, node, 0);
        }
        break;
    }
}

void cx_emit_c(CxArena *arena, CxBuf *out, CxNode *tu,
               const CxSymbolTable *sema, const char *source_name) {
    const char *file = source_name ? source_name : "<input>";

    Emit e;
    e.arena = arena;
    e.out = out;
    e.sema = sema;
    e.file = file;
    e.indent = 0;
    e.self_struct = NULL;

    cx_buf_printf(out, "/* Generated by cx %s from %s. Do not edit. */\n\n",
                  CX_VERSION, file);
    cx_emit_line(out, 1, file);

    walk(&e, tu, NULL, 1);
    cx_buf_puts(out, "\n");
    walk(&e, tu, NULL, 2);
    cx_buf_puts(out, "\n");
    walk(&e, tu, NULL, 3);
    cx_buf_puts(out, "\n");
    walk(&e, tu, NULL, 4);
}
