/* HTTP/TLS owner glue. Listener owns the HTTP client and TLS connection;
 * this adapter borrows both, and must be detached before either is freed.
 * Only ordinary HTTP routes: legacy stream takeover is rejected by reader. */
#ifndef DAP_TLS_HTTP_PEER_H
#define DAP_TLS_HTTP_PEER_H
#include "dap_http_client.h"
#include "dap_tls13_connection.h"
#include "dap_tls_http_output.h"
#include "dap_timerfd.h"

typedef struct dap_tls_http_peer {
    dap_http_client_t *http;
    dap_tls13_connection_t *connection;
    dap_tls_http_output_t output;
    bool shutdown_started;
    bool peer_eof;
    dap_timerfd_t *close_timer; /* Owner-worker only; cancelled by detach. */
    dap_timerfd_t *handshake_timer;
    dap_timerfd_t *lifetime_timer;
    dap_tls_plain_input_fn upgraded_input;
    dap_tls_plain_eof_fn upgraded_eof;
    void *upgraded_arg;
    bool upgrade_pending;
    bool upgraded;
} dap_tls_http_peer_t;

static bool dap_tls_http_peer_handshake_timeout(void *arg)
{
    dap_tls_http_peer_t *peer = arg;
    if (!peer) return false;
    peer->handshake_timer = NULL;
    if (peer->http && peer->http->esocket &&
        !peer->connection->endpoint.channel.tls.ready)
        dap_events_socket_remove_and_delete_unsafe(peer->http->esocket, false);
    return false;
}

static bool dap_tls_http_peer_lifetime_timeout(void *arg)
{
    dap_tls_http_peer_t *peer = arg;
    if (!peer) return false;
    peer->lifetime_timer = NULL;
    if (peer->http && peer->http->esocket)
        dap_events_socket_remove_and_delete_unsafe(peer->http->esocket, false);
    return false;
}

static void dap_tls_http_peer_cancel_timer(dap_timerfd_t **timer)
{
    if (!timer || !*timer) return;
    dap_timerfd_t *owned = *timer;
    *timer = NULL;
    owned->callback_arg = NULL;
    dap_timerfd_delete_unsafe(owned);
}

static bool dap_tls_http_peer_close_timeout(void *arg)
{
    dap_tls_http_peer_t *peer = arg;
    if (!peer) return false;
    peer->close_timer = NULL; /* Timer deletes itself after false. */
    if (peer->http && peer->http->esocket)
        dap_events_socket_remove_and_delete_unsafe(peer->http->esocket, false);
    return false;
}

static void dap_tls_http_peer_resume(void *arg)
{
    dap_tls_http_peer_t *peer = arg;
    if (dap_tls13_socket_schedule(&peer->connection->io))
        peer->http->esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
}

static int dap_tls_http_peer_input(void *arg, const void *data, size_t size)
{
    dap_tls_http_peer_t *peer = arg;
    if (peer->shutdown_started) return -1;
    if (peer->upgraded) return peer->upgraded_input(peer->upgraded_arg, data, size);
    return dap_http_client_feed_plaintext(peer->http, data, size);
}

static int dap_tls_http_peer_eof(void *arg)
{
    dap_tls_http_peer_t *peer = arg;
    peer->peer_eof = true;
    if (peer->upgraded) {
        peer->http->output_close_requested = true;
        return peer->upgraded_eof ? peer->upgraded_eof(peer->upgraded_arg) : 0;
    }
    /* A complete request may be owned by an async route. It is allowed to
     * finish its response, but the connection cannot be reused afterwards. */
    peer->http->keep_alive = false;
    peer->http->out_connection_close = true;
    return 0;
}

/* Advance output only after the route has published a response. One producer
 * turn per TLS event; the owner reschedules only when local progress permits. */
