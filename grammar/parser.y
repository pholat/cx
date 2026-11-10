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

/* Feature parser hooks. Each feature's grammar fragments delegate their node
 * construction to the matching module under `src/features/` (see those files);
 * the resolution is relative to the generated parser (`build/parser.c`). */
#include "../src/features/enum/enum_parser.h"
#include "../src/features/fsm/fsm_parser.h"
#include "../src/features/cast/cast_parser.h"

int yylex(YYSTYPE *yylval, YYLTYPE *yylloc, void *scanner);
void yyerror(YYLTYPE *locp, CxParseCtx *ctx, void *scanner, const char *msg);

/* Build a node with the location of the current rule. `loc` is written as
 * `(@$)`, which Bison rewrites to the rule's YYLTYPE before expansion. */
#define MK(kind, loc)                                                        \
    cx_node_new(ctx->arena, (kind),                                          \
                (CxSpan){(loc).first_line, (loc).first_column,               \
                         (loc).last_line, (loc).last_column})

/* Overwrite a node's span from the current rule location. Used to widen a
 * declarator node to cover its initializer so tools that descend the tree by
 * span (the LSP) can reach the initializer expression. */
#define SETSPAN(node, loc)                                                   \
    ((node)->span = (CxSpan){(loc).first_line, (loc).first_column,           \
                             (loc).last_line, (loc).last_column})

/* The current rule's span as a standalone `CxSpan` value, passed to the feature
 * parser hooks (Bison rewrites `(@$)` to the rule's YYLTYPE). */
#define SPAN(loc)                                                            \
    ((CxSpan){(loc).first_line, (loc).first_column, (loc).last_line,         \
              (loc).last_column})
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
    struct { char *name; char *args; } macro; /* `name!( … )` invocation */
}

%token <str> IDENT INT_LITERAL STRING_LITERAL TYPE_NAME
%token <str> RAW
%token <macro> MACRO
%token <macro> ATTRIBUTE
%token <macro> SM
%token NAMESPACE STRUCT RETURN SCOPE ARROW
%token IF ELSE WHILE DO FOR BREAK CONTINUE SIZEOF SWITCH CASE DEFAULT GOTO AUTO
%token TYPEDEF IMPORT TRAIT IMPLEMENTS SELF
%token ENUM CAST
%token EQ NE LE GE ANDAND OROR FATARROW
%token INC DEC PLUSEQ MINUSEQ STAREQ SLASHEQ PERCENTEQ SHL SHR

%type <node> translation_unit external_decl namespace_decl struct_decl
%type <node> destructor_decl constructor_decl function_decl var_decl block stmt
%type <node> expr_stmt expr param struct_member expr_opt declarator fp_param
%type <node> typedef_decl one_decl import_decl
%type <node> raw_block macro_stmt trait_decl trait_member
%type <node> implements_block impl_member
%type <node> enum_decl enum_member sm_block enum_str_decl
%type <str>  type namespace_path cast_type
%type <vec>  struct_members stmt_list param_list arg_list ctor_args fp_param_list
%type <vec>  one_decl_list trait_members trait_param_list impl_members
%type <vec>  impl_param_list attributes unpack_names
%type <vec>  enum_members enum_member_list

%right '=' PLUSEQ MINUSEQ STAREQ SLASHEQ PERCENTEQ
%right '?'
%left  OROR
%left  ANDAND
%left  '|'
%left  '^'
%left  '&'
%left  EQ NE
%left  '<' '>' LE GE
%left  SHL SHR
%left  '+' '-'
%left  '*' '/' '%'
%precedence UMINUS
%precedence UDEREF UADDR
%precedence '~'
%precedence '!'
%precedence INC DEC
%precedence SIZEOF
%left  '.'
%precedence POSTFIX
%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE

%%

/* Grammar map. Core C-like syntax is grouped below in dependency order; the Cx
 * feature syntaxes are grouped by feature and delegate their node construction
 * to the feature modules under `src/features/`:
 *
 *   enums       `enum_decl`, `enum_members*`, `enum_member*`, `enum_str_decl`
 *   state mach. `sm_block`
 *   cast        the `(cast_type)expr` and `cast[T](expr)` expression rules
 *   traits      `trait_decl`, `implements_block` (see `generics.md`)
 *   macro_rules! is a pre-parse text pass, not a grammar rule.
 */

