#include "dap_https_probe.h"
#define LOG_TAG "dap_https_probe"
#include "dap_strfuncs.h"
#include "dap_net_server_common.h"
#include "dap_http_server.h"
#include "dap_net_trans_server.h"
#include "dap_tls_http_peer.h"
#include "dap_tls_dap_control.h"
#include "dap_wss_server_session.h"
#include "dap_stream_session.h"
#include "dap_stream.h"
#include "../websocket/dap_ws_accept.h"
#include "../websocket/dap_ws_upgrade_request.h"
#include "../websocket/dap_ws_frame_role.h"
#include "../websocket/dap_ws_close.h"
#include "../websocket/include/dap_net_trans_websocket_stream.h"

typedef struct {
    dap_http_server_t http; /* DAP_HTTP_SERVER-compatible listener context. */
    char *certificate, *key;
} https_probe_listener_t;

typedef struct {
    dap_http_client_t http; /* Socket owner; generic socket frees this allocation. */
    dap_tls13_connection_t tls;
    dap_tls_http_peer_t peer;
    size_t sent;
    dap_http_input_t ws_input;
    bool fragmented, ws_closed;
    size_t message_bytes;
    bool vpn_mode;
    dap_wss_server_session_t vpn;
    dap_tls_dap_control_t control;
    dap_stream_t *stream;
    dap_timerfd_t *vpn_idle_timer;
    bool vpn_activity;
} https_probe_client_t;

static bool s_vpn_idle(void *arg)
{
    https_probe_client_t *c = arg;
    if (c->vpn_activity) { c->vpn_activity = false; return true; }
    c->vpn_idle_timer = NULL;
    /* BEGIN TEMP WSS-DIAG — УДАЛИТЬ ПОСЛЕ ДИАГНОСТИКИ */
    log_it(L_WARNING, "[WSS-DIAG] event=idle_timeout uuid=%llu peer=%s",
        (unsigned long long)c->http.esocket->uuid, c->http.esocket->remote_addr_str);
    /* END TEMP WSS-DIAG */
    dap_events_socket_remove_and_delete_unsafe(c->http.esocket, false);
    return false;
}

static int s_vpn_wire(void *arg, const void *data, size_t size)
{
    https_probe_client_t *c = arg;
    dap_tls_plain_write_result_t rc = dap_tls_plain_stream_write(&c->tls.endpoint.channel.tls,
        &c->tls.endpoint.channel.outgoing, data, size);
    /* BEGIN TEMP WSS-DIAG — УДАЛИТЬ ПОСЛЕ ДИАГНОСТИКИ */
    if (rc != DAP_TLS_PLAIN_WRITE_ACCEPTED)
        log_it(L_WARNING, "[WSS-DIAG] event=write_rejected uuid=%llu peer=%s result=%d bytes=%zu plain_queue=%zu sdk_out=%zu",
            (unsigned long long)c->http.esocket->uuid, c->http.esocket->remote_addr_str,
            (int)rc, size, c->tls.endpoint.channel.outgoing.allocated, c->http.esocket->buf_out_size);
    /* END TEMP WSS-DIAG */
    return rc == DAP_TLS_PLAIN_WRITE_ACCEPTED ? 0 : -1;
}
static ssize_t s_vpn_write(dap_stream_t *stream, const void *data, size_t size)
{
    https_probe_client_t *c = stream && stream->esocket ? stream->esocket->_inheritor : NULL;
    if (!c || dap_wss_server_session_write(&c->vpn, data, size)) {
        if (c) c->http.esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
        return -1;
    }
    return (ssize_t)size;
}
static const dap_net_trans_ops_t s_vpn_ops = {.write=s_vpn_write};
/* Separate endpoint capability is still needed before exposing this in GUI. */
static dap_net_trans_t s_vpn_transport = {.type=DAP_NET_TRANS_WSS,
    .ops=&s_vpn_ops, .socket_type=DAP_NET_TRANS_SOCKET_TCP};
