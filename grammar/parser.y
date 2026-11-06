%{
/* Bison grammar for the Cx language (namespaces + RAII struct destructors).
 *
 * Reusable by design: the parser is pure/reentrant, tracks locations for every
 * node, and performs full lookahead correction. Those three properties are
 * exactly what the LSP server needs, so codegen and the language server share
 * this single language definition.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cx/cx.h"
/* Bison's generated header provides YYSTYPE / YYLTYPE; including it here lets
 * us declare yylex with the exact signature the parser calls. */
#include "parser.h"

int yylex(YYSTYPE *yylval, YYLTYPE *yylloc, void *scanner);
void yyerror(YYLTYPE *locp, CxParseCtx *ctx, void *scanner, const char *msg);

/* Build a node with the location of the current rule. `loc` is written as
 * `(@$)`, which Bison rewrites to the rule's YYLTYPE before expansion. */
#define MK(kind, loc)                                                        \
    cx_node_new(ctx->arena, (kind),                                          \
                (CxSpan){(loc).first_line, (loc).first_column,               \
                         (loc).last_line, (loc).last_column})
%}

%define api.pure full
%define parse.error verbose
%define parse.lac full

%locations

%code requires {
#include "cx/parser.h"
}

%parse-param { CxParseCtx *ctx }
%parse-param { void *scanner }
%lex-param   { void *scanner }

%union {
    char   *str;
    CxNode *node;
    CxVec   vec;
}

%token <str> IDENT INT_LITERAL STRING_LITERAL
%token NAMESPACE STRUCT RETURN

%type <node> translation_unit external_decl namespace_decl struct_decl
%type <node> destructor_decl function_decl var_decl block stmt expr_stmt
%type <node> expr param struct_member
%type <str>  type
%type <vec>  struct_members stmt_list param_list arg_list

%right '='
%left  '+' '-'
%left  '*' '/'
%precedence UMINUS
%left  '.'

%%

translation_unit:
      %empty
        { $$ = MK(CX_NODE_TRANSLATION_UNIT, (@$)); ctx->result = $$; }
    | translation_unit external_decl
        { $$ = $1; cx_vec_push(ctx->arena, &$$->children, $2); ctx->result = $$; }
    ;

external_decl:
      namespace_decl
    | struct_decl
    | function_decl
    | var_decl
    ;

namespace_decl:
      NAMESPACE IDENT '{' translation_unit '}'
        { $$ = MK(CX_NODE_NAMESPACE, (@$)); $$->name = $2; $$->children = $4->children; }
    ;

struct_decl:
      STRUCT IDENT '{' struct_members '}' ';'
        { $$ = MK(CX_NODE_STRUCT, (@$)); $$->name = $2; $$->children = $4; }
    ;

struct_members:
      %empty
        { $$ = cx_vec_empty(); }
    | struct_members struct_member
        { $$ = $1; cx_vec_push(ctx->arena, &$$, $2); }
    ;

struct_member:
      var_decl
    | destructor_decl
    ;

destructor_decl:
      '~' IDENT '(' ')' block
        { $$ = MK(CX_NODE_DESTRUCTOR, (@$)); $$->name = $2; $$->body = $5; }
    ;

function_decl:
      type IDENT '(' param_list ')' block
        { $$ = MK(CX_NODE_FUNCTION, (@$)); $$->type = $1; $$->name = $2;
          $$->params = $4; $$->body = $6; }
    | type IDENT '(' param_list ')' ';'
        { $$ = MK(CX_NODE_FUNCTION, (@$)); $$->type = $1; $$->name = $2;
          $$->params = $4; }
    ;

param_list:
      %empty
        { $$ = cx_vec_empty(); }
    | param_list ',' param
        { $$ = $1; cx_vec_push(ctx->arena, &$$, $3); }
    | param
        { $$ = cx_vec_empty(); cx_vec_push(ctx->arena, &$$, $1); }
    ;

param:
      type IDENT
        { $$ = MK(CX_NODE_PARAM, (@$)); $$->type = $1; $$->name = $2; }
    ;