translation_unit:
      %empty
        { $$ = MK(CX_NODE_TRANSLATION_UNIT, (@$)); ctx->result = $$; }
    | translation_unit external_decl
        { $$ = $1; cx_vec_push(ctx->arena, &$$->children, $2); ctx->result = $$; }
    | translation_unit attributes external_decl
        { $$ = $1; $3->attrs = $2; cx_vec_push(ctx->arena, &$$->children, $3);
          ctx->result = $$; }
    ;

/* One or more `#[…]` attributes preceding a declaration. Each becomes a
 * CX_NODE_ATTR with the attribute name and its (verbatim) argument text. */
attributes:
      ATTRIBUTE
        { $$ = cx_vec_empty();
          CxNode *a = MK(CX_NODE_ATTR, (@$)); a->name = $1.name; a->type = $1.args;
          cx_vec_push(ctx->arena, &$$, a); }
    | attributes ATTRIBUTE
        { $$ = $1;
          CxNode *a = MK(CX_NODE_ATTR, (@$)); a->name = $2.name; a->type = $2.args;
          cx_vec_push(ctx->arena, &$$, a); }
    ;

external_decl:
      namespace_decl
    | struct_decl
    | typedef_decl
    | import_decl
    | function_decl
    | var_decl
    | trait_decl
    | enum_decl       /* `enum E { A, B };` */
    | enum_str_decl   /* `#[str] enum Machine::State;` */
    | sm_block        /* `sm Name { … }` state machine */
    | raw_block       /* verbatim C at file scope, e.g. a macro instantiation */
    | macro_stmt      /* `NAME!( … );` C macro invocation passthrough */
    ;

/* ------------------------------------------------------------------ C interop */

/* `raw { … }` — verbatim C, emitted unchanged. The lexer captures the body
 * (balanced braces) and hands it over as the token text. */
raw_block:
      RAW
        { $$ = MK(CX_NODE_RAW, (@$)); $$->name = $1; }
    ;

/* `NAME!( … );` — a C function-like macro invocation, allowed at file scope and
 * statement position (not in expressions, for now). The lexer captures the
 * balanced argument list; codegen emits `NAME(args)` verbatim. This lets macro
 * arguments be types or generated identifiers (e.g. `KLIST_INIT(IntList, int, 0)`). */
macro_stmt:
      MACRO ';'
        { $$ = MK(CX_NODE_MACRO, (@$)); $$->name = $1.name; $$->type = $1.args; }
    ;

/* --------------------------------------------------------------------- enums */

/* `enum E { A, B = 2, C };` — a C enum. The name is registered as a type
 * (mid-rule) so `E v;` lexes `E` as a type name. `#[str]` on the enum makes
 * codegen emit a `E__c_str` switch returning each member's name. */
enum_decl:
      ENUM IDENT
        { cx_typeset_add(&ctx->types, $2); }
      '{' enum_members '}' ';'
        { $$ = cx_enum_decl(ctx, $2, $5, SPAN(@$)); }
    ;

enum_members:
      %empty
        { $$ = cx_vec_empty(); }
    | enum_member_list
        { $$ = $1; }
    | enum_member_list ','   /* trailing comma */
        { $$ = $1; }
    ;

enum_member_list:
      enum_member
        { $$ = cx_vec_empty(); cx_vec_push(ctx->arena, &$$, $1); }
    | enum_member_list ',' enum_member
        { $$ = $1; cx_vec_push(ctx->arena, &$$, $3); }
    ;

enum_member:
      IDENT
        { $$ = MK(CX_NODE_EMPTY, (@$)); $$->name = $1; }
    | IDENT '=' expr
        { $$ = MK(CX_NODE_EMPTY, (@$)); $$->name = $1; $$->init = $3; }
    ;

/* `#[str] enum Machine::State;` — apply `#[str]` to an `sm` machine's generated
 * state/event enum (`name` = the machine, `type` = "State" / "Event"). Codegen
 * emits the `<M>__State__c_str` / `<M>__Event__c_str` helpers at this point. */
