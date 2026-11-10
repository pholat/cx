/* Cx semantic analysis — `auto` inference and `.`/`->` receiver resolution.
 *
 * `infer_auto` rewrites `auto` declarations/return types; `resolve_receivers`
 * lowers `recv.method(args)` into the associated call `Type__method(&recv, …)`.
 * The LSP receiver-type lookup (`cx_sema_receiver_type`) shares the same
 * block-scoped environment. */
#include "cx/symbols.h"
#include "sema_internal.h"

#include <string.h>

/* The declared (or already-inferred) type of a parameter named `name` in
 * function `fn`, or NULL. Parameters are not in the symbol table, so `auto`
 * inference consults the function's AST directly. */
static const char *param_type(CxNode *fn, const char *name) {
    if (!fn || !name) return NULL;
    for (int i = 0; i < fn->params.len; i++) {
        CxNode *p = fn->params.items[i];
        if (p->name && !strcmp(p->name, name)) return p->type;
    }
    return NULL;
}

/* The first `return expr;` reachable in a statement subtree, or NULL. Used to
 * infer an `auto` function's return type ("where easily possible"). */
static CxNode *first_return_stmt(CxNode *n) {
    if (!n) return NULL;
    if (n->kind == CX_NODE_RETURN && n->init) return n;
    CxNode *r;
    if ((r = first_return_stmt(n->body))) return r;
    if ((r = first_return_stmt(n->then))) return r;
    if ((r = first_return_stmt(n->els))) return r;
    for (int i = 0; i < n->children.len; i++)
        if ((r = first_return_stmt(n->children.items[i]))) return r;
    return NULL;
}

/* Infer the type of an expression for `auto`. `fn` is the enclosing function
 * (for parameter lookup) or NULL at file scope. */
static const char *infer_type(BuildCtx *c, CxNode *fn, CxNode *init) {
    if (!init) return NULL;
    switch (init->kind) {
    case CX_NODE_EXPR_CALL: {
        CxSymbol *f = cx_sema_find_function(c->table, init->name);
        if (f && f->type) return f->type;
        CxSymbol *st = cx_sema_find_struct(c->table, init->name);
        if (st) return st->name; /* constructor call */
        return NULL;
    }
    case CX_NODE_EXPR_INT:    return "int";
    case CX_NODE_EXPR_STRING: return "char *";
    case CX_NODE_EXPR_PAREN:  return infer_type(c, fn, init->lhs);
    case CX_NODE_EXPR_TERNARY: {
        const char *t = infer_type(c, fn, init->lhs);
        return t ? t : infer_type(c, fn, init->rhs);
    }
    case CX_NODE_EXPR_CAST:   return init->type;
    case CX_NODE_EXPR_BINARY: {
        /* Comparison/logical operators yield `int`; the arithmetic/bitwise
         * operators keep the (wider) operand type as a first approximation. */
        const char *op = init->name ? init->name : "";
        if (!strcmp(op, "==") || !strcmp(op, "!=") || !strcmp(op, "<") ||
            !strcmp(op, ">") || !strcmp(op, "<=") || !strcmp(op, ">=") ||
            !strcmp(op, "&&") || !strcmp(op, "||"))
            return "int";
        const char *t = infer_type(c, fn, init->lhs);
        return t ? t : infer_type(c, fn, init->rhs);
    }
    case CX_NODE_EXPR_IDENT: {
        const char *pt = param_type(fn, init->name);
        if (pt) return pt;
        CxSymbol *s = cx_sema_find_symbol(c->table, init->name);
        return s ? s->type : NULL;
    }
    default:
        return NULL;
    }
}

/* Rewrite `auto` declarations and `auto` function return types to the inferred
 * type. Walks the whole tree so it covers locals inside function bodies; `fn`
 * tracks the enclosing function for parameter lookup. When `report` is 0 the
 * pass only fills in what it can (a silent first pass), leaving `auto` in place
 * for a later reporting pass — this lets a second run pick up `auto` locals whose
 * initializer only became resolvable after method-call resolution. */
