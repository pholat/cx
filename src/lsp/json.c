#include "json.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *p;
    const char *end;
} Cursor;

static void skip_ws(Cursor *c) {
    while (c->p < c->end &&
           (*c->p == ' ' || *c->p == '\t' || *c->p == '\n' || *c->p == '\r'))
        c->p++;
}

static JsonValue *value_new(JsonKind kind) {
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (v) v->kind = kind;
    return v;
}

/* Parse a JSON string (cursor on the opening quote) into a heap C string. */
static char *parse_string(Cursor *c) {
    if (c->p >= c->end || *c->p != '"') return NULL;
    c->p++;

    CxBuf b;
    cx_buf_init(&b);

    while (c->p < c->end) {
        unsigned char ch = (unsigned char)*c->p++;

        if (ch == '"') {
            if (!b.data) {
                char *empty = (char *)malloc(1);
                if (empty) empty[0] = '\0';
                return empty;
            }
            return cx_buf_take(&b);
        }

        if (ch == '\\') {
            if (c->p >= c->end) break;
            char e = *c->p++;
            switch (e) {
            case '"':  cx_buf_putc(&b, '"');  break;
            case '\\': cx_buf_putc(&b, '\\'); break;
            case '/':  cx_buf_putc(&b, '/');  break;
            case 'b':  cx_buf_putc(&b, '\b'); break;
            case 'f':  cx_buf_putc(&b, '\f'); break;
            case 'n':  cx_buf_putc(&b, '\n'); break;
            case 'r':  cx_buf_putc(&b, '\r'); break;
            case 't':  cx_buf_putc(&b, '\t'); break;
            case 'u': {
                unsigned cp = 0;
                for (int k = 0; k < 4 && c->p < c->end; k++) {
                    char h = *c->p++;
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                }
                if (cp < 0x80) {
                    cx_buf_putc(&b, (char)cp);
                } else if (cp < 0x800) {
                    cx_buf_putc(&b, (char)(0xC0 | (cp >> 6)));
                    cx_buf_putc(&b, (char)(0x80 | (cp & 0x3F)));
                } else {
                    cx_buf_putc(&b, (char)(0xE0 | (cp >> 12)));
                    cx_buf_putc(&b, (char)(0x80 | ((cp >> 6) & 0x3F)));
                    cx_buf_putc(&b, (char)(0x80 | (cp & 0x3F)));
                }
                break;
            }
            default:
                cx_buf_putc(&b, e);
                break;
            }
        } else {
            cx_buf_putc(&b, (char)ch);
        }
    }

    cx_buf_free(&b);
    return NULL; /* unterminated */
}

static JsonValue *parse_value(Cursor *c);

static JsonValue *parse_object(Cursor *c) {
    c->p++; /* '{' */
    JsonValue *v = value_new(JSON_OBJ);
    if (!v) return NULL;

    skip_ws(c);
    if (c->p < c->end && *c->p == '}') {
        c->p++;
        return v;
    }

    int cap = 0;
    for (;;) {
        skip_ws(c);
        char *key = parse_string(c);
        if (!key) {
            cx_json_free(v);
            return NULL;
        }
        skip_ws(c);
        if (c->p >= c->end || *c->p != ':') {
            free(key);
            cx_json_free(v);
            return NULL;
        }
        c->p++;

        JsonValue *val = parse_value(c);
        if (!val) {
            free(key);
            cx_json_free(v);
            return NULL;
        }
        if (v->members_len == cap) {
            cap = cap ? cap * 2 : 8;
            JsonMember *m = (JsonMember *)realloc(v->members, sizeof(JsonMember) * (size_t)cap);
            if (!m) {
                free(key);
                cx_json_free(val);
                cx_json_free(v);
                return NULL;
            }
            v->members = m;
        }
        v->members[v->members_len].key = key;
        v->members[v->members_len].value = val;
        v->members_len++;

        skip_ws(c);
        if (c->p < c->end && *c->p == ',') {
            c->p++;
            continue;
        }
        if (c->p < c->end && *c->p == '}') {
            c->p++;
            break;
        }
        cx_json_free(v);
        return NULL;
    }
    return v;
}

static JsonValue *parse_array(Cursor *c) {
    c->p++; /* '[' */
    JsonValue *v = value_new(JSON_ARR);
    if (!v) return NULL;

    skip_ws(c);
    if (c->p < c->end && *c->p == ']') {
        c->p++;
        return v;
    }

    int cap = 0;
    for (;;) {
        JsonValue *item = parse_value(c);
        if (!item) {
            cx_json_free(v);
            return NULL;
        }
        if (v->items_len == cap) {
            cap = cap ? cap * 2 : 8;
            JsonValue **it = (JsonValue **)realloc(v->items, sizeof(JsonValue *) * (size_t)cap);
            if (!it) {
                cx_json_free(item);
                cx_json_free(v);
                return NULL;
            }
            v->items = it;
        }
        v->items[v->items_len++] = item;

        skip_ws(c);
        if (c->p < c->end && *c->p == ',') {
            c->p++;
            continue;
        }
        if (c->p < c->end && *c->p == ']') {
            c->p++;
            break;
        }
        cx_json_free(v);
        return NULL;
    }
    return v;
}

