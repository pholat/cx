/* Cx `macro_rules!` — driver: definitions, invocation scanning, assembly, API.
 *
 * This is intentionally a *textual* pre-parse pass:
 *
 *   1. the whole source is tokenised into a small token-tree stream (macro_lex),
 *   2. every `macro_rules! name { … }` definition is collected and removed,
 *   3. every invocation `name!( … )` of a known macro is matched against the
 *      rules (macro_match) and replaced by the transcribed stream
 *      (macro_transcribe),
 *   4. the result is fed back to the normal Cx parser.
 *
 * Unknown `NAME!( … )` invocations are left untouched, so the C-macro
 * passthrough implemented by the lexer keeps working.
 *
 * Implemented: fragment specifiers `ident`, `ty`, `expr`, `stmt`, `tt`, `lit`,
 * `path`, `field`; repetitions `$( … )*` / `$( … )+` with an optional single
 * separator (`$( … ),*`); and `##` identifier concatenation. Nested macro
 * invocations are expanded by repeated passes.
 *
 * Hygiene is not implemented yet: identifiers introduced by a macro body are
 * emitted verbatim (the `$…` fragments are, of course, substituted).
 */
#include "cx/cx.h"
#include "macro_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== macros */

typedef struct {
    MPat  **pat;
    int     npat;
    MTrans **tr;
    int     ntr;
    MReq   *reqs;
    int     nreqs;
} MRule;

typedef struct {
    char      *name;
    MRule     *rules;
    int        nrules;
    int        cap;
    CxSpan     span;
    const char *file; /* file the `macro_rules!` definition lives in */
} Macro;

typedef struct {
    Macro *items;
    int    len;
    int    cap;
} MacroTable;

static Macro *find_macro(MacroTable *t, const char *name) {
    for (int i = 0; i < t->len; i++)
        if (t->items[i].name && !strcmp(t->items[i].name, name))
            return &t->items[i];
    return NULL;
}

static Macro *table_add(CxArena *a, MacroTable *t, const char *name,
                        CxSpan span) {
    if (t->len == t->cap) {
        int cap = t->cap ? t->cap * 2 : 8;
        Macro *items = (Macro *)cx_arena_alloc(a, sizeof(Macro) * (size_t)cap);
        if (!items) return NULL;
        if (t->items && t->len)
            memcpy(items, t->items, sizeof(Macro) * (size_t)t->len);
        t->items = items;
        t->cap = cap;
    }
    Macro *m = &t->items[t->len++];
    memset(m, 0, sizeof *m);
    m->name = (char *)name;
    m->span = span;
    return m;
}

static void macro_add_rule(CxArena *a, Macro *m, MRule r) {
    if (m->nrules == m->cap) {
        int cap = m->cap ? m->cap * 2 : 4;
        MRule *rules = (MRule *)cx_arena_alloc(a, sizeof(MRule) * (size_t)cap);
        if (!rules) return;
        if (m->rules && m->nrules)
            memcpy(rules, m->rules, sizeof(MRule) * (size_t)m->nrules);
        m->rules = rules;
        m->cap = cap;
    }
    m->rules[m->nrules++] = r;
}

void macro_map_add(CxArena *a, CxMacroMap *map, const char *name,
                   CxSpan span, const char *file) {
    if (!map || !name) return;
    for (int i = 0; i < map->len; i++)
        if (map->items[i].name && !strcmp(map->items[i].name, name)) return;
    if (map->len == map->cap) {
        int cap = map->cap ? map->cap * 2 : 8;
        CxMacroOrigin *items =
            (CxMacroOrigin *)cx_arena_alloc(a, sizeof(CxMacroOrigin) * (size_t)cap);
        if (!items) return;
        if (map->items && map->len)
            memcpy(items, map->items, sizeof(CxMacroOrigin) * (size_t)map->len);
        map->items = items;
        map->cap = cap;
    }
    map->items[map->len].name = cx_arena_strdup(a, name);
    map->items[map->len].span = span;
    map->items[map->len].file = file ? cx_arena_strdup(a, file) : NULL;
    map->len++;
}

