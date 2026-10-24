#include "dap_https_probe.h"
#include "dap_strfuncs.h"
#include "dap_net_server_common.h"
#include "dap_http_server.h"
#include "dap_net_trans_server.h"
#include "dap_tls_http_peer.h"

typedef struct {
    dap_http_server_t http; /* DAP_HTTP_SERVER-compatible listener context. */
    char *certificate, *key;
} https_probe_listener_t;

typedef struct {
    dap_http_client_t http; /* Socket owner; generic socket frees this allocation. */
    dap_tls13_connection_t tls;
    dap_tls_http_peer_t peer;
    size_t sent;
} https_probe_client_t;

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
        http->out_content_length = 9;
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
    ssize_t n = dap_http_client_write_body(http, body + client->sent, 9 - client->sent);
    if (n < 0) return false;
    client->sent += (size_t)n;
    return client->sent != 9;
}

static void s_delete(dap_events_socket_t *es, void *arg)
{
    (void)arg;
    https_probe_client_t *client = es->_inheritor;
    if (!client) return;
    dap_tls_http_peer_close(&client->peer, &client->tls);
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
    dap_events_socket_callbacks_t accept = {.accept_callback=dap_net_server_accept_callback};
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