enum_str_decl:
      ENUM TYPE_NAME SCOPE IDENT ';'
        { $$ = cx_enum_str_decl(ctx, $2, $4, SPAN(@$)); }
    ;

/* --------------------------------------------------------------- state machine */

/* `sm Name { … }`: the lexer captures the machine name and the verbatim
 * transition body; codegen expands it into enums, a struct and the
 * `init`/`process_event`/`reset`/`get_state` functions. `cx_fsm_decl` registers
 * the name as a type so `Name m;` is a valid declaration. */
sm_block:
      SM semi_opt
        { $$ = cx_fsm_decl(ctx, $1.name, $1.args, SPAN(@$)); }
    ;

/* --------------------------------------------------------------------- traits */

trait_decl:
      TRAIT IDENT '{' trait_members '}' semi_opt
        { $$ = MK(CX_NODE_TRAIT, (@$)); $$->name = $2; $$->children = $4; }
    ;

/* A trait (like an `implements` block) needs no trailing `;`; accept one if
 * written, for consistency with `struct` declarations. */
semi_opt:
      %empty
    | ';'
    ;

trait_members:
      %empty
        { $$ = cx_vec_empty(); }
    | trait_members trait_member
        { $$ = $1; cx_vec_push(ctx->arena, &$$, $2); }
    ;

trait_member:
      type IDENT '(' trait_param_list ')' ';'
        { $$ = MK(CX_NODE_TRAIT_METHOD, (@$)); $$->type = $1; $$->name = $2;
          $$->params = $4; }
    ;

/* A trait method's first parameter is the receiver, written `self`. It has no
 * declared type (the concrete type is supplied by the implementing struct). */
trait_param_list:
      param_list
        { $$ = $1; }
    | SELF
        { $$ = cx_vec_empty();
          CxNode *p = MK(CX_NODE_PARAM, (@1)); p->name = "self";
          cx_vec_push(ctx->arena, &$$, p); }
    | SELF ',' param_list
        { $$ = cx_vec_empty();
          CxNode *p = MK(CX_NODE_PARAM, (@1)); p->name = "self";
          cx_vec_push(ctx->arena, &$$, p);
          for (int i = 0; i < $3.len; i++) cx_vec_push(ctx->arena, &$$, $3.items[i]); }
    ;

implements_block:
      IMPLEMENTS IDENT '{' impl_members '}'
        { $$ = MK(CX_NODE_IMPLEMENTS, (@$)); $$->name = $2; $$->children = $4; }
    ;

impl_members:
      %empty
        { $$ = cx_vec_empty(); }
    | impl_members impl_member
        { $$ = $1; cx_vec_push(ctx->arena, &$$, $2); }
    ;

impl_member:
      type IDENT '(' impl_param_list ')' block
        { $$ = MK(CX_NODE_IMPL_METHOD, (@$)); $$->type = $1; $$->name = $2;
          $$->params = $4; $$->body = $6; }
    ;

impl_param_list:
      param_list
        { $$ = $1; }
    | SELF
        { $$ = cx_vec_empty();
          CxNode *p = MK(CX_NODE_PARAM, (@1)); p->name = "self";
          cx_vec_push(ctx->arena, &$$, p); }
    | SELF ',' param_list
        { $$ = cx_vec_empty();
          CxNode *p = MK(CX_NODE_PARAM, (@1)); p->name = "self";
          cx_vec_push(ctx->arena, &$$, p);
          for (int i = 0; i < $3.len; i++) cx_vec_push(ctx->arena, &$$, $3.items[i]); }
    ;

import_decl:
      IMPORT STRING_LITERAL
        { cx_import(ctx, $2); } /* register imported types before the next token */
      ';'
        { $$ = MK(CX_NODE_EMPTY, (@$)); }
    ;

typedef_decl:
      TYPEDEF type declarator
        { cx_typeset_add(&ctx->types, $3->name); } /* alias is a type henceforth */
      ';'
        { $$ = MK(CX_NODE_TYPEDEF, (@$)); $$->type = $2; $$->name = $3->name;
          $$->is_funcptr = $3->is_funcptr; $$->params = $3->params; }
    ;