const CxSpan *cx_macro_map_find(const CxMacroMap *map, const char *name,
                                const char **file_out) {
    if (file_out) *file_out = NULL;
    if (!map || !name) return NULL;
    for (int i = 0; i < map->len; i++)
        if (map->items[i].name && !strcmp(map->items[i].name, name)) {
            if (file_out) *file_out = map->items[i].file;
            return &map->items[i].span;
        }
    return NULL;
}

/* Record a `requires` binding: `type_text` was bound to a metavariable that must
 * implement `trait` (or, for an `ident`/`path` fragment, name one of its
 * methods). Duplicates (same type + trait) are ignored. */
void macro_req_add(CxArena *a, CxMacroMap *map, const char *type_text,
                   const char *trait, const char *frag, CxSpan span) {
    if (!map || !type_text || !trait) return;
    for (int i = 0; i < map->nreqs; i++)
        if (map->reqs[i].type && map->reqs[i].trait &&
            !strcmp(map->reqs[i].type, type_text) &&
            !strcmp(map->reqs[i].trait, trait))
            return;
    if (map->nreqs == map->reqcap) {
        int cap = map->reqcap ? map->reqcap * 2 : 8;
        CxMacroReq *items =
            (CxMacroReq *)cx_arena_alloc(a, sizeof(CxMacroReq) * (size_t)cap);
        if (!items) return;
        if (map->reqs && map->nreqs)
            memcpy(items, map->reqs, sizeof(CxMacroReq) * (size_t)map->nreqs);
        map->reqs = items;
        map->reqcap = cap;
    }
    map->reqs[map->nreqs].type = cx_arena_strdup(a, type_text);
    map->reqs[map->nreqs].trait = cx_arena_strdup(a, trait);
    map->reqs[map->nreqs].frag = frag ? cx_arena_strdup(a, frag) : NULL;
    map->reqs[map->nreqs].span = span;
    map->nreqs++;
}

/* Record a `macro_rules!` invocation: the name token's site and the macro's
 * definition span. Deduplicated by (name, site). */
void macro_invoke_add(CxArena *a, CxMacroMap *map, const char *name,
                      CxSpan site, CxSpan def, const char *def_file) {
    if (!map || !name) return;
    for (int i = 0; i < map->ninvokes; i++)
        if (map->invokes[i].name && !strcmp(map->invokes[i].name, name) &&
            map->invokes[i].site.first_line == site.first_line &&
            map->invokes[i].site.first_column == site.first_column)
            return;
    if (map->ninvokes == map->invcap) {
        int cap = map->invcap ? map->invcap * 2 : 8;
        CxMacroInvoke *items =
            (CxMacroInvoke *)cx_arena_alloc(a, sizeof(CxMacroInvoke) * (size_t)cap);
        if (!items) return;
        if (map->invokes && map->ninvokes)
            memcpy(items, map->invokes,
                   sizeof(CxMacroInvoke) * (size_t)map->ninvokes);
        map->invokes = items;
        map->invcap = cap;
    }
    map->invokes[map->ninvokes].name = cx_arena_strdup(a, name);
    map->invokes[map->ninvokes].site = site;
    map->invokes[map->ninvokes].def = def;
    map->invokes[map->ninvokes].def_file =
        def_file ? cx_arena_strdup(a, def_file) : NULL;
    map->ninvokes++;
}

const CxSpan *cx_macro_map_find_invocation(const CxMacroMap *map, int line,
                                           int column, const char **file_out) {
    if (file_out) *file_out = NULL;
    if (!map) return NULL;
    for (int i = 0; i < map->ninvokes; i++) {
        CxSpan s = map->invokes[i].site;
        if (line < s.first_line || line > s.last_line) continue;
        if (line == s.first_line && column < s.first_column) continue;
        if (line == s.last_line && column > s.last_column) continue;
        if (file_out) *file_out = map->invokes[i].def_file;
        return &map->invokes[i].def;
    }
    return NULL;
}

/* ============================================================== definitions */

typedef struct {
    char **items;
    int    len;
    int    cap;
} NameVec;

