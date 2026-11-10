#include "cx/cx.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char *read_all(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long n = ftell(f);
    if (n < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    if (len_out) *len_out = got;
    return buf;
}

static int write_all(const char *path, const char *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t wrote = fwrite(data, 1, len, f);
    fclose(f);
    return wrote == len ? 0 : -1;
}

static void usage(void) {
    fprintf(stderr, "Cx %s - C extension transpiler\n\n", CX_VERSION);
    fprintf(stderr, "usage:\n");
    fprintf(stderr, "  cx build  <file.cx|file.hx> [-o <out.c>] [-g]\n");
    fprintf(stderr, "  cx expand <file.cx|file.hx>\n");
    fprintf(stderr, "  cx dump   <file.cx|file.hx>\n");
    fprintf(stderr, "  cx compdb <dir|file.c>... [--include DIR]... [--cc CC] [--std STD]\n");
    fprintf(stderr, "  cx new-feature <name>\n");
    fprintf(stderr, "  cx lsp\n");
    fprintf(stderr, "  cx --version\n");
}

/* ---------------------------------------------------------------- utilities */

/* Create `path` and any missing parent directories (like `mkdir -p`). */
static int mkdir_p(const char *path) {
    char buf[4096];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof buf) return -1;
    memcpy(buf, path, n + 1);
    for (size_t i = 1; i < n; i++) {
        if (buf[i] != '/') continue;
        buf[i] = '\0';
        if (mkdir(buf, 0755) != 0 && errno != EEXIST) return -1;
        buf[i] = '/';
    }
    if (mkdir(buf, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

/* A growable list of arena-owned strings. */
typedef struct {
    char **items;
    int    len;
    int    cap;
} PathList;

static void pl_push(CxArena *a, PathList *l, const char *s) {
    if (l->len == l->cap) {
        int cap = l->cap ? l->cap * 2 : 16;
        char **items = (char **)cx_arena_alloc(a, sizeof(char *) * (size_t)cap);
        if (!items) return;
        if (l->items && l->len)
            memcpy(items, l->items, sizeof(char *) * (size_t)l->len);
        l->items = items;
        l->cap = cap;
    }
    char *copy = (char *)cx_arena_alloc(a, strlen(s) + 1);
    if (!copy) return;
    strcpy(copy, s);
    l->items[l->len++] = copy;
}

static int path_cmp(const void *pa, const void *pb) {
    return strcmp(*(const char *const *)pa, *(const char *const *)pb);
}

/* defined below; used by `scan_sources` */
static int has_ext(const char *path, const char *ext);

/* Collect every `*.c` under `dir` (recursively). */
static void scan_sources(CxArena *a, PathList *out, const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char child[4096];
        snprintf(child, sizeof child, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(child, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_sources(a, out, child);
        } else if (has_ext(e->d_name, ".c")) {
            pl_push(a, out, child);
        }
    }
    closedir(d);
}

static void json_str(CxBuf *b, const char *s) {
    cx_buf_putc(b, '"');
    for (const char *p = s; *p; p++) {
        if (*p == '\\' || *p == '"') {
            cx_buf_putc(b, '\\');
            cx_buf_putc(b, *p);
        } else if (*p == '\n') {
            cx_buf_puts(b, "\\n");
        } else {
            cx_buf_putc(b, *p);
        }
    }
    cx_buf_putc(b, '"');
}

static char *abs_path(CxArena *a, const char *path) {
    char resolved[4096];
    if (realpath(path, resolved)) return cx_arena_strdup(a, resolved);
    return cx_arena_strdup(a, path);
}

/* `cx compdb <dir|file.c>... [--include DIR]... [--cc CC] [--std STD]` — write a
 * `compile_commands.json` covering the given generated `*.c` files (a directory
 * is scanned recursively), so clangd and other C tooling resolve the transpiled
 * output. Each entry's `directory` and first include path are the file's own
 * directory; every `--include` directory follows. The DB is written next to the
 * first path (a directory, or the first `.c`'s directory). */
static int run_compdb(int argc, char **argv) {
    const char *cc = getenv("CC");
    const char *std = "gnu11";
    const char *includes[64];
    const char *paths[64];
    int ninc = 0, npaths = 0;

    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--include") && i + 1 < argc) {
            if (ninc < (int)(sizeof includes / sizeof includes[0]))
                includes[ninc++] = argv[++i];
        } else if (!strcmp(argv[i], "--cc") && i + 1 < argc) {
            cc = argv[++i];
        } else if (!strcmp(argv[i], "--std") && i + 1 < argc) {
            std = argv[++i];
        } else if (npaths < (int)(sizeof paths / sizeof paths[0])) {
            paths[npaths++] = argv[i];
        }
    }
    if (npaths == 0) {
        fprintf(stderr, "usage: cx compdb <dir|file.c>... [--include DIR]... "
                        "[--cc CC] [--std STD]\n");
        return 2;
    }
    if (!cc || !*cc) cc = "cc";

    CxArena *arena = cx_arena_new();

    /* Each path is either a directory (scanned for generated `*.c`) or a single
     * generated `*.c` file. */
    PathList srcs = {0};
    for (int i = 0; i < npaths; i++) {
        struct stat st;
        if (stat(paths[i], &st) == 0 && S_ISDIR(st.st_mode))
            scan_sources(arena, &srcs, paths[i]);
        else if (has_ext(paths[i], ".c"))
            pl_push(arena, &srcs, paths[i]);
        else
            fprintf(stderr, "cx: compdb: skipping '%s' (not a dir or *.c)\n",
                    paths[i]);
    }
    if (srcs.len > 1)
        qsort(srcs.items, (size_t)srcs.len, sizeof(char *), path_cmp);

    CxBuf b;
    cx_buf_init(&b);
    cx_buf_puts(&b, "[\n");
    for (int i = 0; i < srcs.len; i++) {
        char *file = abs_path(arena, srcs.items[i]);
        /* the file's own directory, so its generated `#include "x.h"` resolves */
        char fdir[4096];
        snprintf(fdir, sizeof fdir, "%s", file);
        char *slash = strrchr(fdir, '/');
        if (slash) *slash = '\0';

        cx_buf_puts(&b, "  {\n    \"directory\": ");
        json_str(&b, fdir);
        cx_buf_puts(&b, ",\n    \"file\": ");
        json_str(&b, file);
        cx_buf_puts(&b, ",\n    \"arguments\": [");
        json_str(&b, cc);
        cx_buf_printf(&b, ", \"-std=%s\", ", std);
        json_str(&b, fdir);
        for (int k = 0; k < ninc; k++) {
            char *inc = abs_path(arena, includes[k]);
            cx_buf_puts(&b, ", \"-I");
            cx_buf_puts(&b, inc);
            cx_buf_putc(&b, '"');
        }
        cx_buf_puts(&b, ", \"-c\", ");
        json_str(&b, file);
        cx_buf_puts(&b, "]\n  }");
        cx_buf_puts(&b, i + 1 < srcs.len ? ",\n" : "\n");
    }
    cx_buf_puts(&b, "]\n");

    /* Write next to the first path: `<dir>/compile_commands.json`, or the
     * directory of the first `.c`. */
    char outdir[4096];
    snprintf(outdir, sizeof outdir, "%s", abs_path(arena, paths[0]));
    struct stat st0;
    if (!(stat(paths[0], &st0) == 0 && S_ISDIR(st0.st_mode))) {
        char *slash = strrchr(outdir, '/');
        if (slash) *slash = '\0';
    }
    char out[4096];
    snprintf(out, sizeof out, "%s/compile_commands.json", outdir);

    int rc = 0;
    if (write_all(out, b.data ? b.data : "[]\n", b.len) != 0) {
        fprintf(stderr, "cx: cannot write '%s'\n", out);
        rc = 1;
    } else if (srcs.len == 0) {
        fprintf(stderr, "compdb: no generated *.c found in the given paths "
                        "(wrote an empty %s)\n", out);
    } else {
        printf("compdb: %d entr%s written to %s\n", srcs.len,
               srcs.len == 1 ? "y" : "ies", out);
    }
    cx_buf_free(&b);
    cx_arena_free(arena);
    return rc;
}

