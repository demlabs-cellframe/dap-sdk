/* HTTP plaintext sink backed by the existing owner-worker TLS stream.
 * This adapter owns no session/socket. Its context must outlive HTTP output.
 * The connection owner schedules writable/completion and TLS shutdown. */
#ifndef DAP_TLS_HTTP_OUTPUT_H
#define DAP_TLS_HTTP_OUTPUT_H
#include "dap_tls_plain_stream.h"
#include "dap_http_output.h"

typedef struct dap_tls_http_output {
    dap_tls_reactor_t *tls;
    dap_tls_plain_queue_t *outgoing;
    dap_tls_wire_drained_fn wire_drained;
    void *wire_arg;
} dap_tls_http_output_t;

static inline ssize_t dap_tls_http_output_write(void *arg, const void *data, size_t size)
{
    dap_tls_http_output_t *sink = arg;
    if (!sink || !sink->tls || !sink->outgoing || (!data && size)) return -1;
    if (!size) return 0;
    /* HTTP bodies/cache entries can exceed the TLS queue budget. Admit a
     * bounded prefix rather than making a valid large response fatal. */
    size_t chunk = size > 16384 ? 16384 : size;
    dap_tls_plain_write_result_t rc = dap_tls_plain_stream_write(
        sink->tls, sink->outgoing, data, chunk);
    return rc == DAP_TLS_PLAIN_WRITE_ACCEPTED ? (ssize_t)chunk :
           rc == DAP_TLS_PLAIN_WRITE_BLOCKED ? 0 : -1;
}

static inline int dap_tls_http_output_drained(void *arg)
{
    dap_tls_http_output_t *sink = arg;
    if (!sink) return -1;
    return dap_tls_plain_stream_drain(sink->tls, sink->outgoing,
                                     sink->wire_drained, sink->wire_arg);
}

static inline dap_http_output_t dap_tls_http_output_bind(dap_tls_http_output_t *sink)
{
    return (dap_http_output_t){.write = dap_tls_http_output_write, .arg = sink,
                              .drained = dap_tls_http_output_drained};
}
#endif