static void name_push(CxArena *a, NameVec *v, char *name) {
    if (v->len == v->cap) {
        int cap = v->cap ? v->cap * 2 : 4;
        char **items = (char **)cx_arena_alloc(a, sizeof(char *) * (size_t)cap);
        if (!items) return;
        if (v->items && v->len)
            memcpy(items, v->items, sizeof(char *) * (size_t)v->len);
        v->items = items;
        v->cap = cap;
    }
    v->items[v->len++] = name;
}

/* Optional `requires $x: TraitA + TraitB, $y: TraitC` between the pattern group
 * and `=>`. The requirements are attached to metavariables and recorded on the
 * rule; at each invocation the concrete types bound to those metavariables are
 * recorded on the `CxMacroMap` so sema can check conformance. */
static int parse_requires(CxArena *a, CxDiagList *diags, MToken **toks, int n,
                          int *pi, MRule *rule) {
    int i = *pi;
    if (!(i < n && toks[i]->kind == MT_IDENT && toks[i]->text &&
          !strcmp(toks[i]->text, "requires")))
        return 1; /* no clause */

    MToken *kw = toks[i];
    i++;

    if (i < n && toks[i]->kind == MT_IDENT && toks[i]->text &&
        (!strcmp(toks[i]->text, "trait") || !strcmp(toks[i]->text, "traits"))) {
        cx_error(diags, tok_span(toks[i]),
                 "attach a requirement to a metavariable, e.g. "
                 "`requires $x: Trait`");
        return 0;
    }

    ReqVec reqs = {0};
    for (;;) {
        if (!(i + 2 < n && tok_is_punct(toks[i], "$") &&
              toks[i + 1]->kind == MT_IDENT && tok_is_punct(toks[i + 2], ":"))) {
            cx_error(diags, tok_span(i < n ? toks[i] : kw),
                     "expected `$metavariable: Trait` in requires clause");
            return 0;
        }
        MReq req;
        memset(&req, 0, sizeof req);
        req.var = toks[i + 1]->text;
        i += 3;

        if (i >= n || toks[i]->kind != MT_IDENT) {
            cx_error(diags, tok_span(i < n ? toks[i] : kw),
                     "expected a trait name after ':' in requires clause");
            return 0;
        }

        NameVec tr = {0};
        name_push(a, &tr, toks[i]->text);
        i++;
        while (i + 1 < n && tok_is_punct(toks[i], "+") &&
               toks[i + 1]->kind == MT_IDENT) {
            name_push(a, &tr, toks[i + 1]->text);
            i += 2;
        }
        req.traits = tr.items;
        req.ntraits = tr.len;
        req_push(a, &reqs, req);

        if (i < n && tok_is_punct(toks[i], ",")) {
            i++;
            if (i < n && tok_is_punct(toks[i], "=>")) break; /* trailing comma */
            continue;
        }
        break;
    }

    rule->reqs = reqs.items;
    rule->nreqs = reqs.len;
    *pi = i;
    return 1;
}

static int parse_macro_body(CxArena *a, CxDiagList *diags, Macro *mac,
                            MToken *body) {
    int i = 0;
    while (i < body->nkids) {
        while (i < body->nkids &&
               (tok_is_punct(body->kids[i], ";") ||
                tok_is_punct(body->kids[i], ",")))
            i++;
        if (i >= body->nkids) break;

        if (body->kids[i]->kind != MT_GROUP) {
            cx_error(diags, tok_span(body->kids[i]),
                     "macro '%s': expected a pattern group", mac->name);
            return 0;
        }
        MToken *patgrp = body->kids[i++];
        PatVec pat = parse_pat_group(a, diags, patgrp);

        MRule r;
        memset(&r, 0, sizeof r);
        r.pat = pat.items;
        r.npat = pat.len;

        if (!parse_requires(a, diags, body->kids, body->nkids, &i, &r))
            return 0;

        if (i >= body->nkids || !tok_is_punct(body->kids[i], "=>")) {
            cx_error(diags, tok_span(patgrp),
                     "macro '%s': expected '=>' after pattern", mac->name);
            return 0;
        }
        i++;

        if (i >= body->nkids || body->kids[i]->kind != MT_GROUP) {
            cx_error(diags, tok_span(patgrp),
                     "macro '%s': expected a transcription group", mac->name);
            return 0;
        }
        MToken *trgrp = body->kids[i++];

        TrVec tr = parse_tr_group(a, diags, trgrp);
        r.tr = tr.items;
        r.ntr = tr.len;

        for (int k = 0; k < r.nreqs; k++) {
            if (!pat_has_var(r.pat, r.npat, r.reqs[k].var))
                cx_error(diags, tok_span(patgrp),
                         "requires names unknown metavariable '$%s'",
                         r.reqs[k].var);
        }

        macro_add_rule(a, mac, r);
    }
    return mac->nrules > 0;
}