/* `cx new-feature <name>` — scaffold `src/features/<name>/` with a feature
 * module (the `collect`/`emit` hooks in `feature.h`) and print how to register
 * it. Run from the repo root. */
static int run_new_feature(const char *name) {
    if (!name || !*name) {
        fprintf(stderr, "usage: cx new-feature <name>\n");
        return 2;
    }
    if (!(name[0] >= 'a' && name[0] <= 'z')) {
        fprintf(stderr, "cx: feature name must start with a-z\n");
        return 2;
    }
    for (const char *p = name; *p; p++) {
        int ok = (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
                 *p == '_';
        if (!ok) {
            fprintf(stderr, "cx: feature name must be [a-z][a-z0-9_]* "
                            "(got '%s')\n", name);
            return 2;
        }
    }

    char dir[4096], hpath[4096], cpath[4096];
    snprintf(dir, sizeof dir, "src/features/%s", name);
    snprintf(hpath, sizeof hpath, "%s/%s_feature.h", dir, name);
    snprintf(cpath, sizeof cpath, "%s/%s_feature.c", dir, name);

    struct stat st;
    if (stat(dir, &st) == 0) {
        fprintf(stderr, "cx: '%s' already exists\n", dir);
        return 1;
    }
    if (mkdir_p(dir) != 0) {
        fprintf(stderr, "cx: cannot create '%s'\n", dir);
        return 1;
    }

    /* Uppercase guard: `CX_<NAME>_FEATURE_H`. */
    CxArena *arena = cx_arena_new();
    char guard[256];
    int gi = 0;
    guard[gi++] = 'C';
    guard[gi++] = 'X';
    guard[gi++] = '_';
    for (const char *p = name; *p && gi < (int)sizeof guard - 12; p++)
        guard[gi++] = (char)((*p >= 'a' && *p <= 'z') ? *p - 32 : *p);
    const char *tail = "_FEATURE_H";
    for (const char *p = tail; *p; p++) guard[gi++] = *p;
    guard[gi] = '\0';

    CxBuf h;
    cx_buf_init(&h);
    cx_buf_printf(&h,
        "#ifndef %s\n#define %s\n\n"
        "#include \"../feature.h\"\n\n"
        "/* `CxFeature` for the `%s` feature. Register it in\n"
        " * `src/features/registry.c` and (optionally) add a grammar hook\n"
        " * under `grammar/parser.y`. */\n"
        "extern const CxFeature cx_feature_%s;\n\n"
        "#endif /* %s */\n",
        guard, guard, name, name, guard);

    CxBuf c;
    cx_buf_init(&c);
    cx_buf_printf(&c,
        "/* Cx feature: %s. */\n"
        "#include \"cx/cx.h\"\n"
        "#include \"%s_feature.h\"\n\n"
        "/* Sema: add the feature's symbols for `node` (return 1 when handled). */\n"
        "static int %s_collect(BuildCtx *c, CxNode *node, const char *ns,\n"
        "                       const char *file) {\n"
        "    (void)c;\n    (void)node;\n    (void)ns;\n    (void)file;\n"
        "    return 0;\n"
        "}\n\n"
        "/* Codegen: emit `node` in `phase` (return 1 when handled). */\n"
        "static int %s_emit(Emit *e, CxNode *node, const char *ns, int phase) {\n"
        "    (void)e;\n    (void)node;\n    (void)ns;\n    (void)phase;\n"
        "    return 0;\n"
        "}\n\n"
        "const CxFeature cx_feature_%s = {\"%s\", %s_collect, %s_emit};\n",
        name, name, name, name, name, name, name, name);

    int rc = 0;
    if (write_all(hpath, h.data, h.len) != 0 ||
        write_all(cpath, c.data, c.len) != 0) {
        fprintf(stderr, "cx: cannot write the feature files\n");
        rc = 1;
    } else {
        printf("created %s\n        %s\n\n", hpath, cpath);
        printf("register it in src/features/registry.c:\n");
        printf("  #include \"%s/%s_feature.h\"\n", name, name);
        printf("  ... &cx_feature_%s, ...   (in `features[]`)\n\n", name);
        printf("then add its grammar hook in grammar/parser.y "
               "and a test under tests/golden/.\n");
    }
    cx_buf_free(&h);
    cx_buf_free(&c);
    cx_arena_free(arena);
    return rc;
}

static int has_ext(const char *path, const char *ext) {
    size_t n = strlen(path), m = strlen(ext);
    return n >= m && strcmp(path + n - m, ext) == 0;
}

/* The basename of `path` with its final extension replaced by `newext`
 * (e.g. `/a/b/foo.cx` + `.h` -> `foo.h`). Returned in malloc'd storage. */
static char *replace_ext(const char *path, const char *newext) {
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/') base = p + 1;
    size_t n = strlen(base);
    const char *dot = strrchr(base, '.');
    size_t stem = dot ? (size_t)(dot - base) : n;
    size_t elen = strlen(newext);
    char *out = (char *)malloc(stem + elen + 1);
    if (!out) return NULL;
    memcpy(out, base, stem);
    memcpy(out + stem, newext, elen + 1);
    return out;
}

/* For a `.cx`, the sibling `.hx` path (relative to the source directory). */
static char *sibling_hx(const char *path) {
    size_t n = strlen(path);
    if (n < 3 || strcmp(path + n - 3, ".cx") != 0) return NULL;
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, path, n - 3);
    memcpy(out + n - 3, ".hx", 4); /* copies the terminating NUL too */
    return out;
}

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* `cx expand <file>`: print the source after `macro_rules!` expansion (useful
 * for debugging a macro). No semantic checks are run. */
