/**
 * @file xhttp_session.c
 * @brief XHTTP (Xray splithttp) client session over nghttp2 + dap_tls.
 *        packet-up mode: GET download stream + POST upload packets keyed by a
 *        session id. Carries a single VLESS stream for the DAP carrier.
 *
 *        The VLESS request header (dest) is the first upload packet; the VLESS
 *        response header is stripped from the download stream.
 */

#include "xhttp/xhttp.h"

#include <nghttp2/nghttp2.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct xhttp_session {
    xhttp_io_ops_t *io;
    const xhttp_tls_ops_t *tls;
    void *tls_handle;
    nghttp2_session *h2;

    char session_id[40];
    char base_path[XHTTP_PATH_MAX + 2];
    char host[XHTTP_HOST_MAX];

    int32_t down_id;
    bool down_headers;
    int down_status;
    bool down_closed;
    uint8_t *downbuf; size_t downbuf_len, downbuf_off;
    bool vless_resp_done;

    int32_t up_id;
    bool up_active;
    bool up_done;
    const uint8_t *up_data; size_t up_len, up_off;
    uint64_t seq;

    bool closed;
};

/* ---- TLS write helper ---- */
static int s_tls_write_all(xhttp_session_t *s, const uint8_t *a_data, size_t a_len)
{
    size_t l_off = 0; int l_again = 0;
    while (l_off < a_len) {
        ssize_t l_w = s->tls->write(s->tls->user, s->tls_handle, a_data + l_off, a_len - l_off);
        if (l_w > 0) { l_off += (size_t)l_w; l_again = 0; continue; }
        if (l_w == XHTTP_ERR_AGAIN) { if (++l_again >= 16) return XHTTP_ERR_IO; continue; }
        return XHTTP_ERR_IO;
    }
    return XHTTP_ERR_NONE;
}

/* ---- nghttp2 callbacks ---- */
static int s_on_begin_headers(nghttp2_session *h2, const nghttp2_frame *frame, void *user_data)
{
    (void)h2; (void)frame; (void)user_data; return 0;
}

static int s_on_header(nghttp2_session *h2, const nghttp2_frame *frame,
                       const uint8_t *name, size_t namelen,
                       const uint8_t *value, size_t valuelen, uint8_t flags, void *user_data)
{
    (void)h2; (void)flags;
    xhttp_session_t *s = user_data;
    if (frame->hd.stream_id == s->down_id && namelen == 7 && memcmp(name, ":status", 7) == 0) {
        char l_buf[8]; size_t l_n = valuelen < 7 ? valuelen : 7;
        memcpy(l_buf, value, l_n); l_buf[l_n] = '\0';
        s->down_status = atoi(l_buf);
    }
    return 0;
}

static int s_on_frame_recv(nghttp2_session *h2, const nghttp2_frame *frame, void *user_data)
{
    (void)h2;
    xhttp_session_t *s = user_data;
    int32_t l_sid = frame->hd.stream_id;
    if (l_sid == s->down_id) {
        if (frame->hd.type == NGHTTP2_HEADERS && frame->hd.flags & NGHTTP2_FLAG_END_HEADERS)
            s->down_headers = true;
        if (frame->hd.type == NGHTTP2_DATA && frame->hd.flags & NGHTTP2_FLAG_END_STREAM)
            s->down_closed = true;
    }
    if (l_sid == s->up_id && s->up_active) {
        if (frame->hd.type == NGHTTP2_HEADERS && frame->headers.cat == NGHTTP2_HCAT_RESPONSE)
            s->up_done = true;
    }
    return 0;
}

static int s_on_data_chunk(nghttp2_session *h2, uint8_t flags, int32_t stream_id,
                           const uint8_t *data, size_t len, void *user_data)
{
    (void)h2; (void)flags;
    xhttp_session_t *s = user_data;
    if (stream_id != s->down_id || !len) return 0;
    if (s->downbuf_off) {
        memmove(s->downbuf, s->downbuf + s->downbuf_off, s->downbuf_len - s->downbuf_off);
        s->downbuf_len -= s->downbuf_off; s->downbuf_off = 0;
    }
    uint8_t *l_nb = realloc(s->downbuf, s->downbuf_len + len);
    if (!l_nb) return NGHTTP2_ERR_CALLBACK_FAILURE;
    memcpy(l_nb + s->downbuf_len, data, len);
    s->downbuf = l_nb; s->downbuf_len += len;
    return 0;
}

