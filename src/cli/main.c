#include "cx/cx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    fprintf(stderr, "  cx build  <file.cx|file.hx> [-o <out.c>]\n");
    fprintf(stderr, "  cx expand <file.cx|file.hx>\n");
    fprintf(stderr, "  cx dump   <file.cx|file.hx>\n");
    fprintf(stderr, "  cx lsp\n");
    fprintf(stderr, "  cx --version\n");
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
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-o") && i + 1 < argc)
                out = argv[++i];
            else
                in = argv[i];
        }
        if (!in) {
            usage();
            return 2;
        }
        return run_transpile(in, out, 0);
    }

    usage();
    return 2;
}
