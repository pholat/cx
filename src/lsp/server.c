#include "cx/cx.h"
#include "cx/lsp.h"

#include "json.h"
#include "lsp_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* JSON-RPC dispatch. Diagnostics are pushed on didOpen/didChange/didClose;
 * symbol/definition/hover stay stubbed until later phases. */

static void send_json(const char *json) {
    if (json) cx_lsp_write_message(json);
}

static void respond_null(const JsonValue *id) {
    CxBuf b;
    cx_buf_init(&b);
    cx_buf_puts(&b, "{\"jsonrpc\":\"2.0\",\"id\":");
    cx_json_write(&b, id);
    cx_buf_puts(&b, ",\"result\":null}");
    send_json(b.data);
    cx_buf_free(&b);
}

static void respond_result(const JsonValue *id, const char *result_json) {
    CxBuf b;
    cx_buf_init(&b);
    cx_buf_puts(&b, "{\"jsonrpc\":\"2.0\",\"id\":");
    cx_json_write(&b, id);
    cx_buf_puts(&b, ",\"result\":");
    cx_buf_puts(&b, (result_json && result_json[0]) ? result_json : "null");
    cx_buf_putc(&b, '}');
    send_json(b.data);
    cx_buf_free(&b);
}

/* documentSymbol (kind 0), definition (1), hover (2). Each reparses the stored
 * document through the shared parser + symbol table. */
static void handle_query(const JsonValue *params, const JsonValue *id, int kind) {
    const JsonValue *td = cx_json_get(params, "textDocument");
    const char *uri = cx_json_string(cx_json_get(td, "uri"));
    if (!uri) {
        respond_null(id);
        return;
    }

    CxDocument *doc = cx_doc_store_get(uri);
    const char *text = (doc && doc->text) ? doc->text : "";
    size_t len = doc ? doc->len : 0;

    const JsonValue *pos = cx_json_get(params, "position");
    int line = (int)cx_json_int(cx_json_get(pos, "line"), 0);
    int ch = (int)cx_json_int(cx_json_get(pos, "character"), 0);

    CxBuf result;
    cx_buf_init(&result);
    if (kind == 0) cx_lsp_document_symbols(&result, uri, text, len);
    else if (kind == 1) cx_lsp_definition(&result, uri, text, len, line, ch);
    else cx_lsp_hover(&result, uri, text, len, line, ch);
    respond_result(id, result.data ? result.data : "null");
    cx_buf_free(&result);
}

static void handle_initialize(const JsonValue *id) {
    CxBuf b;
    cx_buf_init(&b);
    cx_buf_puts(&b, "{\"jsonrpc\":\"2.0\",\"id\":");
    cx_json_write(&b, id);
    cx_buf_printf(&b,
                  ",\"result\":{\"capabilities\":{"
                  "\"textDocumentSync\":1," /* 1 = full sync */
                  "\"documentSymbolProvider\":true,"
                  "\"definitionProvider\":true,"
                  "\"hoverProvider\":true"
                  "},\"serverInfo\":{\"name\":\"cx\",\"version\":\"%s\"}}}",
                  CX_VERSION);
    send_json(b.data);
    cx_buf_free(&b);
}

static void publish_diagnostics(const char *uri, const char *text, size_t len) {
    CxBuf b;
    cx_buf_init(&b);
    cx_lsp_diagnostics_notification(&b, uri, text, len);
    send_json(b.data);
    cx_buf_free(&b);
}

static void clear_diagnostics(const char *uri) {
    CxBuf b;
    cx_buf_init(&b);
    cx_buf_puts(&b,
                "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\","
                "\"params\":{\"uri\":");
    cx_json_write_string(&b, uri);
    cx_buf_puts(&b, ",\"diagnostics\":[]}}");
    send_json(b.data);
    cx_buf_free(&b);
}

static void on_did_open(const JsonValue *params) {
    const JsonValue *td = cx_json_get(params, "textDocument");
    const char *uri = cx_json_string(cx_json_get(td, "uri"));
    const char *text = cx_json_string(cx_json_get(td, "text"));
    if (!uri || !text) return;

    cx_doc_store_put(uri, text, strlen(text),
                     (int)cx_json_int(cx_json_get(td, "version"), 0));
    publish_diagnostics(uri, text, strlen(text));
}

static void on_did_change(const JsonValue *params) {
    const JsonValue *td = cx_json_get(params, "textDocument");
    const char *uri = cx_json_string(cx_json_get(td, "uri"));
    if (!uri) return;

    /* Full sync: the last content change carries the whole document. */
    const JsonValue *changes = cx_json_get(params, "contentChanges");
    int n = cx_json_array_len(changes);
    const char *text = cx_json_string(cx_json_get(cx_json_array_at(changes, n - 1), "text"));

    if (text) {
        cx_doc_store_put(uri, text, strlen(text), 0);
        publish_diagnostics(uri, text, strlen(text));
    } else {
        CxDocument *doc = cx_doc_store_get(uri);
        if (doc && doc->text) publish_diagnostics(uri, doc->text, doc->len);
    }
}

static void on_did_close(const JsonValue *params) {
    const JsonValue *td = cx_json_get(params, "textDocument");
    const char *uri = cx_json_string(cx_json_get(td, "uri"));
    if (!uri) return;

    cx_doc_store_remove(uri);
    clear_diagnostics(uri);
}

int cx_lsp_run(void) {
    fprintf(stderr, "cx lsp: server started (stdio)\n");

    for (;;) {
        size_t len = 0;
        char *raw = cx_lsp_read_message(&len);
        if (!raw) break;

        JsonValue *root = cx_json_parse(raw, len);
        free(raw);
        if (!root) continue;

        const char *method = cx_json_string(cx_json_get(root, "method"));
        const JsonValue *id = cx_json_get(root, "id");
        const JsonValue *params = cx_json_get(root, "params");

        if (method && !strcmp(method, "initialize")) {
            handle_initialize(id);
        } else if (method && !strcmp(method, "initialized")) {
            /* notification, no response */
        } else if (method && !strcmp(method, "shutdown")) {
            respond_null(id);
        } else if (method && !strcmp(method, "exit")) {
            cx_json_free(root);
            break;
        } else if (method && !strcmp(method, "textDocument/didOpen")) {
            on_did_open(params);
        } else if (method && !strcmp(method, "textDocument/didChange")) {
            on_did_change(params);
        } else if (method && !strcmp(method, "textDocument/didClose")) {
            on_did_close(params);
        } else if (method && !strcmp(method, "textDocument/documentSymbol")) {
            handle_query(params, id, 0);
        } else if (method && !strcmp(method, "textDocument/definition")) {
            handle_query(params, id, 1);
        } else if (method && !strcmp(method, "textDocument/hover")) {
            handle_query(params, id, 2);
        } else if (id) {
            /* Unknown request: answer with a null result so the client is not stuck. */
            respond_null(id);
        }

        cx_json_free(root);
    }

    fprintf(stderr, "cx lsp: server stopped\n");
    return 0;
}
