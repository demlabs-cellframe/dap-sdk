/* Bounded response serialization; never publishes a truncated header block. */
#ifndef DAP_HTTP_RESPONSE_BUFFER_H
#define DAP_HTTP_RESPONSE_BUFFER_H
#include <stdarg.h>
#include <stdio.h>
#include <stddef.h>
#define DAP_HTTP_RESPONSE_HEADERS_MAX (32U * 1024U)
static inline int dap_http_response_append(char *buffer, size_t capacity,
                                          size_t *used, const char *format, ...)
{
    if (!buffer || !used || *used >= capacity || !format) return -1;
    va_list args;
    va_start(args, format);
    int n = vsnprintf(buffer + *used, capacity - *used, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= capacity - *used) return -1;
    *used += (size_t)n;
    return 0;
}
#endif
