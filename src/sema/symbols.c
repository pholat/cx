#include "cx/symbols.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    CxArena       *arena;
    CxSymbolTable *table;
    CxDiagList    *diags;
} BuildCtx;

static void table_push(CxSymbolTable *t, CxSymbol *s) {
    if (t->len == t->cap) {
        int cap = t->cap ? t->cap * 2 : 16;
        CxSymbol **items =
            (CxSymbol **)realloc(t->items, sizeof(CxSymbol *) * (size_t)cap);
        if (!items) return;
        t->items = items;
        t->cap = cap;
    }
    t->items[t->len++] = s;
}

static char *join_ns(CxArena *a, const char *ns, const char *name) {
    if (!ns || !*ns) return cx_arena_strdup(a, name);
    return cx_arena_printf(a, "%s::%s", ns, name);
}

static CxSymbol *symbol_new(CxArena *a, CxNode *n, const char *ns,
                            CxNodeKind kind, const char *file) {
    CxSymbol *s = (CxSymbol *)cx_arena_alloc(a, sizeof(CxSymbol));
    if (!s) return NULL;
    s->name = n->name;
    s->ns = (ns && *ns) ? cx_arena_strdup(a, ns) : NULL;
    s->qualified = join_ns(a, ns, n->name);
    s->type = n->type;
    s->kind = kind;
    s->span = n->span;
    s->file = file;
    s->decl = n;
    s->dtor = NULL;
    s->ctor = NULL;
    return s;
}

static void walk(BuildCtx *c, CxNode *n, const char *ns, const char *file) {
    if (!n) return;
    switch (n->kind) {
    case CX_NODE_TRANSLATION_UNIT:
    case CX_NODE_NAMESPACE:
        for (int i = 0; i < n->children.len; i++) {
            CxNode *ch = n->children.items[i];
            if (ch->kind == CX_NODE_NAMESPACE)
                walk(c, ch, join_ns(c->arena, ns, ch->name), file);
            else
                walk(c, ch, ns, file);
        }
        break;

    case CX_NODE_STRUCT: {
        CxSymbol *s = symbol_new(c->arena, n, ns, CX_NODE_STRUCT, file);
        if (!s) return;
        table_push(c->table, s);
        for (int i = 0; i < n->children.len; i++) {
            CxNode *m = n->children.items[i];
            if (m->kind == CX_NODE_DESTRUCTOR) {
                if (m->name && n->name && strcmp(m->name, n->name) != 0) {
                    /* A destructor must be named after its struct, otherwise the
                     * generated `cleanup` helper would not belong to the type. */
                    cx_error(c->diags, m->span,
                             "destructor name '~%s' does not match struct '%s'",
                             m->name, n->name);
                } else if (s->dtor) {
                    cx_error(c->diags, m->span,
                             "duplicate destructor for struct '%s'", n->name);
                } else {
                    s->dtor = m;
                }
            } else if (m->kind == CX_NODE_CONSTRUCTOR) {
                if (m->name && n->name && strcmp(m->name, n->name) != 0) {
                    /* C++-style: the constructor is the member named after the
                     * struct, mirroring the destructor rule. */
                    cx_error(c->diags, m->span,
                             "constructor name '%s' does not match struct '%s'",
                             m->name, n->name);
                } else if (s->ctor) {
                    cx_error(c->diags, m->span,
                             "duplicate constructor for struct '%s'", n->name);
                } else {
                    s->ctor = m;
                }
            } else if (m->kind == CX_NODE_STRUCT) {
                /* Nested type: its C tag is <Outer>__<Inner>, so qualify it
                 * with the containing struct's name. */
                walk(c, m, join_ns(c->arena, ns, n->name), file);
            } else if (m->kind == CX_NODE_VAR_DECL) {
                /* Struct fields are resolved structurally by the emitter; the
                 * symbol table does not need them. */
            }
        }
        break;
    }

    case CX_NODE_FUNCTION: {
        CxSymbol *s = symbol_new(c->arena, n, ns, CX_NODE_FUNCTION, file);
        table_push(c->table, s);
        break;
    }

    case CX_NODE_TRAIT: {
        /* A trait is a named set of required member signatures; the node's
         * children are the TRAIT_METHODs. Codegen and the conformance pass
         * look it up by name. */
        CxSymbol *s = symbol_new(c->arena, n, ns, CX_NODE_TRAIT, file);
        table_push(c->table, s);
        break;
    }

    case CX_NODE_VAR_DECL: {
        CxSymbol *s = symbol_new(c->arena, n, ns, CX_NODE_VAR_DECL, file);
        table_push(c->table, s);
        break;
    }

    default:
        break;
    }
}

