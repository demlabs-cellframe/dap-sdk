/* Protocol-independent TLS plaintext delivery on the connection owner worker. */
#ifndef DAP_TLS_PLAIN_STREAM_H
#define DAP_TLS_PLAIN_STREAM_H
#include "dap_tls_reactor.h"

/* Consume/copy all bytes before returning; the buffer is borrowed. An
 * incremental codec retains incomplete input itself. Callbacks must not free
 * the owning connection or recursively drive it. Nonzero means fatal error.
 * eof validates codec state; it can be called again on subsequent drives. */
typedef int (*dap_tls_plain_input_fn)(void *, const void *, size_t);
typedef int (*dap_tls_plain_eof_fn)(void *);
typedef int (*dap_tls_wire_drained_fn)(void *);

/* Graceful close is an owner-driven phase: stop application writes before
 * calling, keep driving on I/O progress until 1, abort on a close deadline.
 * Do not set tls->closing before the plaintext queue has drained: the
 * reactor write path deliberately rejects writes after shutdown starts. */
static inline int dap_tls_plain_stream_shutdown(dap_tls_reactor_t *tls,
    dap_tls_plain_queue_t *outgoing, dap_tls_wire_drained_fn wire_drained, void *arg)
{
    if (!tls || !outgoing || !wire_drained || !tls->ready || tls->failed)
        return -1;
    if (!tls->closing) {
        int rc = dap_tls_plain_queue_pump(outgoing, tls);
        if (rc <= 0) return rc;
    } else if (outgoing->head) {
        tls->failed = true; /* Owner started shutdown before application drain. */
        return -1;
    }
    int rc = dap_tls_reactor_shutdown(tls);
    if (rc <= 0) return rc;
    rc = wire_drained(arg);
    if (rc < 0) tls->failed = true;
    return rc < 0 ? -1 : rc == 1 ? 1 : 0;
}

/* Progress TLS output before checking the lower transport. All callers must
 * serialize this operation with writes on the owner worker. */
static inline int dap_tls_plain_stream_drain(dap_tls_reactor_t *tls,
    dap_tls_plain_queue_t *outgoing, dap_tls_wire_drained_fn wire_drained, void *arg)
{
    if (!tls || !outgoing || !wire_drained || !tls->ready || tls->failed)
        return -1;
    int rc = dap_tls_plain_queue_pump(outgoing, tls);
    if (rc <= 0) return rc;
    rc = wire_drained(arg);
    if (rc < 0) tls->failed = true;
    return rc < 0 ? -1 : rc == 1 ? 1 : 0;
}

typedef enum dap_tls_plain_write_result {
    DAP_TLS_PLAIN_WRITE_FATAL = -1,
    DAP_TLS_PLAIN_WRITE_ACCEPTED = 0,
    DAP_TLS_PLAIN_WRITE_BLOCKED = 1
} dap_tls_plain_write_result_t;

/* ACCEPTED copies the entire buffer; it does not mean peer delivery.
 * BLOCKED accepts nothing: retry on owner-worker writable/queue progress.
 * FATAL is terminal (including allocation/flush failure); never retry the
 * same message after FATAL, since encryption may already have progressed.
 * Oversized single writes are fatal, not permanently retryable backpressure. */
static inline dap_tls_plain_write_result_t dap_tls_plain_stream_write(
    dap_tls_reactor_t *tls, dap_tls_plain_queue_t *outgoing,
    const void *data, size_t size)
{
    if (!tls || !outgoing) return DAP_TLS_PLAIN_WRITE_FATAL;
    if (!data || !size || !tls->ready || tls->failed || tls->closing ||
        size > DAP_TLS_PLAIN_QUEUE_LIMIT - sizeof(dap_tls_plain_chunk_t)) {
        tls->failed = true;
        return DAP_TLS_PLAIN_WRITE_FATAL;
    }
    size_t allocation = sizeof(dap_tls_plain_chunk_t) + size;
    if (outgoing->allocated > DAP_TLS_PLAIN_QUEUE_LIMIT - allocation)
        return DAP_TLS_PLAIN_WRITE_BLOCKED;
    if (dap_tls_plain_queue_push(outgoing, data, size) ||
        dap_tls_plain_queue_pump(outgoing, tls) < 0) {
        tls->failed = true;
        return DAP_TLS_PLAIN_WRITE_FATAL;
    }
    return DAP_TLS_PLAIN_WRITE_ACCEPTED;
}

/* 1 drained, 0 pending/continuation, -1 fatal. Same bounded work and queues
 * for every application protocol; no HTTP/DAP parsing belongs here. */
static inline int dap_tls_plain_stream_drive(dap_tls_reactor_t *tls,
    dap_tls_plain_queue_t *outgoing, dap_tls_plain_input_fn input,
    dap_tls_plain_eof_fn eof, void *arg, bool *budget_exhausted)
{
    if (!tls || !outgoing || !input || !budget_exhausted) return -1;
    *budget_exhausted = false;
    int ready = dap_tls_reactor_handshake(tls);
    if (ready <= 0) return ready;
    uint8_t plaintext[16384];
    for (unsigned budget = 0; budget < 16; ++budget) {
        ssize_t n = dap_tls_reactor_read(tls, plaintext, sizeof(plaintext));
        if (n < 0) return -1;
        if (!n) {
            if (tls->peer_closed && eof && eof(arg)) {
                tls->failed = true;
                return -1;
            }
            return dap_tls_plain_queue_pump(outgoing, tls);
        }
        if (input(arg, plaintext, (size_t)n)) {
            tls->failed = true;
            return -1;
        }
    }
    *budget_exhausted = true;
    return 0;
}
#endif
