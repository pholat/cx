#include "lsp_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

char *cx_lsp_read_message(size_t *len_out) {
    char line[512];
    size_t content_length = 0;

    while (fgets(line, sizeof line, stdin)) {
        if (!strcmp(line, "\r\n") || !strcmp(line, "\n")) break;
        if (!strncasecmp(line, "Content-Length:", 15))
            content_length = (size_t)strtoul(line + 15, NULL, 10);
    }
    if (content_length == 0) return NULL;

    char *buf = (char *)malloc(content_length + 1);
    if (!buf) return NULL;
    size_t got = fread(buf, 1, content_length, stdin);
    buf[got] = '\0';
    if (len_out) *len_out = got;
    return buf;
}

int cx_lsp_write_message(const char *json) {
    size_t n = strlen(json);
    printf("Content-Length: %zu\r\n\r\n%s", n, json);
    fflush(stdout);
    return 0;
}