static JsonValue *parse_number(Cursor *c) {
    const char *start = c->p;
    if (c->p < c->end && (*c->p == '-' || *c->p == '+')) c->p++;
    while (c->p < c->end &&
           ((*c->p >= '0' && *c->p <= '9') || *c->p == '.' || *c->p == 'e' ||
            *c->p == 'E' || *c->p == '-' || *c->p == '+'))
        c->p++;
    if (c->p == start) return NULL;

    char tmp[64];
    size_t n = (size_t)(c->p - start);
    if (n >= sizeof tmp) n = sizeof tmp - 1;
    memcpy(tmp, start, n);
    tmp[n] = '\0';

    JsonValue *v = value_new(JSON_NUM);
    if (v) v->num = strtod(tmp, NULL);
    return v;
}

static JsonValue *parse_value(Cursor *c) {
    skip_ws(c);
    if (c->p >= c->end) return NULL;

    switch (*c->p) {
    case '"': {
        char *s = parse_string(c);
        if (!s) return NULL;
        JsonValue *v = value_new(JSON_STR);
        if (!v) {
            free(s);
            return NULL;
        }
        v->str = s;
        return v;
    }
    case '{':
        return parse_object(c);
    case '[':
        return parse_array(c);
    case 't':
        if (c->end - c->p >= 4 && !strncmp(c->p, "true", 4)) {
            c->p += 4;
            JsonValue *v = value_new(JSON_BOOL);
            if (v) v->boolean = 1;
            return v;
        }
        return NULL;
    case 'f':
        if (c->end - c->p >= 5 && !strncmp(c->p, "false", 5)) {
            c->p += 5;
            return value_new(JSON_BOOL);
        }
        return NULL;
    case 'n':
        if (c->end - c->p >= 4 && !strncmp(c->p, "null", 4)) {
            c->p += 4;
            return value_new(JSON_NULL);
        }
        return NULL;
    default:
        return parse_number(c);
    }
}

JsonValue *cx_json_parse(const char *text, size_t len) {
    if (!text) return NULL;
    Cursor c = {text, text + len};
    return parse_value(&c);
}

void cx_json_free(JsonValue *v) {
    if (!v) return;
    if (v->kind == JSON_STR) free(v->str);
    for (int i = 0; i < v->members_len; i++) {
        free(v->members[i].key);
        cx_json_free(v->members[i].value);
    }
    for (int i = 0; i < v->items_len; i++) cx_json_free(v->items[i]);
    free(v->members);
    free(v->items);
    free(v);
}

const JsonValue *cx_json_get(const JsonValue *obj, const char *key) {
    if (!obj || obj->kind != JSON_OBJ || !key) return NULL;
    for (int i = 0; i < obj->members_len; i++)
        if (strcmp(obj->members[i].key, key) == 0) return obj->members[i].value;
    return NULL;
}

const char *cx_json_string(const JsonValue *v) {
    return (v && v->kind == JSON_STR) ? v->str : NULL;
}

long cx_json_int(const JsonValue *v, long fallback) {
    return (v && v->kind == JSON_NUM) ? (long)v->num : fallback;
}

int cx_json_array_len(const JsonValue *v) {
    return (v && v->kind == JSON_ARR) ? v->items_len : 0;
}

const JsonValue *cx_json_array_at(const JsonValue *v, int index) {
    if (!v || v->kind != JSON_ARR || index < 0 || index >= v->items_len) return NULL;
    return v->items[index];
}

void cx_json_write_string(CxBuf *out, const char *s) {
    cx_buf_putc(out, '"');
    if (s) {
        for (const char *p = s; *p; p++) {
            unsigned char ch = (unsigned char)*p;
            switch (ch) {
            case '"':  cx_buf_puts(out, "\\\""); break;
            case '\\': cx_buf_puts(out, "\\\\"); break;
            case '\n': cx_buf_puts(out, "\\n");  break;
            case '\r': cx_buf_puts(out, "\\r");  break;
            case '\t': cx_buf_puts(out, "\\t");  break;
            case '\b': cx_buf_puts(out, "\\b");  break;
            case '\f': cx_buf_puts(out, "\\f");  break;
            default:
                if (ch < 0x20) cx_buf_printf(out, "\\u%04x", (unsigned)ch);
                else cx_buf_putc(out, (char)ch);
            }
        }
    }
    cx_buf_putc(out, '"');
}

void cx_json_write(CxBuf *out, const JsonValue *v) {
    if (!v) {
        cx_buf_puts(out, "null");
        return;
    }
    switch (v->kind) {
    case JSON_NULL:
        cx_buf_puts(out, "null");
        break;
    case JSON_BOOL:
        cx_buf_puts(out, v->boolean ? "true" : "false");
        break;
    case JSON_NUM: {
        double d = v->num;
        if (d == (double)(long long)d) cx_buf_printf(out, "%lld", (long long)d);
        else cx_buf_printf(out, "%g", d);
        break;
    }
    case JSON_STR:
        cx_json_write_string(out, v->str);
        break;
    case JSON_ARR:
        cx_buf_putc(out, '[');
        for (int i = 0; i < v->items_len; i++) {
            if (i) cx_buf_putc(out, ',');
            cx_json_write(out, v->items[i]);
        }
        cx_buf_putc(out, ']');
        break;
    case JSON_OBJ:
        cx_buf_putc(out, '{');
        for (int i = 0; i < v->members_len; i++) {
            if (i) cx_buf_putc(out, ',');
            cx_json_write_string(out, v->members[i].key);
            cx_buf_putc(out, ':');
            cx_json_write(out, v->members[i].value);
        }
        cx_buf_putc(out, '}');
        break;
    }
}
