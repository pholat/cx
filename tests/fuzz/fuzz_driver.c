/* Cx fuzz driver.
 *
 * Reads one input file and runs it through the front end, then exits 0 — the
 * point is that no input may crash or hang the compiler. Drive it with
 * `tests/fuzz/run.sh` (or `make fuzz`).
 *
 *   cx_fuzz macro <file>   run `cx_macro_expand`
 *   cx_fuzz parse <file>   run `cx_parse` + `cx_sema_build`
 *   cx_fuzz dump  <file>   like `parse`, then print the AST
 */
#include "cx/cx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *slurp(const char *path, size_t *len_out) {
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

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <macro|parse|dump> <file>\n", argv[0]);
        return 2;
    }

    size_t len = 0;
    char *src = slurp(argv[2], &len);
    if (!src) return 0; /* unreadable input: nothing to fuzz */

    CxArena *arena = cx_arena_new();
    CxDiagList diags;
    cx_diaglist_init(&diags, argv[2]);

    if (strcmp(argv[1], "macro") == 0) {
        char *expanded = cx_macro_expand(arena, src, len, argv[2], &diags, NULL);
        (void)expanded;
    } else {
        CxNode *tu = cx_parse(arena, src, len, argv[2], &diags);
        if (tu) {
            CxSymbolTable *sema = cx_sema_build(arena, tu, &diags);
            (void)sema;
            if (strcmp(argv[1], "dump") == 0) cx_node_dump(tu, stdout, 0);
        }
    }

    cx_diaglist_free(&diags);
    cx_arena_free(arena);
    free(src);
    return 0;
}