static int dap_tls_http_peer_progress(void *arg)
{
    dap_tls_http_peer_t *peer = arg;
    dap_http_client_t *http = peer->http;
    dap_events_socket_t *socket = http->esocket;
    /* application_progress runs only after the TLS handshake is ready. */
    dap_tls_http_peer_cancel_timer(&peer->handshake_timer);
    if (!peer->upgraded && !peer->shutdown_started && peer->peer_eof && !http->response_pending && !http->input_paused) {
        /* No producer owns this request: EOF with partial input is fatal;
         * idle EOF can proceed directly to TLS shutdown. */
        if (dap_http_client_input_size(http) ||
            http->state_read != DAP_HTTP_CLIENT_STATE_START) return -1;
        http->output_close_requested = true;
    }
    if (peer->shutdown_started) {
        int rc = dap_tls_plain_stream_shutdown(&peer->connection->endpoint.channel.tls,
            &peer->connection->endpoint.channel.outgoing, dap_tls13_socket_wire_drained,
            &peer->connection->io);
        if (rc == 1) socket->flags |= DAP_SOCK_SIGNAL_CLOSE;
        return rc < 0 ? -1 : 0;
    }
    if (http->response_pending) {
        if (!http->response_production_done) {
            if (peer->upgrade_pending && !http->pending_headers) {
                http->response_production_done = true;
            } else {
            bool more = dap_http_client_write_callback(socket, NULL);
            if (socket->flags & DAP_SOCK_SIGNAL_CLOSE) return -1;
            http->response_production_done = !more;
            }
        }
        int drained = dap_http_client_output_drained(http);
        if (drained < 0) return -1;
        if (http->response_production_done && drained) {
            http->response_pending = false;
            if (peer->upgrade_pending) {
                peer->upgrade_pending = false;
                peer->upgraded = true;
                /* The consumer must copy incomplete bytes before returning.
                 * No socket callback or socket owner changes here. */
                size_t tail = dap_http_client_input_size(http);
                if (tail && peer->upgraded_input(peer->upgraded_arg,
                        dap_http_client_input_data(http), tail)) return -1;
                if (dap_http_client_input_consume(http, tail)) return -1;
                return 0;
            }
            if (http->handler_write_finished) {
                http->handler_write_finished(socket, http->handler_arg);
            } else if (!http->keep_alive || http->out_connection_close || peer->peer_eof) {
                dap_http_client_request_close(http);
            } else {
                http->response_production_done = false;
                http->state_read = DAP_HTTP_CLIENT_STATE_START;
                http->out_cache_position = 0;
                dap_http_client_resume_input(http);
            }
            if (socket->flags & DAP_SOCK_SIGNAL_CLOSE) return -1;
        }
    }
    if (http->output_close_requested) {
        if (!socket->worker) return -1;
        peer->close_timer = dap_timerfd_start_on_worker(socket->worker, 5000,
            dap_tls_http_peer_close_timeout, peer);
        if (!peer->close_timer) return -1;
        peer->shutdown_started = true;
        return dap_tls_http_peer_progress(peer);
    }
    if (!peer->upgraded && !http->input_paused && dap_http_client_input_size(http))
        return dap_http_client_feed_plaintext(http, NULL, 0);
    return 0;
}

/* Called by an explicitly layered-safe HTTP route after validating Upgrade.
 * Route must supply 101 response headers and a bodyless write callback. */
static int dap_tls_http_peer_upgrade(dap_tls_http_peer_t *peer,
    dap_tls_plain_input_fn input, dap_tls_plain_eof_fn eof, void *arg)
{
    if (!peer || !peer->http || !input || peer->upgraded || peer->upgrade_pending ||
        peer->shutdown_started || peer->peer_eof || peer->http->reply_status_code != 101 ||
        peer->http->response_pending || peer->http->pending_headers) return -1;
    peer->upgraded_input = input;
    peer->upgraded_eof = eof;
    peer->upgraded_arg = arg;
    peer->upgrade_pending = true;
    peer->http->keep_alive = true;
    peer->http->out_connection_close = false;
    peer->http->out_content_length = 0;
    dap_http_client_pause_input(peer->http);
    dap_http_client_write(peer->http);
    return 0;
}

