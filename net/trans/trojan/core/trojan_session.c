/**
 * @file trojan_session.c
 * @brief Blocking Trojan session over injected io/tls ports. SDK-free.
 *        Establishes TLS, sends the request header, then pumps raw payload.
 */

#include "trojan/trojan.h"

#include <stdlib.h>
#include <string.h>

struct trojan_session {
    trojan_io_ops_t *io;
    const trojan_tls_ops_t *tls;
    void *tls_handle;
    bool closed;
};

trojan_session_t *trojan_session_connect(const trojan_config_t *a_cfg,
                                         trojan_io_ops_t *a_io,
                                         const trojan_tls_ops_t *a_tls)
{
    if (!a_cfg || !a_io || !a_tls || !a_tls->connect || !a_tls->read ||
        !a_tls->write || !a_tls->close)
        return NULL;
    if (!a_cfg->dest_host[0] || !a_cfg->dest_port)
        return NULL;

    int l_fd = a_io->fd ? a_io->fd(a_io->user) : -1;
    if (l_fd < 0)
        return NULL;

    trojan_session_t *l_s = (trojan_session_t *)calloc(1, sizeof(*l_s));
    if (!l_s)
        return NULL;
    l_s->io = a_io;
    l_s->tls = a_tls;

    trojan_tls_cfg_t l_tcfg;
    memset(&l_tcfg, 0, sizeof(l_tcfg));
    l_tcfg.sni = a_cfg->sni;
    l_tcfg.spki_sha256 = a_cfg->have_pin ? a_cfg->spki_sha256 : NULL;
    l_tcfg.have_pin = a_cfg->have_pin;
    l_tcfg.hello_profile = a_cfg->hello_profile;
    l_tcfg.alpn = "http/1.1";

    l_s->tls_handle = a_tls->connect(a_tls->user, l_fd, &l_tcfg);
    if (!l_s->tls_handle) {
        free(l_s);
        return NULL;
    }

    /* Send the Trojan request header (CONNECT to the node listener). */
    uint8_t l_hdr[TROJAN_HASH_HEX + 2 + 1 + 1 + 1 + 255 + 2 + 2];
    int l_hdr_len = trojan_codec_build_request(a_cfg->password_hash_hex,
                                               a_cfg->dest_host, a_cfg->dest_port,
                                               l_hdr, sizeof(l_hdr));
    if (l_hdr_len < 0) {
        a_tls->close(a_tls->user, l_s->tls_handle);
        free(l_s);
        return NULL;
    }
    size_t l_off = 0;
    while (l_off < (size_t)l_hdr_len) {
        ssize_t l_w = a_tls->write(a_tls->user, l_s->tls_handle,
                                   l_hdr + l_off, (size_t)l_hdr_len - l_off);
        if (l_w <= 0) {
            a_tls->close(a_tls->user, l_s->tls_handle);
            free(l_s);
            return NULL;
        }
        l_off += (size_t)l_w;
    }
    return l_s;
}

int trojan_session_read(trojan_session_t *a_s, uint8_t *a_buf, size_t a_len, size_t *a_out)
{
    if (!a_s || !a_buf || !a_out || a_s->closed)
        return TROJAN_ERR_PARAM;
    *a_out = 0;
    ssize_t l_r = a_s->tls->read(a_s->tls->user, a_s->tls_handle, a_buf, a_len);
    if (l_r > 0) { *a_out = (size_t)l_r; return TROJAN_ERR_NONE; }
    if (l_r == 0) { *a_out = 0; return TROJAN_ERR_CLOSED; }
    /* negative: the tls port already maps to TROJAN_ERR_* (AGAIN/IO/CLOSED) */
    return (int)l_r;
}

int trojan_session_write(trojan_session_t *a_s, const uint8_t *a_buf, size_t a_len)
{
    if (!a_s || !a_buf || !a_len || a_s->closed)
        return TROJAN_ERR_PARAM;
    size_t l_off = 0;
    int l_again = 0;
    while (l_off < a_len) {
        ssize_t l_w = a_s->tls->write(a_s->tls->user, a_s->tls_handle,
                                      a_buf + l_off, a_len - l_off);
        if (l_w > 0) { l_off += (size_t)l_w; l_again = 0; continue; }
        if (l_w == TROJAN_ERR_AGAIN) {
            if (++l_again >= 8) return TROJAN_ERR_IO;
            continue;
        }
        return TROJAN_ERR_IO;
    }
    return TROJAN_ERR_NONE;
}

bool trojan_session_has_pending(trojan_session_t *a_s)
{
    if (!a_s || a_s->closed || !a_s->tls->pending)
        return false;
    return a_s->tls->pending(a_s->tls->user, a_s->tls_handle) > 0;
}

void trojan_session_close(trojan_session_t *a_s)
{
    if (!a_s)
        return;
    if (!a_s->closed) {
        a_s->closed = true;
        if (a_s->tls_handle)
            a_s->tls->close(a_s->tls->user, a_s->tls_handle);
        if (a_s->io && a_s->io->close)
            a_s->io->close(a_s->io->user);
    }
    free(a_s);
}