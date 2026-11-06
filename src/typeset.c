#include "cx/parser.h"

#include <stdlib.h>
#include <string.h>

int cx_typeset_has(const CxTypeSet *ts, const char *name) {
    if (!ts || !name) return 0;
    for (int i = 0; i < ts->len; i++)
        if (strcmp(ts->items[i], name) == 0) return 1;
    return 0;
}

void cx_typeset_add(CxTypeSet *ts, const char *name) {
    if (!ts || !name || !*name) return;
    if (cx_typeset_has(ts, name)) return;

    if (ts->len == ts->cap) {
        int cap = ts->cap ? ts->cap * 2 : 16;
        const char **items =
            (const char **)realloc(ts->items, sizeof(char *) * (size_t)cap);
        if (!items) return;
        ts->items = items;
        ts->cap = cap;
    }
    ts->items[ts->len++] = name;
}

void cx_typeset_free(CxTypeSet *ts) {
    if (!ts) return;
    free(ts->items);
    ts->items = NULL;
    ts->len = 0;
    ts->cap = 0;
}
