#ifndef CX_AST_H
#define CX_AST_H

#include <stddef.h>
#include <stdio.h>

#include "cx/span.h"

/* ------------------------------------------------------------------ arena */
/* Every node, string and list lives in an arena so a whole document parse can
 * be discarded in one shot. This is what makes the LSP request cycle cheap. */
typedef struct CxArena CxArena;

CxArena *cx_arena_new(void);
void       cx_arena_free(CxArena *arena);
void      *cx_arena_alloc(CxArena *arena, size_t size);
char      *cx_arena_strdup(CxArena *arena, const char *s);
char      *cx_arena_strndup(CxArena *arena, const char *s, size_t n);
char      *cx_arena_printf(CxArena *arena, const char *fmt, ...);

/* -------------------------------------------------------------- node kinds */
typedef enum {
    CX_NODE_TRANSLATION_UNIT,
    CX_NODE_NAMESPACE,
    CX_NODE_STRUCT,
    CX_NODE_DESTRUCTOR,
    CX_NODE_CONSTRUCTOR,
    CX_NODE_FUNCTION,
    CX_NODE_PARAM,
    CX_NODE_VAR_DECL,
    CX_NODE_BLOCK,
    CX_NODE_RETURN,
    CX_NODE_EXPR_STMT,
    CX_NODE_EXPR_IDENT,
    CX_NODE_EXPR_INT,
    CX_NODE_EXPR_STRING,
    CX_NODE_EXPR_CALL,
    CX_NODE_EXPR_FIELD,
    CX_NODE_EXPR_ARROW,
    CX_NODE_EXPR_INDEX,
    CX_NODE_EXPR_CAST,
    CX_NODE_EXPR_SIZEOF,
    CX_NODE_EXPR_BINARY,
    CX_NODE_EXPR_UNARY,
    CX_NODE_EXPR_ASSIGN,
    CX_NODE_EXPR_PAREN,
    CX_NODE_EXPR_POSTFIX,
    CX_NODE_EMPTY,
    CX_NODE_IF,
    CX_NODE_WHILE,
    CX_NODE_FOR,
    CX_NODE_BREAK,
    CX_NODE_CONTINUE,
    CX_NODE_SWITCH,
    CX_NODE_CASE,
    CX_NODE_DEFAULT,
    CX_NODE_LABEL,
    CX_NODE_GOTO,
    CX_NODE_TYPEDEF,
    CX_NODE_ERROR
} CxNodeKind;

typedef struct CxNode CxNode;

typedef struct {
    CxNode **items;
    int      len;
    int      cap;
} CxVec;

struct CxNode {
    CxNodeKind kind;
    CxSpan     span;

    char *name;    /* identifier, operator, literal text, field name */
    char *type;    /* declared type text (e.g. "File" or "File *") */
    char *prelude; /* raw preprocessor directives, emitted verbatim at the top */
    char *file;    /* source path this node was parsed from (roots) */

    CxNode *lhs;   /* left operand / field object / for-init */
    CxNode *rhs;   /* right operand */
    CxNode *init;  /* variable initializer / return value */
    CxNode *body;  /* function / destructor block / loop body */

    CxNode *cond;  /* if / while / for condition */
    CxNode *then;  /* if then-branch */
    CxNode *els;   /* if else-branch */
    CxNode *incr;  /* for increment */

    int is_funcptr; /* declarator is `(*name)(params)`; params holds its types */

    CxVec children; /* TU + namespace decls, struct members, block stmts, call args */
    CxVec params;   /* function parameters */
    CxVec imports;  /* translation unit: imported module roots */
};

/* ----------------------------------------------------------------- helpers */
CxNode *cx_node_new(CxArena *arena, CxNodeKind kind, CxSpan span);
CxVec   cx_vec_empty(void);
void    cx_vec_push(CxArena *arena, CxVec *vec, CxNode *node);
const char *cx_node_kind_name(CxNodeKind kind);

/* Debug printer used by `cx dump`. */
void cx_node_dump(const CxNode *node, FILE *out, int depth);

#endif /* CX_AST_H */
