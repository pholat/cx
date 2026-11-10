/* Cx feature: `sm` state machines.
 *
 * The verbatim `sm` body is captured by the lexer (`SMCAP`) and parsed here
 * into `<M>__State`/`<M>__Event` enums, the machine struct, the callback
 * typedefs and the `init`/`reset`/`get_state`/`process_event` functions. The
 * `#[str]` name helpers live in the sibling `str` feature. */
#include "cx/cx.h"
#include "fsm_feature.h"
#include "fsm_parser.h"

#include <string.h>

/* -------------------------------------------------------------- parser hook */

/* Register the machine name as a type (so `Name m;` is a valid declaration) and
 * build the `CX_NODE_FSM` node carrying the verbatim transition body. */
CxNode *cx_fsm_decl(CxParseCtx *ctx, const char *name, const char *body,
                    CxSpan span) {
    cx_typeset_add(&ctx->types, name);
    CxNode *n = cx_node_new(ctx->arena, CX_NODE_FSM, span);
    n->name = (char *)name;
    n->type = (char *)body;
    return n;
}

static void fsm_names_add(CxArena *a, FsmNames *v, const char *name) {
    if (!name || !*name) return;
    for (int i = 0; i < v->len; i++)
        if (strcmp(v->names[i], name) == 0) return;
    if (v->len == v->cap) {
        int cap = v->cap ? v->cap * 2 : 8;
        const char **items =
            (const char **)cx_arena_alloc(a, sizeof(char *) * (size_t)cap);
        if (!items) return;
        if (v->names && v->len)
            memcpy(items, v->names, sizeof(char *) * (size_t)v->len);
        v->names = items;
        v->cap = cap;
    }
    v->names[v->len++] = cx_arena_strdup(a, name);
}

static const char *fsm_skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

/* Read a state/event name: an identifier, or a bracketed pseudo-state
 * `[*]`/`[any]`/`[current]` (returned as "*"/"any"/"current"). */
static const char *fsm_read_name(CxArena *a, const char **pp) {
    const char *p = fsm_skip_ws(*pp);
    char buf[128];
    int n = 0;
    if (*p == '[') {
        p++;
        p = fsm_skip_ws(p);
        while (*p && *p != ']' && n < (int)sizeof buf - 1) buf[n++] = *p++;
        while (n > 0 && (buf[n - 1] == ' ' || buf[n - 1] == '\t')) n--;
        buf[n] = '\0';
        if (*p == ']') p++;
        *pp = p;
        if (strcmp(buf, "*") == 0) return "*";
        if (strcmp(buf, "any") == 0) return "any";
        if (strcmp(buf, "current") == 0) return "current";
        return cx_arena_strdup(a, buf);
    }
    while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
           (*p >= '0' && *p <= '9') || *p == '_') {
        if (n < (int)sizeof buf - 1) buf[n++] = *p;
        p++;
    }
    buf[n] = '\0';
    *pp = p;
    return cx_arena_strdup(a, buf);
}

static FsmTrans *fsm_new_trans(CxArena *a, Fsm *f) {
    if (f->ntrans == f->tcap) {
        int cap = f->tcap ? f->tcap * 2 : 8;
        FsmTrans *items =
            (FsmTrans *)cx_arena_alloc(a, sizeof(FsmTrans) * (size_t)cap);
        if (!items) return NULL;
        if (f->trans && f->ntrans)
            memcpy(items, f->trans, sizeof(FsmTrans) * (size_t)f->ntrans);
        f->trans = items;
        f->tcap = cap;
    }
    FsmTrans *t = &f->trans[f->ntrans++];
    memset(t, 0, sizeof *t);
    return t;
}

/* Copy `src` with line and block comments replaced by a space, so the
 * transition scanner never mistakes comment text for a transition. */
