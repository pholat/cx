#include "lsp_internal.h"

#include <stdlib.h>
#include <string.h>

static CxDocument *g_docs = NULL;

static char *dup_string(const char *s) {
    size_t n = strlen(s);
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n + 1);
    return p;
}

CxDocument *cx_doc_store_get(const char *uri) {
    for (CxDocument *d = g_docs; d; d = d->next)
        if (d->uri && !strcmp(d->uri, uri)) return d;
    return NULL;
}

CxDocument *cx_doc_store_put(const char *uri, const char *text, size_t len,
                             int version) {
    CxDocument *d = cx_doc_store_get(uri);
    if (!d) {
        d = (CxDocument *)calloc(1, sizeof(CxDocument));
        if (!d) return NULL;
        d->uri = dup_string(uri);
        d->next = g_docs;
        g_docs = d;
    }
    free(d->text);
    d->text = (char *)malloc(len + 1);
    if (d->text) {
        memcpy(d->text, text, len);
        d->text[len] = '\0';
    }
    d->len = len;
    d->version = version;
    return d;
}

void cx_doc_store_remove(const char *uri) {
    CxDocument **pp = &g_docs;
    while (*pp) {
        if (!strcmp((*pp)->uri, uri)) {
            CxDocument *dead = *pp;
            *pp = dead->next;
            free(dead->uri);
            free(dead->text);
            free(dead);
            return;
        }
        pp = &(*pp)->next;
    }
}
