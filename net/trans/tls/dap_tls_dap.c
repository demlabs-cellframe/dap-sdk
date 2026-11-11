/*
 * Shared DAP-over-TLS client builders — see dap_tls_dap.h.
 * Extracted from dap_stream_trans_tls.c so the tls13/wss adapter can live in
 * its own translation unit. Behaviour is identical to the former static helpers.
 */
#define LOG_TAG "dap_tls_dap"

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "dap_tls_dap.h"
#include "dap_config.h"
#include "dap_strfuncs.h"
#include "dap_worker.h"
#include "dap_client.h"
#include "dap_enc_base58.h"

static dap_tls13_trust_provider_t s_trust_provider;
static void *s_trust_arg;
static bool s_debug_more = false;

void dap_tls_dap_set_trust_provider(dap_tls13_trust_provider_t a_provider, void *a_arg)
{
    s_trust_provider = a_provider;
    s_trust_arg = a_arg;
}

dap_tls13_trust_provider_t dap_tls_dap_trust_provider(void)
{
    return s_trust_provider;
}

void *dap_tls_dap_trust_arg(void)
{
    return s_trust_arg;
}

void dap_tls_dap_set_debug_more(bool a_enabled)
{
    s_debug_more = a_enabled;
}

void dap_stream_trans_tls13_set_trust_provider(dap_tls13_trust_provider_t provider, void *arg)
{
    dap_tls_dap_set_trust_provider(provider, arg);
}

uint32_t dap_tls_dap_get_caps(dap_net_trans_t *a_trans)
{
    UNUSED(a_trans);
    return DAP_NET_TRANS_CAP_RELIABLE
         | DAP_NET_TRANS_CAP_ORDERED
         | DAP_NET_TRANS_CAP_OBFUSCATION
         | DAP_NET_TRANS_CAP_BIDIRECTIONAL
         | DAP_NET_TRANS_CAP_HIGH_THROUGHPUT
         | DAP_NET_TRANS_CAP_MIMICRY;
}

int dap_tls_dap_stage_prepare(dap_net_trans_t *a_trans,
                              const dap_net_stage_prepare_params_t *a_params,
                              dap_net_stage_prepare_result_t *a_result)
{
    UNUSED(a_trans);
    if (!a_params || !a_result) return -1;
    memset(a_result, 0, sizeof(*a_result));

    dap_events_socket_callbacks_t *l_cbs = a_params->callbacks;
    // Resolve first and create a socket of the resolved family (IPv6/NAT64-safe)
    bool l_resolve_failed = false;
    dap_events_socket_t *l_es = dap_events_socket_create_resolved(a_params->host, a_params->port,
                                                                   SOCK_STREAM, 0, l_cbs, &l_resolve_failed);
    if (!l_es) {
        log_it(L_ERROR, "Failed to %s for DAP-over-TLS transport",
               l_resolve_failed ? "resolve address" : "create TCP socket");
        a_result->error_code = -1;
        return -1;
    }

    l_es->_inheritor = a_params->client_ctx;

    /* Disable Nagle's algorithm on the client-side TCP socket.
     * VPN data consists of individual IP packets (~1420 bytes + headers);
     * Nagle delays small writes up to 200 ms waiting for coalescing or an ACK,
     * which adds unacceptable latency on the VPN hot path. */
    {
        int l_nodelay = 1;
        if (setsockopt(l_es->socket, IPPROTO_TCP, TCP_NODELAY,
                        (const char *)&l_nodelay, sizeof(l_nodelay)) < 0)
            log_it(L_WARNING, "DAP-over-TLS: failed to set TCP_NODELAY: %s", strerror(errno));
    }

    l_es->flags |= DAP_SOCK_CONNECTING | DAP_SOCK_READY_TO_WRITE | DAP_SOCK_READY_TO_READ;
#ifdef DAP_EVENTS_CAPS_IOCP
    l_es->flags &= ~DAP_SOCK_READY_TO_READ;
#else
    int l_err = 0;
    dap_events_socket_connect(l_es, &l_err);
    if (l_err) {
        log_it(L_ERROR, "DAP-over-TLS: TCP connect error: %d", l_err);
        a_result->error_code = l_err;
        dap_events_socket_delete_unsafe(l_es, true);
        return -1;
    }
#endif

    if (a_params->worker)
        dap_worker_add_events_socket(a_params->worker, l_es);

    dap_stream_t *l_stream = dap_stream_new_es_client(l_es,
                                (dap_stream_node_addr_t *)a_params->node_addr,
                                a_params->authorized);
    if (!l_stream) {
        log_it(L_ERROR, "DAP-over-TLS: failed to create stream");
        a_result->error_code = -1;
        memset(&l_es->callbacks, 0, sizeof(l_es->callbacks));
        if (l_es->worker)
            dap_events_socket_remove_and_delete_unsafe(l_es, true);
        else
            dap_events_socket_delete_unsafe(l_es, true);
        return -1;
    }
    l_stream->trans = a_trans;

    a_result->esocket = l_es;
    a_result->stream = l_stream;
    a_result->error_code = 0;

    log_it(L_INFO, "DAP-over-TLS: TCP connect to %s:%u initiated",
           a_params->host, a_params->port);
    return 0;
}