static char *fsm_strip_comments(CxArena *a, const char *src) {
    CxBuf b;
    cx_buf_init(&b);
    for (const char *p = src; *p;) {
        if (p[0] == '/' && p[1] == '/') { /* line comment */
            while (*p && *p != '\n') p++;
            cx_buf_putc(&b, ' ');
            continue;
        }
        if (p[0] == '/' && p[1] == '*') { /* block comment */
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (*p) p += 2;
            cx_buf_putc(&b, ' ');
            continue;
        }
        cx_buf_putc(&b, *p++);
    }
    char *r = cx_arena_strdup(a, b.data ? b.data : "");
    cx_buf_free(&b);
    return r;
}

/* Parse the captured `sm` body into `f`. Sub-state blocks (`State { … }`) are
 * flattened: their transitions are treated as ordinary top-level transitions. */
void fsm_parse(CxArena *a, const char *body, Fsm *f) {
    memset(f, 0, sizeof *f);
    f->arena = a;
    const char *p = body ? fsm_strip_comments(a, body) : "";
    for (;;) {
        p = fsm_skip_ws(p);
        if (!*p) break;
        if (*p == '}') { p++; continue; }
        if (*p == '|') p++; /* parallel-state marker */
        p = fsm_skip_ws(p);

        const char *src = fsm_read_name(a, &p);
        if (!src || !*src) { p++; continue; }
        p = fsm_skip_ws(p);
        if (*p == '{') { p++; continue; } /* block header: `State {` */
        if (!(p[0] == '-' && p[1] == '>')) { p++; continue; }
        p += 2;
        const char *dst = fsm_read_name(a, &p);
        p = fsm_skip_ws(p);

        FsmTrans *t = fsm_new_trans(a, f);
        if (!t) return;
        t->src = src;
        t->dst = dst;

        if (*p == ':') {
            p++;
            t->event = fsm_read_name(a, &p);
            fsm_names_add(a, &f->events, t->event);
            p = fsm_skip_ws(p);
        }
        if (*p == '[') {
            p++;
            for (;;) {
                p = fsm_skip_ws(p);
                if (*p == ']') { p++; break; }
                if (!*p) break;
                const char *g = fsm_read_name(a, &p);
                if (t->nguards < FSM_MAX) t->guards[t->nguards++] = g;
                fsm_names_add(a, &f->guards, g);
                p = fsm_skip_ws(p);
                if (*p == ',') { p++; continue; }
                if (*p == ']') { p++; break; }
            }
            p = fsm_skip_ws(p);
        }
        if (*p == '/') {
            p++;
            for (;;) {
                p = fsm_skip_ws(p);
                if (!*p) break;
                const char *ac = fsm_read_name(a, &p);
                if (!ac || !*ac) break;
                if (t->nactions < FSM_MAX) t->actions[t->nactions++] = ac;
                fsm_names_add(a, &f->actions, ac);
                p = fsm_skip_ws(p);
                if (*p == ',') { p++; continue; }
                break;
            }
        }
        if (strcmp(src, "*") != 0 && strcmp(src, "any") != 0 &&
            strcmp(src, "current") != 0)
            fsm_names_add(a, &f->states, src);
        if (strcmp(dst, "*") != 0 && strcmp(dst, "any") != 0 &&
            strcmp(dst, "current") != 0)
            fsm_names_add(a, &f->states, dst);
    }
}

/* The machine's generated C names are all prefixed with the machine's *bare*
 * name and use cx-style `__` sub-namespacing: states `<M>__state__<s>`, events
 * `<M>__event__<e>`, guard/action struct fields `guard_<g>` / `action_<a>` and
 * the API `<M>__init` etc.  Events are enum members (not assignable); only
 * guards and actions are assignable callbacks. Guards/actions receive the typed
 * machine pointer and the event; the *user* context is the `void *ctx` field. */