/* ================================================================== scanning */

typedef struct {
    size_t start;
    size_t end;
    char  *text; /* replacement, NULL means "remove" */
} Repl;

typedef struct {
    Repl *items;
    int   len;
    int   cap;
} ReplVec;

static void repl_push(CxArena *a, ReplVec *v, size_t start, size_t end,
                      char *text) {
    if (v->len == v->cap) {
        int cap = v->cap ? v->cap * 2 : 16;
        Repl *items = (Repl *)cx_arena_alloc(a, sizeof(Repl) * (size_t)cap);
        if (!items) return;
        if (v->items && v->len)
            memcpy(items, v->items, sizeof(Repl) * (size_t)v->len);
        v->items = items;
        v->cap = cap;
    }
    v->items[v->len].start = start;
    v->items[v->len].end = end;
    v->items[v->len].text = text;
    v->len++;
}

/* Replace a removed definition with the same number of newlines so subsequent
 * source lines keep their original line numbers. */
static char *blank_lines(CxArena *a, const char *src, size_t start, size_t end) {
    size_t len = strlen(src);
    int n = 0;
    for (size_t i = start; i < end && i < len; i++)
        if (src[i] == '\n') n++;
    if (n <= 0) return NULL;
    char *s = (char *)cx_arena_alloc(a, (size_t)n + 1);
    if (!s) return NULL;
    for (int i = 0; i < n; i++) s[i] = '\n';
    s[n] = '\0';
    return s;
}

static int is_macro_def_start(MToken **t, int i, int n) {
    return i + 3 < n && t[i]->kind == MT_IDENT &&
           t[i]->text && !strcmp(t[i]->text, "macro_rules") &&
           tok_is_punct(t[i + 1], "!") && t[i + 2]->kind == MT_IDENT &&
           t[i + 3]->kind == MT_GROUP && t[i + 3]->open == '{';
}

/* Collect all `macro_rules!` definitions (recursively, at any block scope). */
static void scan_defs(CxArena *a, CxDiagList *diags, const char *src,
                      MToken **toks, int n, MacroTable *tab, ReplVec *reps,
                      int *found, const char *file) {
    for (int i = 0; i < n; i++) {
        if (is_macro_def_start(toks, i, n)) {
            MToken *name = toks[i + 2];
            MToken *body = toks[i + 3];
            CxSpan span = tok_span(toks[i]);
            Macro *mac = find_macro(tab, name->text);
            if (mac) {
                /* Redefinition: the newest rule set replaces the old one. */
                mac->nrules = 0;
                mac->rules = NULL;
                mac->cap = 0;
                mac->span = span;
                mac->file = file ? cx_arena_strdup(a, file) : NULL;
            } else {
                mac = table_add(a, tab, name->text, span);
                if (mac) mac->file = file ? cx_arena_strdup(a, file) : NULL;
            }
            if (!mac) return;
            if (!parse_macro_body(a, diags, mac, body)) {
                cx_error(diags, span, "macro '%s' has no rules", name->text);
            }
            /* An attribute immediately before the definition belongs to it. */
            size_t start = toks[i]->start;
            for (int k = i - 1; k >= 0; k--) {
                if (toks[k]->kind == MT_OPAQUE && toks[k]->text &&
                    toks[k]->text[0] == '#' && toks[k]->text[1] == '[')
                    start = toks[k]->start;
                else
                    break;
            }
            /* Consume an optional trailing `;` so it does not become a stray
             * empty statement at file scope. */
            size_t end = body->end;
            int skip = 3;
            if (i + 4 < n && tok_is_punct(toks[i + 4], ";")) {
                end = toks[i + 4]->end;
                skip = 4;
            }
            repl_push(a, reps, start, end, blank_lines(a, src, start, end));
            *found = 1;
            i += skip;
            continue;
        }
        if (toks[i]->kind == MT_GROUP)
            scan_defs(a, diags, src, toks[i]->kids, toks[i]->nkids, tab, reps,
                      found, file);
    }
}

