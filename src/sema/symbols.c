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

static CxSymbol *symbol_new(CxArena *a, CxNode *n, const char *ns, CxNodeKind kind) {
    CxSymbol *s = (CxSymbol *)cx_arena_alloc(a, sizeof(CxSymbol));
    if (!s) return NULL;
    s->name = n->name;
    s->ns = (ns && *ns) ? cx_arena_strdup(a, ns) : NULL;
    s->qualified = join_ns(a, ns, n->name);
    s->type = n->type;
    s->kind = kind;
    s->span = n->span;
    s->decl = n;
    s->dtor = NULL;
    s->ctor = NULL;
    return s;
}

static void walk(BuildCtx *c, CxNode *n, const char *ns) {
    if (!n) return;
    switch (n->kind) {
    case CX_NODE_TRANSLATION_UNIT:
    case CX_NODE_NAMESPACE:
        for (int i = 0; i < n->children.len; i++) {
            CxNode *ch = n->children.items[i];
            if (ch->kind == CX_NODE_NAMESPACE)
                walk(c, ch, join_ns(c->arena, ns, ch->name));
            else
                walk(c, ch, ns);
        }
        break;

    case CX_NODE_STRUCT: {
        CxSymbol *s = symbol_new(c->arena, n, ns, CX_NODE_STRUCT);
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
                walk(c, m, join_ns(c->arena, ns, n->name));
            } else if (m->kind == CX_NODE_VAR_DECL) {
                symbol_new(c->arena, m, ns, CX_NODE_VAR_DECL); /* register */
            }
        }
        break;
    }

    case CX_NODE_FUNCTION: {
        CxSymbol *s = symbol_new(c->arena, n, ns, CX_NODE_FUNCTION);
        table_push(c->table, s);
        break;
    }

    case CX_NODE_VAR_DECL: {
        CxSymbol *s = symbol_new(c->arena, n, ns, CX_NODE_VAR_DECL);
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
    for (int i = 0; i < n->children.len; i++) check_calls(c, n->children.items[i]);
    for (int i = 0; i < n->params.len; i++) check_calls(c, n->params.items[i]);
}

CxSymbolTable *cx_sema_build(CxArena *arena, CxNode *tu, CxDiagList *diags) {
    CxSymbolTable *table =
        (CxSymbolTable *)cx_arena_alloc(arena, sizeof(CxSymbolTable));
    if (!table) return NULL;
    BuildCtx c = {arena, table, diags};
    walk(&c, tu, NULL);
    check_calls(&c, tu);
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
