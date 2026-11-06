#include "cx/diag.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

void cx_diaglist_init(CxDiagList *list, const char *filename) {
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
    list->filename = filename;
}

void cx_diaglist_free(CxDiagList *list) {
    free(list->items);
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

int cx_diaglist_has_errors(const CxDiagList *list) {
    for (int i = 0; i < list->len; i++)
        if (list->items[i].severity == CX_DIAG_ERROR) return 1;
    return 0;
}

void cx_diag_add(CxDiagList *list, CxSeverity severity, CxSpan span,
                 const char *fmt, ...) {
    if (list->len == list->cap) {
        int cap = list->cap ? list->cap * 2 : 8;
        CxDiagnostic *items =
            (CxDiagnostic *)realloc(list->items, sizeof(CxDiagnostic) * (size_t)cap);
        if (!items) return;
        list->items = items;
        list->cap = cap;
    }
    CxDiagnostic *d = &list->items[list->len++];
    d->severity = severity;
    d->span = span;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(d->message, sizeof d->message, fmt, ap);
    va_end(ap);
}

void cx_error(CxDiagList *list, CxSpan span, const char *fmt, ...) {
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    cx_diag_add(list, CX_DIAG_ERROR, span, "%s", msg);
}

void cx_warning(CxDiagList *list, CxSpan span, const char *fmt, ...) {
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    cx_diag_add(list, CX_DIAG_WARNING, span, "%s", msg);
}

static const char *severity_name(CxSeverity s) {
    switch (s) {
    case CX_DIAG_ERROR:   return "error";
    case CX_DIAG_WARNING: return "warning";
    case CX_DIAG_NOTE:    return "note";
    }
    return "note";
}

void cx_diaglist_print(const CxDiagList *list, FILE *out) {
    const char *file = list->filename ? list->filename : "<input>";
    for (int i = 0; i < list->len; i++) {
        const CxDiagnostic *d = &list->items[i];
        fprintf(out, "%s:%d:%d: %s: %s\n", file, d->span.first_line,
                d->span.first_column, severity_name(d->severity), d->message);
    }
}