static int s_on_stream_close(nghttp2_session *h2, int32_t stream_id, uint32_t error_code, void *user_data)
{
    (void)h2; (void)error_code;
    xhttp_session_t *s = user_data;
    if (stream_id == s->down_id) s->down_closed = true;
    if (stream_id == s->up_id) { s->up_done = true; s->up_active = false; }
    return 0;
}

static ssize_t s_upload_read(nghttp2_session *h2, int32_t stream_id, uint8_t *buf, size_t length,
                             uint32_t *data_flags, nghttp2_data_source *source, void *user_data)
{
    (void)h2; (void)stream_id; (void)source;
    xhttp_session_t *s = user_data;
    size_t l_avail = s->up_len - s->up_off;
    size_t l_n = l_avail < length ? l_avail : length;
    if (l_n) memcpy(buf, s->up_data + s->up_off, l_n);
    s->up_off += l_n;
    if (s->up_off >= s->up_len) *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    return (ssize_t)l_n;
}

/* ---- IO pump: flush nghttp2 output, feed TLS input ---- */
static int s_pump(xhttp_session_t *s)
{
    for (;;) {
        const uint8_t *l_data = NULL;
        ssize_t l_n = nghttp2_session_mem_send(s->h2, &l_data);
        if (l_n < 0) return XHTTP_ERR_PROTO;
        if (l_n == 0) break;
        if (s_tls_write_all(s, l_data, (size_t)l_n) != XHTTP_ERR_NONE) return XHTTP_ERR_IO;
    }
    uint8_t l_buf[16384];
    ssize_t l_r = s->tls->read(s->tls->user, s->tls_handle, l_buf, sizeof(l_buf));
    if (l_r > 0) {
        ssize_t l_fed = nghttp2_session_mem_recv(s->h2, l_buf, (size_t)l_r);
        if (l_fed < 0) return XHTTP_ERR_PROTO;
    } else if (l_r == XHTTP_ERR_CLOSED || l_r == 0) {
        s->down_closed = true;
    } else if (l_r != XHTTP_ERR_AGAIN) {
        return XHTTP_ERR_IO;
    }
    return XHTTP_ERR_NONE;
}

/* ---- request helpers ---- */
static void s_gen_session_id(char *a_out)
{
    static const char l_hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) a_out[i] = l_hex[rand() % 16];
    a_out[32] = '\0';
}

#define NV(k, v) { (const uint8_t *)(k), (const uint8_t *)(v), sizeof(k) - 1, sizeof(v) - 1 }

/* Referer with x_padding query (default placement). Padding length must be in
 * the server's XPaddingBytes range (default 100-1000); use 'X' (repeat-x). */
static void s_build_referer(xhttp_session_t *s, char *a_out, size_t a_out_size)
{
    int l_pad_len = 100 + (rand() % 901);
    char l_pad[1001];
    memset(l_pad, 'X', (size_t)l_pad_len);
    l_pad[l_pad_len] = '\0';
    snprintf(a_out, a_out_size, "https://%s%s?x_padding=%s", s->host, s->base_path, l_pad);
}

static int s_submit_download(xhttp_session_t *s)
{
    char l_path[XHTTP_PATH_MAX + 64];
    snprintf(l_path, sizeof(l_path), "%s%s", s->base_path, s->session_id);
    char l_referer[XHTTP_PATH_MAX + 1100];
    s_build_referer(s, l_referer, sizeof(l_referer));
    nghttp2_nv l_nva[] = {
        NV(":method", "GET"), NV(":scheme", "https"),
        { (const uint8_t *)":authority", (const uint8_t *)s->host, 10, strlen(s->host) },
        { (const uint8_t *)":path", (const uint8_t *)l_path, 5, strlen(l_path) },
        NV("user-agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"),
        NV("accept", "*/*"),
        NV("accept-language", "en-US,en;q=0.9"),
        { (const uint8_t *)"referer", (const uint8_t *)l_referer, 7, strlen(l_referer) },
        NV("sec-fetch-mode", "cors"), NV("sec-fetch-site", "same-origin"),
    };
    s->down_id = nghttp2_submit_request(s->h2, NULL, l_nva, sizeof(l_nva) / sizeof(l_nva[0]), NULL, s);
    return s->down_id < 0 ? XHTTP_ERR_PROTO : XHTTP_ERR_NONE;
}