int dap_tls_dap_send_enc_init(dap_stream_t *a_stream, tls_mimicry_ctx_t *a_ctx,
                              dap_events_socket_t *a_es)
{
    UNUSED(a_es);
    /* Node address: base58-encode the stream's node addr (set at stream
     * creation from a_params->node_addr). Fall back to the legacy anonymous
     * placeholder — same logic as the HTTP transport. */
    char l_node_addr_b58[32] = "gd4y5yh78w42aaagh";
    if (a_stream->node.uint64) {
        uint64_t l_addr_le = a_stream->node.uint64;
        size_t l_b58_len = dap_enc_base58_encode(&l_addr_le, sizeof(l_addr_le), l_node_addr_b58);
        if (!l_b58_len)
            dap_strncpy(l_node_addr_b58, "gd4y5yh78w42aaagh", sizeof(l_node_addr_b58) - 1);
    }

    char l_enc_init_url[512];
    snprintf(l_enc_init_url, sizeof(l_enc_init_url),
             "/%s/%s?enc_type=%d,pkey_exchange_type=%d,pkey_exchange_size=%zu,"
             "block_key_size=%zu,protocol_version=%d,sign_count=%zu",
             DAP_UPLINK_PATH_ENC_INIT, l_node_addr_b58,
             a_ctx->handshake_params.enc_type,
             a_ctx->handshake_params.pkey_exchange_type,
             a_ctx->handshake_params.pkey_exchange_size,
             a_ctx->handshake_params.block_key_size,
             a_ctx->handshake_params.protocol_version,
             a_ctx->handshake_params.sign_count);

    /* Body = base64-encoded alice_pub_key */
    size_t l_body_b64_size = ((a_ctx->handshake_params.alice_pub_key_size + 2) / 3) * 4 + 1;
    char *l_body_b64 = DAP_NEW_Z_SIZE(char, l_body_b64_size);
    if (!l_body_b64)
        return -1;

    static const char s_b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t l_in_len = a_ctx->handshake_params.alice_pub_key_size;
    const uint8_t *l_in = a_ctx->handshake_params.alice_pub_key;
    size_t l_out_pos = 0;
    for (size_t i = 0; i < l_in_len; i += 3) {
        uint32_t n = (uint32_t)l_in[i] << 16;
        if (i + 1 < l_in_len) n |= (uint32_t)l_in[i + 1] << 8;
        if (i + 2 < l_in_len) n |= (uint32_t)l_in[i + 2];
        l_body_b64[l_out_pos++] = s_b64[(n >> 18) & 0x3F];
        l_body_b64[l_out_pos++] = s_b64[(n >> 12) & 0x3F];
        l_body_b64[l_out_pos++] = (i + 1 < l_in_len) ? s_b64[(n >> 6) & 0x3F] : '=';
        l_body_b64[l_out_pos++] = (i + 2 < l_in_len) ? s_b64[n & 0x3F] : '=';
    }
    l_body_b64[l_out_pos] = '\0';

    char l_http_post[8192];
    int l_post_size = snprintf(l_http_post, sizeof(l_http_post),
        "POST %s HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: text/text\r\n"
        "Content-Length: %zu\r\n"
        "Connection: keep-alive\r\n"
        "\r\n"
        "%s",
        l_enc_init_url, l_out_pos, l_body_b64);

    int l_ret = 0;
    if (l_post_size < 0 || (size_t)l_post_size >= sizeof(l_http_post)) {
        log_it(L_ERROR, "TLS enc_init: HTTP request too large (%d bytes)", l_post_size);
        l_ret = -1;
    } else {
        ssize_t l_sent = dap_stream_trans_write_unsafe(a_stream, l_http_post, (size_t)l_post_size);
        if (l_sent > 0)
            log_it(L_NOTICE, "TLS enc_init sent (%zd bytes) through TLS channel", l_sent);
        else {
            log_it(L_ERROR, "TLS enc_init: failed to send through TLS channel");
            l_ret = -1;
        }
    }
    DAP_DELETE(l_body_b64);
    return l_ret;
}