static int s_vpn_enc(void *arg, const void *data, size_t size, void **out, size_t *length)
{
    https_probe_client_t *c = arg;
    return dap_tls_dap_control_request(&c->control, true, data, size, out, length);
}
static int s_vpn_ctl(void *arg, const void *data, size_t size, void **out, size_t *length)
{
    https_probe_client_t *c = arg;
    int rc = dap_tls_dap_control_request(&c->control, false, data, size, out, length);
    if (rc) return rc;
    unsigned id = 0;
    char *text = calloc(1, *length + 1);
    if (!text) return -1;
    memcpy(text, *out, *length);
    int parsed = sscanf(text, "%u", &id);
    free(text);
    dap_stream_session_t *session = parsed == 1 && id ? dap_stream_session_id_mt(id) : NULL;
    if (!session || c->stream) return -1;
    c->stream = dap_stream_new_es_server(c->http.esocket, session);
    if (!c->stream) return -1;
    c->stream->trans = &s_vpn_transport;
    /* The connection owner owns teardown. The generic direct keepalive can
     * delete the stream behind our back on socket error; replace its lifecycle
     * with an inbound-activity deadline (client DAP keepalives count). */
    if (c->stream->keepalive_timer) {
        dap_timerfd_delete_unsafe(c->stream->keepalive_timer);
        c->stream->keepalive_timer = NULL;
    }
    c->vpn_activity = true;
    c->vpn_idle_timer = dap_timerfd_start_on_worker(c->http.esocket->worker, 60000, s_vpn_idle, c);
    if (!c->vpn_idle_timer) return -1;
    dap_tls_http_peer_cancel_timer(&c->peer.lifetime_timer);
    return 0;
}
static int s_vpn_data(void *arg, const void *data, size_t size)
{
    https_probe_client_t *c = arg;
    if (!c->stream || dap_stream_data_proc_read_ext(c->stream, data, size) != size) return -1;
    c->vpn_activity = true;
    return 0;
}
static int s_vpn_input(void *arg, const void *data, size_t size)
{
    https_probe_client_t *c = arg;
    int rc = dap_wss_dap_input(&c->vpn.wire, data, size);
    /* BEGIN TEMP WSS-DIAG — УДАЛИТЬ ПОСЛЕ ДИАГНОСТИКИ */
    if (rc) log_it(L_WARNING, "[WSS-DIAG] event=input_failed uuid=%llu phase=%d bytes=%zu ws_buffer=%zu",
        (unsigned long long)c->http.esocket->uuid, (int)c->vpn.dispatch.phase,
        size, c->vpn.wire.incoming.size);
    /* END TEMP WSS-DIAG */
    if (!rc && c->vpn.wire.closed) dap_http_client_request_close(&c->http);
    return rc;
}
static int s_vpn_eof(void *arg)
{
    https_probe_client_t *c = arg;
    return c->vpn.wire.incoming.size || c->vpn.wire.dap.header_size || c->vpn.wire.dap.payload ? -1 : 0;
}

static int s_ws_send(https_probe_client_t *client, dap_ws_opcode_t opcode,
                     bool fin, const void *data, size_t size)
{
    uint8_t *wire = malloc(size + 14);
    if (!wire) return -1;
    size_t length = 0;
    int rc = dap_net_trans_websocket_build_frame(wire, size + 14, opcode, fin, false,
                                                data, size, &length);
    if (!rc) rc = dap_tls_plain_stream_write(&client->tls.endpoint.channel.tls,
        &client->tls.endpoint.channel.outgoing, wire, length) == DAP_TLS_PLAIN_WRITE_ACCEPTED ? 0 : -1;
    free(wire);
    return rc;
}

static int s_ws_input(void *arg, const void *data, size_t size)
{
    https_probe_client_t *client = arg;
    if (client->ws_closed || dap_http_input_append(&client->ws_input, data, size)) return -1;
    while (client->ws_input.size) {
        const uint8_t *bytes = client->ws_input.data;
        size_t available = client->ws_input.size, length = 0, consumed = 0;
        uint8_t *payload = NULL;
        dap_ws_opcode_t opcode;
        bool fin;
        int rc = dap_ws_frame_role_check(bytes, available, true);
        if (!rc) rc = dap_ws_frame_budget_check(bytes, available, 65536);
        if (!rc) rc = dap_net_trans_websocket_parse_frame(bytes, available,
            &opcode, &fin, &payload, &length, &consumed);
        if (rc == -2) return 0;
        if (rc) return -1;
        if (dap_ws_fragment_accept(&client->fragmented, opcode, fin) ||
            dap_ws_message_budget(&client->message_bytes, opcode, fin, length)) rc = -1;
        else if (opcode == DAP_WS_OPCODE_CLOSE) {
            if (!dap_ws_close_valid(payload, length)) rc = -1;
            else {
                rc = s_ws_send(client, opcode, true, payload, length);
                client->ws_closed = true;
                dap_http_client_request_close(&client->http);
            }
        } else if (opcode == DAP_WS_OPCODE_PING)
            rc = s_ws_send(client, DAP_WS_OPCODE_PONG, true, payload, length);
        else if (opcode != DAP_WS_OPCODE_PONG)
            rc = s_ws_send(client, opcode, fin, payload, length);
        free(payload);
        if (rc) return -1;
        dap_http_input_consume(&client->ws_input, consumed);
        if (client->ws_closed) return 0;
    }
    return 0;
}

