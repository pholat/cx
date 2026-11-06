#ifndef CX_LSP_JSON_H
#define CX_LSP_JSON_H

#include <stddef.h>

#include "cx/buf.h"

/* Minimal JSON parser/serializer — just enough for the LSP transport, without
 * pulling in a third-party dependency. Only what the server needs is provided. */

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUM,
    JSON_STR,
    JSON_ARR,
    JSON_OBJ
} JsonKind;

typedef struct JsonValue JsonValue;

typedef struct {
    char      *key;
    JsonValue *value;
} JsonMember;

struct JsonValue {
    JsonKind    kind;
    int         boolean;
    double      num;
    char       *str;       /* JSON_STR */
    JsonValue **items;     /* JSON_ARR */
    int         items_len;
    JsonMember *members;   /* JSON_OBJ */
    int         members_len;
};

JsonValue *cx_json_parse(const char *text, size_t len);
void       cx_json_free(JsonValue *value);

const JsonValue *cx_json_get(const JsonValue *object, const char *key);
const char      *cx_json_string(const JsonValue *value);
long             cx_json_int(const JsonValue *value, long fallback);
int              cx_json_array_len(const JsonValue *value);
const JsonValue *cx_json_array_at(const JsonValue *value, int index);

/* Serialize a value (used to echo request ids). */
void cx_json_write(CxBuf *out, const JsonValue *value);
/* Write a quoted, escaped JSON string. */
void cx_json_write_string(CxBuf *out, const char *s);

#endif /* CX_LSP_JSON_H */