namespace_decl:
      NAMESPACE namespace_path '{' translation_unit '}'
        { $$ = MK(CX_NODE_NAMESPACE, (@$)); $$->name = $2; $$->children = $4->children; }
    ;

/* A namespace name is one or more identifiers joined by `::`, e.g. `aa::bb`. */
namespace_path:
      IDENT
        { $$ = $1; }
    | namespace_path SCOPE IDENT
        { $$ = cx_arena_printf(ctx->arena, "%s::%s", $1, $3); }
    ;

struct_decl:
      STRUCT IDENT
        { cx_typeset_add(&ctx->types, $2); } /* register before the body */
      '{' struct_members '}' ';'
        { $$ = MK(CX_NODE_STRUCT, (@$)); $$->name = $2; $$->children = $5; }
    ;

struct_members:
      %empty
        { $$ = cx_vec_empty(); }
    | struct_members struct_member
        { $$ = $1; cx_vec_push(ctx->arena, &$$, $2); }
    | struct_members attributes struct_member
        { $$ = $1; $3->attrs = $2; cx_vec_push(ctx->arena, &$$, $3); }
    ;

struct_member:
      var_decl
    | destructor_decl
    | constructor_decl
    | struct_decl          /* nested type: struct Inner { ... }; */
    | implements_block     /* `implements Trait { … }` */
    | raw_block            /* verbatim member, e.g. a macro-generated type */
    ;

destructor_decl:
      '~' TYPE_NAME '(' ')' block
        { $$ = MK(CX_NODE_DESTRUCTOR, (@$)); $$->name = $2; $$->body = $5; }
    ;

/* C++-style constructor: a member named after the struct. The semantic pass
 * enforces that the name matches. */
constructor_decl:
      TYPE_NAME '(' param_list ')' block
        { $$ = MK(CX_NODE_CONSTRUCTOR, (@$)); $$->name = $1;
          $$->params = $3; $$->body = $5; }
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
      type declarator
        { $$ = MK(CX_NODE_PARAM, (@$)); $$->type = $1; $$->name = $2->name;
          $$->is_funcptr = $2->is_funcptr; $$->params = $2->params; }
    ;

/* A declarator is a plain name or a function-pointer form `(*name)(params)`. */
declarator:
      IDENT
        { $$ = MK(CX_NODE_EMPTY, (@$)); $$->name = $1; }
    | '(' '*' IDENT ')' '(' fp_param_list ')'
        { $$ = MK(CX_NODE_EMPTY, (@$)); $$->name = $3;
          $$->is_funcptr = 1; $$->params = $6; }
    ;

fp_param_list:
      %empty
        { $$ = cx_vec_empty(); }
    | fp_param_list ',' fp_param
        { $$ = $1; cx_vec_push(ctx->arena, &$$, $3); }
    | fp_param
        { $$ = cx_vec_empty(); cx_vec_push(ctx->arena, &$$, $1); }
    ;

fp_param:
      type
        { $$ = MK(CX_NODE_PARAM, (@$)); $$->type = $1; }
    | type IDENT
        { $$ = MK(CX_NODE_PARAM, (@$)); $$->type = $1; $$->name = $2; }
    ;

type:
      TYPE_NAME    { $$ = $1; }
    | AUTO         { $$ = "auto"; } /* inferred in the semantic pass */
    | type '*'     { $$ = cx_arena_printf(ctx->arena, "%s *", $1); }
    | type TYPE_NAME { $$ = cx_arena_printf(ctx->arena, "%s %s", $1, $2); }
    /* `Type::sub` — a Cx-qualified type (e.g. an `sm` machine's generated
     * enums: `Machine::State` / `Machine::Event`); lowers to `Type__sub`. */
    | TYPE_NAME SCOPE IDENT
        { $$ = cx_arena_printf(ctx->arena, "%s__%s", $1, $3); }
    ;

