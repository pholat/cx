#ifndef CX_MACRO_INTERNAL_H
#define CX_MACRO_INTERNAL_H

/* Private interface shared by the `src/macro/` translation units:
 *
 *   macro_lex.c         source text  -> token tree (MToken)
 *   macro_match.c       pattern + tokens -> bindings (MEnv)
 *   macro_transcribe.c  transcription + bindings -> source text
 *   macro_driver.c      definitions, invocation scanning, assembly, API
 *
 * Only the public surface in `cx/macro.h` is visible outside this directory. */

#include "cx/macro.h"
#include "cx/buf.h"

#include <stddef.h>

/* =================================================================== tokens */

typedef enum {
    MT_IDENT,
    MT_NUM,
    MT_STRING,
    MT_PUNCT,
    MT_GROUP,
    MT_OPAQUE /* preprocessor line, `raw { … }`, `#[…]` */
} MTKind;

typedef struct MToken MToken;

struct MToken {
    MTKind  kind;
    char   *text;  /* leaf text (ident/punct/literal/opaque) */
    char    open;  /* group delimiters */
    char    close;
    MToken **kids;
    int     nkids;
    int     line;
    int     col;
    size_t  start; /* byte offsets in the source buffer */
    size_t  end;
};

typedef struct {
    MToken **items;
    int      len;
    int      cap;
} MTVec;

typedef struct {
    CxArena    *arena;
    CxDiagList *diags;
    const char *src;
    size_t      len;
    size_t      pos;
    int         line;
    int         col;
} LexCtx;

/* --------------------------------------------------------------- tokenizer */

void   lex_tokens(LexCtx *l, char stop, MTVec *out);
int    is_ident_start(char c);
int    tok_is_punct(const MToken *t, const char *s);
int    tok_is(const MToken *t, const char *s);
CxSpan tok_span(const MToken *t);
CxSpan tok_span_full(const MToken *t);

/* ================================================================ patterns */

typedef struct MPat MPat; /* defined in macro_match.c */

typedef struct {
    MPat **items;
    int    len;
    int    cap;
} PatVec;

typedef struct {
    char  *var;
    char **traits;
    int    ntraits;
} MReq;

typedef struct {
    MReq *items;
    int   len;
    int   cap;
} ReqVec;

typedef struct MTrans MTrans; /* defined in macro_transcribe.c */

typedef struct {
    MTrans **items;
    int      len;
    int      cap;
} TrVec;

/* Transcription output sink: `join` suppresses the next inter-token space
 * after a `##` concatenation. */
typedef struct {
    CxBuf *out;
    int    join;
} Out;

/* ================================================================= matching */

typedef struct {
    char     *name;
    MToken ***caps; /* one token list per repetition iteration */
    int      *lens;
    int     **paths;  /* repetition index path per capture (nested repetitions) */
    int      *depths; /* length of each capture's path */
    int       ncaps;
    int       cap;
} MBind;

typedef struct {
    MBind *items;
    int    len;
    int    cap;
    int   *path;    /* current repetition index path (nested repetitions) */
    int    npath;
    int    pathcap;
} MEnv;

/* ------------------------------------------- pattern parsing (macro_match.c) */

PatVec      parse_pat_group(CxArena *a, CxDiagList *diags, MToken *g);
int         pat_has_var(MPat **pats, int n, const char *name);
const char *pat_find_frag(MPat **pats, int n, const char *name);
void        req_push(CxArena *a, ReqVec *v, MReq req);

/* --------------------------------------------------- matching (macro_match.c) */

int     match_seq(CxArena *a, CxDiagList *diags, MPat **pats, int np,
                  MToken **in, int nin, int *pos, MEnv *env);
MBind  *env_get(MEnv *e, const char *name);
int     bind_find(const MBind *b, const int *path, int npath);
MToken *single_ident_cap(MBind *b, const int *path, int npath);

/* ---------------------------------------------- transcription (transcribe.c) */

TrVec parse_tr_group(CxArena *a, CxDiagList *diags, MToken *g);
void  transcribe(CxArena *a, CxDiagList *diags, CxSpan span, MPat **pat,
                 int npat, MTrans **tr, int ntr, MEnv *env, const int *path,
                 int npath, Out *o, CxMacroMap *map, const char *def_file);
/* Render a captured token slice as source text (recovers a `requires` type). */
void  render_tokens(CxBuf *b, MToken **toks, int n);

/* ------------------------------------------------- map recording (driver.c) */

void macro_map_add(CxArena *a, CxMacroMap *map, const char *name, CxSpan span,
                   const char *file);
void macro_req_add(CxArena *a, CxMacroMap *map, const char *type_text,
                   const char *trait, const char *frag, CxSpan span);
void macro_invoke_add(CxArena *a, CxMacroMap *map, const char *name, CxSpan site,
                      CxSpan def, const char *def_file);

#endif /* CX_MACRO_INTERNAL_H */
