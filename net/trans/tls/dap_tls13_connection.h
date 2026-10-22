/* One owner-worker connection shared by client/server transport adapters. */
#ifndef DAP_TLS13_CONNECTION_H
#define DAP_TLS13_CONNECTION_H
#include "dap_tls13_endpoint.h"
#include "dap_tls13_socket.h"

typedef struct dap_tls13_connection {
    dap_tls13_endpoint_t endpoint;
    dap_tls13_socket_t io;
} dap_tls13_connection_t;

static void dap_tls13_connection_clear(dap_tls13_connection_t *c)
{
    if (!c) return;
    dap_tls13_socket_unbind(&c->io);
    dap_tls13_endpoint_clear(&c->endpoint);
}

/* Initialize only on TCP-connected sockets. Failed initialization does not
 * alter the existing SDK callbacks; the transport owner reports the error. */
static int dap_tls13_connection_client(dap_tls13_connection_t *c,
    dap_events_socket_t *es, const char *pin, const char *sni,
    dap_tls13_frame_callback_t dispatch, void *arg,
    void (*ready)(void *), void *ready_arg)
{
    if (!c || !es || c->io.socket) return -1;
    if (dap_tls13_endpoint_client(&c->endpoint, pin, sni,
            dap_tls13_socket_send_wire, &c->io, dispatch, arg)) return -1;
    if (dap_tls13_socket_bind(&c->io, es, &c->endpoint.channel)) {
        dap_tls13_endpoint_clear(&c->endpoint);
        return -1;
    }
    c->io.ready = ready;
    c->io.ready_arg = ready_arg;
    if (dap_tls13_socket_drive(&c->io) < 0) {
        dap_tls13_connection_clear(c);
        return -1;
    }
    return 0;
}

static int dap_tls13_connection_server(dap_tls13_connection_t *c,
    dap_events_socket_t *es, const char *cert, const char *key,
    dap_tls13_frame_callback_t dispatch, void *arg)
{
    if (!c || !es || c->io.socket) return -1;
    if (dap_tls13_endpoint_server(&c->endpoint, cert, key,
            dap_tls13_socket_send_wire, &c->io, dispatch, arg)) return -1;
    if (dap_tls13_socket_bind(&c->io, es, &c->endpoint.channel)) {
        dap_tls13_endpoint_clear(&c->endpoint);
        return -1;
    }
    return 0;
}
#endif