/* ============================================== macros across import boundaries */

typedef struct {
    char **items;
    int    len;
    int    cap;
} PathVec;

static void path_push(CxArena *a, PathVec *v, const char *s) {
    if (v->len == v->cap) {
        int cap = v->cap ? v->cap * 2 : 8;
        char **items = (char **)cx_arena_alloc(a, sizeof(char *) * (size_t)cap);
        if (!items) return;
        if (v->items && v->len)
            memcpy(items, v->items, sizeof(char *) * (size_t)v->len);
        v->items = items;
        v->cap = cap;
    }
    v->items[v->len++] = (char *)s;
}

/* Strip the surrounding quotes from a string token. */
static const char *str_unquote(CxArena *a, const char *s) {
    size_t n = s ? strlen(s) : 0;
    if (n >= 2 && s[0] == '"' && s[n - 1] == '"')
        return cx_arena_strndup(a, s + 1, n - 2);
    return s;
}

/* Collect the paths of every `import "…"` in a token tree (recursively). */
static void find_imports(CxArena *a, MToken **toks, int n, PathVec *out) {
    for (int i = 0; i < n; i++) {
        MToken *t = toks[i];
        if (t->kind == MT_IDENT && t->text && !strcmp(t->text, "import") &&
            i + 1 < n && toks[i + 1]->kind == MT_STRING) {
            path_push(a, out, str_unquote(a, toks[i + 1]->text));
        }
        if (t->kind == MT_GROUP) find_imports(a, t->kids, t->nkids, out);
    }
}

/* Resolve `path` relative to `base_file` (a plain path or a `file://` URI). */
static void resolve_rel(const char *base_file, const char *path, char *out,
                        size_t n) {
    const char *bf = base_file ? base_file : "";
    int is_uri = strncmp(bf, "file://", 7) == 0;
    const char *dir_file = is_uri ? bf + 7 : bf;
    if (path[0] == '/') {
        snprintf(out, n, "%s", path);
        return;
    }
    const char *slash = strrchr(dir_file, '/');
    if (slash)
        snprintf(out, n, "%.*s/%s", (int)(slash - dir_file), dir_file, path);
    else
        snprintf(out, n, "%s", path);
}

/* Read a whole file into malloc'd storage (NUL-terminated), or NULL. */
static char *read_file(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    if (len_out) *len_out = got;
    return buf;
}

/* Collect `macro_rules!` definitions from `src` (when `collect_own`) and,
 * recursively, from the files it imports, into `tab`. This is what lets a `.hx`
 * export a macro to its importer. `depth` bounds import cycles. */
static void collect_imports_from(CxArena *a, CxDiagList *diags, const char *src,
                                 size_t len, const char *filename, MacroTable *tab,
                                 int depth, int collect_own) {
    if (!src || !filename || depth > 16) return;

    LexCtx lx;
    memset(&lx, 0, sizeof lx);
    lx.arena = a;
    lx.diags = diags;
    lx.src = src;
    lx.len = len;
    lx.line = 1;
    lx.col = 1;
    MTVec toks = {0};
    lex_tokens(&lx, '\0', &toks);

    if (collect_own) {
        ReplVec reps = {0};
        int found = 0;
        scan_defs(a, diags, src, toks.items, toks.len, tab, &reps, &found,
                  filename);
    }

    PathVec paths = {0};
    find_imports(a, toks.items, toks.len, &paths);
    for (int i = 0; i < paths.len; i++) {
        char resolved[1024];
        resolve_rel(filename, paths.items[i], resolved, sizeof resolved);
        size_t n = 0;
        char *buf = read_file(resolved, &n);
        if (!buf) continue;
        collect_imports_from(a, diags, buf, n, resolved, tab, depth + 1, 1);
        free(buf);
    }
}