static int dap_tls_http_peer_attach(dap_tls_http_peer_t *peer,
    dap_http_client_t *http, dap_tls13_connection_t *connection, size_t input_limit)
{
    if (!peer || peer->http || !http || !http->esocket || !http->esocket->worker || !connection ||
        connection->io.socket != http->esocket || http->esocket->_inheritor != http ||
        connection->endpoint.channel.tls.ready) return -1;
    peer->output = (dap_tls_http_output_t){
        .tls = &connection->endpoint.channel.tls,
        .outgoing = &connection->endpoint.channel.outgoing,
        .wire_drained = dap_tls13_socket_wire_drained, .wire_arg = &connection->io};
    if (dap_http_client_enable_plaintext(http, input_limit, dap_tls_http_output_bind(&peer->output)))
        return -1;
    peer->http = http;
    peer->connection = connection;
    peer->handshake_timer = dap_timerfd_start_on_worker(http->esocket->worker, 10000,
        dap_tls_http_peer_handshake_timeout, peer);
    if (!peer->handshake_timer) {
        /* No callbacks have been published yet; roll back the input/output
         * configuration so failure retains no references to this peer. */
        http->output = (dap_http_output_t){0};
        http->use_plaintext_input = false;
        dap_http_input_clear(&http->plaintext_input);
        memset(peer, 0, sizeof(*peer));
        return -1;
    }
    http->input_resume_notify = dap_tls_http_peer_resume;
    http->input_resume_arg = peer;
    connection->endpoint.channel.plain_input = dap_tls_http_peer_input;
    connection->endpoint.channel.plain_eof = dap_tls_http_peer_eof;
    connection->endpoint.channel.plain_arg = peer;
    connection->io.application_progress = dap_tls_http_peer_progress;
    connection->io.application_arg = peer;
    connection->io.application_owns_shutdown = true;
    /* Probe endpoint serves one short response; cap total lifetime including
     * slow headers, stalled output and shutdown. Not a future WSS policy. */
    peer->lifetime_timer = dap_timerfd_start_on_worker(http->esocket->worker, 30000,
        dap_tls_http_peer_lifetime_timeout, peer);
    if (!peer->lifetime_timer) {
        http->esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
        return -1;
    }
    return 0;
}

static void dap_tls_http_peer_detach(dap_tls_http_peer_t *peer)
{
    if (!peer || !peer->http) return;
    dap_tls_http_peer_cancel_timer(&peer->close_timer);
    dap_tls_http_peer_cancel_timer(&peer->handshake_timer);
    dap_tls_http_peer_cancel_timer(&peer->lifetime_timer);
    peer->http->input_resume_notify = NULL;
    peer->http->input_resume_arg = NULL;
    peer->http->output = (dap_http_output_t){0};
    peer->connection->io.application_progress = NULL;
    peer->connection->io.application_arg = NULL;
    peer->connection->io.application_owns_shutdown = false;
    peer->connection->endpoint.channel.plain_input = NULL;
    peer->connection->endpoint.channel.plain_arg = NULL;
    peer->connection->endpoint.channel.plain_eof = NULL;
    memset(peer, 0, sizeof(*peer));
}
/* Accepted-socket setup. The caller must first create a standard HTTP client
 * and install its delete/error callbacks, then retain peer+connection until
 * socket deletion. No network callbacks run synchronously during setup. */
static int dap_tls_http_peer_open(dap_tls_http_peer_t *peer,
    dap_http_client_t *http, dap_tls13_connection_t *connection,
    const char *certificate, const char *key, size_t input_limit)
{
    if (!peer || peer->http || !http || !http->esocket || !connection) return -1;
    if (dap_tls13_connection_server_plain(connection, http->esocket,
            certificate, key, dap_tls_http_peer_input, dap_tls_http_peer_eof, peer))
        return -1;
    if (dap_tls_http_peer_attach(peer, http, connection, input_limit)) {
        dap_tls_http_peer_detach(peer);
        dap_tls13_connection_clear(connection);
        return -1;
    }
    if (dap_tls_openssl_context_http1_server(connection->endpoint.context)) {
        dap_tls_http_peer_detach(peer);
        dap_tls13_connection_clear(connection);
        return -1;
    }
    return 0;
}

/* Must precede HTTP client deletion. The HTTP client itself remains owned by
 * the listener, including any outstanding asynchronous request. */
static void dap_tls_http_peer_close(dap_tls_http_peer_t *peer,
                                    dap_tls13_connection_t *connection)
{
    dap_tls_http_peer_detach(peer);
    dap_tls13_connection_clear(connection);
}
#endif