static void infer_auto(BuildCtx *c, CxNode *n, CxNode *fn, int report) {
    if (!n) return;
    if (n->kind == CX_NODE_FUNCTION) fn = n;

    if (n->kind == CX_NODE_VAR_DECL && n->type && strcmp(n->type, "auto") == 0) {
        const char *t = NULL;
        for (int i = 0; i < n->children.len && !t; i++)
            t = infer_type(c, fn, n->children.items[i]->init);
        if (t && strcmp(t, "auto") != 0) n->type = (char *)t;
        else if (report) cx_error(c->diags, n->span, "cannot infer type for 'auto'");
    }

    /* `auto` function return type: inferred from the first `return expr;`.
     * The function's symbol is updated too, so callers that use the return
     * type (including `auto` at the call site) see the resolved type. */
    if (n->kind == CX_NODE_FUNCTION && n->type && strcmp(n->type, "auto") == 0) {
        CxNode *ret = first_return_stmt(n->body);
        const char *t = ret ? infer_type(c, fn, ret->init) : NULL;
        if (t && strcmp(t, "auto") != 0) {
            n->type = (char *)t;
            for (int i = 0; i < c->table->len; i++)
                if (c->table->items[i]->decl == n) {
                    c->table->items[i]->type = (char *)t;
                    break;
                }
        } else if (report) {
            cx_error(c->diags, n->span,
                     "cannot infer return type for 'auto' function");
        }
    }

    infer_auto(c, n->lhs, fn, report);
    infer_auto(c, n->rhs, fn, report);
    infer_auto(c, n->init, fn, report);
    infer_auto(c, n->body, fn, report);
    infer_auto(c, n->cond, fn, report);
    infer_auto(c, n->then, fn, report);
    infer_auto(c, n->els, fn, report);
    infer_auto(c, n->incr, fn, report);
    for (int i = 0; i < n->children.len; i++)
        infer_auto(c, n->children.items[i], fn, report);
    for (int i = 0; i < n->params.len; i++)
        infer_auto(c, n->params.items[i], fn, report);
}

/* ------------------------------------------------ receiver type environment */

/* A tiny lexical scope mapping a local/parameter name to its declared type.
 * Used to resolve a method call's receiver (`l.push(x)` needs `l`'s type). It is
 * a flat function-wide list (block shadowing is not modelled yet); lookup walks
 * from the end so the most recent declaration wins. */
typedef struct {
    const char *name;
    const char *type;
} ScopeEnt;

typedef struct {
    CxArena  *arena;
    ScopeEnt *items;
    int       len;
    int       cap;
} Scope;

static void scope_push(Scope *s, const char *name, const char *type) {
    if (!name || !type) return;
    if (s->len == s->cap) {
        int cap = s->cap ? s->cap * 2 : 16;
        ScopeEnt *items =
            (ScopeEnt *)cx_arena_alloc(s->arena, sizeof(ScopeEnt) * (size_t)cap);
        if (!items) return;
        if (s->items && s->len)
            memcpy(items, s->items, sizeof(ScopeEnt) * (size_t)s->len);
        s->items = items;
        s->cap = cap;
    }
    s->items[s->len].name = name;
    s->items[s->len].type = type;
    s->len++;
}

static const char *scope_lookup(const Scope *s, const char *name) {
    if (!s || !name) return NULL;
    for (int i = s->len - 1; i >= 0; i--)
        if (s->items[i].name && !strcmp(s->items[i].name, name))
            return s->items[i].type;
    return NULL;
}

static void resolve_receivers(BuildCtx *c, Scope *s, CxNode *n);

/* Best-effort static type of an expression, used to type a method receiver.
 * Handles identifiers (locals/params/globals), constructor/function calls,
 * chained method calls (`a.b()` -> the return type of `A__b`), field/arrow
 * access (struct member type), subscripts (element type), parens, casts and
 * simple binary/ternary expressions. Returns NULL when the type is unknown. */