static char *expand_invocation(CxArena *a, CxDiagList *diags, Macro *mac,
                               MToken *args, CxSpan span, CxMacroMap *map) {
    for (int r = 0; r < mac->nrules; r++) {
        MRule *rule = &mac->rules[r];
        MEnv env = {0};
        int pos = 0;
        if (match_seq(a, diags, rule->pat, rule->npat, args->kids, args->nkids,
                      &pos, &env) &&
            pos == args->nkids) {
            /* Record the invocation itself so "go to definition" on its name
             * lands in the `macro_rules!` definition. */
            if (map)
                macro_invoke_add(a, map, mac->name, span, mac->span, mac->file);
            /* Checked `requires`: record the concrete type each constrained
             * metavariable was bound to, so the semantic pass can verify that
             * the type implements the required trait(s). */
            if (map) {
                for (int q = 0; q < rule->nreqs; q++) {
                    MReq *rq = &rule->reqs[q];
                    MBind *b = env_get(&env, rq->var);
                    if (!b) continue;
                    const char *frag =
                        pat_find_frag(rule->pat, rule->npat, rq->var);
                    for (int ci = 0; ci < b->ncaps; ci++) {
                        CxBuf tb;
                        cx_buf_init(&tb);
                        render_tokens(&tb, b->caps[ci], b->lens[ci]);
                        for (int t = 0; t < rq->ntraits; t++)
                            macro_req_add(a, map, tb.data ? tb.data : "",
                                          rq->traits[t], frag, span);
                        cx_buf_free(&tb);
                    }
                }
            }
            CxBuf buf;
            cx_buf_init(&buf);
            Out out = {&buf, 0};
            transcribe(a, diags, span, rule->pat, rule->npat, rule->tr,
                       rule->ntr, &env, NULL, 0, &out, map, mac->file);
            char *result = cx_arena_strdup(a, buf.data ? buf.data : "");
            cx_buf_free(&buf);
            return result;
        }
    }
    cx_error(diags, span, "no matching rule for macro '%s'", mac->name);
    return cx_arena_strdup(a, "");
}

/* Heuristic: does the `{ … }` group at `idx` introduce item declarations
 * (namespace/struct/trait/implements) rather than statements? Used only to
 * decide whether the `;` after an item-position macro invocation belongs to
 * the invocation (declarations already carry their own `;`). */
static int group_item_scope(MToken **toks, int idx) {
    for (int j = idx - 1; j >= 0; j--) {
        MToken *t = toks[j];
        if (t->kind == MT_GROUP && t->close == '}') break;
        if (t->kind == MT_PUNCT &&
            (tok_is_punct(t, ";") || tok_is_punct(t, "}") ||
             tok_is_punct(t, "{")))
            break;
        if (t->kind == MT_IDENT &&
            (!strcmp(t->text, "namespace") || !strcmp(t->text, "struct") ||
             !strcmp(t->text, "trait") || !strcmp(t->text, "implements")))
            return 1;
        if (tok_is_punct(t, ")")) return 0;
    }
    return 0;
}