static int run_expand(const char *in) {
    size_t len = 0;
    char *src = read_all(in, &len);
    if (!src) {
        fprintf(stderr, "cx: cannot read '%s'\n", in);
        return 1;
    }

    CxArena *arena = cx_arena_new();
    CxDiagList diags;
    cx_diaglist_init(&diags, in);
    char *expanded = cx_macro_expand(arena, src, len, in, &diags, NULL);
    fputs(expanded, stdout);
    int rc = cx_diaglist_has_errors(&diags) ? 1 : 0;
    cx_diaglist_print(&diags, stderr);
    cx_diaglist_free(&diags);
    cx_arena_free(arena);
    free(src);
    return rc;
}

static int run_transpile(const char *in, const char *out, int dump) {
    int header = has_ext(in, ".hx");
    size_t len = 0;
    char *src = read_all(in, &len);
    if (!src) {
        fprintf(stderr, "cx: cannot read '%s'\n", in);
        return 1;
    }

    CxArena *arena = cx_arena_new();
    CxDiagList diags;
    cx_diaglist_init(&diags, in);

    CxNode *tu = cx_parse(arena, src, len, in, &diags);
    int rc = 0;

    /* `.cx` / `.hx` pairing: if a sibling `.hx` exists, the generated `.c`
     * includes its header and skips the declarations that header already
     * provides (see cx_emit_c). Keep the 1:1 basename mapping. */
    if (tu && !header) {
        char *hx = sibling_hx(in);
        if (hx) {
            if (file_exists(hx)) {
                char *inc = replace_ext(in, ".h");
                if (inc) {
                    tu->paired_header = cx_arena_strdup(arena, inc);
                    free(inc);
                }
            }
            free(hx);
        }
    }

    if (dump) {
        cx_node_dump(tu, stdout, 0);
    } else if (cx_diaglist_has_errors(&diags) || !tu) {
        rc = 1;
    } else {
        CxSymbolTable *sema = cx_sema_build(arena, tu, &diags);
        if (cx_diaglist_has_errors(&diags)) {
            rc = 1;
        } else {
            CxBuf buf;
            cx_buf_init(&buf);
            if (header) cx_emit_h(arena, &buf, tu, sema, in);
            else cx_emit_c(arena, &buf, tu, sema, in);
            if (out) {
                if (write_all(out, buf.data, buf.len) != 0) {
                    fprintf(stderr, "cx: cannot write '%s'\n", out);
                    rc = 1;
                }
            } else {
                fwrite(buf.data, 1, buf.len, stdout);
            }
            cx_buf_free(&buf);
        }
    }

    cx_diaglist_print(&diags, stderr);
    cx_diaglist_free(&diags);
    cx_arena_free(arena);
    free(src);
    return rc;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    if (!strcmp(argv[1], "--version") || !strcmp(argv[1], "-v")) {
        printf("cx %s\n", CX_VERSION);
        return 0;
    }
    if (!strcmp(argv[1], "lsp")) {
        return cx_lsp_run();
    }
    if (!strcmp(argv[1], "expand")) {
        if (argc < 3) {
            usage();
            return 2;
        }
        return run_expand(argv[2]);
    }
    if (!strcmp(argv[1], "dump")) {
        if (argc < 3) {
            usage();
            return 2;
        }
        return run_transpile(argv[2], NULL, 1);
    }
    if (!strcmp(argv[1], "build")) {
        const char *in = NULL;
        const char *out = NULL;
        int line_map = 0;
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-o") && i + 1 < argc)
                out = argv[++i];
            else if (!strcmp(argv[i], "-g") || !strcmp(argv[i], "--line-map"))
                line_map = 1;
            else
                in = argv[i];
        }
        if (!in) {
            usage();
            return 2;
        }
        cx_codegen_set_line_map(line_map);
        return run_transpile(in, out, 0);
    }
    if (!strcmp(argv[1], "compdb"))
        return run_compdb(argc - 2, argv + 2);
    if (!strcmp(argv[1], "new-feature")) {
        if (argc < 3) {
            usage();
            return 2;
        }
        return run_new_feature(argv[2]);
    }

    usage();
    return 2;
}
