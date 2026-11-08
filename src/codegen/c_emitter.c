#include "cx/cx.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    CxArena             *arena;
    CxBuf               *out;
    const CxSymbolTable *sema;
    const char          *file;
    int                  indent;
    int                  header;      /* declaration-only (.hx) output */
    const CxNode        *self_struct; /* set while emitting a destructor body */
} Emit;

static void ind(Emit *e) {
    for (int i = 0; i < e->indent; i++) cx_buf_puts(e->out, "    ");
}

/* The struct a declared type refers to (stars stripped), or NULL. When
 * `is_pointer` is given it is set for pointer types, which are not
 * auto-constructed/destructed. */
static CxSymbol *struct_of(Emit *e, const char *type, int *is_pointer) {
    if (is_pointer) *is_pointer = 0;
    if (!type || !*type) return NULL;
    if (is_pointer && strchr(type, '*')) *is_pointer = 1;

    size_t n = strlen(type);
    size_t base = n;
    while (base > 0 && (type[base - 1] == '*' || type[base - 1] == ' ')) base--;
    char name[128];
    size_t k = base < sizeof name - 1 ? base : sizeof name - 1;
    memcpy(name, type, k);
    name[k] = '\0';
    return cx_sema_find_struct(e->sema, name);
}

/* Mangled C tag for a declared type, or NULL when the type is not a struct. */
static char *mangled_struct(Emit *e, const char *type) {
    CxSymbol *st = struct_of(e, type, NULL);
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

/* `type name`, or `type (*name)(params)` when the declarator is a function
 * pointer. `d` carries name / is_funcptr / params. */
static void emit_declarator(Emit *e, const char *type, const CxNode *d) {
    emit_type(e, type);
    if (d->is_funcptr) {
        cx_buf_printf(e->out, " (*%s)(", d->name);
        for (int i = 0; i < d->params.len; i++) {
            if (i) cx_buf_puts(e->out, ", ");
            emit_type(e, d->params.items[i]->type);
        }
        cx_buf_putc(e->out, ')');
    } else {
        cx_buf_putc(e->out, ' ');
        cx_buf_puts(e->out, d->name);
    }
}

/* Emit a node that carries its own type + declarator (param, typedef, field of
 * a struct member list is handled separately). */
static void emit_decl(Emit *e, CxNode *n) {
    emit_declarator(e, n->type, n);
}

/* Emit a `raw { … }` body verbatim (first line indented, the rest kept as
 * written). A trailing newline is ensured so following output starts cleanly. */
static void emit_raw(Emit *e, const char *text) {
    if (!text) return;
    ind(e);
    cx_buf_puts(e->out, text);
    size_t n = strlen(text);
    if (n == 0 || text[n - 1] != '\n') cx_buf_putc(e->out, '\n');
}

/* Emit `NAME(args)` for a C macro invocation passthrough (args are verbatim). */
static void emit_macro_call(Emit *e, CxNode *n) {
    cx_buf_puts(e->out, n->name ? n->name : "");
    cx_buf_putc(e->out, '(');
    if (n->type) cx_buf_puts(e->out, n->type);
    cx_buf_putc(e->out, ')');
}

/* ------------------------------------------------------------- attributes */

/* Attributes emitted as leading specifiers, before the return type. */
static int attr_is_leading(const char *name) {
    return name && (!strcmp(name, "inline") || !strcmp(name, "noreturn"));
}

/* Attributes consumed by the front-end (never emitted as-is). `ctor`/`dtor` are
 * rejected by sema but listed here too so they are never emitted as C attrs. */
static int attr_is_semantic(const char *name) {
    return name && (!strcmp(name, "export") || !strcmp(name, "cleanup") ||
                    !strcmp(name, "test") || !strcmp(name, "ctor") ||
                    !strcmp(name, "dtor"));
}

static void emit_attr_leading(Emit *e, CxNode *n) {
    if (!n) return;
    for (int i = 0; i < n->attrs.len; i++) {
        const char *a = n->attrs.items[i]->name;
        if (!a) continue;
        if (!strcmp(a, "inline")) cx_buf_puts(e->out, "inline ");
        else if (!strcmp(a, "noreturn")) cx_buf_puts(e->out, "_Noreturn ");
    }
}

/* Emit `__attribute__((…))` for every trailing attribute (unknown names pass
 * through as raw C attributes). `#[repr(packed)]` folds to `packed`. */
static void emit_attr_trailing(Emit *e, CxNode *n) {
    if (!n) return;
    for (int i = 0; i < n->attrs.len; i++) {
        CxNode *a = n->attrs.items[i];
        if (!a->name || attr_is_leading(a->name) || attr_is_semantic(a->name))
            continue;
        if (!strcmp(a->name, "repr") && a->type && !strcmp(a->type, "packed"))
            cx_buf_puts(e->out, " __attribute__((packed))");
        else if (a->type)
            cx_buf_printf(e->out, " __attribute__((%s(%s)))", a->name, a->type);
        else
            cx_buf_printf(e->out, " __attribute__((%s))", a->name);
    }
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

static int struct_has_field(const CxNode *s, const char *name) {
    if (!s || !name) return 0;
    for (int i = 0; i < s->children.len; i++) {
        CxNode *m = s->children.items[i];
        if (m->kind != CX_NODE_VAR_DECL) continue;
        for (int k = 0; k < m->children.len; k++)
            if (m->children.items[k]->name &&
                !strcmp(m->children.items[k]->name, name))
                return 1;
    }
    return 0;
}

static void emit_expr(Emit *e, CxNode *n) {
    if (!n) return;
    switch (n->kind) {
    case CX_NODE_MACRO:
        emit_macro_call(e, n);
        break;
    case CX_NODE_EXPR_IDENT:
        if (e->self_struct && struct_has_field(e->self_struct, n->name)) {
            cx_buf_printf(e->out, "self->%s", n->name);
        } else {
            /* A bare identifier that names a function is a function pointer
             * (e.g. passing a callback); emit its mangled/exported name. */
            CxSymbol *fn = cx_sema_find_function(e->sema, n->name);
            if (fn) cx_buf_puts(e->out, fn->export_name
                                            ? fn->export_name
                                            : cx_mangle(e->arena, fn->ns, fn->name));
            else cx_buf_puts(e->out, n->name);
        }
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
    case CX_NODE_EXPR_TERNARY:
        emit_expr(e, n->cond);
        cx_buf_puts(e->out, " ? ");
        emit_expr(e, n->lhs);
        cx_buf_puts(e->out, " : ");
        emit_expr(e, n->rhs);
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
        cx_buf_putc(e->out, ' ');
        cx_buf_puts(e->out, n->name ? n->name : "=");
        cx_buf_putc(e->out, ' ');
        emit_expr(e, n->rhs);
        break;
    case CX_NODE_EXPR_POSTFIX:
        emit_expr(e, n->lhs);
        cx_buf_puts(e->out, n->name);
        break;
    case CX_NODE_EXPR_FIELD:
        emit_expr(e, n->lhs);
        cx_buf_putc(e->out, '.');
        cx_buf_puts(e->out, n->name);
        break;
    case CX_NODE_EXPR_ARROW:
        emit_expr(e, n->lhs);
        cx_buf_puts(e->out, "->");
        cx_buf_puts(e->out, n->name);
        break;
    case CX_NODE_EXPR_INDEX:
        emit_expr(e, n->lhs);
        cx_buf_putc(e->out, '[');
        emit_expr(e, n->rhs);
        cx_buf_putc(e->out, ']');
        break;
    case CX_NODE_EXPR_CAST:
        cx_buf_putc(e->out, '(');
        emit_type(e, n->type);
        cx_buf_putc(e->out, ')');
        emit_expr(e, n->lhs);
        break;
    case CX_NODE_EXPR_SIZEOF:
        cx_buf_puts(e->out, "sizeof(");
        if (n->type) emit_type(e, n->type);
        else emit_expr(e, n->lhs);
        cx_buf_putc(e->out, ')');
        break;
    case CX_NODE_EXPR_CALL: {
        /* A non-NULL `lhs` marks an unresolved `recv.method(args)` /
         * `recv->method(args)` (sema rewrites the receiver-typed ones to plain
         * calls and clears `lhs`). Emit it back verbatim — e.g. a call through a
         * C function-pointer field. */
        if (n->lhs) {
            emit_expr(e, n->lhs);
            cx_buf_puts(e->out, n->type ? n->type : ".");
            cx_buf_puts(e->out, n->name ? n->name : "");
            cx_buf_putc(e->out, '(');
            for (int i = 0; i < n->children.len; i++) {
                if (i) cx_buf_puts(e->out, ", ");
                emit_expr(e, n->children.items[i]);
            }
            cx_buf_putc(e->out, ')');
            break;
        }
        CxSymbol *fn = cx_sema_find_function(e->sema, n->name);
        if (fn)
            cx_buf_puts(e->out, fn->export_name
                                    ? fn->export_name
                                    : cx_mangle(e->arena, fn->ns, fn->name));
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
static void emit_branch(Emit *e, CxNode *n);

/* ---------------------------------------------------------------- traits */

static int is_receiver(const CxNode *p) {
    return p && p->name && strcmp(p->name, "self") == 0;
}

static int struct_has_implements(const CxNode *s) {
    for (int i = 0; s && i < s->children.len; i++)
        if (s->children.items[i]->kind == CX_NODE_IMPLEMENTS) return 1;
    return 0;
}

/* `struct <Trait>__vtable { ret (*m)(params); … };` — one static table type per
 * trait, shared by every implementing struct. */
static void emit_vtable_struct(Emit *e, CxNode *trait, const char *ns) {
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

static void emit_impl_protos(Emit *e, CxNode *s, const char *ns) {
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

static void emit_impl_defs(Emit *e, CxNode *s, const char *ns) {
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
static void emit_trait_installs_self(Emit *e, CxNode *s, const char *ns) {
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
static void emit_trait_installs_var(Emit *e, CxNode *s, const char *ns,
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
static void emit_trait_global_init(Emit *e, CxNode *s, const char *ns) {
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

/* A constructor call is any call whose callee names a struct that has a ctor. */
static int is_ctor_init(Emit *e, CxNode *init) {
    if (!init || init->kind != CX_NODE_EXPR_CALL || !init->name) return 0;
    CxSymbol *st = cx_sema_find_struct(e->sema, init->name);
    return st && st->ctor != NULL;
}

static void emit_var_decl(Emit *e, CxNode *n, int is_local) {
    int ptr = 0;
    CxSymbol *st = struct_of(e, n->type, &ptr);
    CxNode *st_node = (st && st->kind == CX_NODE_STRUCT) ? st->decl : NULL;
    int traits = (!ptr && st_node && struct_has_implements(st_node));

    for (int k = 0; k < n->children.len; k++) {
        CxNode *d = n->children.items[k];
        int ctor = is_ctor_init(e, d->init);

        ind(e);
        emit_declarator(e, n->type, d);

        /* `cleanup` only applies to automatic (local) variables. */
        if (is_local) {
            char *dtor = cx_raii_dtor_name(e->arena, e->sema, n->type);
            if (dtor) cx_buf_printf(e->out, " __attribute__((cleanup(%s)))", dtor);
        }
        emit_attr_trailing(e, n); /* `#[aligned(8)] int x;` etc. */

        if (d->init && !ctor) {
            cx_buf_puts(e->out, " = ");
            emit_expr(e, d->init);
        } else if (!is_local && !ctor && !d->init && traits) {
            /* A global has no place for a statement, so install the trait tables
             * with a designated initializer (other fields become zero). */
            emit_trait_global_init(e, st_node, st->ns);
        }
        cx_buf_puts(e->out, ";\n");

        if (ctor && is_local) {
            /* T v(args);  ->  T v [cleanup];  T__ctor(&v, args); */
            char *q = mangled_struct(e, n->type);
            ind(e);
            cx_buf_printf(e->out, "%s__ctor(&%s", q ? q : d->name, d->name);
            for (int i = 0; i < d->init->children.len; i++) {
                cx_buf_puts(e->out, ", ");
                emit_expr(e, d->init->children.items[i]);
            }
            cx_buf_puts(e->out, ");\n");
        } else if (ctor) {
            ind(e);
            cx_buf_printf(e->out,
                          "/* constructor call for '%s' ignored at file scope */\n",
                          d->name);
        } else if (is_local && traits) {
            /* `T v;` does not call a constructor, so install the trait tables
             * (the vtable pointers) explicitly. */
            emit_trait_installs_var(e, st_node, st->ns, d->name);
        }
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
    case CX_NODE_RAW:
        emit_raw(e, n->name);
        break;
    case CX_NODE_MACRO:
        ind(e);
        emit_macro_call(e, n);
        cx_buf_puts(e->out, ";\n");
        break;
    case CX_NODE_IF:
        ind(e);
        cx_buf_puts(e->out, "if (");
        emit_expr(e, n->cond);
        cx_buf_puts(e->out, ") ");
        emit_branch(e, n->then);
        if (n->els) {
            ind(e);
            cx_buf_puts(e->out, "else ");
            emit_branch(e, n->els);
        }
        break;
    case CX_NODE_WHILE:
        ind(e);
        cx_buf_puts(e->out, "while (");
        emit_expr(e, n->cond);
        cx_buf_puts(e->out, ") ");
        emit_branch(e, n->body);
        break;
    case CX_NODE_DO:
        ind(e);
        cx_buf_puts(e->out, "do ");
        emit_branch(e, n->body);
        ind(e);
        cx_buf_puts(e->out, "while (");
        emit_expr(e, n->cond);
        cx_buf_puts(e->out, ");\n");
        break;
    case CX_NODE_FOR:
        ind(e);
        cx_buf_puts(e->out, "for (");
        if (n->lhs) emit_expr(e, n->lhs);
        cx_buf_puts(e->out, "; ");
        if (n->cond) emit_expr(e, n->cond);
        cx_buf_puts(e->out, "; ");
        if (n->incr) emit_expr(e, n->incr);
        cx_buf_puts(e->out, ") ");
        emit_branch(e, n->body);
        break;
    case CX_NODE_BREAK:
        ind(e);
        cx_buf_puts(e->out, "break;\n");
        break;
    case CX_NODE_CONTINUE:
        ind(e);
        cx_buf_puts(e->out, "continue;\n");
        break;
    case CX_NODE_SWITCH:
        ind(e);
        cx_buf_puts(e->out, "switch (");
        emit_expr(e, n->cond);
        cx_buf_puts(e->out, ") ");
        emit_branch(e, n->body);
        break;
    case CX_NODE_CASE:
        ind(e);
        cx_buf_puts(e->out, "case ");
        emit_expr(e, n->lhs);
        cx_buf_puts(e->out, ":\n");
        if (n->body) emit_stmt(e, n->body);
        break;
    case CX_NODE_DEFAULT:
        ind(e);
        cx_buf_puts(e->out, "default:\n");
        if (n->body) emit_stmt(e, n->body);
        break;
    case CX_NODE_LABEL:
        ind(e);
        cx_buf_puts(e->out, n->name);
        cx_buf_puts(e->out, ":\n");
        if (n->body) emit_stmt(e, n->body);
        break;
    case CX_NODE_GOTO:
        ind(e);
        cx_buf_puts(e->out, "goto ");
        cx_buf_puts(e->out, n->name);
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

/* A controlled statement (if/while/for body), always wrapped in braces. */
static void emit_branch(Emit *e, CxNode *n) {
    cx_buf_puts(e->out, "{\n");
    e->indent++;
    if (n && n->kind == CX_NODE_BLOCK) {
        for (int i = 0; i < n->children.len; i++)
            emit_stmt(e, n->children.items[i]);
    } else if (n) {
        emit_stmt(e, n);
    }
    e->indent--;
    ind(e);
    cx_buf_puts(e->out, "}\n");
}

static void emit_function_proto(Emit *e, CxNode *n, const char *ns) {
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
    if (!dtor) return;

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
    if (!ctor) return;

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

static const char *child_ns(Emit *e, const char *ns, CxNode *child) {
    if (!ns || !*ns) return cx_arena_strdup(e->arena, child->name);
    return cx_arena_printf(e->arena, "%s::%s", ns, child->name);
}

/* Emit a struct and (recursively) any nested struct declarations. Phase 1 emits
 * nested types first so the embedding struct sees a complete type. */
static void emit_struct_pass(Emit *e, CxNode *s, const char *ns, int phase) {
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

/* Five passes so the emitted C has a valid dependency order:
 *   0. preamble: trait vtable types, raw blocks, C macro invocations
 *   1. struct definitions + typedefs  (complete types first)
 *   2. function/ctor/dtor/impl protos (may return/accept structs by value)
 *   3. dtor/ctor + impl methods + vtables (tables before ctors install them)
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
    case 0:
        /* Preamble: trait vtable types, verbatim `raw` blocks, and C macro
         * instantiations — everything a later struct/function may depend on. */
        if (node->kind == CX_NODE_TRAIT) {
            emit_vtable_struct(e, node, ns);
        } else if (node->kind == CX_NODE_RAW) {
            emit_raw(e, node->name);
        } else if (node->kind == CX_NODE_MACRO) {
            ind(e);
            emit_macro_call(e, node);
            cx_buf_puts(e->out, ";\n");
        }
        break;
    case 1:
        if (node->kind == CX_NODE_STRUCT) {
            emit_struct_pass(e, node, ns, 1);
        } else if (node->kind == CX_NODE_TYPEDEF) {
            cx_emit_line(e->out, node->span.first_line, e->file);
            ind(e);
            cx_buf_puts(e->out, "typedef ");
            emit_decl(e, node);
            cx_buf_puts(e->out, ";\n");
        }
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

/* `foo.hx` / `foo.cx` -> `foo.h`, the generated header an import refers to. */
static char *import_header(CxArena *a, const char *path) {
    size_t n = path ? strlen(path) : 0;
    if (n >= 3 &&
        (strcmp(path + n - 3, ".hx") == 0 || strcmp(path + n - 3, ".cx") == 0))
        return cx_arena_printf(a, "%.*s.h", (int)(n - 3), path);
    return cx_arena_strdup(a, path ? path : "");
}

static void emit_includes(CxBuf *out, CxArena *arena, CxNode *tu) {
    if (!tu) return;
    for (int i = 0; i < tu->imports.len; i++) {
        CxNode *imp = tu->imports.items[i];
        cx_buf_printf(out, "#include \"%s\"\n", import_header(arena, imp->name));
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
    e.header = 0;
    e.self_struct = NULL;

    cx_buf_printf(out, "/* Generated by cx %s from %s. Do not edit. */\n\n",
                  CX_VERSION, file);

    if (tu && tu->prelude && tu->prelude[0]) {
        cx_buf_puts(out, tu->prelude);
        cx_buf_putc(out, '\n');
    }
    /* Pairing: `foo.cx` includes the generated header of `foo.hx`, so the
     * declarations live in one place. */
    int paired = tu && tu->paired_header && tu->paired_header[0];
    if (paired) cx_buf_printf(out, "#include \"%s\"\n", tu->paired_header);
    emit_includes(out, arena, tu);

    cx_emit_line(out, 1, file);

    if (paired) {
        /* The header already provides the preamble (trait vtable types, raw
         * blocks, macro invocations), the typedefs and struct definitions; and
         * its prototypes match the ones we would emit. Re-emitting the types
         * would be a C redefinition, so emit only prototypes for the `.cx`'s
         * own helpers and the definitions (impl methods/tables, ctor/dtor
         * helpers, function bodies, globals). */
        walk(&e, tu, NULL, 2);
        cx_buf_puts(out, "\n");
        walk(&e, tu, NULL, 3);
        cx_buf_puts(out, "\n");
        walk(&e, tu, NULL, 4);
        return;
    }

    walk(&e, tu, NULL, 0);
    cx_buf_puts(out, "\n");
    walk(&e, tu, NULL, 1);
    cx_buf_puts(out, "\n");
    walk(&e, tu, NULL, 2);
    cx_buf_puts(out, "\n");
    walk(&e, tu, NULL, 3);
    cx_buf_puts(out, "\n");
    walk(&e, tu, NULL, 4);
}

/* Declaration-only output for a `.hx` header: include guard, typedefs, struct
 * definitions, and prototypes (functions, constructors, destructors). No
 * definitions, so a `.c` from the matching `.cx` provides the symbols. */
void cx_emit_h(CxArena *arena, CxBuf *out, CxNode *tu,
               const CxSymbolTable *sema, const char *source_name) {
    const char *file = source_name ? source_name : "<input>";

    Emit e;
    e.arena = arena;
    e.out = out;
    e.sema = sema;
    e.file = file;
    e.indent = 0;
    e.header = 1;
    e.self_struct = NULL;

    /* Guard name: basename (no directory), uppercase, non-alnum -> '_'. */
    const char *base = file;
    for (const char *p = file; *p; p++)
        if (*p == '/') base = p + 1;
    CxBuf guard;
    cx_buf_init(&guard);
    cx_buf_puts(&guard, "CX_");
    for (const char *p = base; *p && *p != '.'; p++)
        cx_buf_putc(&guard, (char)((*p >= 'a' && *p <= 'z') ? *p - 32 : *p));
    cx_buf_puts(&guard, "_H");

    cx_buf_printf(out, "/* Generated by cx %s from %s. Do not edit. */\n\n",
                  CX_VERSION, file);
    if (tu && tu->prelude && tu->prelude[0]) {
        cx_buf_puts(out, tu->prelude);
        cx_buf_putc(out, '\n');
    }
    emit_includes(out, arena, tu);
    cx_buf_printf(out, "#ifndef %s\n#define %s\n\n", guard.data, guard.data);

    cx_emit_line(out, 1, file);
    walk(&e, tu, NULL, 0); /* trait vtable types + raw/macro preamble */
    cx_buf_puts(out, "\n");
    walk(&e, tu, NULL, 1); /* typedefs + struct definitions */
    cx_buf_puts(out, "\n");
    walk(&e, tu, NULL, 2); /* functions + ctor/dtor prototypes */

    cx_buf_printf(out, "\n#endif /* %s */\n", guard.data);
    cx_buf_free(&guard);
}
