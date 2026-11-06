#ifndef CX_SPAN_H
#define CX_SPAN_H

/* Source location. Field-for-field compatible with Bison's YYLTYPE so a span
 * can be copied straight out of a semantic action. Lines/columns are 1-based.
 */
typedef struct {
    int first_line;
    int first_column;
    int last_line;
    int last_column;
} CxSpan;

static inline int cx_span_is_known(CxSpan s) { return s.first_line > 0; }

#endif /* CX_SPAN_H */
