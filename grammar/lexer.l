%{
/* Flex scanner for the Cx language.
 *
 * Reentrant + Bison bridge + location tracking so a single parse can run over
 * any buffer (which is how the LSP reparses documents on demand).
 */
#include <stdio.h>
#include <stdlib.h>
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
%option noinput nounput
%option extra-type="CxParseCtx *"

/* Start conditions for capture. RAWCAP copies a balanced-brace body to
 * ctx->raw; MACROCAP copies a balanced-paren argument list to ctx->macro_args;
 * ATTR collects the body of a #[...] attribute into ctx->attr_body.
 * NB: these names must not collide with the Bison token names, since Flex emits
 * `#define RAWCAP 1` etc. which would otherwise shadow `return RAW;`.
 *
 * The rules section below deliberately has no standalone comments: Flex rejects
 * multi-line comments there (it parses them as rules). The preprocessor-line
 * rule excludes `#[` at line start so Cx attributes are not swallowed; an
 * attribute is then collected by the ATTR condition and split into a name plus
 * an optional verbatim argument string. */
%x RAWCAP
%x MACROCAP
%x ATTR

DIGIT   [0-9]
IDENT   [A-Za-z_][A-Za-z0-9_]*

%%

[ \t\r]+            { /* horizontal whitespace */ }
\n                  { /* newline (line/column advance in YY_USER_ACTION) */ }
"//"[^\n]*          { /* line comment */ }
"/*"([^*]|\*+[^*/])*\*+"/"  { /* block comment */ }
^[ \t]*"#"[^[\n][^\n]*  { CxParseCtx *c = yyget_extra(yyscanner); cx_buf_puts(&c->prelude, yytext); cx_buf_putc(&c->prelude, '\n'); }

"#["                { CxParseCtx *c = yyget_extra(yyscanner); cx_buf_clear(&c->attr_body); BEGIN(ATTR); }
<ATTR>"]"           {
    CxParseCtx *c = yyget_extra(yyscanner);
    const char *body = c->attr_body.data ? c->attr_body.data : "";
    int blen = (int)c->attr_body.len;
    int lp = -1;
    for (int i = 0; i < blen; i++) if (body[i] == '(') { lp = i; break; }
    if (lp >= 0) {
        yylval->macro.name = cx_arena_strndup(c->arena, body, (size_t)lp);
        yylval->macro.args = cx_arena_strndup(c->arena, body + lp + 1,
                                              (size_t)(blen - lp - 2));
    } else {
        yylval->macro.name = cx_arena_strndup(c->arena, body, (size_t)blen);
        yylval->macro.args = NULL;
    }
    BEGIN(INITIAL);
    return ATTRIBUTE;
}
<ATTR>[^\n]         { cx_buf_putc(&yyget_extra(yyscanner)->attr_body, yytext[0]); }
<ATTR>\n            { BEGIN(INITIAL); /* unterminated attribute */ }
<ATTR><<EOF>>       { BEGIN(INITIAL); return 0; }

"namespace"     { return NAMESPACE; }
"struct"        { return STRUCT; }
"return"        { return RETURN; }
"sizeof"        { return SIZEOF; }
"auto"          { return AUTO; }
"typedef"       { return TYPEDEF; }
"import"        { return IMPORT; }
"if"            { return IF; }
"else"          { return ELSE; }
"while"         { return WHILE; }
"for"           { return FOR; }
"break"         { return BREAK; }
"continue"      { return CONTINUE; }
"switch"        { return SWITCH; }
"case"          { return CASE; }
"default"       { return DEFAULT; }
"goto"          { return GOTO; }
"trait"         { return TRAIT; }
"implements"    { return IMPLEMENTS; }
"self"          { return SELF; }

"raw"[ \t\r\n]*"{"  {
    CxParseCtx *c = yyget_extra(yyscanner);
    c->raw_line = yylloc->first_line;
    c->raw_col = yylloc->first_column;
    c->raw_depth = 1;
    cx_buf_clear(&c->raw);
    BEGIN(RAWCAP);
}

{IDENT}"!"[ \t\r\n]*"("  {
    CxParseCtx *c = yyget_extra(yyscanner);
    int n = 0;
    while (n < yyleng && yytext[n] != '!') n++;
    c->macro_name = cx_arena_strndup(c->arena, yytext, (size_t)n);
    c->macro_line = yylloc->first_line;
    c->macro_col = yylloc->first_column;
    c->macro_depth = 1;
    cx_buf_clear(&c->macro_args);
    BEGIN(MACROCAP);
}

<RAWCAP>"{"                    { CxParseCtx *c = yyget_extra(yyscanner); c->raw_depth++; cx_buf_putc(&c->raw, '{'); }
<RAWCAP>"}"                    {
    CxParseCtx *c = yyget_extra(yyscanner);
    if (--c->raw_depth == 0) {
        BEGIN(INITIAL);
        yylval->str = cx_arena_strndup(c->arena, c->raw.data ? c->raw.data : "", c->raw.len);
        yylloc->first_line = c->raw_line;
        yylloc->first_column = c->raw_col;
        return RAW;
    }
    cx_buf_putc(&c->raw, '}');
}
<RAWCAP>[^{}\n]+               { cx_buf_puts(&yyget_extra(yyscanner)->raw, yytext); }
<RAWCAP>\n                     { cx_buf_putc(&yyget_extra(yyscanner)->raw, '\n'); }
<RAWCAP><<EOF>>                { BEGIN(INITIAL); return 0; }

<MACROCAP>"("                  { CxParseCtx *c = yyget_extra(yyscanner); c->macro_depth++; cx_buf_putc(&c->macro_args, '('); }
<MACROCAP>")"                  {
    CxParseCtx *c = yyget_extra(yyscanner);
    if (--c->macro_depth == 0) {
        BEGIN(INITIAL);
        yylval->macro.name = c->macro_name;
        yylval->macro.args = cx_arena_strndup(c->arena, c->macro_args.data ? c->macro_args.data : "", c->macro_args.len);
        yylloc->first_line = c->macro_line;
        yylloc->first_column = c->macro_col;
        return MACRO;
    }
    cx_buf_putc(&c->macro_args, ')');
}
<MACROCAP>\"(\\.|[^"\\])*\"     { cx_buf_puts(&yyget_extra(yyscanner)->macro_args, yytext); }
<MACROCAP>[^()"\n]+             { cx_buf_puts(&yyget_extra(yyscanner)->macro_args, yytext); }
<MACROCAP>\n                   { cx_buf_putc(&yyget_extra(yyscanner)->macro_args, '\n'); }
<MACROCAP>.                    { cx_buf_putc(&yyget_extra(yyscanner)->macro_args, yytext[0]); }
<MACROCAP><<EOF>>              { BEGIN(INITIAL); return 0; }

{DIGIT}+                    { yylval->str = cx_arena_strndup(yyget_extra(yyscanner)->arena, yytext, (size_t)yyleng); return INT_LITERAL; }
\"(\\.|[^"\\])*\"           { yylval->str = cx_arena_strndup(yyget_extra(yyscanner)->arena, yytext, (size_t)yyleng); return STRING_LITERAL; }
{IDENT}                     { CxParseCtx *c = yyget_extra(yyscanner); char *s = cx_arena_strndup(c->arena, yytext, (size_t)yyleng); yylval->str = s; return cx_typeset_has(&c->types, s) ? TYPE_NAME : IDENT; }