/* Second pass: a call whose callee names a struct is a constructor call, so the
 * struct must actually declare a constructor. This also catches `T v(args);`
 * written for a type that has no constructor. */
static void check_calls(BuildCtx *c, CxNode *n) {
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

/* ------------------------------------------------------------- traits */

static CxNode *find_impl_method(CxNode *impl_block, const char *name) {
    if (!impl_block || !name) return NULL;
    for (int i = 0; i < impl_block->children.len; i++) {
        CxNode *m = impl_block->children.items[i];
        if (m->kind == CX_NODE_IMPL_METHOD && m->name && !strcmp(m->name, name))
            return m;
    }
    return NULL;
}

static int is_receiver(const CxNode *p) {
    return p && p->name && strcmp(p->name, "self") == 0;
}

/* Compare a required trait method with an implementation, ignoring the leading
 * `self` receiver (whose concrete type differs by design). */
static int same_signature(CxNode *tm, CxNode *im) {
    const char *ta = tm->type ? tm->type : "void";
    const char *tb = im->type ? im->type : "void";
    if (strcmp(ta, tb) != 0) return 0;

    int ta0 = tm->params.len > 0 && is_receiver(tm->params.items[0]) ? 1 : 0;
    int ib0 = im->params.len > 0 && is_receiver(im->params.items[0]) ? 1 : 0;
    if (tm->params.len - ta0 != im->params.len - ib0) return 0;
    for (int i = 0; i < tm->params.len - ta0; i++) {
        const char *a = tm->params.items[ta0 + i]->type;
        const char *b = im->params.items[ib0 + i]->type;
        if (strcmp(a ? a : "void", b ? b : "void") != 0) return 0;
    }
    return 1;
}

/* Static conformance: every method a trait requires must be present in the
 * struct's `implements` block with a matching signature. */
static void check_implements(BuildCtx *c, CxNode *n) {
    if (!n) return;
    if (n->kind == CX_NODE_STRUCT) {
        for (int i = 0; i < n->children.len; i++) {
            CxNode *m = n->children.items[i];
            if (m->kind != CX_NODE_IMPLEMENTS) continue;
            CxSymbol *tr = cx_sema_find_trait(c->table, m->name);
            if (!tr) {
                cx_error(c->diags, m->span, "unknown trait '%s'", m->name);
                continue;
            }
            CxNode *decl = tr->decl;
            for (int k = 0; decl && k < decl->children.len; k++) {
                CxNode *tm = decl->children.items[k];
                if (tm->kind != CX_NODE_TRAIT_METHOD) continue;
                CxNode *im = find_impl_method(m, tm->name);
                if (!im)
                    cx_error(c->diags, m->span,
                             "struct '%s' does not implement '%s' of trait '%s'",
                             n->name, tm->name, m->name);
                else if (!same_signature(tm, im))
                    cx_error(c->diags, im->span,
                             "signature of '%s' does not match trait '%s'",
                             tm->name, m->name);
            }
        }
    }
    check_implements(c, n->lhs);
    check_implements(c, n->rhs);
    check_implements(c, n->init);
    check_implements(c, n->body);
    check_implements(c, n->cond);
    check_implements(c, n->then);
    check_implements(c, n->els);
    check_implements(c, n->incr);
    for (int i = 0; i < n->children.len; i++)
        check_implements(c, n->children.items[i]);
    for (int i = 0; i < n->params.len; i++)
        check_implements(c, n->params.items[i]);
}

/* Infer the type of an initializer for `auto`. */
static const char *infer_type(BuildCtx *c, CxNode *init) {
    if (!init) return NULL;
    switch (init->kind) {
    case CX_NODE_EXPR_CALL: {
        CxSymbol *fn = cx_sema_find_function(c->table, init->name);
        if (fn && fn->type) return fn->type;
        CxSymbol *st = cx_sema_find_struct(c->table, init->name);
        if (st) return st->name; /* constructor call */
        return NULL;
    }
    case CX_NODE_EXPR_INT:    return "int";
    case CX_NODE_EXPR_STRING: return "char *";
    case CX_NODE_EXPR_PAREN:  return infer_type(c, init->lhs);
    case CX_NODE_EXPR_CAST:   return init->type;
    case CX_NODE_EXPR_IDENT: {
        CxSymbol *s = cx_sema_find_symbol(c->table, init->name);
        return s ? s->type : NULL;
    }
    default:
        return NULL;
    }
}

/* Rewrite `auto` declarations to the inferred type. Walks the whole tree so it
 * covers locals inside function bodies. */
static void infer_auto(BuildCtx *c, CxNode *n) {
    if (!n) return;
    if (n->kind == CX_NODE_VAR_DECL && n->type && strcmp(n->type, "auto") == 0) {
        const char *t = NULL;
        for (int i = 0; i < n->children.len && !t; i++)
            t = infer_type(c, n->children.items[i]->init);
        if (t) n->type = (char *)t;
        else cx_error(c->diags, n->span, "cannot infer type for 'auto'");
    }
    infer_auto(c, n->lhs);
    infer_auto(c, n->rhs);
    infer_auto(c, n->init);
    infer_auto(c, n->body);
    infer_auto(c, n->cond);
    infer_auto(c, n->then);
    infer_auto(c, n->els);
    infer_auto(c, n->incr);
    for (int i = 0; i < n->children.len; i++) infer_auto(c, n->children.items[i]);
    for (int i = 0; i < n->params.len; i++) infer_auto(c, n->params.items[i]);
}

CxSymbolTable *cx_sema_build(CxArena *arena, CxNode *tu, CxDiagList *diags) {
    CxSymbolTable *table =
        (CxSymbolTable *)cx_arena_alloc(arena, sizeof(CxSymbolTable));
    if (!table) return NULL;
    BuildCtx c = {arena, table, diags};
    walk(&c, tu, NULL, tu ? tu->file : NULL);
    for (int i = 0; tu && i < tu->imports.len; i++)
        walk(&c, tu->imports.items[i], NULL, tu->imports.items[i]->file);
    infer_auto(&c, tu);
    check_calls(&c, tu);
    check_implements(&c, tu);
    return table;
}

CxSymbol *cx_sema_lookup(const CxSymbolTable *t, const char *qualified) {
    if (!t || !qualified) return NULL;
    for (int i = 0; i < t->len; i++)
        if (t->items[i]->qualified && !strcmp(t->items[i]->qualified, qualified))
            return t->items[i];
    return NULL;
}

static CxSymbol *find_by_kind(const CxSymbolTable *t, const char *name,
                              CxNodeKind kind) {
    if (!t || !name) return NULL;
    for (int i = 0; i < t->len; i++)
        if (t->items[i]->kind == kind && t->items[i]->name &&
            !strcmp(t->items[i]->name, name))
            return t->items[i];
    return NULL;
}

CxSymbol *cx_sema_find_struct(const CxSymbolTable *t, const char *name) {
    return find_by_kind(t, name, CX_NODE_STRUCT);
}

CxSymbol *cx_sema_find_function(const CxSymbolTable *t, const char *name) {
    return find_by_kind(t, name, CX_NODE_FUNCTION);
}

CxSymbol *cx_sema_find_trait(const CxSymbolTable *t, const char *name) {
    return find_by_kind(t, name, CX_NODE_TRAIT);
}

CxSymbol *cx_sema_find_symbol(const CxSymbolTable *t, const char *name) {
    if (!t || !name) return NULL;
    for (int i = 0; i < t->len; i++)
        if (t->items[i]->name && !strcmp(t->items[i]->name, name))
            return t->items[i];
    return NULL;
}

/* Strip trailing pointer/array decorations from a type so "File *" -> "File". */
static void type_base(const char *type, char *buf, size_t n) {
    size_t i = 0;
    while (type[i] && type[i] != ' ' && type[i] != '*' && type[i] != '[' &&
           i + 1 < n) {
        buf[i] = type[i];
        i++;
    }
    buf[i] = '\0';
}

CxSymbol *cx_sema_dtor_for(const CxSymbolTable *t, const char *type) {
    if (!type) return NULL;
    char base[128];
    type_base(type, base, sizeof base);
    CxSymbol *s = cx_sema_find_struct(t, base);
    if (s && s->dtor) return s;
    return NULL;
}

int cx_sema_type_has_dtor(const CxSymbolTable *t, const char *type) {
    return cx_sema_dtor_for(t, type) != NULL;
}
