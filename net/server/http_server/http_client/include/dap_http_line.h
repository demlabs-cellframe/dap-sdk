/* Bounded HTTP/1 line framing, independent of sockets and TLS.
 * The caller retains input until a complete line is available. */
#ifndef DAP_HTTP_LINE_H
#define DAP_HTTP_LINE_H

#include <stddef.h>
#include <string.h>

/* 1: complete (length includes CRLF), 0: need more, -1: malformed/too long.
 * limit includes CRLF. Bytes following the first line are not examined. */
static inline int dap_http_line_read(const void *data, size_t size,
                                     size_t limit, size_t *length)
{
    if (!length)
        return -1;
    *length = 0;
    if ((!data && size) || limit < 2)
        return -1;
    if (!size)
        return 0;
    const unsigned char *bytes = data;
    size_t bounded = size < limit ? size : limit;
    for (size_t i = 0; i < bounded; ++i) {
        if (!bytes[i])
            return -1;
        if (bytes[i] == '\n') {
            if (!i || bytes[i - 1] != '\r')
                return -1;
            *length = i + 1;
            return 1;
        }
        if (bytes[i] == '\r' && i + 1 < bounded && bytes[i + 1] != '\n')
            return -1;
    }
    return size >= limit ? -1 : 0;
}
#endif
