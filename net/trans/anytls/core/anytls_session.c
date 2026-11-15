/**
 * @file anytls_session.c
 * @brief Blocking AnyTLS session (single stream for the DAP carrier). SDK-free.
 *        TLS + auth + cmdSettings/SYN/SYNACK + PSH/FIN, padding scheme, and
 *        server control frames (waste/heartbeat/alert/serverSettings/update).
 */

#include "anytls/anytls.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define CMD_WASTE        0
#define CMD_SYN          1
#define CMD_PSH          2
#define CMD_FIN          3
#define CMD_SETTINGS     4
#define CMD_ALERT        5
#define CMD_UPDATE_PAD   6
#define CMD_SYNACK       7
#define CMD_HEART_REQ    8
#define CMD_HEART_RESP   9
#define CMD_SERVER_SET   10

#define ANYTLS_STREAM_ID 1
#define MAX_FRAME_DATA   0xFFFF

typedef struct { uint16_t min, max; bool check; } pad_entry_t;
typedef struct {
    int stop;
    int nentries[16];
    pad_entry_t entries[16][16];
} padding_scheme_t;

struct anytls_session {
    anytls_io_ops_t *io;
    const anytls_tls_ops_t *tls;
    void *tls_handle;
    uint8_t *inbuf; size_t inbuf_len;
    uint8_t *pending; size_t pending_len, pending_off;
    padding_scheme_t scheme;
    uint64_t write_count;
    bool stream_open;
    bool peer_fin;
    bool closed;
    int version;
};

/* ---- padding scheme ---- */
static void s_scheme_default(padding_scheme_t *a_s)
{
    memset(a_s, 0, sizeof(*a_s));
    a_s->stop = 8;
    /* 0=30-30 */
    a_s->nentries[0] = 1; a_s->entries[0][0] = (pad_entry_t){30, 30, false};
    /* 1=100-400 */
    a_s->nentries[1] = 1; a_s->entries[1][0] = (pad_entry_t){100, 400, false};
    /* 2=400-500,c,500-1000,c,500-1000,c,500-1000,c,500-1000 */
    a_s->nentries[2] = 9;
    a_s->entries[2][0] = (pad_entry_t){400, 500, false};
    a_s->entries[2][1] = (pad_entry_t){0, 0, true};
    a_s->entries[2][2] = (pad_entry_t){500, 1000, false};
    a_s->entries[2][3] = (pad_entry_t){0, 0, true};
    a_s->entries[2][4] = (pad_entry_t){500, 1000, false};
    a_s->entries[2][5] = (pad_entry_t){0, 0, true};
    a_s->entries[2][6] = (pad_entry_t){500, 1000, false};
    a_s->entries[2][7] = (pad_entry_t){0, 0, true};
    a_s->entries[2][8] = (pad_entry_t){500, 1000, false};
    /* 3=9-9,500-1000 */
    a_s->nentries[3] = 2;
    a_s->entries[3][0] = (pad_entry_t){9, 9, false};
    a_s->entries[3][1] = (pad_entry_t){500, 1000, false};
    /* 4..7=500-1000 */
    for (int i = 4; i <= 7; ++i) {
        a_s->nentries[i] = 1; a_s->entries[i][0] = (pad_entry_t){500, 1000, false};
    }
}

static void s_scheme_parse(padding_scheme_t *a_s, const char *a_text, size_t a_len)
{
    s_scheme_default(a_s);
    if (!a_text || !a_len) return;
    char *l_buf = (char *)malloc(a_len + 1);
    if (!l_buf) return;
    memcpy(l_buf, a_text, a_len); l_buf[a_len] = '\0';
    char *l_save = NULL;
    for (char *l_line = strtok_r(l_buf, "\n", &l_save); l_line;
         l_line = strtok_r(NULL, "\n", &l_save)) {
        char *l_eq = strchr(l_line, '=');
        if (!l_eq) continue;
        *l_eq = '\0';
        const char *l_key = l_line;
        char *l_val = l_eq + 1;
        if (strcmp(l_key, "stop") == 0) {
            a_s->stop = atoi(l_val);
            if (a_s->stop > 16) a_s->stop = 16;
            continue;
        }
        int l_idx = atoi(l_key);
        if (l_idx < 0 || l_idx >= 16) continue;
        a_s->nentries[l_idx] = 0;
        char *l_save2 = NULL;
        for (char *l_tok = strtok_r(l_val, ",", &l_save2); l_tok;
             l_tok = strtok_r(NULL, ",", &l_save2)) {
            if (a_s->nentries[l_idx] >= 16) break;
            pad_entry_t l_e = {0, 0, false};
            if (strcmp(l_tok, "c") == 0) { l_e.check = true; }
            else {
                int l_min = 0, l_max = 0;
                if (sscanf(l_tok, "%d-%d", &l_min, &l_max) == 2 && l_max >= l_min && l_min >= 0) {
                    l_e.min = (uint16_t)l_min; l_e.max = (uint16_t)l_max;
                } else continue;
            }
            a_s->entries[l_idx][a_s->nentries[l_idx]++] = l_e;
        }
    }
    free(l_buf);
}