/* A cast/`sizeof` type. Deliberately *excludes* the qualified `Type::sub` form
 * so that `(T)` in the shared `'('` state stays unambiguous against the
 * parenthesised-expression rule `'(' expr ')'` (`Type::sub` is also an
 * expression). Cast to a qualified type with `cast[T](x)`. */
cast_type:
      TYPE_NAME    { $$ = $1; }
    | AUTO         { $$ = "auto"; }
    | cast_type '*'     { $$ = cx_arena_printf(ctx->arena, "%s *", $1); }
    | cast_type TYPE_NAME { $$ = cx_arena_printf(ctx->arena, "%s %s", $1, $2); }
    ;

var_decl:
      type one_decl_list ';'
        { $$ = MK(CX_NODE_VAR_DECL, (@$)); $$->type = $1; $$->children = $2; }
    | type IDENT '(' ctor_args ')' ';'
        { $$ = MK(CX_NODE_VAR_DECL, (@$)); $$->type = $1;
          CxNode *d = MK(CX_NODE_EMPTY, (@$)); d->name = $2;
          CxNode *call = MK(CX_NODE_EXPR_CALL, (@$));
          call->name = $1; call->children = $4; d->init = call;
          cx_vec_push(ctx->arena, &$$->children, d); }
    ;

/* One or more declarators sharing a type, e.g. `int a, b = 2, c;`. Each entry
 * is a declarator node (name / function-pointer shape) with an optional init. */
one_decl_list:
      one_decl
        { $$ = cx_vec_empty(); cx_vec_push(ctx->arena, &$$, $1); }
    | one_decl_list ',' one_decl
        { $$ = $1; cx_vec_push(ctx->arena, &$$, $3); }
    ;

one_decl:
      declarator
        { $$ = $1; }
    | declarator '=' expr
        { $$ = $1; $$->init = $3; SETSPAN($$, (@$)); }
    | declarator '=' TYPE_NAME '(' arg_list ')'
        { $$ = $1; CxNode *call = MK(CX_NODE_EXPR_CALL, (@$));
          call->name = $3; call->children = $5; $$->init = call;
          SETSPAN($$, (@$)); }
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
    | IF '(' expr ')' stmt %prec LOWER_THAN_ELSE
        { $$ = MK(CX_NODE_IF, (@$)); $$->cond = $3; $$->then = $5; }
    | IF '(' expr ')' stmt ELSE stmt
        { $$ = MK(CX_NODE_IF, (@$)); $$->cond = $3; $$->then = $5; $$->els = $7; }
    | WHILE '(' expr ')' stmt
        { $$ = MK(CX_NODE_WHILE, (@$)); $$->cond = $3; $$->body = $5; }
    | DO stmt WHILE '(' expr ')' ';'
        { $$ = MK(CX_NODE_DO, (@$)); $$->body = $2; $$->cond = $5; }
    | FOR '(' expr_opt ';' expr_opt ';' expr_opt ')' stmt
        { $$ = MK(CX_NODE_FOR, (@$)); $$->lhs = $3; $$->cond = $5;
          $$->incr = $7; $$->body = $9; }
    | BREAK ';'
        { $$ = MK(CX_NODE_BREAK, (@$)); }
    | CONTINUE ';'
        { $$ = MK(CX_NODE_CONTINUE, (@$)); }
    | SWITCH '(' expr ')' stmt
        { $$ = MK(CX_NODE_SWITCH, (@$)); $$->cond = $3; $$->body = $5; }
    | CASE expr ':' stmt
        { $$ = MK(CX_NODE_CASE, (@$)); $$->lhs = $2; $$->body = $4; }
    | DEFAULT ':' stmt
        { $$ = MK(CX_NODE_DEFAULT, (@$)); $$->body = $3; }
    | IDENT ':' stmt
        { $$ = MK(CX_NODE_LABEL, (@$)); $$->name = $1; $$->body = $3; }
    | GOTO IDENT ';'
        { $$ = MK(CX_NODE_GOTO, (@$)); $$->name = $2; }
    | raw_block
    | macro_stmt
    /* Multiple value unpacking: `auto [a, b] = f();` (first fields, in order)
     * and `auto [a, c] = f()=>[a, c];` (fields by name). Lowered in sema to a
     * temporary plus one typed declaration per target. */
    | AUTO '[' unpack_names ']' '=' expr ';'
        { $$ = MK(CX_NODE_UNPACK, (@$)); $$->type = "auto";
          $$->children = $3; $$->init = $6; }
    | AUTO '[' unpack_names ']' '=' expr FATARROW '[' unpack_names ']' ';'
        { $$ = MK(CX_NODE_UNPACK, (@$)); $$->type = "auto";
          $$->children = $3; $$->init = $6; $$->params = $9; }
    ;