type:
      IDENT        { $$ = $1; }
    /* Pointer/array declarators need symbol-table feedback to disambiguate
     * `T * x` from `a * b`; that lands with the phase-4 semantic layer. */
    ;

var_decl:
      type IDENT ';'
        { $$ = MK(CX_NODE_VAR_DECL, (@$)); $$->type = $1; $$->name = $2; }
    | type IDENT '=' expr ';'
        { $$ = MK(CX_NODE_VAR_DECL, (@$)); $$->type = $1; $$->name = $2;
          $$->init = $4; }
    ;

block:
      '{' stmt_list '}'
        { $$ = MK(CX_NODE_BLOCK, (@$)); $$->children = $2; }
    ;

stmt_list:
      %empty
        { $$ = cx_vec_empty(); }
    | stmt_list stmt
        { $$ = $1; cx_vec_push(ctx->arena, &$$, $2); }
    ;

stmt:
      var_decl
    | expr_stmt
    | block
    | ';'
        { $$ = MK(CX_NODE_EMPTY, (@$)); }
    | RETURN expr ';'
        { $$ = MK(CX_NODE_RETURN, (@$)); $$->init = $2; }
    | RETURN ';'
        { $$ = MK(CX_NODE_RETURN, (@$)); }
    ;

expr_stmt:
      expr ';'
        { $$ = MK(CX_NODE_EXPR_STMT, (@$)); $$->lhs = $1; }
    ;

expr:
      IDENT
        { $$ = MK(CX_NODE_EXPR_IDENT, (@$)); $$->name = $1; }
    | INT_LITERAL
        { $$ = MK(CX_NODE_EXPR_INT, (@$)); $$->name = $1; }
    | STRING_LITERAL
        { $$ = MK(CX_NODE_EXPR_STRING, (@$)); $$->name = $1; }
    | IDENT '(' arg_list ')'
        { $$ = MK(CX_NODE_EXPR_CALL, (@$)); $$->name = $1; $$->children = $3; }
    | '(' expr ')'
        { $$ = MK(CX_NODE_EXPR_PAREN, (@$)); $$->lhs = $2; }
    | expr '.' IDENT
        { $$ = MK(CX_NODE_EXPR_FIELD, (@$)); $$->lhs = $1; $$->name = $3; }
    | expr '+' expr
        { $$ = MK(CX_NODE_EXPR_BINARY, (@$)); $$->name = "+"; $$->lhs = $1; $$->rhs = $3; }
    | expr '-' expr
        { $$ = MK(CX_NODE_EXPR_BINARY, (@$)); $$->name = "-"; $$->lhs = $1; $$->rhs = $3; }
    | expr '*' expr
        { $$ = MK(CX_NODE_EXPR_BINARY, (@$)); $$->name = "*"; $$->lhs = $1; $$->rhs = $3; }
    | expr '/' expr
        { $$ = MK(CX_NODE_EXPR_BINARY, (@$)); $$->name = "/"; $$->lhs = $1; $$->rhs = $3; }
    | '-' expr %prec UMINUS
        { $$ = MK(CX_NODE_EXPR_UNARY, (@$)); $$->name = "-"; $$->lhs = $2; }
    | expr '=' expr
        { $$ = MK(CX_NODE_EXPR_ASSIGN, (@$)); $$->lhs = $1; $$->rhs = $3; }
    ;

arg_list:
      %empty
        { $$ = cx_vec_empty(); }
    | arg_list ',' expr
        { $$ = $1; cx_vec_push(ctx->arena, &$$, $3); }
    | expr
        { $$ = cx_vec_empty(); cx_vec_push(ctx->arena, &$$, $1); }
    ;

%%

void yyerror(YYLTYPE *locp, CxParseCtx *ctx, void *scanner, const char *msg) {
    (void)scanner;
    CxSpan span = {locp->first_line, locp->first_column,
                   locp->last_line, locp->last_column};
    cx_error(ctx->diags, span, "%s", msg);
}
