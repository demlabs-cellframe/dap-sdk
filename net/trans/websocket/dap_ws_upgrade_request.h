/* WebSocket nonce is canonical base64 encoding of exactly 16 bytes. */
#ifndef DAP_WS_UPGRADE_REQUEST_H
#define DAP_WS_UPGRADE_REQUEST_H
#include "dap_ws_upgrade_response.h"

/* Strict singleton policy for handshake identity/framing fields. */
static inline int dap_ws_request_headers_valid(const dap_http_header_t *headers,
                                               int http11, size_t body_size)
{
    if (!http11 || body_size) return 0;
    unsigned host = 0, key = 0, version = 0, upgrade = 0, connection = 0, length = 0;
    for (const dap_http_header_t *h = headers; h; h = h->next) {
        if (!strcasecmp(h->name, "Host")) {
            if (++host != 1 || !h->value[0]) return 0;
            for (const unsigned char *p = (const unsigned char *)h->value; *p; ++p)
                if (*p <= 32 || *p == 127 || *p == ',' || *p == '/' || *p == '@') return 0;
        } else if (!strcasecmp(h->name, "Sec-WebSocket-Key")) { if (++key != 1) return 0; }
        else if (!strcasecmp(h->name, "Sec-WebSocket-Version")) { if (++version != 1) return 0; }
        else if (!strcasecmp(h->name, "Upgrade")) { if (++upgrade != 1) return 0; }
        else if (!strcasecmp(h->name, "Connection")) { if (++connection != 1) return 0; }
        else if (!strcasecmp(h->name, "Transfer-Encoding")) return 0;
        else if (!strcasecmp(h->name, "Content-Length")) {
            if (++length != 1 || strcmp(h->value, "0")) return 0;
        }
    }
    return host == 1 && key == 1 && version == 1 && upgrade == 1 && connection == 1;
}

static inline int dap_ws_request_key_valid(const char *key)
{
    if (!key || strlen(key) != 24 || key[22] != '=' || key[23] != '=') return 0;
    const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i = 0; i < 22; ++i) {
        const char *digit = strchr(alphabet, key[i]);
        if (!digit || (i == 21 && ((size_t)(digit - alphabet) & 15))) return 0;
    }
    return 1;
}

static inline int dap_ws_request_fields_valid(const char *method, const char *upgrade,
    const char *connection, const char *key)
{
    return method && !strcmp(method, "GET") && upgrade && connection &&
        dap_ws_has_token(upgrade, "websocket") && dap_ws_has_token(connection, "upgrade") &&
        !dap_ws_has_token(connection, "close") && dap_ws_request_key_valid(key);
}
#endif
