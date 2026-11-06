#ifndef CX_SYMBOLS_H
#define CX_SYMBOLS_H

#include "cx/ast.h"
#include "cx/diag.h"

/* One declared entity. `ns` is the language-level namespace path using "::"
 * (e.g. "io::net"); `qualified` is the display name (e.g. "io::net::Socket").
 * The C-level mangled name is produced on demand by cx_mangle(). */
typedef struct {
    char      *name;      /* bare identifier */
    char      *ns;        /* namespace path or NULL for global scope */
    char      *qualified; /* language-level qualified name */
    char      *type;      /* declared type, for vars and struct fields */
    CxNodeKind kind;
    CxSpan     span;
    const char *file;      /* source path the declaration was parsed from */
    CxNode    *decl;      /* originating AST node */
    CxNode    *dtor;      /* destructor body for structs, else NULL */
    CxNode    *ctor;      /* constructor body for structs, else NULL */
} CxSymbol;

typedef struct {
    CxSymbol **items;
    int        len;
    int        cap;
} CxSymbolTable;

CxSymbolTable *cx_sema_build(CxArena *arena, CxNode *tu, CxDiagList *diags);

CxSymbol *cx_sema_lookup(const CxSymbolTable *table, const char *qualified);
CxSymbol *cx_sema_find_struct(const CxSymbolTable *table, const char *name);
CxSymbol *cx_sema_find_function(const CxSymbolTable *table, const char *name);
CxSymbol *cx_sema_find_trait(const CxSymbolTable *table, const char *name);
CxSymbol *cx_sema_find_symbol(const CxSymbolTable *table, const char *name);

int       cx_sema_type_has_dtor(const CxSymbolTable *table, const char *type);
CxSymbol *cx_sema_dtor_for(const CxSymbolTable *table, const char *type);

/* RAII helper (implemented in src/sema/raii.c): the mangled name of the
 * destructor function for `type`, or NULL when the type has no destructor. */
char *cx_raii_dtor_name(CxArena *arena, const CxSymbolTable *table,
                        const char *type);

#endif /* CX_SYMBOLS_H */