static uint16_t s_rand_range(uint16_t a_min, uint16_t a_max)
{
    if (a_max <= a_min) return a_min;
    return (uint16_t)(a_min + (rand() % (a_max - a_min + 1)));
}

/* ---- low-level TLS writes ---- */
static int s_tls_write_all(anytls_session_t *s, const uint8_t *a_data, size_t a_len)
{
    size_t l_off = 0; int l_again = 0;
    while (l_off < a_len) {
        ssize_t l_w = s->tls->write(s->tls->user, s->tls_handle, a_data + l_off, a_len - l_off);
        if (l_w > 0) { l_off += (size_t)l_w; l_again = 0; continue; }
        if (l_w == ANYTLS_ERR_AGAIN) { if (++l_again >= 16) return ANYTLS_ERR_IO; continue; }
        return ANYTLS_ERR_IO;
    }
    return ANYTLS_ERR_NONE;
}

static int s_send_frame(anytls_session_t *s, uint8_t a_cmd, uint32_t a_sid,
                        const uint8_t *a_data, size_t a_len)
{
    uint8_t l_hdr[7];
    l_hdr[0] = a_cmd;
    l_hdr[1] = (uint8_t)(a_sid >> 24); l_hdr[2] = (uint8_t)(a_sid >> 16);
    l_hdr[3] = (uint8_t)(a_sid >> 8);  l_hdr[4] = (uint8_t)a_sid;
    l_hdr[5] = (uint8_t)(a_len >> 8);  l_hdr[6] = (uint8_t)(a_len & 0xFF);
    int l_rc = s_tls_write_all(s, l_hdr, 7);
    if (l_rc != ANYTLS_ERR_NONE) return l_rc;
    if (a_len) return s_tls_write_all(s, a_data, a_len);
    return ANYTLS_ERR_NONE;
}

static int s_send_psh_all(anytls_session_t *s, const uint8_t *a_data, size_t a_len)
{
    size_t l_off = 0;
    while (l_off < a_len) {
        size_t l_chunk = a_len - l_off;
        if (l_chunk > MAX_FRAME_DATA) l_chunk = MAX_FRAME_DATA;
        int l_rc = s_send_frame(s, CMD_PSH, ANYTLS_STREAM_ID, a_data + l_off, l_chunk);
        if (l_rc != ANYTLS_ERR_NONE) return l_rc;
        l_off += l_chunk;
    }
    return ANYTLS_ERR_NONE;
}

static int s_send_waste(anytls_session_t *s, size_t a_len)
{
    uint8_t l_zero[1024]; memset(l_zero, 0, sizeof(l_zero));
    size_t l_off = 0;
    while (l_off < a_len) {
        size_t l_chunk = a_len - l_off;
        if (l_chunk > sizeof(l_zero)) l_chunk = sizeof(l_zero);
        int l_rc = s_send_frame(s, CMD_WASTE, 0, l_zero, l_chunk);
        if (l_rc != ANYTLS_ERR_NONE) return l_rc;
        l_off += l_chunk;
    }
    return ANYTLS_ERR_NONE;
}

/* Write PRE-FRAMED bytes (e.g. packet1 = settings+SYN+PSH) as one TLS write,
 * padded with cmdWaste up to the scheme target size. Does NOT re-wrap in PSH
 * (the bytes are already frames). */
