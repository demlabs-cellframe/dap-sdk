/* TLS13 connection-owned channel: encrypted reactor I/O + framed DAP messages.
 * Socket callbacks call input/writable; the FSM owns frame dispatch. */
#ifndef DAP_TLS13_CHANNEL_H
#define DAP_TLS13_CHANNEL_H
#include "dap_tls_reactor.h"
#include "dap_tls13_frame.h"

typedef struct dap_tls13_channel {
    dap_tls_reactor_t tls;
    dap_tls_plain_queue_t outgoing;
    dap_tls13_frame_reader_t incoming;
    dap_tls13_frame_callback_t dispatch;
    void *dispatch_arg;
    bool read_budget_exhausted;
} dap_tls13_channel_t;

static int dap_tls13_channel_send(dap_tls13_channel_t *c, uint8_t type,
                                  const void *data, size_t size)
{
    if (!c || !data || !c->tls.ready || c->tls.failed || c->tls.closing) return -1;
    uint8_t header[6];
    if (dap_tls13_frame_header(header, type, size)) return -1;
    /* Header and body must be queued atomically; no half-message on OOM. */
    if (size > DAP_TLS_PLAIN_QUEUE_LIMIT - sizeof(dap_tls_plain_chunk_t) - sizeof(header)) return -1;
    size_t total = size + sizeof(header);
    uint8_t *frame = malloc(total);
    if (!frame) return -1;
    memcpy(frame, header, sizeof(header));
    memcpy(frame + sizeof(header), data, size);
    int rc = dap_tls_plain_queue_push(&c->outgoing, frame, total);
    free(frame);
    if (rc) return -1;
    return dap_tls_plain_queue_pump(&c->outgoing, &c->tls) < 0 ? -1 : 0;
}

/* 1: authenticated and drained this turn, 0: reschedule/I/O pending, -1: fail.
 * A zero return after read budget exhaustion requires a scheduled owner-worker
 * continuation even if the socket has no new encrypted bytes. */
static int dap_tls13_channel_drive(dap_tls13_channel_t *c)
{
    if (!c || !c->dispatch) return -1;
    c->read_budget_exhausted = false;
    int ready = dap_tls_reactor_handshake(&c->tls);
    if (ready <= 0) return ready;
    uint8_t plaintext[16384];
    for (unsigned budget = 0; budget < 16; ++budget) {
        ssize_t n = dap_tls_reactor_read(&c->tls, plaintext, sizeof(plaintext));
        if (n < 0) return -1;
        if (!n) {
            if (c->tls.peer_closed && (c->incoming.header_size || c->incoming.payload)) {
                c->tls.failed = true; /* authenticated close mid-message */
                return -1;
            }
            return dap_tls_plain_queue_pump(&c->outgoing, &c->tls);
        }
        if (dap_tls13_frame_feed(&c->incoming, plaintext, (size_t)n,
                                c->dispatch, c->dispatch_arg)) {
            c->tls.failed = true;
            return -1;
        }
    }
    c->read_budget_exhausted = true;
    return 0;
}

static int dap_tls13_channel_input(dap_tls13_channel_t *c, const void *data, size_t size)
{
    if (!c || dap_tls_reactor_feed(&c->tls, data, size)) return -1;
    return dap_tls13_channel_drive(c);
}

static void dap_tls13_channel_clear(dap_tls13_channel_t *c)
{
    dap_tls_plain_queue_clear(&c->outgoing);
    dap_tls13_frame_clear(&c->incoming);
    dap_tls_session_free(c->tls.session);
    memset(c, 0, sizeof(*c));
}
#endif