static const char *expr_type(BuildCtx *c, Scope *s, CxNode *e) {
    if (!e) return NULL;
    switch (e->kind) {
    case CX_NODE_EXPR_IDENT: {
        const char *t = scope_lookup(s, e->name);
        if (t) return t;
        CxSymbol *g = cx_sema_find_symbol(c->table, e->name);
        return g ? g->type : NULL;
    }
    case CX_NODE_EXPR_PAREN:
        return expr_type(c, s, e->lhs);
    case CX_NODE_EXPR_CAST:
        return e->type;
    case CX_NODE_EXPR_BINARY:
    case CX_NODE_EXPR_TERNARY:
        return expr_type(c, s, e->lhs);
    case CX_NODE_EXPR_INDEX:
        return expr_type(c, s, e->lhs);
    case CX_NODE_EXPR_CALL: {
        if (!e->lhs && e->name) {
            CxSymbol *fn = cx_sema_find_function(c->table, e->name);
            if (fn && fn->type) return fn->type;
            CxSymbol *st = cx_sema_find_struct(c->table, e->name);
            if (st) return st->name; /* constructor call */
        }
        /* A still-unresolved `recv.method()`: resolve through the receiver
         * type (`a.b()` -> the return type of `A__b`). */
        if (e->lhs && e->name) {
            const char *rt = expr_type(c, s, e->lhs);
            if (rt) {
                char base[128];
                type_base(rt, base, sizeof base);
                char *fname =
                    cx_arena_printf(c->arena, "%s__%s", base, e->name);
                CxSymbol *fn = cx_sema_find_function(c->table, fname);
                if (fn && fn->type) return fn->type;
            }
        }
        return NULL;
    }
    case CX_NODE_EXPR_FIELD:
    case CX_NODE_EXPR_ARROW: {
        const char *t = expr_type(c, s, e->lhs);
        if (!t) return NULL;
        char base[128];
        type_base(t, base, sizeof base);
        CxSymbol *st = cx_sema_find_struct(c->table, base);
        CxNode *decl = st ? st->decl : NULL;
        for (int i = 0; decl && i < decl->children.len; i++) {
            CxNode *m = decl->children.items[i];
            if (m->kind != CX_NODE_VAR_DECL) continue;
            for (int k = 0; k < m->children.len; k++) {
                CxNode *d = m->children.items[k];
                if (d->name && !strcmp(d->name, e->name)) return m->type;
            }
        }
        return NULL;
    }
    default:
        return NULL;
    }
}

/* Lower `auto [a, b] = expr;` / `auto [a, c] = expr=>[a, c];` into a list of
 * declarations:
 *   <Struct> __cx_unpack_L_C = expr;
 *   <FieldType> a = __cx_unpack_L_C.<field>;
 *   ...
 * `resolve_receivers` supplies the block-scoped environment used to type
 * `expr` and splices the result into the enclosing block (so the targets stay
 * visible to the following statements). */