static int s_ws_eof(void *arg)
{
    https_probe_client_t *client = arg;
    return client->ws_input.size || client->fragmented ? -1 : 0;
}

static bool s_upgrade_headers(dap_http_client_t *http, void *arg)
{
    (void)arg;
    return http->reply_status_code == 101;
}

static void s_upgrade(dap_http_client_t *http, void *arg)
{
    (void)arg;
    https_probe_client_t *client = (https_probe_client_t *)http;
    dap_http_header_t *key = dap_http_header_find(http->in_headers, "Sec-WebSocket-Key");
    dap_http_header_t *version = dap_http_header_find(http->in_headers, "Sec-WebSocket-Version");
    dap_http_header_t *upgrade = dap_http_header_find(http->in_headers, "Upgrade");
    dap_http_header_t *connection = dap_http_header_find(http->in_headers, "Connection");
    char accept[29];
    if (!dap_ws_request_headers_valid(http->in_headers, http->http_version_11, http->in_content_length) ||
        !key || !version || !upgrade || !connection || strcmp(version->value, "13") ||
        !dap_ws_request_fields_valid(http->action, upgrade->value, connection->value, key->value) ||
        dap_ws_accept_key(key->value, accept, sizeof(accept))) {
        http->reply_status_code = 400;
        http->state_read = DAP_HTTP_CLIENT_STATE_NONE;
        http->keep_alive = false;
        http->out_connection_close = true;
        dap_http_client_pause_input(http);
        dap_http_client_write(http);
        return;
    }
    if (dap_http_input_init(&client->ws_input, 131072) ||
        !dap_http_header_add(&http->out_headers, "Upgrade", "websocket") ||
        !dap_http_header_add(&http->out_headers, "Connection", "Upgrade") ||
        !dap_http_header_add(&http->out_headers, "Sec-WebSocket-Accept", accept)) {
        http->esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
        return;
    }
    http->reply_status_code = 101;
    client->vpn_mode = !strcmp(http->proc->url, "/vpn");
    if (client->vpn_mode && dap_wss_server_session_init(&client->vpn,
            s_vpn_enc, s_vpn_ctl, s_vpn_data, client, s_vpn_wire, client)) {
        http->esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
        return;
    }
    if (dap_tls_http_peer_upgrade(&client->peer,
            client->vpn_mode ? s_vpn_input : s_ws_input,
            client->vpn_mode ? s_vpn_eof : s_ws_eof, client))
        http->esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
}

static void s_final(dap_server_t *server, void *arg)
{
    (void)arg;
    https_probe_listener_t *listener = server->_inheritor;
    if (!listener) return;
    dap_http_url_proc_t *route, *tmp;
    HASH_ITER(hh, listener->http.url_proc, route, tmp) {
        HASH_DEL(listener->http.url_proc, route);
        pthread_rwlock_destroy(&route->cache_rwlock);
        free(route);
    }
    free(listener->certificate);
    free(listener->key);
    free(listener);
    server->_inheritor = NULL;
}

void dap_https_probe_stop(dap_server_t *server)
{
    dap_server_delete(server);
}

static void s_headers(dap_http_client_t *http, void *arg)
{
    (void)arg;
    http->keep_alive = false;
    http->out_connection_close = true;
    if (strcmp(http->action, "GET") || http->in_content_length) {
        http->reply_status_code = 400;
        http->state_read = DAP_HTTP_CLIENT_STATE_NONE;
    } else {
        http->reply_status_code = 200;
        http->out_content_length = !strcmp(http->proc->url, "/bulk") ? 1024U * 1024U : 9;
        strcpy(http->out_content_type, "text/plain");
        http->state_read = DAP_HTTP_CLIENT_STATE_DATA;
    }
    dap_http_client_pause_input(http);
    dap_http_client_write(http);
}

static bool s_body(dap_http_client_t *http, void *arg)
{
    (void)arg;
    https_probe_client_t *client = (https_probe_client_t *)http;
    static const char body[] = "https-ok\n";
    unsigned char block[16384];
    size_t remaining = http->out_content_length - client->sent;
    size_t count = remaining < sizeof(block) ? remaining : sizeof(block);
    const void *data = body + (client->sent < sizeof(body) ? client->sent : 0);
    if (http->out_content_length != 9) {
        for (size_t i = 0; i < count; ++i) block[i] = (unsigned char)((client->sent + i) % 251);
        data = block;
    }
    ssize_t n = dap_http_client_write_body(http, data, count);
    if (n < 0) return false;
    client->sent += (size_t)n;
    return client->sent != http->out_content_length;
}

