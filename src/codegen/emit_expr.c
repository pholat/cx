/* Cx code generation — types, declarators, attributes and expressions. */
#include "cx/cx.h"
#include "c_emitter.h"
#include "../features/cast/cast_feature.h"

#include <stdio.h>
#include <string.h>

void ind(Emit *e) {
    for (int i = 0; i < e->indent; i++) cx_buf_puts(e->out, "    ");
}

/* The struct a declared type refers to (stars stripped), or NULL. When
 * `is_pointer` is given it is set for pointer types, which are not
 * auto-constructed/destructed. */
CxSymbol *struct_of(Emit *e, const char *type, int *is_pointer) {
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
char *mangled_struct(Emit *e, const char *type) {
    CxSymbol *st = struct_of(e, type, NULL);
    if (!st) return NULL;
    return cx_mangle(e->arena, st->ns, st->name);
}

/* Emit a declared type. Struct names are qualified to their mangled C tag so
 * that same-named structs in different namespaces never collide. */
void emit_type(Emit *e, const char *type) {
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
    CxSymbol *en = st ? NULL : cx_sema_find_enum(e->sema, name);
    CxSymbol *fs = (st || en) ? NULL : cx_sema_find_fsm(e->sema, name);
    if (st)
        cx_buf_printf(e->out, "struct %s", cx_mangle(e->arena, st->ns, st->name));
    else if (en)
        cx_buf_puts(e->out, cx_mangle(e->arena, en->ns, en->name));
    else if (fs)
        cx_buf_puts(e->out, fs->name); /* FSM names are emitted bare */
    else
        cx_buf_puts(e->out, name);

    cx_buf_puts(e->out, type + base); /* re-attach " *" etc. */
}

/* `type name`, or `type (*name)(params)` when the declarator is a function
 * pointer. `d` carries name / is_funcptr / params. */
void emit_declarator(Emit *e, const char *type, const CxNode *d) {
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
void emit_decl(Emit *e, CxNode *n) {
    emit_declarator(e, n->type, n);
}

/* Emit a `raw { … }` body verbatim (first line indented, the rest kept as
 * written). A trailing newline is ensured so following output starts cleanly. */
void emit_raw(Emit *e, const char *text) {
    if (!text) return;
    ind(e);
    cx_buf_puts(e->out, text);
    size_t n = strlen(text);
    if (n == 0 || text[n - 1] != '\n') cx_buf_putc(e->out, '\n');
}

/* Emit `NAME(args)` for a C macro invocation passthrough (args are verbatim). */
void emit_macro_call(Emit *e, CxNode *n) {
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

void emit_attr_leading(Emit *e, CxNode *n) {
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
void emit_attr_trailing(Emit *e, CxNode *n) {
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

void emit_expr(Emit *e, CxNode *n) {
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
        cx_cast_emit(e, n);
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