static int s_write_frames_padded(anytls_session_t *s, const uint8_t *a_frames, size_t a_len)
{
    uint64_t l_idx = s->write_count;
    padding_scheme_t *l_sc = &s->scheme;
    int l_rc = s_tls_write_all(s, a_frames, a_len);
    if (l_rc != ANYTLS_ERR_NONE) return l_rc;
    if (l_idx >= (uint64_t)l_sc->stop || l_idx >= 16 || l_sc->nentries[l_idx] == 0)
        return ANYTLS_ERR_NONE;
    size_t l_target = 0;
    for (int e = 0; e < l_sc->nentries[l_idx]; ++e) {
        pad_entry_t *l_pe = &l_sc->entries[l_idx][e];
        if (!l_pe->check) l_target += s_rand_range(l_pe->min, l_pe->max);
    }
    if (a_len < l_target)
        return s_send_waste(s, l_target - a_len);
    return ANYTLS_ERR_NONE;
}

/* Padding-aware write of one user buffer as packet s->write_count. */
static int s_padded_write(anytls_session_t *s, const uint8_t *a_data, size_t a_len)
{
    uint64_t l_idx = s->write_count;
    padding_scheme_t *l_sc = &s->scheme;
    if (l_idx >= (uint64_t)l_sc->stop || l_idx >= 16 || l_sc->nentries[l_idx] == 0)
        return s_send_psh_all(s, a_data, a_len);

    size_t l_off = 0;
    for (int e = 0; e < l_sc->nentries[l_idx]; ++e) {
        pad_entry_t *l_pe = &l_sc->entries[l_idx][e];
        if (l_pe->check) {
            if (l_off >= a_len) return ANYTLS_ERR_NONE;  /* 'c': no data left */
            continue;
        }
        uint16_t l_size = s_rand_range(l_pe->min, l_pe->max);
        size_t l_chunk = (a_len - l_off) < l_size ? (a_len - l_off) : l_size;
        if (l_chunk) {
            int l_rc = s_send_frame(s, CMD_PSH, ANYTLS_STREAM_ID, a_data + l_off, l_chunk);
            if (l_rc != ANYTLS_ERR_NONE) return l_rc;
            l_off += l_chunk;
        }
        if (l_chunk < l_size) {
            int l_rc = s_send_waste(s, l_size - l_chunk);
            if (l_rc != ANYTLS_ERR_NONE) return l_rc;
        }
    }
    if (l_off < a_len)
        return s_send_psh_all(s, a_data + l_off, a_len - l_off);
    return ANYTLS_ERR_NONE;
}

/* ---- incoming frame handling ---- */
static int s_pending_append(anytls_session_t *s, const uint8_t *a_data, size_t a_len)
{
    /* compact consumed prefix first */
    if (s->pending_off) {
        memmove(s->pending, s->pending + s->pending_off, s->pending_len - s->pending_off);
        s->pending_len -= s->pending_off; s->pending_off = 0;
    }
    uint8_t *l_buf = (uint8_t *)realloc(s->pending, s->pending_len + a_len);
    if (!l_buf) return ANYTLS_ERR_NOMEM;
    memcpy(l_buf + s->pending_len, a_data, a_len);
    s->pending = l_buf; s->pending_len += a_len;
    return ANYTLS_ERR_NONE;
}

static void s_handle_frame(anytls_session_t *s, uint8_t a_cmd, uint32_t a_sid,
                           const uint8_t *a_data, size_t a_len)
{
    switch (a_cmd) {
    case CMD_PSH:
        if (a_sid == ANYTLS_STREAM_ID && a_len) s_pending_append(s, a_data, a_len);
        break;
    case CMD_WASTE:
        break;
    case CMD_SYNACK:
        if (a_sid == ANYTLS_STREAM_ID) {
            if (a_len) s->closed = true;   /* data = error message */
            else s->stream_open = true;
        }
        break;
    case CMD_FIN:
        if (a_sid == ANYTLS_STREAM_ID) s->peer_fin = true;
        break;
    case CMD_SERVER_SET: {
        char *l_buf = (char *)malloc(a_len + 1);
        if (l_buf) {
            memcpy(l_buf, a_data, a_len); l_buf[a_len] = '\0';
            char *l_v = strstr(l_buf, "v=");
            if (l_v) s->version = atoi(l_v + 2);
            free(l_buf);
        }
        break;
    }
    case CMD_ALERT:
        s->closed = true;
        break;
    case CMD_UPDATE_PAD:
        s_scheme_parse(&s->scheme, (const char *)a_data, a_len);
        break;
    case CMD_HEART_REQ:
        s_send_frame(s, CMD_HEART_RESP, 0, NULL, 0);
        break;
    default:
        break;
    }
}

/* Read TLS bytes, parse complete frames, handle them. Returns ANYTLS_ERR_NONE
 * if progress, AGAIN if no data now, CLOSED/IO on terminal. */
