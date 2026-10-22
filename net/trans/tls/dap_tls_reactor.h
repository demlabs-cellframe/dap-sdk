/* Owner-worker adapter. The caller owns the SDK socket and TLS session.
 * No protocol/authentication/payment policy belongs in this layer. */
#ifndef DAP_TLS_REACTOR_H
#define DAP_TLS_REACTOR_H
#include "dap_tls.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

typedef ssize_t (*dap_tls_wire_send_fn)(void *, const void *, size_t);
typedef struct dap_tls_reactor {
    dap_tls_session_t *session;
    dap_tls_wire_send_fn send;
    void *send_arg;
    uint8_t wire[16384];
    size_t offset, size;
    bool server, ready, failed, closing, peer_closed;
} dap_tls_reactor_t;

/* Bounded plaintext queue for the all-or-error SDK stream write contract.
 * Positive enqueue means ownership was copied, not that the peer ACKed it. */
#define DAP_TLS_PLAIN_QUEUE_LIMIT (256U * 1024U)
typedef struct dap_tls_plain_chunk {
    struct dap_tls_plain_chunk *next;
    size_t size, offset;
    uint8_t data[];
} dap_tls_plain_chunk_t;
typedef struct dap_tls_plain_queue {
    dap_tls_plain_chunk_t *head, *tail;
    size_t allocated;
} dap_tls_plain_queue_t;

static void dap_tls_plain_queue_clear(dap_tls_plain_queue_t *q)
{
    while (q->head) {
        dap_tls_plain_chunk_t *next = q->head->next;
        free(q->head);
        q->head = next;
    }
    q->tail = NULL;
    q->allocated = 0;
}

static int dap_tls_plain_queue_push(dap_tls_plain_queue_t *q, const void *data, size_t size)
{
    if (!q || !data || !size || size > DAP_TLS_PLAIN_QUEUE_LIMIT - sizeof(dap_tls_plain_chunk_t)) return -1;
    size_t allocation = sizeof(dap_tls_plain_chunk_t) + size;
    if (q->allocated > DAP_TLS_PLAIN_QUEUE_LIMIT - allocation) return -1;
    dap_tls_plain_chunk_t *chunk = malloc(allocation);
    if (!chunk) return -1;
    *chunk = (dap_tls_plain_chunk_t){.size = size};
    memcpy(chunk->data, data, size);
    if (q->tail) q->tail->next = chunk;
    else q->head = chunk;
    q->tail = chunk;
    q->allocated += allocation;
    return 0;
}

/* 1 drained, 0 backpressure, -1 fatal. Preserve unsent ciphertext exactly. */
static int dap_tls_reactor_flush(dap_tls_reactor_t *r)
{
    if (!r || r->failed || !r->session || !r->send) return -1;
    /* Bound work per reactor event as well as retained memory. */
    for (unsigned budget = 0; budget < 16; ++budget) {
        if (r->offset == r->size) {
            ssize_t n = dap_tls_openssl_session_drain(r->session, r->wire, sizeof(r->wire));
            if (n < 0) { r->failed = true; return -1; }
            r->offset = 0;
            r->size = (size_t)n;
            if (!n) return 1;
        }
        size_t remaining = r->size - r->offset;
        ssize_t sent = r->send(r->send_arg, r->wire + r->offset, remaining);
        if (sent < 0 || (size_t)sent > remaining) { r->failed = true; return -1; }
        if (!sent) return 0;
        r->offset += (size_t)sent;
    }
    return 0; /* Schedule another writable turn, never busy-loop. */
}

/* Called on input or writable notification. Ready means authenticated on
 * the client; the backend enforces the provisioned pin before returning 1. */
static int dap_tls_reactor_handshake(dap_tls_reactor_t *r)
{
    if (!r || r->failed || !r->session) return -1;
    if (r->ready) return dap_tls_reactor_flush(r) < 0 ? -1 : 1;
    int flushed = dap_tls_reactor_flush(r);
    if (flushed <= 0) return flushed;
    int rc = r->server ? dap_tls_session_accept(r->session) : dap_tls_session_connect(r->session);
    if (rc < 0) { r->failed = true; return -1; }
    r->ready = rc == 1;
    if (dap_tls_reactor_flush(r) < 0) return -1;
    return r->ready ? 1 : 0;
}

