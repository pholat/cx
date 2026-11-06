%{
/* Flex scanner for the Cx language.
 *
 * Reentrant + Bison bridge + location tracking so a single parse can run over
 * any buffer (which is how the LSP reparses documents on demand).
 */
#include <string.h>

#include "cx/cx.h"
#include "cx/parser.h"
#include "parser.h" /* Bison-generated token definitions */

/* Track line/column explicitly (rather than relying on Flex's yylineno
 * timing) so locations are exact, including across multi-line tokens. */
#define YY_USER_ACTION                                                       \
    do {                                                                     \
        CxParseCtx *cx_ctx_ = yyget_extra(yyscanner);                        \
        yylloc->first_line = cx_ctx_->line;                                  \
        yylloc->first_column = cx_ctx_->column;                              \
        for (int cx_i_ = 0; cx_i_ < (int)yyleng; cx_i_++) {                  \
            if (yytext[cx_i_] == '\n') {                                     \
                cx_ctx_->line++;                                             \
                cx_ctx_->column = 1;                                         \
            } else {                                                         \
                cx_ctx_->column++;                                           \
            }                                                                \
        }                                                                    \
        yylloc->last_line = cx_ctx_->line;                                   \
        yylloc->last_column = cx_ctx_->column - 1;                           \
        cx_ctx_->last_span.first_line = yylloc->first_line;                  \
        cx_ctx_->last_span.first_column = yylloc->first_column;              \
        cx_ctx_->last_span.last_line = yylloc->last_line;                    \
        cx_ctx_->last_span.last_column = yylloc->last_column;                \
    } while (0);
%}

%option reentrant bison-bridge bison-locations
%option noyywrap
%option extra-type="CxParseCtx *"

DIGIT   [0-9]
IDENT   [A-Za-z_][A-Za-z0-9_]*

%%

[ \t\r]+            { /* horizontal whitespace */ }
\n                  { /* newline (line/column advance in YY_USER_ACTION) */ }
"//"[^\n]*          { /* line comment */ }
"/*"([^*]|\*+[^*/])*\*+"/"  { /* block comment */ }
#[^\n]*             { /* preprocessor lines are currently ignored */ }

"namespace"     { return NAMESPACE; }
"struct"        { return STRUCT; }
"return"        { return RETURN; }

{DIGIT}+                    { yylval->str = cx_arena_strndup(yyget_extra(yyscanner)->arena, yytext, (size_t)yyleng); return INT_LITERAL; }
\"(\\.|[^"\\])*\"           { yylval->str = cx_arena_strndup(yyget_extra(yyscanner)->arena, yytext, (size_t)yyleng); return STRING_LITERAL; }
{IDENT}                     { yylval->str = cx_arena_strndup(yyget_extra(yyscanner)->arena, yytext, (size_t)yyleng); return IDENT; }

"::"                        { return SCOPE; }
":"                         { return ':'; }
[-+*/=.;,~(){}]             { return (unsigned char)yytext[0]; }
.                           { /* skip anything else */ }

%%

CxNode *cx_parse(CxArena *arena, const char *src, size_t len,
                 const char *filename, CxDiagList *diags) {
    CxParseCtx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.arena = arena;
    ctx.diags = diags;
    ctx.filename = filename;
    ctx.line = 1;
    ctx.column = 1;

    yyscan_t scanner = NULL;
    if (yylex_init_extra(&ctx, &scanner) != 0) return NULL;

    YY_BUFFER_STATE buffer = yy_scan_bytes(src, (int)len, scanner);
    yyparse(&ctx, scanner);

    yy_delete_buffer(buffer, scanner);
    yylex_destroy(scanner);
    return ctx.result;
}
