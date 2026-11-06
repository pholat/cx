#include "cx/ast.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define CX_ARENA_BLOCK (64 * 1024)
#define CX_ARENA_ALIGN 16

typedef struct CxArenaBlock {
    struct CxArenaBlock *next;
    size_t               used;
    size_t               cap;
    unsigned char        data[];
} CxArenaBlock;

struct CxArena {
    CxArenaBlock *head;
};

static size_t align_up(size_t n, size_t a) { return (n + (a - 1)) & ~(a - 1); }

CxArena *cx_arena_new(void) {
    return (CxArena *)calloc(1, sizeof(CxArena));
}

static CxArenaBlock *arena_block_new(CxArena *arena, size_t need) {
    size_t cap = CX_ARENA_BLOCK;
    if (need > cap) cap = align_up(need, CX_ARENA_ALIGN);
    CxArenaBlock *b = (CxArenaBlock *)malloc(sizeof(CxArenaBlock) + cap);
    if (!b) return NULL;
    b->next = arena->head;
    b->used = 0;
    b->cap = cap;
    arena->head = b;
    return b;
}

void *cx_arena_alloc(CxArena *arena, size_t size) {
    if (!arena) return NULL;
    size = align_up(size ? size : 1, CX_ARENA_ALIGN);
    CxArenaBlock *b = arena->head;
    if (!b || b->used + size > b->cap) {
        b = arena_block_new(arena, size);
        if (!b) return NULL;
    }
    void *p = b->data + b->used;
    b->used += size;
    memset(p, 0, size);
    return p;
}

void cx_arena_free(CxArena *arena) {
    if (!arena) return;
    CxArenaBlock *b = arena->head;
    while (b) {
        CxArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    free(arena);
}

char *cx_arena_strndup(CxArena *arena, const char *s, size_t n) {
    char *p = (char *)cx_arena_alloc(arena, n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char *cx_arena_strdup(CxArena *arena, const char *s) {
    return cx_arena_strndup(arena, s, strlen(s));
}

char *cx_arena_printf(CxArena *arena, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(ap2);
        return NULL;
    }
    char *p = (char *)cx_arena_alloc(arena, (size_t)n + 1);
    if (p) vsnprintf(p, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return p;
}

CxNode *cx_node_new(CxArena *arena, CxNodeKind kind, CxSpan span) {
    CxNode *n = (CxNode *)cx_arena_alloc(arena, sizeof(CxNode));
    if (!n) return NULL;
    n->kind = kind;
    n->span = span;
    return n;
}

CxVec cx_vec_empty(void) {
    CxVec v = {NULL, 0, 0};
    return v;
}

void cx_vec_push(CxArena *arena, CxVec *vec, CxNode *node) {
    if (!vec || !node) return;
    if (vec->len == vec->cap) {
        int cap = vec->cap ? vec->cap * 2 : 8;
        CxNode **items =
            (CxNode **)cx_arena_alloc(arena, sizeof(CxNode *) * (size_t)cap);
        if (!items) return;
        if (vec->items && vec->len)
            memcpy(items, vec->items, sizeof(CxNode *) * (size_t)vec->len);
        vec->items = items;
        vec->cap = cap;
    }
    vec->items[vec->len++] = node;
}

const char *cx_node_kind_name(CxNodeKind kind) {
    switch (kind) {
    case CX_NODE_TRANSLATION_UNIT: return "translation_unit";
    case CX_NODE_NAMESPACE:        return "namespace";
    case CX_NODE_STRUCT:           return "struct";
    case CX_NODE_DESTRUCTOR:       return "destructor";
    case CX_NODE_CONSTRUCTOR:      return "constructor";
    case CX_NODE_FUNCTION:         return "function";
    case CX_NODE_PARAM:            return "param";
    case CX_NODE_VAR_DECL:         return "var_decl";
    case CX_NODE_BLOCK:            return "block";
    case CX_NODE_RETURN:           return "return";
    case CX_NODE_EXPR_STMT:        return "expr_stmt";
    case CX_NODE_EXPR_IDENT:       return "ident";
    case CX_NODE_EXPR_INT:         return "int";
    case CX_NODE_EXPR_STRING:      return "string";
    case CX_NODE_EXPR_CALL:        return "call";
    case CX_NODE_EXPR_FIELD:       return "field";
    case CX_NODE_EXPR_ARROW:       return "arrow";
    case CX_NODE_EXPR_INDEX:       return "index";
    case CX_NODE_EXPR_CAST:        return "cast";
    case CX_NODE_EXPR_SIZEOF:      return "sizeof";
    case CX_NODE_EXPR_BINARY:      return "binary";
    case CX_NODE_EXPR_UNARY:       return "unary";
    case CX_NODE_EXPR_ASSIGN:      return "assign";
    case CX_NODE_EXPR_PAREN:       return "paren";
    case CX_NODE_EXPR_POSTFIX:     return "postfix";
    case CX_NODE_EMPTY:            return "empty";
    case CX_NODE_IF:               return "if";
    case CX_NODE_WHILE:            return "while";
    case CX_NODE_FOR:              return "for";
    case CX_NODE_BREAK:            return "break";
    case CX_NODE_CONTINUE:         return "continue";
    case CX_NODE_SWITCH:           return "switch";
    case CX_NODE_CASE:             return "case";
    case CX_NODE_DEFAULT:          return "default";
    case CX_NODE_LABEL:            return "label";
    case CX_NODE_GOTO:             return "goto";
    case CX_NODE_TYPEDEF:          return "typedef";
    case CX_NODE_RAW:              return "raw";
    case CX_NODE_MACRO:            return "macro";
    case CX_NODE_TRAIT:            return "trait";
    case CX_NODE_TRAIT_METHOD:     return "trait_method";
    case CX_NODE_IMPLEMENTS:       return "implements";
    case CX_NODE_IMPL_METHOD:      return "impl_method";
    case CX_NODE_ERROR:            return "error";
    }
    return "?";
}

static void dump_indent(FILE *out, int depth) {
    for (int i = 0; i < depth; i++) fputs("  ", out);
}

void cx_node_dump(const CxNode *n, FILE *out, int depth) {
    if (!n) return;
    dump_indent(out, depth);
    fprintf(out, "%s", cx_node_kind_name(n->kind));
    if (n->name) fprintf(out, " name=%s", n->name);
    if (n->type) fprintf(out, " type=%s", n->type);
    fprintf(out, " [%d:%d-%d:%d]\n", n->span.first_line, n->span.first_column,
            n->span.last_line, n->span.last_column);

    for (int i = 0; i < n->params.len; i++) {
        dump_indent(out, depth + 1);
        fputs("param:\n", out);
        cx_node_dump(n->params.items[i], out, depth + 2);
    }
    if (n->init) {
        dump_indent(out, depth + 1);
        fputs("init:\n", out);
        cx_node_dump(n->init, out, depth + 2);
    }
    if (n->lhs) {
        dump_indent(out, depth + 1);
        fputs("lhs:\n", out);
        cx_node_dump(n->lhs, out, depth + 2);
    }
    if (n->rhs) {
        dump_indent(out, depth + 1);
        fputs("rhs:\n", out);
        cx_node_dump(n->rhs, out, depth + 2);
    }
    if (n->body) {
        dump_indent(out, depth + 1);
        fputs("body:\n", out);
        cx_node_dump(n->body, out, depth + 2);
    }
    for (int i = 0; i < n->children.len; i++) {
        dump_indent(out, depth + 1);
        fprintf(out, "child[%d]:\n", i);
        cx_node_dump(n->children.items[i], out, depth + 2);
    }
}
