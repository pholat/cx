/* Cx semantic analysis — diagnostics.
 *
 * Tree-walking checks that report errors: a call whose callee names a struct
 * must find a constructor (`check_calls`), and `#[ctor]`/`#[dtor]` attributes
 * are rejected in favour of the fixed constructor/destructor syntax
 * (`check_attrs`). */
#include "cx/symbols.h"
#include "sema_internal.h"

#include <string.h>

/* Second pass: a call whose callee names a struct is a constructor call, so the
 * struct must actually declare a constructor. This also catches `T v(args);`
 * written for a type that has no constructor. */
void check_calls(BuildCtx *c, CxNode *n) {
    if (!n) return;
    if (n->kind == CX_NODE_EXPR_CALL && n->name) {
        CxSymbol *st = cx_sema_find_struct(c->table, n->name);
        if (st && !st->ctor)
            cx_error(c->diags, n->span,
                     "no constructor defined for struct '%s'", st->name);
    }
    check_calls(c, n->lhs);
    check_calls(c, n->rhs);
    check_calls(c, n->init);
    check_calls(c, n->body);
    check_calls(c, n->cond);
    check_calls(c, n->then);
    check_calls(c, n->els);
    check_calls(c, n->incr);
    for (int i = 0; i < n->children.len; i++) check_calls(c, n->children.items[i]);
    for (int i = 0; i < n->params.len; i++) check_calls(c, n->params.items[i]);
}

/* Reject attributes for constructor/destructor declaration: Cx keeps its fixed
 * `Type(args) { … }` / `~Type() { … }` syntax for those. */
void check_attrs(BuildCtx *c, CxNode *n) {
    if (!n) return;
    for (int i = 0; i < n->attrs.len; i++) {
        CxNode *a = n->attrs.items[i];
        if (!a->name) continue;
        if (!strcmp(a->name, "ctor"))
            cx_error(c->diags, a->span,
                     "`#[ctor]` is not supported; declare a constructor as "
                     "`Type(args) { … }`");
        else if (!strcmp(a->name, "dtor"))
            cx_error(c->diags, a->span,
                     "`#[dtor]` is not supported; declare a destructor as "
                     "`~Type() { … }`");
    }
    check_attrs(c, n->lhs);
    check_attrs(c, n->rhs);
    check_attrs(c, n->init);
    check_attrs(c, n->body);
    check_attrs(c, n->cond);
    check_attrs(c, n->then);
    check_attrs(c, n->els);
    check_attrs(c, n->incr);
    for (int i = 0; i < n->children.len; i++)
        check_attrs(c, n->children.items[i]);
    for (int i = 0; i < n->params.len; i++)
        check_attrs(c, n->params.items[i]);
}