static int s_pump_read(anytls_session_t *s)
{
    uint8_t l_buf[16384];
    ssize_t l_r = s->tls->read(s->tls->user, s->tls_handle, l_buf, sizeof(l_buf));
    if (l_r == ANYTLS_ERR_AGAIN) return ANYTLS_ERR_AGAIN;
    if (l_r == ANYTLS_ERR_CLOSED || l_r == 0) return ANYTLS_ERR_CLOSED;
    if (l_r < 0) return ANYTLS_ERR_IO;

    uint8_t *l_nb = (uint8_t *)realloc(s->inbuf, s->inbuf_len + (size_t)l_r);
    if (!l_nb) return ANYTLS_ERR_NOMEM;
    memcpy(l_nb + s->inbuf_len, l_buf, (size_t)l_r);
    s->inbuf = l_nb; s->inbuf_len += (size_t)l_r;

    size_t l_off = 0;
    while (s->inbuf_len - l_off >= 7) {
        const uint8_t *l_p = s->inbuf + l_off;
        uint8_t l_cmd = l_p[0];
        uint32_t l_sid = ((uint32_t)l_p[1] << 24) | ((uint32_t)l_p[2] << 16) |
                         ((uint32_t)l_p[3] << 8) | l_p[4];
        size_t l_dlen = ((size_t)l_p[5] << 8) | l_p[6];
        if (s->inbuf_len - l_off < 7 + l_dlen) break;
        s_handle_frame(s, l_cmd, l_sid, l_p + 7, l_dlen);
        l_off += 7 + l_dlen;
    }
    if (l_off) {
        memmove(s->inbuf, s->inbuf + l_off, s->inbuf_len - l_off);
        s->inbuf_len -= l_off;
    }
    return ANYTLS_ERR_NONE;
}