static CxVec lower_unpack(BuildCtx *c, Scope *s, CxNode *n) {
    CxSpan span = n->span;
    CxVec none = cx_vec_empty();
    const char *stype = expr_type(c, s, n->init);
    char base[128];
    CxSymbol *st = NULL;
    if (stype) {
        type_base(stype, base, sizeof base);
        st = cx_sema_find_struct(c->table, base);
    }
    if (!st || !st->decl) {
        cx_error(c->diags, span,
                 "cannot unpack: the value is not a known struct");
        return none;
    }

    int is_ptr = strchr(stype, '*') != NULL;
    char *tmp = cx_arena_printf(c->arena, "__cx_unpack_%d_%d", span.first_line,
                                span.first_column);
    CxNode *td = cx_node_new(c->arena, CX_NODE_EMPTY, span);
    td->name = tmp;
    td->init = n->init;
    CxNode *temp = cx_node_new(c->arena, CX_NODE_VAR_DECL, span);
    temp->type = (char *)stype;
    cx_vec_push(c->arena, &temp->children, td);

    CxVec out = cx_vec_empty();
    cx_vec_push(c->arena, &out, temp);

    CxNode *decl = st->decl;
    int want = n->children.len;
    int explicit_fields = n->params.len > 0;

    for (int i = 0; i < want; i++) {
        CxNode *target = n->children.items[i];
        const char *fname = NULL;
        const char *ftype = NULL;

        if (explicit_fields) {
            if (i < n->params.len && n->params.items[i]->name) {
                fname = n->params.items[i]->name;
                for (int k = 0; decl && k < decl->children.len && !ftype; k++) {
                    CxNode *m = decl->children.items[k];
                    if (m->kind != CX_NODE_VAR_DECL) continue;
                    for (int q = 0; q < m->children.len; q++) {
                        CxNode *d = m->children.items[q];
                        if (d->name && !strcmp(d->name, fname)) {
                            ftype = m->type;
                            break;
                        }
                    }
                }
            }
        } else {
            /* positional: the i-th declared field, in declaration order */
            int idx = 0;
            for (int k = 0; decl && k < decl->children.len && !ftype; k++) {
                CxNode *m = decl->children.items[k];
                if (m->kind != CX_NODE_VAR_DECL) continue;
                for (int q = 0; q < m->children.len; q++) {
                    CxNode *d = m->children.items[q];
                    if (idx == i) {
                        fname = d->name;
                        ftype = m->type;
                        break;
                    }
                    idx++;
                }
            }
        }

        if (!fname || !ftype) {
            cx_error(c->diags, target->span,
                     "cannot unpack field %d of '%s'", i + 1, st->name);
            continue;
        }

        CxNode *rid = cx_node_new(c->arena, CX_NODE_EXPR_IDENT, target->span);
        rid->name = tmp;
        CxNode *field = cx_node_new(
            c->arena, is_ptr ? CX_NODE_EXPR_ARROW : CX_NODE_EXPR_FIELD,
            target->span);
        field->lhs = rid;
        field->name = (char *)fname;

        CxNode *vd = cx_node_new(c->arena, CX_NODE_VAR_DECL, target->span);
        vd->type = (char *)ftype;
        CxNode *d = cx_node_new(c->arena, CX_NODE_EMPTY, target->span);
        d->name = target->name;
        d->init = field;
        cx_vec_push(c->arena, &vd->children, d);
        cx_vec_push(c->arena, &out, vd);
    }

    return out;
}

/* Walk a method-like body (constructor/destructor/`implements` method) with
 * `self` bound to the enclosing struct's type (`<Struct> *`, since the emitter
 * binds the receiver as a pointer) and the declared parameters in scope. */
static void walk_method_body(BuildCtx *c, Scope *s, CxNode *m,
                             const char *self_type) {
    int save = s->len;
    if (self_type) scope_push(s, "self", self_type);
    for (int i = 0; i < m->params.len; i++) {
        CxNode *p = m->params.items[i];
        if (p->name && p->type) scope_push(s, p->name, p->type);
    }
    resolve_receivers(c, s, m->body);
    s->len = save;
}

/* Rewrite a method call `recv.method(args)` (a `CX_NODE_EXPR_CALL` carrying a
 * receiver in `lhs`) whose receiver is a local/parameter/global of a known
 * struct type into the associated call `Type__method(&recv, args)` — a pointer
 * receiver is passed through unchanged. A call whose receiver cannot be typed
 * (or whose method has no `Type__method` function) is left as written, so the
 * emitter can reproduce `recv.method(args)` (e.g. a C function-pointer field).
 *
 * A block-scoped environment: a function's parameters are pushed on entry, and
 * locals are pushed/popped per block, so an inner declaration shadows an outer
 * one and disappears at its `}`. Constructor/destructor/`implements` method
 * bodies are walked with `self` bound to the enclosing struct. */