static int s_submit_upload(xhttp_session_t *s, const uint8_t *a_data, size_t a_len)
{
    char l_path[XHTTP_PATH_MAX + 96];
    snprintf(l_path, sizeof(l_path), "%s%s/%llu", s->base_path, s->session_id,
             (unsigned long long)s->seq);
    s->seq++;
    char l_clen[32]; snprintf(l_clen, sizeof(l_clen), "%zu", a_len);
    char l_referer[XHTTP_PATH_MAX + 1100];
    s_build_referer(s, l_referer, sizeof(l_referer));
    nghttp2_nv l_nva[] = {
        NV(":method", "POST"), NV(":scheme", "https"),
        { (const uint8_t *)":authority", (const uint8_t *)s->host, 10, strlen(s->host) },
        { (const uint8_t *)":path", (const uint8_t *)l_path, 5, strlen(l_path) },
        NV("user-agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"),
        NV("accept", "*/*"),
        { (const uint8_t *)"content-length", (const uint8_t *)l_clen, 14, strlen(l_clen) },
        { (const uint8_t *)"referer", (const uint8_t *)l_referer, 7, strlen(l_referer) },
        NV("sec-fetch-mode", "cors"), NV("sec-fetch-site", "same-origin"),
    };
    size_t l_nvlen = sizeof(l_nva) / sizeof(l_nva[0]);
    s->up_data = a_data; s->up_len = a_len; s->up_off = 0;
    s->up_done = false; s->up_active = true;
    nghttp2_data_provider l_dp;
    memset(&l_dp, 0, sizeof(l_dp));
    l_dp.source.ptr = s;
    l_dp.read_callback = s_upload_read;
    s->up_id = nghttp2_submit_request(s->h2, NULL, l_nva, l_nvlen, &l_dp, s);
    if (s->up_id < 0) { s->up_active = false; return XHTTP_ERR_PROTO; }
    /* pump until this upload's response is received */
    for (int i = 0; i < 400 && !s->up_done && !s->closed; ++i) {
        int l_rc = s_pump(s);
        if (l_rc != XHTTP_ERR_NONE && l_rc != XHTTP_ERR_AGAIN) return l_rc;
    }
    s->up_active = false;
    return s->up_done ? XHTTP_ERR_NONE : XHTTP_ERR_IO;
}

/* ---- public API ---- */
xhttp_session_t *xhttp_session_connect(const xhttp_config_t *a_cfg,
                                       xhttp_io_ops_t *a_io, const xhttp_tls_ops_t *a_tls)
{
    if (!a_cfg || !a_io || !a_tls || !a_tls->connect || !a_tls->read ||
        !a_tls->write || !a_tls->close || !a_cfg->dest_host[0] || !a_cfg->dest_port)
        return NULL;
    int l_fd = a_io->fd ? a_io->fd(a_io->user) : -1;
    if (l_fd < 0) return NULL;

    xhttp_session_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->io = a_io; s->tls = a_tls; s->down_id = -1; s->up_id = -1;
    s_gen_session_id(s->session_id);
    snprintf(s->host, sizeof(s->host), "%s", a_cfg->host[0] ? a_cfg->host : a_cfg->sni);
    snprintf(s->base_path, sizeof(s->base_path), "%s", a_cfg->path[0] ? a_cfg->path : "/");
    size_t l_pl = strlen(s->base_path);
    if (l_pl && s->base_path[l_pl - 1] != '/' && l_pl + 1 < sizeof(s->base_path)) {
        s->base_path[l_pl] = '/'; s->base_path[l_pl + 1] = '\0';
    }

    xhttp_tls_cfg_t l_tcfg; memset(&l_tcfg, 0, sizeof(l_tcfg));
    l_tcfg.sni = a_cfg->sni;
    l_tcfg.spki_sha256 = a_cfg->have_pin ? a_cfg->spki_sha256 : NULL;
    l_tcfg.have_pin = a_cfg->have_pin;
    l_tcfg.hello_profile = a_cfg->hello_profile;
    l_tcfg.alpn = "h2";
    s->tls_handle = a_tls->connect(a_tls->user, l_fd, &l_tcfg);
    if (!s->tls_handle) { free(s); return NULL; }

    nghttp2_session_callbacks *l_cbs = NULL;
    if (nghttp2_session_callbacks_new(&l_cbs) != 0) { a_tls->close(a_tls->user, s->tls_handle); free(s); return NULL; }
    nghttp2_session_callbacks_set_on_begin_headers_callback(l_cbs, s_on_begin_headers);
    nghttp2_session_callbacks_set_on_header_callback(l_cbs, s_on_header);
    nghttp2_session_callbacks_set_on_frame_recv_callback(l_cbs, s_on_frame_recv);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(l_cbs, s_on_data_chunk);
    nghttp2_session_callbacks_set_on_stream_close_callback(l_cbs, s_on_stream_close);
    int l_rc = nghttp2_session_client_new(&s->h2, l_cbs, s);
    nghttp2_session_callbacks_del(l_cbs);
    if (l_rc != 0) { a_tls->close(a_tls->user, s->tls_handle); free(s); return NULL; }

    nghttp2_settings_entry l_iv[] = {
        { NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100 },
        { NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 1 << 20 },
    };
    nghttp2_submit_settings(s->h2, NGHTTP2_FLAG_NONE, l_iv, 2);
    nghttp2_session_set_local_window_size(s->h2, NGHTTP2_FLAG_NONE, 0, 1 << 20);

    if (s_submit_download(s) != XHTTP_ERR_NONE) goto fail;

    /* first upload packet = VLESS request header (dest) */
    uint8_t l_vless[264];
    int l_vlen = xhttp_codec_build_vless_request(a_cfg->uuid, a_cfg->dest_host, a_cfg->dest_port,
                                                 l_vless, sizeof(l_vless));
    if (l_vlen < 0) goto fail;
    if (s_submit_upload(s, l_vless, (size_t)l_vlen) != XHTTP_ERR_NONE) goto fail;

    /* wait for download stream headers (200) */
    for (int i = 0; i < 400 && !s->down_headers && !s->down_closed; ++i) {
        int l_r = s_pump(s);
        if (l_r != XHTTP_ERR_NONE && l_r != XHTTP_ERR_AGAIN) goto fail;
    }
    if (!s->down_headers || s->down_status != 200) goto fail;
    return s;

fail:
    if (s->h2) nghttp2_session_del(s->h2);
    a_tls->close(a_tls->user, s->tls_handle);
    free(s->downbuf); free(s);
    return NULL;
}