int dap_tls_dap_send_session_request(dap_stream_t *a_stream, dap_net_session_params_t *a_params)
{
    if (!a_stream || !a_params || !a_params->channels) {
        log_it(L_ERROR, "TLS session_create: invalid parameters");
        return -1;
    }

    if (!a_stream->trans_ctx) {
        log_it(L_ERROR, "TLS session_create: stream has no trans_ctx");
        return -2;
    }

    /* Build stream_ctl request body (protocol version) */
    char l_request[16];
    int l_request_size = snprintf(l_request, sizeof(l_request), "%d", DAP_CLIENT_PROTOCOL_VERSION);
    if (l_request_size < 0 || (size_t)l_request_size >= sizeof(l_request)) {
        log_it(L_ERROR, "TLS session_create: protocol version string too large");
        return -3;
    }

    /* Build stream_ctl URL with channel parameters */
    char *l_suburl = dap_strdup_printf("channels=%s,enc_type=%d,enc_key_size=%zu,enc_headers=%d",
                                        a_params->channels, a_params->enc_type,
                                         a_params->enc_key_size, a_params->enc_headers ? 1 : 0);
    if (!l_suburl) return -3;

    char l_stream_ctl_url[1024] = { '\0' };
    int l_url_size = snprintf(l_stream_ctl_url, sizeof(l_stream_ctl_url), "/%s/%s",
              DAP_UPLINK_PATH_STREAM_CTL, l_suburl);
    if (l_url_size < 0 || (size_t)l_url_size >= sizeof(l_stream_ctl_url)) {
        DAP_DELETE(l_suburl);
        return -3;
    }

    debug_if(s_debug_more, L_DEBUG, "TLS session_create: sending stream_ctl via TLS channel: %s",
           l_stream_ctl_url);

    /* Build complete HTTP POST request */
    char l_http_post[2048];
    int l_http_post_size = snprintf(l_http_post, sizeof(l_http_post),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: text/text\r\n"
        "Content-Length: %zu\r\n"
        "Connection: keep-alive\r\n"
        "\r\n"
        "%s",
        l_stream_ctl_url,
        "localhost",  /* Host header (server processes by path) */
        (size_t)l_request_size,
        l_request);

    DAP_DELETE(l_suburl);

    if (l_http_post_size < 0 || (size_t)l_http_post_size >= sizeof(l_http_post)) {
        log_it(L_ERROR, "TLS session_create: HTTP request too large (%d bytes)", l_http_post_size);
        return -3;
    }

    debug_if(s_debug_more, L_DEBUG, "TLS session_create: HTTP POST %d bytes via TLS channel",
           l_http_post_size);

    ssize_t l_sent = dap_stream_trans_write_unsafe(a_stream, l_http_post, (size_t)l_http_post_size);

    if (l_sent != l_http_post_size) {
        log_it(L_ERROR, "TLS session_create: failed to send stream_ctl via TLS");
        return -3;
    }

    return 0;
}