static void resolve_receivers(BuildCtx *c, Scope *s, CxNode *n) {
    if (!n) return;

    if (n->kind == CX_NODE_UNPACK) {
        /* Non-block statement position (e.g. an `if` body): wrap the lowered
         * declarations so they stay scoped to that statement. Direct block
         * children are spliced below so their targets remain visible after. */
        CxVec stmts = lower_unpack(c, s, n);
        n->kind = CX_NODE_BLOCK;
        n->children = stmts;
        n->init = NULL;
        n->params = cx_vec_empty();
    }

    if (n->kind == CX_NODE_STRUCT) {
        /* Fields must not leak into the local scope; only the method bodies are
         * walked, with `self` bound. */
        const char *self = cx_arena_printf(c->arena, "%s *", n->name);
        for (int i = 0; i < n->children.len; i++) {
            CxNode *m = n->children.items[i];
            if (m->kind == CX_NODE_STRUCT) {
                resolve_receivers(c, s, m); /* nested: its own `self` */
            } else if (m->kind == CX_NODE_CONSTRUCTOR ||
                       m->kind == CX_NODE_DESTRUCTOR) {
                walk_method_body(c, s, m, self);
            } else if (m->kind == CX_NODE_IMPLEMENTS) {
                for (int k = 0; k < m->children.len; k++) {
                    CxNode *im = m->children.items[k];
                    if (im->kind == CX_NODE_IMPL_METHOD)
                        walk_method_body(c, s, im, self);
                }
            }
        }
        return;
    }

    if (n->kind == CX_NODE_BLOCK) {
        /* Block scope: locals declared here (or in a nested block) are popped
         * on exit, so an inner declaration shadowing an outer one does not leak
         * past its block. An unpack statement is spliced into this block so its
         * targets are visible to the following statements. */
        int save = s->len;
        CxVec out = cx_vec_empty();
        int changed = 0;
        for (int i = 0; i < n->children.len; i++) {
            CxNode *ch = n->children.items[i];
            if (ch->kind == CX_NODE_UNPACK) {
                CxVec stmts = lower_unpack(c, s, ch);
                for (int k = 0; k < stmts.len; k++) {
                    cx_vec_push(c->arena, &out, stmts.items[k]);
                    resolve_receivers(c, s, stmts.items[k]);
                }
                changed = 1;
            } else {
                cx_vec_push(c->arena, &out, ch);
                resolve_receivers(c, s, ch);
            }
        }
        if (changed) n->children = out;
        s->len = save;
        return;
    }

    if (n->kind == CX_NODE_FUNCTION) {
        int save = s->len;
        for (int i = 0; i < n->params.len; i++) {
            CxNode *p = n->params.items[i];
            if (p->name) scope_push(s, p->name, p->type);
        }
        resolve_receivers(c, s, n->body);
        s->len = save;
        return;
    }

    if (n->kind == CX_NODE_VAR_DECL) {
        for (int i = 0; i < n->children.len; i++) {
            CxNode *d = n->children.items[i];
            if (d->name) scope_push(s, d->name, n->type);
        }
    }

    if (n->kind == CX_NODE_EXPR_CALL && n->lhs && n->name) {
        /* Resolve the receiver first: for a chained call `a.b().c()` the inner
         * call must become `Base__b(&a)` before the outer receiver's type can
         * be read off its return type. This also handles computed receivers
         * such as `make().method()` or `arr[i].method()`. */
        resolve_receivers(c, s, n->lhs);
        const char *rt = expr_type(c, s, n->lhs);
        if (rt) {
            char base[128];
            type_base(rt, base, sizeof base);
            char *fname = cx_arena_printf(c->arena, "%s__%s", base, n->name);
            if (cx_sema_find_function(c->table, fname)) {
                CxVec args = cx_vec_empty();
                if (strchr(rt, '*')) {
                    cx_vec_push(c->arena, &args, n->lhs); /* pointer receiver */
                } else {
                    CxNode *addr =
                        cx_node_new(c->arena, CX_NODE_EXPR_UNARY, n->lhs->span);
                    addr->name = "&";
                    addr->lhs = n->lhs;
                    cx_vec_push(c->arena, &args, addr);
                }
                for (int i = 0; i < n->children.len; i++)
                    cx_vec_push(c->arena, &args, n->children.items[i]);
                n->kind = CX_NODE_EXPR_CALL;
                n->name = fname;
                n->children = args;
                n->lhs = NULL; /* now an ordinary call */
            }
        }
    }

    resolve_receivers(c, s, n->lhs);
    resolve_receivers(c, s, n->rhs);
    resolve_receivers(c, s, n->init);
    resolve_receivers(c, s, n->body);
    resolve_receivers(c, s, n->cond);
    resolve_receivers(c, s, n->then);
    resolve_receivers(c, s, n->els);
    resolve_receivers(c, s, n->incr);
    for (int i = 0; i < n->children.len; i++)
        resolve_receivers(c, s, n->children.items[i]);
    for (int i = 0; i < n->params.len; i++)
        resolve_receivers(c, s, n->params.items[i]);
}