void emit_fsm_def(Emit *e, CxNode *n, const char *ns) {
    (void)ns; /* generated names are bare, matching the synthetic API symbols */
    Fsm f;
    fsm_parse(e->arena, n->type, &f);
    const char *m = n->name;
    cx_emit_line(e->out, n->span.first_line, e->file);

    ind(e);
    cx_buf_printf(e->out, "typedef enum %s__State {\n", m);
    e->indent++;
    ind(e);
    cx_buf_printf(e->out, "%s__state__entry = 0", m);
    for (int i = 0; i < f.states.len; i++) {
        cx_buf_puts(e->out, ",\n");
        ind(e);
        cx_buf_printf(e->out, "%s__state__%s", m, f.states.names[i]);
    }
    cx_buf_putc(e->out, '\n');
    e->indent--;
    ind(e);
    cx_buf_printf(e->out, "} %s__State;\n", m);

    ind(e);
    cx_buf_printf(e->out, "typedef enum %s__Event {\n", m);
    e->indent++;
    if (f.events.len == 0) {
        ind(e);
        cx_buf_printf(e->out, "%s__event__none = 0\n", m);
    }
    for (int i = 0; i < f.events.len; i++) {
        ind(e);
        cx_buf_printf(e->out, "%s__event__%s", m, f.events.names[i]);
        if (i + 1 < f.events.len) cx_buf_putc(e->out, ',');
        cx_buf_putc(e->out, '\n');
    }
    e->indent--;
    ind(e);
    cx_buf_printf(e->out, "} %s__Event;\n", m);

    /* Forward-declare the machine type, then the callback typedefs (which name
     * it), then the struct itself. A guard/action always receives the typed
     * machine, the from (exit) state, the to (entry) state and the event
     * (never bare integers). */
    ind(e);
    cx_buf_printf(e->out, "typedef struct %s %s;\n", m, m);
    ind(e);
    cx_buf_printf(e->out,
                  "typedef int (*%s__Guard)(%s *m, %s__State from, "
                  "%s__State to, %s__Event event);\n",
                  m, m, m, m, m);
    ind(e);
    cx_buf_printf(e->out,
                  "typedef void (*%s__Action)(%s *m, %s__State from, "
                  "%s__State to, %s__Event event);\n",
                  m, m, m, m, m);

    ind(e);
    cx_buf_printf(e->out, "struct %s {\n", m);
    e->indent++;
    ind(e);
    cx_buf_printf(e->out, "%s__State state;\n", m);
    ind(e);
    cx_buf_puts(e->out, "void *ctx; /* user context */\n");
    for (int i = 0; i < f.guards.len; i++) {
        ind(e);
        cx_buf_printf(e->out, "%s__Guard guard_%s;\n", m, f.guards.names[i]);
    }
    for (int i = 0; i < f.actions.len; i++) {
        ind(e);
        cx_buf_printf(e->out, "%s__Action action_%s;\n", m, f.actions.names[i]);
    }
    e->indent--;
    ind(e);
    cx_buf_puts(e->out, "};\n\n");
}

void emit_fsm_protos(Emit *e, CxNode *n, const char *ns) {
    (void)ns;
    const char *m = n->name;
    ind(e);
    cx_buf_printf(e->out, "void %s__init(%s *m, void *ctx);\n", m, m);
    ind(e);
    cx_buf_printf(e->out, "void %s__reset(%s *m);\n", m, m);
    ind(e);
    cx_buf_printf(e->out, "%s__State %s__get_state(%s *m);\n", m, m, m);
    ind(e);
    cx_buf_printf(e->out, "void %s__process_event(%s *m, %s__Event event);\n",
                  m, m, m);
}

/* Emit the C expression for a transition endpoint state. `any`/`current` mean
 * "the machine's current state"; `*` is the entry pseudo-state. */
static void fsm_emit_state_expr(Emit *e, const char *m, const char *st) {
    if (!st || strcmp(st, "any") == 0 || strcmp(st, "current") == 0)
        cx_buf_puts(e->out, "m->state");
    else if (strcmp(st, "*") == 0)
        cx_buf_printf(e->out, "%s__state__entry", m);
    else
        cx_buf_printf(e->out, "%s__state__%s", m, st);
}

