/* Cx semantic analysis — trait conformance and checked `requires`.
 *
 * `check_implements` verifies every method an `implements` block owes a trait,
 * and `check_macro_requires` verifies the `requires` bindings a `macro_rules!`
 * expansion recorded (structs, primitives and function/method bindings). */
#include "cx/symbols.h"
#include "cx/macro.h"
#include "sema_internal.h"

#include <string.h>

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
void check_implements(BuildCtx *c, CxNode *n) {
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

/* Does struct declaration `decl` contain an `implements <trait>` block? */
static int struct_implements_trait(const CxNode *decl, const char *trait) {
    if (!decl || !trait) return 0;
    for (int i = 0; i < decl->children.len; i++) {
        CxNode *m = decl->children.items[i];
        if (m->kind == CX_NODE_IMPLEMENTS && m->name &&
            !strcmp(m->name, trait))
            return 1;
    }
    return 0;
}

/* Is `name` a single identifier (no qualifier, pointer or array decoration)? */
static int is_bare_ident(const char *name) {
    if (!name || !*name) return 0;
    for (const char *p = name; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '_'))
            return 0;
    return 1;
}

/* Does trait `tr` require a method named `name`? */
static int trait_has_method(const CxSymbol *tr, const char *name) {
    if (!tr || !tr->decl || !name) return 0;
    for (int i = 0; i < tr->decl->children.len; i++) {
        CxNode *m = tr->decl->children.items[i];
        if (m->kind == CX_NODE_TRAIT_METHOD && m->name && !strcmp(m->name, name))
            return 1;
    }
    return 0;
}

/* The built-in type keywords. A primitive type's words are all drawn from this
 * set (`int`, `unsigned int`, `const char`, `float *`, …); `const`/`volatile`
 * are qualifiers. */
static int builtin_type_word(const char *w, size_t n) {
    static const char *const words[] = {"void",     "char",   "short",  "int",
                                        "long",     "float",  "double", "signed",
                                        "unsigned", "bool",   "const",
                                        "volatile", NULL};
    for (int i = 0; words[i]; i++)
        if (strlen(words[i]) == n && strncmp(words[i], w, n) == 0) return 1;
    return 0;
}

/* True if `type` names a built-in (primitive) Cx type — every word is a built-in
 * type keyword and at least one is present. Used so a `requires` bound on a
 * primitive is checked against the built-in capability traits rather than
 * skipped. */
static int is_primitive_type(const char *type) {
    if (!type || !*type) return 0;
    int any = 0;
    const char *p = type;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '[') break;
        if (*p == '*' || *p == '&') {
            p++;
            continue;
        }
        const char *s = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '*' && *p != '&' &&
               *p != '[')
            p++;
        if (!builtin_type_word(s, (size_t)(p - s))) return 0;
        any = 1;
    }
    return any;
}

/* The capability traits every primitive type implements. A fixed set; the names
 * match the `requires` examples in the docs. */
static int primitive_impls_trait(const char *trait) {
    return trait && (!strcmp(trait, "Hashable") || !strcmp(trait, "Equatable") ||
                     !strcmp(trait, "Comparable") || !strcmp(trait, "Ordered"));
}

/* Checked `requires`: for every requirement recorded at a `macro_rules!`
 * invocation, verify conformance.
 *
 * * A concrete type that resolves to a *struct* must have a matching
 *   `implements` block.
 * * A *primitive* type implements a fixed set of built-in capability traits
 *   (`Hashable`, `Equatable`, `Comparable`, `Ordered`) — so `requires $k:
 *   Hashable` on `int` passes and `requires $k: Drop` on `int` fails.
 * * An `ident`/`path` metavariable that names no struct is a *function* binding:
 *   it must name one of the trait's required methods (e.g. `requires $a: A` for
 *   the function `a` passed as A's method).
 * * Anything else (a C type Cx does not know) is not checkable and is skipped.
 *
 * The required trait itself is always checked for existence. */
void check_macro_requires(BuildCtx *c, CxNode *tu) {
    if (!tu || !tu->macro_map) return;
    CxMacroMap *map = tu->macro_map;
    for (int i = 0; i < map->nreqs; i++) {
        CxMacroReq *r = &map->reqs[i];
        if (!r->type || !r->trait) continue;
        CxSymbol *tr = cx_sema_find_trait(c->table, r->trait);
        if (!tr) {
            cx_error(c->diags, r->span,
                     "unknown trait '%s' in `requires` clause", r->trait);
            continue;
        }

        char base[128];
        type_base(r->type, base, sizeof base);

        CxSymbol *st = cx_sema_find_struct(c->table, base);
        if (st) {
            if (!struct_implements_trait(st->decl, r->trait))
                cx_error(c->diags, r->span,
                         "type '%s' bound to a `requires` metavariable does not "
                         "implement trait '%s'",
                         st->name, r->trait);
            continue;
        }

        int is_fn_frag = r->frag &&
                         (!strcmp(r->frag, "ident") || !strcmp(r->frag, "path"));
        if (is_fn_frag && is_bare_ident(r->type)) {
            if (!trait_has_method(tr, r->type))
                cx_error(c->diags, r->span,
                         "'%s' does not name a method of trait '%s'", r->type,
                         r->trait);
            continue;
        }
        /* A primitive type implements the built-in capability traits; anything
         * else (a C type Cx does not know) is not checkable (skip). */
        if (is_primitive_type(r->type)) {
            if (!primitive_impls_trait(r->trait))
                cx_error(c->diags, r->span,
                         "primitive type '%s' does not implement trait '%s'",
                         r->type, r->trait);
            continue;
        }
        /* otherwise: not checkable in this cut (skip) */
    }
}
