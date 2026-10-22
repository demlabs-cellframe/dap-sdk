/* TLS13-only framing of authenticated plaintext. No ownership of TLS/socket.
 * Header: version(1), type(1), payload length(4, network byte order).
 * DAP messages are opaque bytes; authentication/payment formats are unchanged. */
#ifndef DAP_TLS13_FRAME_H
#define DAP_TLS13_FRAME_H
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define DAP_TLS13_FRAME_MAX (1024U * 1024U)
enum { DAP_TLS13_ENC_REQUEST = 1, DAP_TLS13_ENC_REPLY,
       DAP_TLS13_CTL_REQUEST, DAP_TLS13_CTL_REPLY, DAP_TLS13_STREAM };
typedef struct dap_tls13_frame_reader {
    uint8_t header[6];
    size_t header_size, payload_size, received;
    uint8_t *payload;
    int failed;
} dap_tls13_frame_reader_t;
typedef int (*dap_tls13_frame_callback_t)(void *, uint8_t, const void *, size_t);

static int dap_tls13_frame_header(uint8_t out[6], uint8_t type, size_t size)
{
    if (!out || type < DAP_TLS13_ENC_REQUEST || type > DAP_TLS13_STREAM ||
        !size || size > DAP_TLS13_FRAME_MAX) return -1;
    out[0] = 1; out[1] = type;
    for (unsigned i = 0; i < 4; ++i) out[2+i] = (uint8_t)(size >> (24-8*i));
    return 0;
}

static void dap_tls13_frame_clear(dap_tls13_frame_reader_t *r)
{
    free(r->payload);
    memset(r, 0, sizeof(*r));
}

/* Callback borrows payload only during the call. It must defer destruction
 * of the reader/owner until feed returns (same as SDK read callbacks). */
static int dap_tls13_frame_feed(dap_tls13_frame_reader_t *r, const void *data,
                               size_t size, dap_tls13_frame_callback_t cb, void *arg)
{
    if (!r || r->failed || (!data && size) || !cb) return -1;
    const uint8_t *p = data;
    while (size) {
        if (r->header_size < sizeof(r->header)) {
            size_t n = sizeof(r->header) - r->header_size;
            if (n > size) n = size;
            memcpy(r->header + r->header_size, p, n);
            p += n; size -= n; r->header_size += n;
            if (r->header_size < sizeof(r->header)) continue;
            uint32_t length = 0;
            for (unsigned i = 0; i < 4; ++i) length = (length << 8) | r->header[2+i];
            if (r->header[0] != 1 || r->header[1] < DAP_TLS13_ENC_REQUEST ||
                r->header[1] > DAP_TLS13_STREAM || !length || length > DAP_TLS13_FRAME_MAX)
                goto fail;
            r->payload_size = length;
            r->payload = malloc(length);
            if (!r->payload) goto fail;
        }
        size_t n = r->payload_size - r->received;
        if (n > size) n = size;
        memcpy(r->payload + r->received, p, n);
        p += n; size -= n; r->received += n;
        if (r->received == r->payload_size) {
            int rc = cb(arg, r->header[1], r->payload, r->payload_size);
            dap_tls13_frame_clear(r);
            if (rc) goto fail;
        }
    }
    return 0;
fail:
    dap_tls13_frame_clear(r);
    r->failed = 1;
    return -1;
}
#endif
