/* Socket glue for TLS13. Attach through callbacks.arg, never overwrite the
 * client's _inheritor (the SDK FSM owns it). Owner must cancel its continuation
 * before socket/context destruction. All functions run on the owner worker. */
#ifndef DAP_TLS13_SOCKET_H
#define DAP_TLS13_SOCKET_H
#include "dap_events_socket.h"
#include "dap_worker.h"
#include "dap_context.h"
#include "dap_tls13_channel.h"

typedef struct dap_tls13_socket {
    dap_tls13_channel_t *channel; /* borrowed from connection-owned endpoint */
    dap_events_socket_t *socket;
    void (*ready)(void *);
    void *ready_arg;
    bool ready_notified;
    bool continuation_pending;
    void *previous_arg;
    void (*previous_read)(dap_events_socket_t *, void *);
    bool (*previous_write)(dap_events_socket_t *, void *);
    void (*previous_delete)(dap_events_socket_t *, void *);
    void (*previous_write_finished)(dap_events_socket_t *, void *);
    void (*previous_error)(dap_events_socket_t *, int);
} dap_tls13_socket_t;

static int dap_tls13_socket_drive(dap_tls13_socket_t *s);
static void dap_tls13_socket_read(dap_events_socket_t *es, void *arg);
static void dap_tls13_socket_unbind(dap_tls13_socket_t *s);
static void dap_tls13_socket_error(dap_events_socket_t *es, int error)
{
    dap_tls13_socket_t *s = es ? es->callbacks.arg : NULL;
    if (!s || s->socket != es) return;
    /* Restore all callbacks before delegation: the owner can delete its
     * connection synchronously. Do not access s after invoking it. */
    void (*callback)(dap_events_socket_t *, int) = s->previous_error;
    dap_tls13_socket_unbind(s);
    if (callback) callback(es, error);
    else es->flags |= DAP_SOCK_SIGNAL_CLOSE;
}
static void dap_tls13_socket_deleted(dap_events_socket_t *es, void *arg)
{
    dap_tls13_socket_t *s = arg;
    if (!s || s->socket != es) return;
    void (*callback)(dap_events_socket_t *, void *) = s->previous_delete;
    void *previous_arg = s->previous_arg;
    dap_tls13_socket_unbind(s);
    if (callback) callback(es, previous_arg);
}

static void dap_tls13_socket_write_finished(dap_events_socket_t *es, void *arg)
{
    dap_tls13_socket_t *s = arg;
    if (!s || s->socket != es) return;
    /* Pending TLS bytes refill buf_out before the reactor disables POLLOUT. */
    dap_tls13_socket_drive(s);
}
typedef struct dap_tls13_continuation {
    dap_events_socket_uuid_t uuid;
} dap_tls13_continuation_t;

static void dap_tls13_socket_continue(void *arg)
{
    dap_tls13_continuation_t *request = arg;
    dap_worker_t *worker = dap_worker_get_current();
    dap_events_socket_t *es = worker ? dap_context_find(worker->context, request->uuid) : NULL;
    free(request);
    if (!es || es->callbacks.read_callback != dap_tls13_socket_read) return;
    dap_tls13_socket_t *s = es->callbacks.arg;
    if (!s || s->socket != es) return;
    s->continuation_pending = false;
    dap_tls13_socket_drive(s);
}

static ssize_t dap_tls13_socket_send_wire(void *arg, const void *data, size_t size)
{
    dap_tls13_socket_t *s = arg;
    dap_events_socket_t *es = s->socket;
    if (!es || (es->flags & DAP_SOCK_SIGNAL_CLOSE)) return -1;
    /* Bound ciphertext retained by SDK separately from plaintext and BIOs. */
    const size_t limit = 256U * 1024U;
    if (size > limit || es->buf_out_size > limit - size) {
        dap_events_socket_set_writable_unsafe(es, true);
        return 0;
    }
    size_t n = dap_events_socket_write_unsafe(es, data, size);
    return n == size ? (ssize_t)n : -1;
}

static int dap_tls13_socket_wire_drained(void *arg)
{
    dap_tls13_socket_t *s = arg;
    if (!s || !s->socket || (s->socket->flags & DAP_SOCK_SIGNAL_CLOSE)) return -1;
    return s->socket->buf_out_size == 0 ? 1 : 0;
}

