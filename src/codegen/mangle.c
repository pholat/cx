#include "cx/codegen.h"

#include <string.h>

/* Namespace paths use "::" (language-level) but C identifiers need "__".
 * Global scope (NULL / "") leaves the name untouched. */
char *cx_mangle(CxArena *arena, const char *ns, const char *name) {
    if (!name) return cx_arena_strdup(arena, "");
    if (!ns || !*ns) return cx_arena_strdup(arena, name);

    size_t ns_len = strlen(ns);
    size_t name_len = strlen(name);
    char *out = (char *)cx_arena_alloc(arena, ns_len * 2 + name_len + 3);
    if (!out) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < ns_len; i++) {
        if (ns[i] == ':' && i + 1 < ns_len && ns[i + 1] == ':') {
            out[j++] = '_';
            out[j++] = '_';
            i++;
        } else {
            out[j++] = ns[i];
        }
    }
    out[j++] = '_';
    out[j++] = '_';
    memcpy(out + j, name, name_len);
    out[j + name_len] = '\0';
    return out;
}