"++"                        { return INC; }
"--"                        { return DEC; }
"+="                        { return PLUSEQ; }
"-="                        { return MINUSEQ; }
"*="                        { return STAREQ; }
"/="                        { return SLASHEQ; }
"%="                        { return PERCENTEQ; }
"<<"                        { return SHL; }
">>"                        { return SHR; }
"&&"                        { return ANDAND; }
"||"                        { return OROR; }
"=="                        { return EQ; }
"!="                        { return NE; }
"<="                        { return LE; }
">="                        { return GE; }
"->"                        { return ARROW; }
"::"                        { return SCOPE; }
":"                         { return ':'; }
"["                         { return '['; }
"]"                         { return ']'; }
"<"                         { return '<'; }
">"                         { return '>'; }
"!"                         { return '!'; }
"&"                         { return '&'; }
"|"                         { return '|'; }
"^"                         { return '^'; }
[-+*/%=.;,~(){}]            { return (unsigned char)yytext[0]; }
.                           { /* skip anything else */ }

%%

/* Parse `src` with `ctx`. Nested (import) parses save/restore the fields a
 * sub-parse would otherwise clobber, so the outer parse continues cleanly. */
static CxNode *parse_source(CxParseCtx *ctx, const char *src, size_t len,
                            const char *filename, int nested) {
    int saved_line = ctx->line;
    int saved_col = ctx->column;
    CxNode *saved_result = ctx->result;
    const char *saved_file = ctx->filename;
    CxSpan saved_span = ctx->last_span;
    size_t saved_prelude = ctx->prelude.len;

    ctx->line = 1;
    ctx->column = 1;
    ctx->result = NULL;
    ctx->filename = filename;

    yyscan_t scanner = NULL;
    if (yylex_init_extra(ctx, &scanner) != 0) return NULL;
    YY_BUFFER_STATE buffer = yy_scan_bytes(src, (int)len, scanner);
    yyparse(ctx, scanner);
    yy_delete_buffer(buffer, scanner);
    yylex_destroy(scanner);

    CxNode *result = ctx->result;

    if (nested) {
        ctx->line = saved_line;
        ctx->column = saved_col;
        ctx->result = saved_result;
        ctx->filename = saved_file;
        ctx->last_span = saved_span;
        if (ctx->prelude.data) {
            if (saved_prelude <= ctx->prelude.len) ctx->prelude.len = saved_prelude;
            ctx->prelude.data[ctx->prelude.len] = '\0';
        }
    }
    return result;
}

