/* Endpoint-owned TLS configuration. Never shares pins between servers.
 * This layer deliberately has no CDB, GUI or payment dependencies. */
#ifndef DAP_TLS13_ENDPOINT_H
#define DAP_TLS13_ENDPOINT_H
#include "dap_tls13_channel.h"

typedef struct dap_tls13_endpoint {
    dap_tls_context_t *context;
    dap_tls13_channel_t channel;
} dap_tls13_endpoint_t;

static int dap_tls13_pin_decode(const char *hex, uint8_t pin[32])
{
    if (!hex || !pin || strlen(hex) != 64) return -1;
    uint8_t decoded[32] = {0};
    for (size_t i = 0; i < 64; ++i) {
        unsigned char c = (unsigned char)hex[i];
        unsigned value;
        if (c >= '0' && c <= '9') value = c - '0';
        else if (c >= 'a' && c <= 'f') value = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') value = c - 'A' + 10;
        else return -1;
        decoded[i / 2] = (uint8_t)((decoded[i / 2] << 4) | value);
    }
    memcpy(pin, decoded, sizeof(decoded));
    return 0;
}

static void dap_tls13_endpoint_clear(dap_tls13_endpoint_t *e)
{
    if (!e) return;
    dap_tls13_channel_clear(&e->channel);
    dap_tls_context_free(e->context);
    memset(e, 0, sizeof(*e));
}

/* Destination must be zero-initialized. Failure leaves no owned resources.
 * Caller supplies an authenticated, out-of-band pin, not one learned from
 * this connection. SNI is optional; pin authenticates server key identity. */
static int dap_tls13_endpoint_client_protocol(dap_tls13_endpoint_t *e, const char *pin_hex,
                                    const char *sni, dap_tls_wire_send_fn send,
                                    void *send_arg, dap_tls13_frame_callback_t dispatch,
                                    void *dispatch_arg, bool http1)
{
    uint8_t pin[32];
    if (!e || e->context || e->channel.tls.session || !send || !dispatch ||
        dap_tls13_pin_decode(pin_hex, pin)) return -1;
    e->context = dap_tls_context_new_client();
    if (!e->context || dap_tls_context_set_min_version(e->context, 0x0304)) goto fail;
    static const unsigned char alpn[] = {8,'h','t','t','p','/','1','.','1'};
    if (http1 && dap_tls_context_set_alpn(e->context, alpn, sizeof(alpn))) goto fail;
    e->channel.tls.session = dap_tls_session_new(e->context);
    if (!e->channel.tls.session ||
        dap_tls_openssl_session_set_memory_io(e->channel.tls.session) ||
        dap_tls_openssl_session_set_spki_pin(e->channel.tls.session, pin)) goto fail;
    if (sni && *sni && dap_tls_session_set_hostname(e->channel.tls.session, sni)) goto fail;
    e->channel.tls.send = send;
    e->channel.tls.send_arg = send_arg;
    e->channel.dispatch = dispatch;
    e->channel.dispatch_arg = dispatch_arg;
    return 0;
fail:
    dap_tls13_endpoint_clear(e);
    return -1;
}

static int dap_tls13_endpoint_client(dap_tls13_endpoint_t *e, const char *pin_hex,
    const char *sni, dap_tls_wire_send_fn send, void *send_arg,
    dap_tls13_frame_callback_t dispatch, void *dispatch_arg)
{
    return dap_tls13_endpoint_client_protocol(e, pin_hex, sni, send, send_arg,
                                             dispatch, dispatch_arg, false);
}

static int dap_tls13_endpoint_server_init(dap_tls13_endpoint_t *e, const char *cert,
                                     const char *key, dap_tls_wire_send_fn send,
                                     void *send_arg)
{
    if (!e || e->context || e->channel.tls.session || !cert || !key || !send) return -1;
    e->context = dap_tls_context_new_server(cert, key);
    if (!e->context || dap_tls_context_set_min_version(e->context, 0x0304)) goto fail;
    e->channel.tls.session = dap_tls_session_new(e->context);
    if (!e->channel.tls.session || dap_tls_openssl_session_set_memory_io(e->channel.tls.session)) goto fail;
    e->channel.tls.server = true;
    e->channel.tls.send = send;
    e->channel.tls.send_arg = send_arg;
    return 0;
fail:
    dap_tls13_endpoint_clear(e);
    return -1;
}

static int dap_tls13_endpoint_server(dap_tls13_endpoint_t *e, const char *cert,
    const char *key, dap_tls_wire_send_fn send, void *send_arg,
    dap_tls13_frame_callback_t dispatch, void *dispatch_arg)
{
    if (!dispatch || dap_tls13_endpoint_server_init(e, cert, key, send, send_arg)) return -1;
    e->channel.dispatch = dispatch;
    e->channel.dispatch_arg = dispatch_arg;
    return 0;
}

static int dap_tls13_endpoint_server_plain(dap_tls13_endpoint_t *e, const char *cert,
    const char *key, dap_tls_wire_send_fn send, void *send_arg,
    dap_tls_plain_input_fn input, dap_tls_plain_eof_fn eof, void *arg)
{
    if (!input || dap_tls13_endpoint_server_init(e, cert, key, send, send_arg)) return -1;
    e->channel.plain_input = input;
    e->channel.plain_eof = eof;
    e->channel.plain_arg = arg;
    return 0;
}
#endif
