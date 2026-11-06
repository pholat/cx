#ifndef CX_DIAG_H
#define CX_DIAG_H

#include <stdio.h>

#include "cx/span.h"

typedef enum {
    CX_DIAG_ERROR,
    CX_DIAG_WARNING,
    CX_DIAG_NOTE
} CxSeverity;

typedef struct {
    CxSeverity severity;
    CxSpan     span;
    char       message[256];
} CxDiagnostic;

typedef struct {
    CxDiagnostic *items;
    int           len;
    int           cap;
    const char   *filename;
} CxDiagList;

void cx_diaglist_init(CxDiagList *list, const char *filename);
void cx_diaglist_free(CxDiagList *list);
int  cx_diaglist_has_errors(const CxDiagList *list);

void cx_diag_add(CxDiagList *list, CxSeverity severity, CxSpan span,
                 const char *fmt, ...);
void cx_error(CxDiagList *list, CxSpan span, const char *fmt, ...);
void cx_warning(CxDiagList *list, CxSpan span, const char *fmt, ...);

void cx_diaglist_print(const CxDiagList *list, FILE *out);

#endif /* CX_DIAG_H */