/* A comma-separated list of identifiers (unpack targets or field names). */
unpack_names:
      IDENT
        { $$ = cx_vec_empty();
          CxNode *d = MK(CX_NODE_EMPTY, (@1)); d->name = $1;
          cx_vec_push(ctx->arena, &$$, d); }
    | unpack_names ',' IDENT
        { $$ = $1;
          CxNode *d = MK(CX_NODE_EMPTY, (@3)); d->name = $3;
          cx_vec_push(ctx->arena, &$$, d); }
    ;

expr_stmt:
      expr ';'
        { $$ = MK(CX_NODE_EXPR_STMT, (@$)); $$->lhs = $1; }
    ;

expr:
      IDENT
        { $$ = MK(CX_NODE_EXPR_IDENT, (@$)); $$->name = $1; }
    | SELF /* the receiver, usable inside `implements` method bodies */
        { $$ = MK(CX_NODE_EXPR_IDENT, (@$)); $$->name = "self"; }
    | INT_LITERAL
        { $$ = MK(CX_NODE_EXPR_INT, (@$)); $$->name = $1; }
    | STRING_LITERAL
        { $$ = MK(CX_NODE_EXPR_STRING, (@$)); $$->name = $1; }
    | IDENT '(' arg_list ')'
        { $$ = MK(CX_NODE_EXPR_CALL, (@$)); $$->name = $1; $$->children = $3; }
    /* Associated call, e.g. `IntList::new(...)`. It lowers to a function named
     * `IntList__new` (the `__` convention), resolved/mangled by sema/codegen
     * like any other call. This makes static container entry points read as
     * methods without needing receiver-type inference. */
    | TYPE_NAME SCOPE IDENT '(' arg_list ')'
        { $$ = MK(CX_NODE_EXPR_CALL, (@$));
          $$->name = cx_arena_printf(ctx->arena, "%s__%s", $1, $3);
          $$->children = $5; }
    /* `Type::member` without a call — a generated name (e.g. an enum member or
     * a container constant); lowers to the `Type__member` identifier. */
    | TYPE_NAME SCOPE IDENT
        { $$ = MK(CX_NODE_EXPR_IDENT, (@$));
          $$->name = cx_arena_printf(ctx->arena, "%s__%s", $1, $3); }
    /* `Type::sub::member` — a two-level generated name (`Type__sub__member`),
     * e.g. the event constants of an `sm` machine: `Machine::event::start`. */
    | TYPE_NAME SCOPE IDENT SCOPE IDENT
        { $$ = MK(CX_NODE_EXPR_IDENT, (@$));
          $$->name = cx_arena_printf(ctx->arena, "%s__%s__%s", $1, $3, $5); }
    | TYPE_NAME SCOPE IDENT SCOPE IDENT '(' arg_list ')'
        { $$ = MK(CX_NODE_EXPR_CALL, (@$));
          $$->name = cx_arena_printf(ctx->arena, "%s__%s__%s", $1, $3, $5);
          $$->children = $7; }
    | '(' expr ')'
        { $$ = MK(CX_NODE_EXPR_PAREN, (@$)); $$->lhs = $2; }
    | expr '.' IDENT
        { $$ = MK(CX_NODE_EXPR_FIELD, (@$)); $$->lhs = $1; $$->name = $3; }
    /* `recv.method(args)`: a method call. Sema resolves the receiver's type and
     * lowers it to the associated call `Type__method(&recv, args)` (a pointer
     * receiver is passed through). Left unresolved (unknown receiver, e.g. a C
     * function-pointer field) it is emitted back as `recv.method(args)`. */
    | expr '.' IDENT '(' arg_list ')' %prec POSTFIX
        { $$ = MK(CX_NODE_EXPR_CALL, (@$)); $$->lhs = $1; $$->name = $3;
          $$->type = "."; $$->children = $5; }
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
    | '!' expr
        { $$ = MK(CX_NODE_EXPR_UNARY, (@$)); $$->name = "!"; $$->lhs = $2; }
    | expr '%' expr
        { $$ = MK(CX_NODE_EXPR_BINARY, (@$)); $$->name = "%"; $$->lhs = $1; $$->rhs = $3; }
    | expr '<' expr
        { $$ = MK(CX_NODE_EXPR_BINARY, (@$)); $$->name = "<"; $$->lhs = $1; $$->rhs = $3; }
    | expr '>' expr
        { $$ = MK(CX_NODE_EXPR_BINARY, (@$)); $$->name = ">"; $$->lhs = $1; $$->rhs = $3; }
    | expr LE expr
        { $$ = MK(CX_NODE_EXPR_BINARY, (@$)); $$->name = "<="; $$->lhs = $1; $$->rhs = $3; }
    | expr GE expr
        { $$ = MK(CX_NODE_EXPR_BINARY, (@$)); $$->name = ">="; $$->lhs = $1; $$->rhs = $3; }
    | expr EQ expr
        { $$ = MK(CX_NODE_EXPR_BINARY, (@$)); $$->name = "=="; $$->lhs = $1; $$->rhs = $3; }
    | expr NE expr
        { $$ = MK(CX_NODE_EXPR_BINARY, (@$)); $$->name = "!="; $$->lhs = $1; $$->rhs = $3; }
    | expr ANDAND expr
        { $$ = MK(CX_NODE_EXPR_BINARY, (@$)); $$->name = "&&"; $$->lhs = $1; $$->rhs = $3; }
    | expr OROR expr
        { $$ = MK(CX_NODE_EXPR_BINARY, (@$)); $$->name = "||"; $$->lhs = $1; $$->rhs = $3; }
    | expr '|' expr
        { $$ = MK(CX_NODE_EXPR_BINARY, (@$)); $$->name = "|"; $$->lhs = $1; $$->rhs = $3; }
    | expr '^' expr
        { $$ = MK(CX_NODE_EXPR_BINARY, (@$)); $$->name = "^"; $$->lhs = $1; $$->rhs = $3; }
    | expr '&' expr
        { $$ = MK(CX_NODE_EXPR_BINARY, (@$)); $$->name = "&"; $$->lhs = $1; $$->rhs = $3; }
    | expr SHL expr
        { $$ = MK(CX_NODE_EXPR_BINARY, (@$)); $$->name = "<<"; $$->lhs = $1; $$->rhs = $3; }
    | expr SHR expr
        { $$ = MK(CX_NODE_EXPR_BINARY, (@$)); $$->name = ">>"; $$->lhs = $1; $$->rhs = $3; }
    | '&' expr %prec UADDR
        { $$ = MK(CX_NODE_EXPR_UNARY, (@$)); $$->name = "&"; $$->lhs = $2; }
    | '*' expr %prec UDEREF
        { $$ = MK(CX_NODE_EXPR_UNARY, (@$)); $$->name = "*"; $$->lhs = $2; }
    | expr ARROW IDENT
        { $$ = MK(CX_NODE_EXPR_ARROW, (@$)); $$->lhs = $1; $$->name = $3; }
    /* `recv->method(args)`: a method call through a pointer receiver. Sema lowers
     * it to `Type__method(recv, args)` (the pointer is passed through). */
    | expr ARROW IDENT '(' arg_list ')' %prec POSTFIX
        { $$ = MK(CX_NODE_EXPR_CALL, (@$)); $$->lhs = $1; $$->name = $3;
          $$->type = "->"; $$->children = $5; }
    | expr '[' expr ']'
        { $$ = MK(CX_NODE_EXPR_INDEX, (@$)); $$->lhs = $1; $$->rhs = $3; }
    | '(' cast_type ')' expr %prec UDEREF
        { $$ = cx_cast_expr(ctx, $2, $4, SPAN(@$)); }
    /* `cast[T](expr)` — readable sugar for the C cast `(T)expr`. */
    | CAST '[' type ']' '(' expr ')'
        { $$ = cx_cast_expr(ctx, $3, $6, SPAN(@$)); }
    | SIZEOF '(' cast_type ')' %prec SIZEOF
        { $$ = MK(CX_NODE_EXPR_SIZEOF, (@$)); $$->type = $3; }
    | SIZEOF expr
        { $$ = MK(CX_NODE_EXPR_SIZEOF, (@$)); $$->lhs = $2; }
    | '~' expr
        { $$ = MK(CX_NODE_EXPR_UNARY, (@$)); $$->name = "~"; $$->lhs = $2; }
    | INC expr
        { $$ = MK(CX_NODE_EXPR_UNARY, (@$)); $$->name = "++"; $$->lhs = $2; }
    | DEC expr
        { $$ = MK(CX_NODE_EXPR_UNARY, (@$)); $$->name = "--"; $$->lhs = $2; }
    | expr INC %prec POSTFIX
        { $$ = MK(CX_NODE_EXPR_POSTFIX, (@$)); $$->name = "++"; $$->lhs = $1; }
    | expr DEC %prec POSTFIX
        { $$ = MK(CX_NODE_EXPR_POSTFIX, (@$)); $$->name = "--"; $$->lhs = $1; }
    /* `cond ? then : else` — right-associative, binding tighter than `=` and
     * looser than `||` (like C). */
    | expr '?' expr ':' expr %prec '?'
        { $$ = MK(CX_NODE_EXPR_TERNARY, (@$));
          $$->cond = $1; $$->lhs = $3; $$->rhs = $5; }
    | expr '=' expr
        { $$ = MK(CX_NODE_EXPR_ASSIGN, (@$)); $$->name = "="; $$->lhs = $1; $$->rhs = $3; }
    | expr PLUSEQ expr
        { $$ = MK(CX_NODE_EXPR_ASSIGN, (@$)); $$->name = "+="; $$->lhs = $1; $$->rhs = $3; }
    | expr MINUSEQ expr
        { $$ = MK(CX_NODE_EXPR_ASSIGN, (@$)); $$->name = "-="; $$->lhs = $1; $$->rhs = $3; }
    | expr STAREQ expr
        { $$ = MK(CX_NODE_EXPR_ASSIGN, (@$)); $$->name = "*="; $$->lhs = $1; $$->rhs = $3; }
    | expr SLASHEQ expr
        { $$ = MK(CX_NODE_EXPR_ASSIGN, (@$)); $$->name = "/="; $$->lhs = $1; $$->rhs = $3; }
    | expr PERCENTEQ expr
        { $$ = MK(CX_NODE_EXPR_ASSIGN, (@$)); $$->name = "%="; $$->lhs = $1; $$->rhs = $3; }
    ;

arg_list:
      %empty
        { $$ = cx_vec_empty(); }
    | arg_list ',' expr
        { $$ = $1; cx_vec_push(ctx->arena, &$$, $3); }
    | expr
        { $$ = cx_vec_empty(); cx_vec_push(ctx->arena, &$$, $1); }
    ;

/* Non-empty argument list, used by `Type var(args);` so it stays disjoint from
 * a zero-argument function prototype `Type var();`. */
ctor_args:
      expr
        { $$ = cx_vec_empty(); cx_vec_push(ctx->arena, &$$, $1); }
    | ctor_args ',' expr
        { $$ = $1; cx_vec_push(ctx->arena, &$$, $3); }
    ;

/* Optional expression, used by `for (init; cond; incr)`. May be NULL. */
expr_opt:
      %empty { $$ = NULL; }
    | expr   { $$ = $1; }
    ;

%%

void yyerror(YYLTYPE *locp, CxParseCtx *ctx, void *scanner, const char *msg) {
    (void)scanner;
    CxSpan span = {locp->first_line, locp->first_column,
                   locp->last_line, locp->last_column};
    cx_error(ctx->diags, span, "%s", msg);
}
