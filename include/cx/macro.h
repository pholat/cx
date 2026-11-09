#ifndef CX_MACRO_H
#define CX_MACRO_H

#include <stddef.h>

#include "cx/ast.h"
#include "cx/diag.h"

/* Token-level expansion of Cx `macro_rules!` definitions and invocations.
 *
 * The expander runs before the Cx parser: it removes `macro_rules!` items,
 * expands every known `name!( … )` invocation to source text, and feeds the
 * result back into the normal lexer/parser. Unknown `NAME!( … )` invocations
 * are left untouched (they are C macro passthrough handled by the lexer).
 *
 * Returns an arena-allocated NUL-terminated expanded source. Definitions are
 * removed; their line count is preserved with blank lines so later diagnostics
 * keep pointing at roughly the right place.
 */
/* One generated-name -> macro-definition mapping. When a transcription writes
 * `$name::method`, the expander emits the internal `Name__method` identifier and
 * records where `$name` appears in the macro definition. The LSP uses this so
 * "go to definition" on a macro-generated symbol lands in the macro body. */
typedef struct {
    char   *name; /* generated Cx name, e.g. "IntList__new" */
    CxSpan  span; /* source span in the macro definition */
    char   *file; /* file the macro definition lives in (imported `.hx`?) */
} CxMacroOrigin;

/* One `requires` binding observed at an invocation site: the concrete type that
 * a constrained metavariable was bound to, and a trait it must implement. The
 * semantic pass checks these after the AST is built (checked `requires`). `frag`
 * is the metavariable's fragment specifier (`ty`, `ident`, `path`, `expr`, …),
 * which selects the check: a type fragment requires `implements`, an
 * `ident`/`path` fragment names one of the trait's methods. */
typedef struct {
    char   *type;  /* concrete type text bound to the metavariable, e.g. "Row" */
    char   *trait; /* required trait name, e.g. "Iterable" */
    char   *frag;  /* fragment specifier bound in the pattern, e.g. "ident" */
    CxSpan  span;  /* invocation site, for diagnostics */
} CxMacroReq;

/* One `name!( … )` invocation of a Cx `macro_rules!` macro: where it was written
 * (`site`) and where the macro is defined (`def`). The LSP uses this so "go to
 * definition" on the invocation name lands in the `macro_rules!` definition. */
typedef struct {
    char   *name;     /* invocation name, e.g. "define_int_map" */
    CxSpan  site;     /* invocation site in the source (the name token) */
    CxSpan  def;      /* `macro_rules!` definition span */
    char   *def_file; /* file the `macro_rules!` definition lives in */
} CxMacroInvoke;

typedef struct CxMacroMap {
    CxMacroOrigin *items;
    int            len;
    int            cap;
    CxMacroReq    *reqs;   /* checked-`requires` bindings (one per trait) */
    int            nreqs;
    int            reqcap;
    CxMacroInvoke *invokes; /* `macro_rules!` invocations (site -> definition) */
    int            ninvokes;
    int            invcap;
} CxMacroMap;

/* The `macro_rules!` definition span for a generated name, or NULL. When
 * `file_out` is non-NULL it receives the file the definition lives in (may be a
 * different file than the one being edited when the macro came from an
 * imported `.hx`). */
const CxSpan *cx_macro_map_find(const CxMacroMap *map, const char *name,
                                const char **file_out);

/* The `macro_rules!` definition span for a macro invocation covering the
 * 1-based (line, column), or NULL. `file_out` (if non-NULL) receives the file
 * the definition lives in. */
const CxSpan *cx_macro_map_find_invocation(const CxMacroMap *map, int line,
                                           int column, const char **file_out);

/* `map` is optional; when provided it receives generated-name origins and the
 * concrete types bound to `requires`-constrained metavariables. */
char *cx_macro_expand(CxArena *arena, const char *src, size_t len,
                      const char *filename, CxDiagList *diags,
                      CxMacroMap *map);

#endif /* CX_MACRO_H */
