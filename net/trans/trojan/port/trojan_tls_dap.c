/**
 * @file trojan_tls_dap.c
 * @brief Trojan TLS port backed by the product dap_tls abstraction (reuses
 *        browser hello profiles, SPKI pin, cross-platform backend). This is
 *        the SDK-aware port; the core stays SDK-free behind trojan_tls_ops.
 */

#include "trojan/trojan.h"

#include "dap_tls.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    dap_tls_context_t *ctx;
    dap_tls_session_t *session;
} trojan_tls_handle_t;

static int s_map_err(dap_tls_session_t *a_s, int a_ret)
{
    dap_tls_error_t l_e = dap_tls_session_get_error(a_s, a_ret);
    switch (l_e) {
        case DAP_TLS_ERR_WANT_READ:
        case DAP_TLS_ERR_WANT_WRITE: return TROJAN_ERR_AGAIN;
        case DAP_TLS_ERR_CLOSED:     return TROJAN_ERR_CLOSED;
        default:                     return TROJAN_ERR_IO;
    }
}

static void *s_connect(void *a_user, int a_fd, const trojan_tls_cfg_t *a_cfg)
{
    (void)a_user;
    if (!a_cfg) return NULL;

    trojan_tls_handle_t *l_h = (trojan_tls_handle_t *)calloc(1, sizeof(*l_h));
    if (!l_h) return NULL;

    l_h->ctx = dap_tls_context_new_client();
    if (!l_h->ctx) { free(l_h); return NULL; }

    if (a_cfg->hello_profile > 0)
        dap_tls_context_set_hello_profile(l_h->ctx, (dap_tls_hello_profile_t)a_cfg->hello_profile);

    /* ALPN wire format: length-prefixed protocol list. */
    if (a_cfg->alpn && a_cfg->alpn[0]) {
        size_t l_len = strlen(a_cfg->alpn);
        if (l_len <= 255) {
            uint8_t l_wire[256];
            l_wire[0] = (uint8_t)l_len;
            memcpy(l_wire + 1, a_cfg->alpn, l_len);
            dap_tls_context_set_alpn(l_h->ctx, l_wire, (uint32_t)(l_len + 1));
        }
    }

    l_h->session = dap_tls_session_new(l_h->ctx);
    if (!l_h->session) { dap_tls_context_free(l_h->ctx); free(l_h); return NULL; }

    dap_tls_session_set_fd(l_h->session, a_fd);
    if (a_cfg->sni && a_cfg->sni[0])
        dap_tls_session_set_hostname(l_h->session, a_cfg->sni);
    if (a_cfg->have_pin && a_cfg->spki_sha256)
        dap_tls_openssl_session_set_spki_pin(l_h->session, a_cfg->spki_sha256);

    /* dap_tls_session_connect returns 1 on success, 0 on want-read/write,
     * -1 on failure. The fd session is blocking, so success is 1. */
    if (dap_tls_session_connect(l_h->session) != 1) {
        dap_tls_session_free(l_h->session);
        dap_tls_context_free(l_h->ctx);
        free(l_h);
        return NULL;
    }
    return l_h;
}

static ssize_t s_read(void *a_user, void *a_tls, void *a_buf, size_t a_len)
{
    (void)a_user;
    trojan_tls_handle_t *l_h = a_tls;
    ssize_t l_r = dap_tls_session_read(l_h->session, a_buf, a_len);
    if (l_r >= 0) return l_r;
    return s_map_err(l_h->session, (int)l_r);
}

static ssize_t s_write(void *a_user, void *a_tls, const void *a_buf, size_t a_len)
{
    (void)a_user;
    trojan_tls_handle_t *l_h = a_tls;
    ssize_t l_w = dap_tls_session_write(l_h->session, a_buf, a_len);
    if (l_w >= 0) return l_w;
    return s_map_err(l_h->session, (int)l_w);
}

static int s_pending(void *a_user, void *a_tls)
{
    (void)a_user;
    trojan_tls_handle_t *l_h = a_tls;
    return dap_tls_session_pending(l_h->session);
}

static void s_close(void *a_user, void *a_tls)
{
    (void)a_user;
    trojan_tls_handle_t *l_h = a_tls;
    if (!l_h) return;
    if (l_h->session) {
        dap_tls_session_shutdown(l_h->session);
        dap_tls_session_free(l_h->session);
    }
    if (l_h->ctx)
        dap_tls_context_free(l_h->ctx);
    free(l_h);
}

static const trojan_tls_ops_t s_trojan_tls_dap_ops = {
    .user = NULL,
    .connect = s_connect,
    .read = s_read,
    .write = s_write,
    .pending = s_pending,
    .close = s_close,
};

const trojan_tls_ops_t *trojan_tls_dap(void)
{
    return &s_trojan_tls_dap_ops;
}