/* The `auto`-inference + receiver-resolution sequence `cx_sema_build` runs. */
void cx_sema_receivers_pass(BuildCtx *c, CxNode *tu) {
    /* A silent `auto` pass first fills in the locals it can type (so a receiver
     * declared `auto` gets its concrete type), then receivers are resolved,
     * then a reporting pass infers any `auto` that only became resolvable once
     * a method call was lowered (e.g. `auto x = l.pop();`). */
    infer_auto(c, tu, NULL, 0);
    Scope scope = {c->arena, NULL, 0, 0};
    resolve_receivers(c, &scope, tu);
    infer_auto(c, tu, NULL, 1);
}

/* ------------------------------------------------------ LSP receiver lookup */

/* See symbols.h. Types an already-resolved expression for computed receivers. */
const char *cx_sema_expr_type(CxArena *arena, const CxSymbolTable *table,
                              CxNode *expr) {
    if (!arena || !expr) return NULL;
    BuildCtx c;
    c.arena = arena;
    c.table = (CxSymbolTable *)table;
    c.diags = NULL;
    Scope s = {arena, NULL, 0, 0};
    return expr_type(&c, &s, expr);
}

static int pos_in_span(CxSpan s, int line, int col) {
    if (s.first_line <= 0) return 0;
    if (line < s.first_line || line > s.last_line) return 0;
    if (line == s.first_line && col < s.first_column) return 0;
    if (line == s.last_line && col > s.last_column) return 0;
    return 1;
}

/* The innermost body owner (`function`, `constructor`, `destructor`, or an
 * `implements` method) whose span contains the 1-based position, or NULL. Its
 * parameters and locals are the ones visible at the position. */
static CxNode *innermost_body_owner(CxNode *n, int line, int col) {
    if (!n) return NULL;
    if ((n->kind == CX_NODE_FUNCTION || n->kind == CX_NODE_CONSTRUCTOR ||
         n->kind == CX_NODE_DESTRUCTOR || n->kind == CX_NODE_IMPL_METHOD) &&
        pos_in_span(n->span, line, col))
        return n;
    CxNode *kids[8] = {n->lhs, n->rhs,  n->init, n->body,
                       n->cond, n->then, n->els, n->incr};
    for (int i = 0; i < 8; i++) {
        CxNode *r = innermost_body_owner(kids[i], line, col);
        if (r) return r;
    }
    for (int i = 0; i < n->children.len; i++) {
        CxNode *r = innermost_body_owner(n->children.items[i], line, col);
        if (r) return r;
    }
    for (int i = 0; i < n->params.len; i++) {
        CxNode *r = innermost_body_owner(n->params.items[i], line, col);
        if (r) return r;
    }
    return NULL;
}

/* The name of the struct whose constructor/destructor/`implements` method body
 * contains the 1-based position (so `self` can be typed), or NULL. */
