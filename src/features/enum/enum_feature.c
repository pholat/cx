/* Cx feature: enums.
 *
 * An enum is a named type; codegen emits it as a `typedef enum …` and mangles
 * the tag like a struct. The `#[str]` name helpers live in the sibling `str`
 * feature. */
#include "cx/cx.h"
#include "enum_feature.h"
#include "enum_parser.h"

/* -------------------------------------------------------------- parser hooks */

CxNode *cx_enum_decl(CxParseCtx *ctx, const char *name, CxVec members,
                     CxSpan span) {
    CxNode *n = cx_node_new(ctx->arena, CX_NODE_ENUM, span);
    n->name = (char *)name;
    n->children = members;
    return n;
}

CxNode *cx_enum_str_decl(CxParseCtx *ctx, const char *machine, const char *which,
                         CxSpan span) {
    CxNode *n = cx_node_new(ctx->arena, CX_NODE_ENUM_STR, span);
    n->name = (char *)machine;
    n->type = (char *)which;
    return n;
}

/* `typedef enum <tag> { <Enum>__A, <Enum>__B = 2 } <tag>;` — enumerators are
 * prefixed with the *bare* enum name so `Enum::A` (which the parser lowers to
 * `Enum__A`) resolves regardless of the namespace. */
void emit_enum_def(Emit *e, CxNode *n, const char *ns) {
    char *q = cx_mangle(e->arena, ns, n->name);
    cx_emit_line(e->out, n->span.first_line, e->file);
    ind(e);
    cx_buf_printf(e->out, "typedef enum %s {\n", q);
    e->indent++;
    for (int i = 0; i < n->children.len; i++) {
        CxNode *m = n->children.items[i];
        ind(e);
        cx_buf_printf(e->out, "%s__%s", n->name, m->name);
        if (m->init) {
            cx_buf_puts(e->out, " = ");
            emit_expr(e, m->init);
        }
        if (i + 1 < n->children.len) cx_buf_putc(e->out, ',');
        cx_buf_putc(e->out, '\n');
    }
    e->indent--;
    ind(e);
    cx_buf_printf(e->out, "} %s;\n", q);
}

/* Codegen: the enum definition is a phase-1 (types) emission. */
static int enum_emit(Emit *e, CxNode *n, const char *ns, int phase) {
    if (n->kind != CX_NODE_ENUM || phase != 1) return 0;
    emit_enum_def(e, n, ns);
    return 1;
}

/* Sema: register the enum as a named type. */
static int enum_collect(BuildCtx *c, CxNode *n, const char *ns,
                        const char *file) {
    if (n->kind != CX_NODE_ENUM) return 0;
    CxSymbol *s = symbol_new(c->arena, n, ns, CX_NODE_ENUM, file);
    table_push(c->table, s);
    return 1;
}

const CxFeature cx_feature_enum = {"enum", enum_collect, enum_emit};