/* ---- public API ---- */
anytls_session_t *anytls_session_connect(const anytls_config_t *a_cfg,
                                         anytls_io_ops_t *a_io,
                                         const anytls_tls_ops_t *a_tls)
{
    if (!a_cfg || !a_io || !a_tls || !a_tls->connect || !a_tls->read ||
        !a_tls->write || !a_tls->close || !a_cfg->dest_host[0] || !a_cfg->dest_port)
        return NULL;
    int l_fd = a_io->fd ? a_io->fd(a_io->user) : -1;
    if (l_fd < 0) return NULL;

    anytls_session_t *s = (anytls_session_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->io = a_io; s->tls = a_tls; s->version = 2;
    s_scheme_default(&s->scheme);

    anytls_tls_cfg_t l_tcfg; memset(&l_tcfg, 0, sizeof(l_tcfg));
    l_tcfg.sni = a_cfg->sni;
    l_tcfg.spki_sha256 = a_cfg->have_pin ? a_cfg->spki_sha256 : NULL;
    l_tcfg.have_pin = a_cfg->have_pin;
    l_tcfg.hello_profile = a_cfg->hello_profile;
    l_tcfg.alpn = NULL;  /* AnyTLS negotiates no ALPN */
    s->tls_handle = a_tls->connect(a_tls->user, l_fd, &l_tcfg);
    if (!s->tls_handle) { free(s); return NULL; }

    /* auth: sha256 + padding0 (scheme packet 0). */
    uint16_t l_pad0 = 30;
    if (s->scheme.nentries[0] > 0 && !s->scheme.entries[0][0].check)
        l_pad0 = s_rand_range(s->scheme.entries[0][0].min, s->scheme.entries[0][0].max);
    uint8_t l_auth[32 + 2 + 1024];
    int l_auth_len = anytls_codec_build_auth(a_cfg->password_hash, l_pad0, l_auth, sizeof(l_auth));
    if (l_auth_len < 0 || s_tls_write_all(s, l_auth, (size_t)l_auth_len) != ANYTLS_ERR_NONE) {
        a_tls->close(a_tls->user, s->tls_handle); free(s); return NULL;
    }
    s->write_count = 1;

    /* packet 1: cmdSettings + cmdSYN + cmdPSH(SocksAddr). */
    char l_settings[160];
    int l_set_len = snprintf(l_settings, sizeof(l_settings),
                             "v=2\nclient=anytls-c/0.1\npadding-md5=%s\n",
                             a_cfg->padding_md5[0] ? a_cfg->padding_md5 : "");
    uint8_t l_socks[264];
    int l_socks_len = anytls_codec_build_socksaddr(a_cfg->dest_host, a_cfg->dest_port,
                                                   l_socks, sizeof(l_socks));
    if (l_socks_len < 0) { a_tls->close(a_tls->user, s->tls_handle); free(s); return NULL; }

    uint8_t l_pkt[1024]; size_t l_pkt_len = 0;
    int l_n = anytls_codec_build_frame(CMD_SETTINGS, 0, (const uint8_t *)l_settings,
                                       (size_t)l_set_len, l_pkt + l_pkt_len, sizeof(l_pkt) - l_pkt_len);
    if (l_n < 0) { a_tls->close(a_tls->user, s->tls_handle); free(s); return NULL; }
    l_pkt_len += (size_t)l_n;
    l_n = anytls_codec_build_frame(CMD_SYN, ANYTLS_STREAM_ID, NULL, 0,
                                   l_pkt + l_pkt_len, sizeof(l_pkt) - l_pkt_len);
    if (l_n < 0) { a_tls->close(a_tls->user, s->tls_handle); free(s); return NULL; }
    l_pkt_len += (size_t)l_n;
    l_n = anytls_codec_build_frame(CMD_PSH, ANYTLS_STREAM_ID, l_socks, (size_t)l_socks_len,
                                   l_pkt + l_pkt_len, sizeof(l_pkt) - l_pkt_len);
    if (l_n < 0) { a_tls->close(a_tls->user, s->tls_handle); free(s); return NULL; }
    l_pkt_len += (size_t)l_n;

    if (s_write_frames_padded(s, l_pkt, l_pkt_len) != ANYTLS_ERR_NONE) {
        a_tls->close(a_tls->user, s->tls_handle); free(s); return NULL;
    }
    s->write_count = 2;

    /* wait for SYNACK (v2) / server settings; bounded read loop. */
    for (int i = 0; i < 8 && !s->stream_open && !s->closed; ++i) {
        int l_rc = s_pump_read(s);
        if (l_rc == ANYTLS_ERR_CLOSED || l_rc == ANYTLS_ERR_IO) break;
    }
    if (!s->stream_open || s->closed) {
        a_tls->close(a_tls->user, s->tls_handle);
        free(s->inbuf); free(s->pending); free(s);
        return NULL;
    }
    return s;
}

int anytls_session_read(anytls_session_t *s, uint8_t *a_buf, size_t a_len, size_t *a_out)
{
    if (!s || !a_buf || !a_out) return ANYTLS_ERR_PARAM;
    *a_out = 0;
    if (s->closed) return ANYTLS_ERR_CLOSED;
    for (int i = 0; i < 200; ++i) {
        size_t l_avail = s->pending_len - s->pending_off;
        if (l_avail) {
            size_t l_n = l_avail < a_len ? l_avail : a_len;
            memcpy(a_buf, s->pending + s->pending_off, l_n);
            s->pending_off += l_n;
            *a_out = l_n;
            return ANYTLS_ERR_NONE;
        }
        if (s->peer_fin) return ANYTLS_ERR_CLOSED;
        int l_rc = s_pump_read(s);
        if (l_rc == ANYTLS_ERR_AGAIN) {
            if (s->peer_fin) return ANYTLS_ERR_CLOSED;
            return ANYTLS_ERR_AGAIN;
        }
        if (l_rc != ANYTLS_ERR_NONE) return l_rc;
    }
    return ANYTLS_ERR_AGAIN;
}

int anytls_session_write(anytls_session_t *s, const uint8_t *a_buf, size_t a_len)
{
    if (!s || !a_buf || !a_len || s->closed) return ANYTLS_ERR_PARAM;
    int l_rc = s_padded_write(s, a_buf, a_len);
    s->write_count++;
    return l_rc;
}

bool anytls_session_has_pending(anytls_session_t *s)
{
    if (!s || s->closed) return false;
    if (s->pending_len > s->pending_off) return true;
    if (s->tls->pending) return s->tls->pending(s->tls->user, s->tls_handle) > 0;
    return false;
}

void anytls_session_close(anytls_session_t *s)
{
    if (!s) return;
    if (!s->closed) {
        s->closed = true;
        if (s->tls_handle) {
            s_send_frame(s, CMD_FIN, ANYTLS_STREAM_ID, NULL, 0);
            s->tls->close(s->tls->user, s->tls_handle);
        }
        if (s->io && s->io->close) s->io->close(s->io->user);
    }
    free(s->inbuf); free(s->pending); free(s);
}