static void fsm_emit_dst(Emit *e, const char *m, const FsmTrans *t) {
    if (strcmp(t->dst, "current") == 0 || strcmp(t->dst, "any") == 0) return;
    if (strcmp(t->dst, "*") == 0) {
        ind(e);
        cx_buf_printf(e->out, "m->state = %s__state__entry;\n", m);
        return;
    }
    ind(e);
    cx_buf_printf(e->out, "m->state = %s__state__%s;\n", m, t->dst);
}

static void fsm_emit_actions(Emit *e, const char *m, const FsmTrans *t) {
    for (int i = 0; i < t->nactions; i++) {
        ind(e);
        cx_buf_printf(e->out, "if (m->action_%s) m->action_%s(m, ",
                      t->actions[i], t->actions[i]);
        fsm_emit_state_expr(e, m, t->src); /* from (exit) state */
        cx_buf_puts(e->out, ", ");
        fsm_emit_state_expr(e, m, t->dst); /* to (entry) state */
        cx_buf_puts(e->out, ", event);\n");
    }
}

/* Emit the transition `if`-blocks for source state `src`, in definition order
 * (first matching transition wins; a failed guard chain falls through). */
static void fsm_emit_transitions(Emit *e, const char *m, const Fsm *f,
                                 const char *src) {
    for (int i = 0; i < f->ntrans; i++) {
        const FsmTrans *t = &f->trans[i];
        if (strcmp(t->src, src) != 0) continue;

        ind(e);
        if (t->event)
            cx_buf_printf(e->out, "if (event == %s__event__%s) {\n", m,
                          t->event);
        else
            cx_buf_puts(e->out, "{\n");
        e->indent++;
        if (t->nguards > 0) {
            ind(e);
            cx_buf_puts(e->out, "if (");
            for (int g = 0; g < t->nguards; g++) {
                if (g) cx_buf_puts(e->out, " && ");
                cx_buf_printf(e->out, "m->guard_%s && m->guard_%s(m, ",
                              t->guards[g], t->guards[g]);
                fsm_emit_state_expr(e, m, t->src); /* from (exit) state */
                cx_buf_puts(e->out, ", ");
                fsm_emit_state_expr(e, m, t->dst); /* to (entry) state */
                cx_buf_puts(e->out, ", event)");
            }
            cx_buf_puts(e->out, ") {\n");
            e->indent++;
            fsm_emit_actions(e, m, t);
            fsm_emit_dst(e, m, t);
            ind(e);
            cx_buf_puts(e->out, "return;\n");
            e->indent--;
            ind(e);
            cx_buf_puts(e->out, "}\n");
        } else {
            fsm_emit_actions(e, m, t);
            fsm_emit_dst(e, m, t);
            ind(e);
            cx_buf_puts(e->out, "return;\n");
        }
        e->indent--;
        ind(e);
        cx_buf_puts(e->out, "}\n");
    }
}