int xhttp_session_read(xhttp_session_t *s, uint8_t *a_buf, size_t a_len, size_t *a_out)
{
    if (!s || !a_buf || !a_out) return XHTTP_ERR_PARAM;
    *a_out = 0;
    if (s->closed) return XHTTP_ERR_CLOSED;
    for (int i = 0; i < 400; ++i) {
        /* strip VLESS response header: ver(1) + addons_len(1) + addons */
        if (!s->vless_resp_done) {
            size_t l_avail = s->downbuf_len - s->downbuf_off;
            if (l_avail >= 2) {
                uint8_t l_addons = s->downbuf[s->downbuf_off + 1];
                if (l_avail >= (size_t)(2 + l_addons)) {
                    s->downbuf_off += 2 + l_addons;
                    s->vless_resp_done = true;
                }
            }
        }
        size_t l_avail = s->downbuf_len - s->downbuf_off;
        if (s->vless_resp_done && l_avail) {
            size_t l_n = l_avail < a_len ? l_avail : a_len;
            memcpy(a_buf, s->downbuf + s->downbuf_off, l_n);
            s->downbuf_off += l_n;
            *a_out = l_n;
            return XHTTP_ERR_NONE;
        }
        if (s->down_closed) return XHTTP_ERR_CLOSED;
        int l_rc = s_pump(s);
        if (l_rc == XHTTP_ERR_AGAIN) {
            if (s->down_closed) return XHTTP_ERR_CLOSED;
            return XHTTP_ERR_AGAIN;
        }
        if (l_rc != XHTTP_ERR_NONE) return l_rc;
    }
    return XHTTP_ERR_AGAIN;
}

int xhttp_session_write(xhttp_session_t *s, const uint8_t *a_buf, size_t a_len)
{
    if (!s || !a_buf || !a_len || s->closed) return XHTTP_ERR_PARAM;
    return s_submit_upload(s, a_buf, a_len);
}

bool xhttp_session_has_pending(xhttp_session_t *s)
{
    if (!s || s->closed) return false;
    if (s->vless_resp_done && s->downbuf_len > s->downbuf_off) return true;
    if (s->tls->pending) return s->tls->pending(s->tls->user, s->tls_handle) > 0;
    return false;
}

void xhttp_session_close(xhttp_session_t *s)
{
    if (!s) return;
    if (!s->closed) {
        s->closed = true;
        if (s->h2) {
            nghttp2_session_terminate_session(s->h2, NGHTTP2_NO_ERROR);
            const uint8_t *l_d;
            ssize_t l_n = nghttp2_session_mem_send(s->h2, &l_d);
            if (l_n > 0) s_tls_write_all(s, l_d, (size_t)l_n);
            nghttp2_session_del(s->h2);
        }
        if (s->tls_handle) s->tls->close(s->tls->user, s->tls_handle);
        if (s->io && s->io->close) s->io->close(s->io->user);
    }
    free(s->downbuf); free(s);
}