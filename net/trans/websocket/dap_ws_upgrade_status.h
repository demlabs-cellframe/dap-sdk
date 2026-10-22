/* Status-line validation only; the caller must also validate Upgrade,
 * Connection and Sec-WebSocket-Accept before declaring the channel open. */
#ifndef DAP_WS_UPGRADE_STATUS_H
#define DAP_WS_UPGRADE_STATUS_H
#include <stddef.h>
#include <string.h>
static inline int dap_ws_upgrade_status_valid(const void *data, size_t size)
{
    const unsigned char *p = data;
    static const char prefix[] = "HTTP/1.1 101 ";
    if (!p || size < sizeof(prefix) - 1 + 2 ||
        memcmp(p, prefix, sizeof(prefix) - 1)) return 0;
    for (size_t i = sizeof(prefix) - 1; i < size; ++i) {
        if (p[i] == '\r') return i + 1 < size && p[i + 1] == '\n';
        if ((p[i] < 32 && p[i] != '\t') || p[i] == 127) return 0;
    }
    return 0;
}
#endif