static const char *enclosing_struct_for_self(CxNode *n, int line, int col,
                                             const char *cur) {
    if (!n) return NULL;
    if (n->kind == CX_NODE_STRUCT) cur = n->name;
    if ((n->kind == CX_NODE_IMPL_METHOD || n->kind == CX_NODE_CONSTRUCTOR ||
         n->kind == CX_NODE_DESTRUCTOR) &&
        pos_in_span(n->span, line, col))
        return cur;
    CxNode *kids[8] = {n->lhs, n->rhs,  n->init, n->body,
                       n->cond, n->then, n->els, n->incr};
    for (int i = 0; i < 8; i++) {
        const char *r = enclosing_struct_for_self(kids[i], line, col, cur);
        if (r) return r;
    }
    for (int i = 0; i < n->children.len; i++) {
        const char *r =
            enclosing_struct_for_self(n->children.items[i], line, col, cur);
        if (r) return r;
    }
    for (int i = 0; i < n->params.len; i++) {
        const char *r =
            enclosing_struct_for_self(n->params.items[i], line, col, cur);
        if (r) return r;
    }
    return NULL;
}

/* Walk a statement subtree accumulating the bindings visible at the 1-based
 * (line, col), honouring block scope: a nested block's locals are popped on exit,
 * and statements starting after the cursor are ignored. */
static void collect_visible(CxNode *n, int line, int col, Scope *s) {
    if (!n) return;
    if (n->kind == CX_NODE_BLOCK) {
        int save = s->len;
        /* Keep this block's bindings when the cursor is inside it; pop them when
         * the whole block is before the cursor (its scope has closed). */
        int within =
            (line > n->span.first_line ||
             (line == n->span.first_line && col >= n->span.first_column)) &&
            (line < n->span.last_line ||
             (line == n->span.last_line && col <= n->span.last_column));
        for (int i = 0; i < n->children.len; i++) {
            CxNode *st = n->children.items[i];
            if (st->span.first_line > line ||
                (st->span.first_line == line && st->span.first_column > col))
                continue; /* declared after the cursor */
            collect_visible(st, line, col, s);
        }
        if (!within) s->len = save;
        return;
    }
    if (n->kind == CX_NODE_VAR_DECL) {
        for (int i = 0; i < n->children.len; i++) {
            CxNode *d = n->children.items[i];
            if (d->name) scope_push(s, d->name, n->type);
        }
        return;
    }
    /* Descend into nested statement positions (if/while/for bodies). */
    if (n->then) collect_visible(n->then, line, col, s);
    if (n->els) collect_visible(n->els, line, col, s);
    if (n->body) collect_visible(n->body, line, col, s);
}

const char *cx_sema_receiver_type(CxArena *arena, CxNode *tu, int line,
                                  int column, const char *name) {
    if (!tu || !name) return NULL;
    /* `self` inside a constructor/destructor/`implements` method body is the
     * enclosing struct. */
    if (strcmp(name, "self") == 0)
        return enclosing_struct_for_self(tu, line, column, NULL);
    CxNode *fn = innermost_body_owner(tu, line, column);
    if (!fn || !arena) return NULL;
    Scope s = {arena, NULL, 0, 0};
    for (int i = 0; i < fn->params.len; i++) {
        CxNode *p = fn->params.items[i];
        if (p->name && p->type) scope_push(&s, p->name, p->type);
    }
    /* Walk the body's top-level statements individually: passing the body block
     * to `collect_visible` would pop its own bindings on exit (block scoping),
     * leaving no locals to look up. Nested blocks still pop as they should. */
    CxNode *body = fn->body;
    if (body && body->kind == CX_NODE_BLOCK) {
        for (int i = 0; i < body->children.len; i++) {
            CxNode *st = body->children.items[i];
            if (st->span.first_line > line ||
                (st->span.first_line == line && st->span.first_column > column))
                continue; /* declared after the cursor */
            collect_visible(st, line, column, &s);
        }
    } else {
        collect_visible(body, line, column, &s);
    }
    return scope_lookup(&s, name);
}
