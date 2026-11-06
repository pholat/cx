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
                if (s->dtor)
                    cx_error(c->diags, m->span,
                             "duplicate destructor for struct '%s'", n->name);
                else
                    s->dtor = m;
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

CxSymbolTable *cx_sema_build(CxArena *arena, CxNode *tu, CxDiagList *diags) {
    CxSymbolTable *table =
        (CxSymbolTable *)cx_arena_alloc(arena, sizeof(CxSymbolTable));
    if (!table) return NULL;
    BuildCtx c = {arena, table, diags};
    walk(&c, tu, NULL);
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