/* `import "path";` — parse the target (sharing the type set so its type names
 * become visible) and record it for the symbol table and a `#include`. */
void cx_import(CxParseCtx *ctx, const char *raw) {
    char path[512];
    size_t n = raw ? strlen(raw) : 0;
    if (n >= 2 && raw[0] == '"' && raw[n - 1] == '"') {
        size_t m = n - 2;
        if (m >= sizeof path) m = sizeof path - 1;
        memcpy(path, raw + 1, m);
        path[m] = '\0';
    } else {
        size_t m = n < sizeof path - 1 ? n : sizeof path - 1;
        if (raw) memcpy(path, raw, m);
        path[m] = '\0';
    }

    const char *base_file = ctx->filename ? ctx->filename : "";
    int base_is_uri = strncmp(base_file, "file://", 7) == 0; /* LSP passes URIs */
    const char *dir_file = base_is_uri ? base_file + 7 : base_file;

    char resolved[1024];
    if (path[0] == '/') {
        snprintf(resolved, sizeof resolved, "%s", path);
    } else {
        const char *slash = strrchr(dir_file, '/');
        if (slash)
            snprintf(resolved, sizeof resolved, "%.*s/%s",
                     (int)(slash - dir_file), dir_file, path);
        else
            snprintf(resolved, sizeof resolved, "%s", path);
    }

    for (int i = 0; i < ctx->active_len; i++)
        if (strcmp(ctx->active[i], resolved) == 0) return; /* cycle */

    FILE *f = fopen(resolved, "rb");
    if (!f) {
        cx_error(ctx->diags, ctx->last_span, "cannot open import '%s'", path);
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    if (size < 0) size = 0;
    char *src = (char *)malloc((size_t)size + 1);
    if (!src) { fclose(f); return; }
    size_t got = fread(src, 1, (size_t)size, f);
    fclose(f);
    src[got] = '\0';

    if (ctx->active_len == ctx->active_cap) {
        int cap = ctx->active_cap ? ctx->active_cap * 2 : 8;
        const char **a =
            (const char **)realloc(ctx->active, sizeof(char *) * (size_t)cap);
        if (a) { ctx->active = a; ctx->active_cap = cap; }
    }
    if (ctx->active_len < ctx->active_cap)
        ctx->active[ctx->active_len++] = cx_arena_strdup(ctx->arena, resolved);

    char *expanded =
        cx_macro_expand(ctx->arena, src, got, resolved, ctx->diags, NULL);
    CxNode *imported = parse_source(ctx, expanded ? expanded : src,
                                    expanded ? strlen(expanded) : got,
                                    resolved, 1);
    free(src);

    if (ctx->active_len > 0) ctx->active_len--;

    if (imported) {
        imported->name = cx_arena_strdup(ctx->arena, path);
        imported->file = base_is_uri
                             ? cx_arena_printf(ctx->arena, "file://%s", resolved)
                             : cx_arena_strdup(ctx->arena, resolved);
        cx_vec_push(ctx->arena, &ctx->imports, imported);
    }
}

CxNode *cx_parse(CxArena *arena, const char *src, size_t len,
                 const char *filename, CxDiagList *diags) {
    CxParseCtx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.arena = arena;
    ctx.diags = diags;
    ctx.filename = filename;
    ctx.line = 1;
    ctx.column = 1;
    cx_buf_init(&ctx.prelude);
    cx_buf_init(&ctx.raw);
    cx_buf_init(&ctx.attr_body);
    cx_buf_init(&ctx.macro_args);

    static const char *builtin_types[] = {
        "void", "char", "short", "int", "long", "float", "double",
        "unsigned", "signed", "_Bool", "bool", "size_t", "ptrdiff_t",
        "const", "volatile", NULL};
    for (int i = 0; builtin_types[i]; i++)
        cx_typeset_add(&ctx.types, builtin_types[i]);

    CxMacroMap *map = (CxMacroMap *)cx_arena_alloc(arena, sizeof(CxMacroMap));
    char *expanded = cx_macro_expand(arena, src, len, filename, diags, map);
    CxNode *root = parse_source(&ctx, expanded ? expanded : src,
                                expanded ? strlen(expanded) : len, filename, 0);

    if (root) {
        if (ctx.prelude.data)
            root->prelude = cx_arena_strdup(arena, ctx.prelude.data);
        root->file = cx_arena_strdup(arena, filename);
        root->imports = ctx.imports;
        root->macro_map = map;
    }
    cx_buf_free(&ctx.prelude);
    cx_buf_free(&ctx.raw);
    cx_buf_free(&ctx.attr_body);
    cx_buf_free(&ctx.macro_args);
    cx_typeset_free(&ctx.types);
    free(ctx.active);
    return root;
}
