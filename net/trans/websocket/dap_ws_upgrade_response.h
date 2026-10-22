/* Validate a complete response header block; uses the SDK HTTP field parser. */
#ifndef DAP_WS_UPGRADE_RESPONSE_H
#define DAP_WS_UPGRADE_RESPONSE_H
#include "dap_ws_upgrade_status.h"
#include "dap_http_header.h"
#include <strings.h>

#define DAP_WS_UPGRADE_HEADERS_MAX 32768U
/* 1 complete, 0 incomplete, -1 over budget/invalid. Ignore coalesced frames
 * after the first terminator when enforcing the HTTP header budget. */
static inline int dap_ws_upgrade_header_size(const void *data, size_t size, size_t *header_size)
{
    if (!header_size) return -1;
    *header_size = 0;
    if (!data && size) return -1;
    const unsigned char *p = data;
    size_t scan = size < DAP_WS_UPGRADE_HEADERS_MAX ? size : DAP_WS_UPGRADE_HEADERS_MAX;
    for (size_t i = 0; i + 3 < scan; ++i) {
        if (!memcmp(p + i, "\r\n\r\n", 4)) {
            *header_size = i + 4;
            return 1;
        }
    }
    return size >= DAP_WS_UPGRADE_HEADERS_MAX ? -1 : 0;
}

static inline int dap_ws_has_token(const char *value, const char *token)
{
    size_t wanted = strlen(token);
    while (*value) {
        while (*value == ' ' || *value == '\t' || *value == ',') ++value;
        const char *start = value;
        while (*value && *value != ',') ++value;
        const char *end = value;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) --end;
        if ((size_t)(end - start) == wanted && !strncasecmp(start, token, wanted)) return 1;
    }
    return 0;
}

static inline int dap_ws_upgrade_response_valid(const void *data, size_t size,
    const char *expected_accept, const char *offered_protocol)
{
    if (!expected_accept || !dap_ws_upgrade_status_valid(data, size) || size > DAP_WS_UPGRADE_HEADERS_MAX) return 0;
    const char *p = data, *end = p + size;
    const char *lf = memchr(p, '\n', size);
    if (!lf) return 0;
    p = lf + 1;
    int upgrade = 0, connection = 0, accept = 0, protocol = 0;
    unsigned fields = 0;
    while (p < end) {
        lf = memchr(p, '\n', (size_t)(end - p));
        if (!lf || lf == p || lf[-1] != '\r') return 0;
        size_t length = (size_t)(lf + 1 - p);
        if (length == 2) return lf + 1 == end && upgrade && connection && accept;
        if (++fields > 128 || length > 4096) return 0;
        char name[256], value[4096];
        if (dap_http_header_parse_line(p, length, name, sizeof(name), value, sizeof(value))) return 0;
        if (!strcasecmp(name, "Upgrade")) upgrade |= dap_ws_has_token(value, "websocket");
        else if (!strcasecmp(name, "Connection")) connection |= dap_ws_has_token(value, "upgrade");
        else if (!strcasecmp(name, "Sec-WebSocket-Accept")) {
            if (accept || strcmp(value, expected_accept)) return 0;
            accept = 1;
        } else if (!strcasecmp(name, "Sec-WebSocket-Extensions")) return 0;
        else if (!strcasecmp(name, "Sec-WebSocket-Protocol")) {
            if (protocol || !offered_protocol || !dap_ws_has_token(offered_protocol, value)) return 0;
            /* Subprotocol names are case-sensitive, unlike HTTP tokens. */
            if (!*value || strchr(value, ',')) return 0;
            int matched = 0;
            const char *offer = offered_protocol;
            while (*offer) {
                while (*offer == ' ' || *offer == '\t' || *offer == ',') ++offer;
                const char *start = offer;
                while (*offer && *offer != ',') ++offer;
                const char *last = offer;
                while (last > start && (last[-1] == ' ' || last[-1] == '\t')) --last;
                if ((size_t)(last - start) == strlen(value) && !memcmp(start, value, strlen(value))) matched = 1;
            }
            if (!matched) return 0;
            protocol = 1;
        }
        p = lf + 1;
    }
    return 0;
}
#endif
