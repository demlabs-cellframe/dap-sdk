/* Private owner-thread plaintext tail shared by TLS client and server. */
#ifndef DAP_TLS_STREAM_BUFFER_H
#define DAP_TLS_STREAM_BUFFER_H
#include <stdlib.h>
#include <string.h>

static int s_tls_stream_feed(dap_stream_t *stream, uint8_t **tail, size_t *size,
                             const void *data, size_t length)
{
    const uint8_t *input = data;
    size_t total = length;
    if (*size) {
        if (length > SIZE_MAX - *size) return -1;
        total += *size;
        uint8_t *joined = realloc(*tail, total);
        if (!joined) return -1;
        memcpy(joined + *size, data, length);
        *tail = joined;
        input = joined;
    }
    size_t consumed = dap_stream_data_proc_read_ext(stream, input, total);
    if (consumed > total) return -1;
    size_t remaining = total - consumed;
    if (remaining > DAP_STREAM_PKT_SIZE_MAX + sizeof(dap_stream_pkt_hdr_t)) return -1;
    if (remaining) {
        if (*size) {
            memmove(*tail, input + consumed, remaining);
        } else {
            uint8_t *saved = malloc(remaining);
            if (!saved) return -1;
            memcpy(saved, input + consumed, remaining);
            free(*tail);
            *tail = saved;
        }
    } else {
        free(*tail);
        *tail = NULL;
    }
    *size = remaining;
    return 0;
}
#endif