static int dap_tls_reactor_feed(dap_tls_reactor_t *r, const void *data, size_t size)
{
    if (!r || r->failed || !r->session) return -1;
    if (!size) return 0;
    if (dap_tls_openssl_session_feed(r->session, data, size) != (ssize_t)size) {
        r->failed = true;
        return -1;
    }
    return 0;
}

/* At most one record per call. The caller retains unaccepted plaintext and
 * retries the SAME bytes after WANT_*; positive return consumes that prefix. */
static ssize_t dap_tls_reactor_write(dap_tls_reactor_t *r, const void *data, size_t size)
{
    if (!r || r->failed || !r->ready || r->closing || r->peer_closed || !data) return -1;
    if (!size) return 0;
    int flushed = dap_tls_reactor_flush(r);
    if (flushed <= 0) return flushed;
    size_t chunk = size > 16384 ? 16384 : size;
    ssize_t written = dap_tls_session_write(r->session, data, chunk);
    if (written <= 0) {
        dap_tls_error_t error = dap_tls_session_get_error(r->session, (int)written);
        if (error != DAP_TLS_ERR_WANT_READ && error != DAP_TLS_ERR_WANT_WRITE) {
            r->failed = true;
            return -1;
        }
        if (dap_tls_reactor_flush(r) < 0) return -1;
        return 0;
    }
    if (dap_tls_reactor_flush(r) < 0) return -1;
    return written;
}

/* Preserve buffer identity across SSL WANT_* retries; no realloc/memmove of
 * pending plaintext. Budget avoids starving other transports on the worker. */
static int dap_tls_plain_queue_pump(dap_tls_plain_queue_t *q, dap_tls_reactor_t *r)
{
    for (unsigned budget = 0; q->head && budget < 16; ++budget) {
        dap_tls_plain_chunk_t *chunk = q->head;
        ssize_t n = dap_tls_reactor_write(r, chunk->data + chunk->offset, chunk->size - chunk->offset);
        if (n <= 0) return (int)n;
        chunk->offset += (size_t)n;
        if (chunk->offset == chunk->size) {
            q->head = chunk->next;
            if (!q->head) q->tail = NULL;
            q->allocated -= sizeof(*chunk) + chunk->size;
            free(chunk);
        }
    }
    return q->head ? 0 : dap_tls_reactor_flush(r);
}

/* Positive: plaintext bytes. Zero: needs more I/O or orderly peer closure
 * (distinguish using peer_closed). Negative: terminal protocol/I/O error. */
static ssize_t dap_tls_reactor_read(dap_tls_reactor_t *r, void *data, size_t size)
{
    if (!r || r->failed || !r->ready || !data || !size) return -1;
    if (r->peer_closed) return 0;
    ssize_t n = dap_tls_session_read(r->session, data, size);
    if (n <= 0) {
        /* Capture before another SSL operation changes error state. */
        dap_tls_error_t error = dap_tls_session_get_error(r->session, (int)n);
        if (error == DAP_TLS_ERR_CLOSED) r->peer_closed = true;
        else if (error != DAP_TLS_ERR_WANT_READ && error != DAP_TLS_ERR_WANT_WRITE) {
            r->failed = true;
            return -1;
        }
    }
    if (dap_tls_reactor_flush(r) < 0) return -1;
    return n > 0 ? n : 0;
}

/* Send close_notify and wait for the peer without blocking a worker.
 * The owner must impose a close deadline and release the session on abort. */
static int dap_tls_reactor_shutdown(dap_tls_reactor_t *r)
{
    if (!r || r->failed || !r->ready) return -1;
    r->closing = true;
    int flushed = dap_tls_reactor_flush(r);
    if (flushed <= 0) return flushed;
    int rc = dap_tls_session_shutdown(r->session);
    if (rc < 0) { r->failed = true; return -1; }
    flushed = dap_tls_reactor_flush(r);
    if (flushed < 0) return -1;
    return rc == 1 && flushed == 1 ? 1 : 0;
}
#endif