static void s_delete(dap_events_socket_t *es, void *arg)
{
    (void)arg;
    https_probe_client_t *client = es->_inheritor;
    if (!client) return;
    /* BEGIN TEMP WSS-DIAG — УДАЛИТЬ ПОСЛЕ ДИАГНОСТИКИ */
    if (client->vpn_mode)
        log_it(L_NOTICE, "[WSS-DIAG] event=delete uuid=%llu peer=%s phase=%d tls_failed=%d peer_eof=%d plain_queue=%zu sdk_out=%zu",
            (unsigned long long)es->uuid, es->remote_addr_str, (int)client->vpn.dispatch.phase,
            client->tls.endpoint.channel.tls.failed, client->peer.peer_eof,
            client->tls.endpoint.channel.outgoing.allocated, es->buf_out_size);
    /* END TEMP WSS-DIAG */
    if (client->vpn_idle_timer) {
        dap_timerfd_delete_unsafe(client->vpn_idle_timer);
        client->vpn_idle_timer = NULL;
    }
    dap_tls_http_peer_close(&client->peer, &client->tls);
    if (client->stream) {
        client->stream->esocket = NULL;
        client->stream->esocket_worker = NULL;
        client->stream->esocket_uuid = 0;
        dap_stream_delete_unsafe(client->stream);
    }
    dap_wss_server_session_clear(&client->vpn);
    dap_http_input_clear(&client->ws_input);
    dap_http_client_delete(es, NULL);
}

static void s_error(dap_events_socket_t *es, int error)
{
    (void)error;
    es->flags |= DAP_SOCK_SIGNAL_CLOSE;
}

static void s_new(dap_events_socket_t *es, void *arg)
{
    (void)arg;
    https_probe_listener_t *listener = es->server ? es->server->_inheritor : NULL;
    https_probe_client_t *client = calloc(1, sizeof(*client));
    if (!listener || !client) {
        free(client);
        es->flags |= DAP_SOCK_SIGNAL_CLOSE;
        return;
    }
    es->_inheritor = client;
    client->http.esocket = es;
    client->http.http = &listener->http;
    client->http.socket_num = es->socket;
    client->http.state_read = DAP_HTTP_CLIENT_STATE_START;
    if (dap_tls_http_peer_open(&client->peer, &client->http, &client->tls,
            listener->certificate, listener->key, 65536))
        es->flags |= DAP_SOCK_SIGNAL_CLOSE;
}

dap_server_t *dap_https_probe_start(const char *address, uint16_t port,
                                   const char *certificate, const char *private_key)
{
    if (!address || !port || !certificate || !private_key) return NULL;
    dap_tls_context_t *check = dap_tls_context_new_server(certificate, private_key);
    if (!check) return NULL;
    dap_tls_context_free(check);
    https_probe_listener_t *listener = calloc(1, sizeof(*listener));
    if (!listener) return NULL;
    listener->certificate = dap_strdup(certificate);
    listener->key = dap_strdup(private_key);
    if (!listener->certificate || !listener->key) goto fail;
    dap_events_socket_callbacks_t callbacks = {.new_callback=s_new,
        .delete_callback=s_delete, .error_callback=s_error};
    dap_server_t *server = dap_server_new(NULL, NULL, &callbacks);
    if (!server) goto fail;
    server->_inheritor = listener;
    server->final_callback = s_final;
    listener->http.server = server;
    strcpy(listener->http.server_name, "HTTPS");
    dap_http_url_proc_t *route = dap_http_add_proc(&listener->http, "/", NULL,
        NULL, NULL, s_headers, NULL, NULL, s_body, NULL);
    if (!route) { dap_server_delete(server); return NULL; }
    route->layered_io_safe = true;
    route = dap_http_add_proc(&listener->http, "/bulk", NULL,
        NULL, NULL, s_headers, NULL, NULL, s_body, NULL);
    if (!route) { dap_server_delete(server); return NULL; }
    route->layered_io_safe = true;
    dap_events_socket_callbacks_t accept = {.accept_callback=dap_net_server_accept_callback};
    route = dap_http_add_proc(&listener->http, "/ws", NULL,
        NULL, NULL, s_upgrade, s_upgrade_headers, NULL, NULL, NULL);
    if (!route) { dap_server_delete(server); return NULL; }
    route->layered_io_safe = route->layered_upgrade_safe = true;
    route = dap_http_add_proc(&listener->http, "/vpn", NULL,
        NULL, NULL, s_upgrade, s_upgrade_headers, NULL, NULL, NULL);
    if (!route) { dap_server_delete(server); return NULL; }
    route->layered_io_safe = route->layered_upgrade_safe = true;
    if (dap_server_listen_addr_add(server, address, port, DESCRIPTOR_TYPE_SOCKET_LISTENING, &accept)) {
        dap_server_delete(server);
        return NULL;
    }
    return server;
fail:
    free(listener->certificate);
    free(listener->key);
    free(listener);
    return NULL;
}
