/* Cx code generation — statements, blocks and declarations. */
#include "cx/cx.h"
#include "c_emitter.h"

#include <string.h>

static void emit_branch(Emit *e, CxNode *n);

/* A constructor call is any call whose callee names a struct that has a ctor. */
static int is_ctor_init(Emit *e, CxNode *init) {
    if (!init || init->kind != CX_NODE_EXPR_CALL || !init->name) return 0;
    CxSymbol *st = cx_sema_find_struct(e->sema, init->name);
    return st && st->ctor != NULL;
}

void emit_var_decl(Emit *e, CxNode *n, int is_local) {
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

void emit_stmt(Emit *e, CxNode *n) {
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

void emit_block(Emit *e, CxNode *body) {
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