static int dap_tls13_socket_drive(dap_tls13_socket_t *s)
{
    if (!s || !s->socket || !s->channel) return -1;
    if (s->socket->flags & DAP_SOCK_SIGNAL_CLOSE) return -1;
    int rc = dap_tls13_channel_drive(s->channel);
    if (rc < 0) {
        s->socket->flags |= DAP_SOCK_SIGNAL_CLOSE;
        return -1;
    }
    if (s->channel->tls.ready && !s->ready_notified) {
        s->ready_notified = true;
        if (s->ready) s->ready(s->ready_arg);
    }
    if (s->channel->tls.peer_closed) {
        /* Retire only after the reply has left both BIO and SDK queues. */
        int shutdown = dap_tls_plain_stream_shutdown(&s->channel->tls,
            &s->channel->outgoing, dap_tls13_socket_wire_drained, s);
        if (shutdown < 0 || shutdown == 1)
            s->socket->flags |= DAP_SOCK_SIGNAL_CLOSE;
    }
    if (s->channel->read_budget_exhausted && !s->continuation_pending &&
        !(s->socket->flags & DAP_SOCK_SIGNAL_CLOSE)) {
        dap_tls13_continuation_t *request = malloc(sizeof(*request));
        if (!request || !s->socket->worker) {
            free(request);
            s->socket->flags |= DAP_SOCK_SIGNAL_CLOSE;
            return -1;
        }
        request->uuid = s->socket->uuid;
        s->continuation_pending = true;
        dap_worker_exec_callback_on(s->socket->worker, dap_tls13_socket_continue, request);
    }
    return rc;
}

static void dap_tls13_socket_read(dap_events_socket_t *es, void *arg)
{
    dap_tls13_socket_t *s = arg;
    if (!s || s->socket != es || !s->channel) return;
    if (es->buf_in_size) {
        int rc = dap_tls_reactor_feed(&s->channel->tls, es->buf_in, es->buf_in_size);
        es->buf_in_size = 0;
        if (rc < 0) { es->flags |= DAP_SOCK_SIGNAL_CLOSE; return; }
    }
    dap_tls13_socket_drive(s);
}

static bool dap_tls13_socket_write(dap_events_socket_t *es, void *arg)
{
    dap_tls13_socket_t *s = arg;
    if (!s || s->socket != es) return false;
    return dap_tls13_socket_drive(s) >= 0;
}

static int dap_tls13_socket_bind(dap_tls13_socket_t *s, dap_events_socket_t *es,
                                 dap_tls13_channel_t *channel)
{
    if (!s || !es || !channel || !channel->tls.session) return -1;
    if (s->socket) return -1;
    s->previous_arg = es->callbacks.arg;
    s->previous_read = es->callbacks.read_callback;
    s->previous_write = es->callbacks.write_callback;
    s->previous_delete = es->callbacks.delete_callback;
    s->previous_write_finished = es->callbacks.write_finished_callback;
    s->previous_error = es->callbacks.error_callback;
    s->socket = es;
    s->channel = channel;
    channel->tls.send = dap_tls13_socket_send_wire;
    channel->tls.send_arg = s;
    es->callbacks.arg = s;
    es->callbacks.read_callback = dap_tls13_socket_read;
    es->callbacks.write_callback = dap_tls13_socket_write;
    es->callbacks.delete_callback = dap_tls13_socket_deleted;
    es->callbacks.write_finished_callback = dap_tls13_socket_write_finished;
    es->callbacks.error_callback = dap_tls13_socket_error;
    return 0;
}

/* Must run before endpoint destruction. Queued continuations resolve UUID
 * and check read_callback, so a detached connection is not dereferenced. */
static void dap_tls13_socket_unbind(dap_tls13_socket_t *s)
{
    if (!s) return;
    dap_events_socket_t *es = s->socket;
    if (es && es->callbacks.arg == s) {
        es->callbacks.arg = s->previous_arg;
        es->callbacks.read_callback = s->previous_read;
        es->callbacks.write_callback = s->previous_write;
        es->callbacks.delete_callback = s->previous_delete;
        es->callbacks.write_finished_callback = s->previous_write_finished;
        es->callbacks.error_callback = s->previous_error;
    }
    memset(s, 0, sizeof(*s));
}
#endif