void emit_fsm_funcs(Emit *e, CxNode *n, const char *ns) {
    (void)ns;
    Fsm f;
    fsm_parse(e->arena, n->type, &f);
    const char *m = n->name;

    ind(e);
    cx_buf_printf(e->out, "void %s__init(%s *m, void *ctx) {\n", m, m);
    e->indent++;
    ind(e);
    cx_buf_printf(e->out, "m->state = %s__state__entry;\n", m);
    ind(e);
    cx_buf_puts(e->out, "m->ctx = ctx;\n");
    for (int i = 0; i < f.guards.len; i++) {
        ind(e);
        cx_buf_printf(e->out, "m->guard_%s = 0;\n", f.guards.names[i]);
    }
    for (int i = 0; i < f.actions.len; i++) {
        ind(e);
        cx_buf_printf(e->out, "m->action_%s = 0;\n", f.actions.names[i]);
    }
    e->indent--;
    ind(e);
    cx_buf_puts(e->out, "}\n\n");

    ind(e);
    cx_buf_printf(e->out, "void %s__reset(%s *m) {\n", m, m);
    e->indent++;
    ind(e);
    cx_buf_printf(e->out, "m->state = %s__state__entry;\n", m);
    e->indent--;
    ind(e);
    cx_buf_puts(e->out, "}\n\n");

    ind(e);
    cx_buf_printf(e->out, "%s__State %s__get_state(%s *m) {\n", m, m, m);
    e->indent++;
    ind(e);
    cx_buf_puts(e->out, "return m->state;\n");
    e->indent--;
    ind(e);
    cx_buf_puts(e->out, "}\n\n");

    ind(e);
    cx_buf_printf(e->out, "void %s__process_event(%s *m, %s__Event event) {\n",
                  m, m, m);
    e->indent++;
    for (int i = 0; i < f.states.len; i++) {
        ind(e);
        cx_buf_printf(e->out, "if (m->state == %s__state__%s) {\n", m,
                      f.states.names[i]);
        e->indent++;
        fsm_emit_transitions(e, m, &f, f.states.names[i]);
        e->indent--;
        ind(e);
        cx_buf_puts(e->out, "}\n");
    }
    {
        int entry = 0;
        for (int i = 0; i < f.ntrans; i++)
            if (strcmp(f.trans[i].src, "*") == 0) entry = 1;
        if (entry) {
            ind(e);
            cx_buf_printf(e->out, "if (m->state == %s__state__entry) {\n", m);
            e->indent++;
            fsm_emit_transitions(e, m, &f, "*");
            e->indent--;
            ind(e);
            cx_buf_puts(e->out, "}\n");
        }
    }
    /* `[any]` transitions run last, as the fallback when nothing else matched. */
    fsm_emit_transitions(e, m, &f, "any");
    e->indent--;
    ind(e);
    cx_buf_puts(e->out, "}\n\n");
}

/* Codegen: enums/struct/typedefs in phase 1, prototypes in phase 2, bodies in
 * phase 4. */
static int fsm_emit(Emit *e, CxNode *n, const char *ns, int phase) {
    if (n->kind != CX_NODE_FSM) return 0;
    if (phase == 1) { emit_fsm_def(e, n, ns); return 1; }
    if (phase == 2) { emit_fsm_protos(e, n, ns); return 1; }
    if (phase == 4) { emit_fsm_funcs(e, n, ns); return 1; }
    return 0;
}

/* Sema: register the machine's type, and its generated API as *function*
 * symbols so `.`-method calls (`m.init(ctx)`, `m.process_event(ev)`, …) lower
 * to the associated calls `Machine__init(&m, ctx)` etc. The C names are bare
 * (no namespace), matching what `emit_fsm_*` emits; `get_state` returns the
 * machine's `__State` enum (not an int). */
static int fsm_collect(BuildCtx *c, CxNode *n, const char *ns,
                       const char *file) {
    if (n->kind != CX_NODE_FSM) return 0;
    CxSymbol *s = symbol_new(c->arena, n, ns, CX_NODE_FSM, file);
    table_push(c->table, s);
    static const char *const methods[] = {"init", "reset", "get_state",
                                          "process_event"};
    for (int i = 0; i < 4; i++) {
        CxSymbol *fn = (CxSymbol *)cx_arena_alloc(c->arena, sizeof(CxSymbol));
        if (!fn) break;
        memset(fn, 0, sizeof *fn);
        fn->name = cx_arena_printf(c->arena, "%s__%s", n->name, methods[i]);
        fn->ns = NULL;
        fn->qualified = fn->name;
        fn->type = (strcmp(methods[i], "get_state") == 0)
                       ? cx_arena_printf(c->arena, "%s__State", n->name)
                       : (char *)"void";
        fn->kind = CX_NODE_FUNCTION;
        fn->span = n->span;
        fn->file = file;
        fn->decl = n;
        table_push(c->table, fn);
    }
    return 1;
}

const CxFeature cx_feature_fsm = {"sm", fsm_collect, fsm_emit};