static void scan_invocations(CxArena *a, CxDiagList *diags, MToken **toks,
                             int n, MacroTable *tab, ReplVec *reps, int *found,
                             int item_scope, CxMacroMap *map) {
    for (int i = 0; i < n; i++) {
        if (is_macro_def_start(toks, i, n)) {
            i += 3; /* skip the definition body */
            continue;
        }
        if (toks[i]->kind == MT_IDENT && i + 2 < n &&
            tok_is_punct(toks[i + 1], "!") && toks[i + 2]->kind == MT_GROUP) {
            Macro *mac = find_macro(tab, toks[i]->text);
            if (mac) {
                MToken *args = toks[i + 2];
                char *text = expand_invocation(a, diags, mac, args,
                                               tok_span_full(toks[i]), map);
                size_t end = args->end;
                int skip = 2;
                /* At item scope a bare `name!(…);` invocation owns its
                 * trailing semicolon: the expansion supplies complete
                 * declarations. In expression position the `;` is the
                 * statement terminator and must stay. */
                int at_item_start =
                    item_scope &&
                    (i == 0 || tok_is_punct(toks[i - 1], ";") ||
                     tok_is_punct(toks[i - 1], "}") ||
                     (toks[i - 1]->kind == MT_GROUP &&
                      toks[i - 1]->close == '}'));
                if (at_item_start && i + 3 < n &&
                    tok_is_punct(toks[i + 3], ";")) {
                    end = toks[i + 3]->end;
                    skip = 3;
                }
                repl_push(a, reps, toks[i]->start, end, text);
                *found = 1;
                i += skip;
                continue;
            }
        }
        if (toks[i]->kind == MT_GROUP) {
            int child_item = 0;
            if (toks[i]->open == '{')
                child_item = group_item_scope(toks, i);
            scan_invocations(a, diags, toks[i]->kids, toks[i]->nkids, tab, reps,
                             found, child_item, map);
        }
    }
}

/* ================================================================ assembling */

static int repl_cmp(const void *pa, const void *pb) {
    const Repl *a = (const Repl *)pa;
    const Repl *b = (const Repl *)pb;
    if (a->start < b->start) return -1;
    if (a->start > b->start) return 1;
    return 0;
}

static void buf_append(CxBuf *b, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) cx_buf_putc(b, s[i]);
}

static char *apply_repls(CxArena *a, const char *src, size_t len,
                         ReplVec *reps) {
    if (reps->len > 1)
        qsort(reps->items, (size_t)reps->len, sizeof(Repl), repl_cmp);

    CxBuf out;
    cx_buf_init(&out);
    size_t pos = 0;
    for (int i = 0; i < reps->len; i++) {
        Repl *r = &reps->items[i];
        if (r->start < pos) continue; /* overlapping replacement */
        if (r->start > len) break;
        if (r->start > pos) {
            buf_append(&out, src + pos, r->start - pos);
            pos = r->start;
        }
        if (r->text) cx_buf_puts(&out, r->text);
        pos = r->end > pos ? r->end : pos;
    }
    if (pos < len) buf_append(&out, src + pos, len - pos);

    char *result = cx_arena_strdup(a, out.data ? out.data : "");
    cx_buf_free(&out);
    return result;
}

/* ------------------------------------------------------------------ API */

char *cx_macro_expand(CxArena *arena, const char *src, size_t len,
                      const char *filename, CxDiagList *diags,
                      CxMacroMap *map) {
    if (!src) return cx_arena_strdup(arena, "");

    MacroTable table = {0};
    /* An imported `.hx` may export `macro_rules!`: collect definitions from the
     * files this one imports before expanding. The current file's own
     * definitions are collected by the loop below (so they take precedence). */
    collect_imports_from(arena, diags, src, len, filename, &table, 0, 0);

    char *cur = cx_arena_strndup(arena, src, len);

    for (int pass = 0; pass < 64; pass++) {
        size_t cur_len = strlen(cur);

        LexCtx lx;
        memset(&lx, 0, sizeof lx);
        lx.arena = arena;
        lx.diags = diags;
        lx.src = cur;
        lx.len = cur_len;
        lx.line = 1;
        lx.col = 1;

        MTVec toks = {0};
        lex_tokens(&lx, '\0', &toks);

        ReplVec reps = {0};
        int found = 0;

        scan_defs(arena, diags, cur, toks.items, toks.len, &table, &reps,
                  &found, filename);
        scan_invocations(arena, diags, toks.items, toks.len, &table, &reps,
                         &found, 1, map);

        if (!found) return cur;

        char *next = apply_repls(arena, cur, cur_len, &reps);
        if (!next || strcmp(next, cur) == 0) return next ? next : cur;
        cur = next;
    }

    cx_error(diags, (CxSpan){1, 1, 1, 1},
             "macro expansion exceeded the maximum depth");
    return cur;
}
