/**
 * @file shadowtls_session.c
 * @brief ShadowTLS client session (v2 + v3, auto-detected from the handshake).
 *
 * The relayed TLS 1.3 handshake lives in shadowtls_tls13.c (manual client; the
 * ClientHello SessionID is HMAC-signed for v3). If the server proves v3 by the
 * residual XOR+HMAC handshake framing (tls.authorized), data uses v3 framing:
 * ApplicationData [4B streaming HMAC][data], C/S keyed by ServerRandom+"C"/"S",
 * tag fed back after each frame; residual handshake-server frames are filtered.
 * Otherwise the session falls back to v2: the first ApplicationData record
 * carries an 8-byte HMAC-SHA1(password, server_handshake_bytes) challenge.
 */

#include "shadowtls/shadowtls.h"
#include "shadowtls_tls13.h"

#include <openssl/hmac.h>
#include <openssl/evp.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define TLS_HDR      5
#define HMAC_V2      8
#define HMAC_V3      4
#define MAX_CHUNK    16000

struct shadowtls_session {
    shadowtls_io_ops_t *io;
    stls_tls_t tls;
    char password[128];
    uint8_t challenge[HMAC_V2];
    bool first_packet;
    bool closed;
    /* v3 data framing (auto-detected via tls.authorized) */
    bool v3;
    bool v3_residual;
    HMAC_CTX *hmac_c, *hmac_s;
    uint8_t *recbuf; size_t recbuf_len;
    uint8_t *downbuf; size_t downbuf_len, downbuf_off;
};

static int s_io_write_all(shadowtls_session_t *s, const uint8_t *d, size_t len)
{
    size_t off = 0; int again = 0;
    while (off < len) {
        ssize_t w = s->io->write(s->io->user, d + off, len - off);
        if (w > 0) { off += (size_t)w; again = 0; continue; }
        if (w == SHADOWTLS_ERR_AGAIN) { if (++again >= 32) return SHADOWTLS_ERR_IO; continue; }
        return SHADOWTLS_ERR_IO;
    }
    return SHADOWTLS_ERR_NONE;
}

/* v3 data HMAC: HMAC-SHA1(password) seeded with ServerRandom || dir ("C"/"S"). */
static HMAC_CTX *s_data_hmac_new(shadowtls_session_t *s, const char *dir)
{
    HMAC_CTX *h = HMAC_CTX_new();
    if (!h) return NULL;
    HMAC_Init_ex(h, s->password, (int)strlen(s->password), EVP_sha1(), NULL);
    HMAC_Update(h, s->tls.server_random, 32);
    HMAC_Update(h, (const uint8_t *)dir, 1);
    return h;
}

/* finalize a clone (does not consume the streaming state) */
static void s_hmac_peek(HMAC_CTX *h, uint8_t *out, unsigned int *out_len)
{
    HMAC_CTX *c = HMAC_CTX_new();
    HMAC_CTX_copy(c, h);
    HMAC_Final(c, out, out_len);
    HMAC_CTX_free(c);
}

/* v3 write: ApplicationData records [4B streaming HMAC_C][data]; the tag is fed
 * back into the HMAC instance after each frame (cut/splice resistance). */
static int s_v3_write(shadowtls_session_t *s, const uint8_t *data, size_t len)
{
    size_t off = 0;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > MAX_CHUNK) chunk = MAX_CHUNK;
        uint8_t tag[HMAC_V3]; unsigned int tl = 0;
        HMAC_Update(s->hmac_c, data + off, chunk);
        s_hmac_peek(s->hmac_c, tag, &tl);
        HMAC_Update(s->hmac_c, tag, HMAC_V3);
        uint8_t hdr[TLS_HDR + HMAC_V3];
        hdr[0] = 0x17; hdr[1] = 0x03; hdr[2] = 0x03;
        uint16_t fl = (uint16_t)(HMAC_V3 + chunk);
        hdr[3] = (uint8_t)(fl >> 8); hdr[4] = (uint8_t)(fl & 0xFF);
        memcpy(hdr + TLS_HDR, tag, HMAC_V3);
        if (s_io_write_all(s, hdr, TLS_HDR + HMAC_V3) != SHADOWTLS_ERR_NONE) return SHADOWTLS_ERR_IO;
        if (s_io_write_all(s, data + off, chunk) != SHADOWTLS_ERR_NONE) return SHADOWTLS_ERR_IO;
        off += chunk;
    }
    return SHADOWTLS_ERR_NONE;
}

/* v2 write: ApplicationData records; first record carries the 8-byte challenge.
 * The challenge is computed lazily at the first write from ALL raw server bytes
 * received up to that point (matches the server's hash boundary). */
static int s_data_write(shadowtls_session_t *s, const uint8_t *data, size_t len)
{
    if (s->v3) return s_v3_write(s, data, len);
    if (s->first_packet) {
        HMAC_CTX *h = HMAC_CTX_new();
        HMAC_Init_ex(h, s->password, (int)strlen(s->password), EVP_sha1(), NULL);
        HMAC_Update(h, s->tls.hs_capture, s->tls.hs_capture_len);
        uint8_t full[EVP_MAX_MD_SIZE]; unsigned int flen = 0;
        HMAC_Final(h, full, &flen);
        memcpy(s->challenge, full, HMAC_V2);
        HMAC_CTX_free(h);
        if (getenv("SHADOWTLS_DEBUG")) {
            fprintf(stderr, "[stls] capture_len=%zu authorized=%d challenge=",
                    s->tls.hs_capture_len, (int)s->tls.authorized);
            for (int i = 0; i < HMAC_V2; ++i) fprintf(stderr, "%02x", s->challenge[i]);
            fprintf(stderr, "\n");
            FILE *f = fopen(getenv("SHADOWTLS_DEBUG"), "wb");
            if (f) { fwrite(s->tls.hs_capture, 1, s->tls.hs_capture_len, f); fclose(f); }
        }
    }
    size_t off = 0;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > MAX_CHUNK) chunk = MAX_CHUNK;
        size_t prefix = s->first_packet ? HMAC_V2 : 0;
        uint8_t hdr[TLS_HDR + HMAC_V2];
        hdr[0] = 0x17; hdr[1] = 0x03; hdr[2] = 0x03;
        uint16_t fl = (uint16_t)(prefix + chunk);
        hdr[3] = (uint8_t)(fl >> 8); hdr[4] = (uint8_t)(fl & 0xFF);
        if (prefix) memcpy(hdr + TLS_HDR, s->challenge, HMAC_V2);
        if (s_io_write_all(s, hdr, TLS_HDR + prefix) != SHADOWTLS_ERR_NONE) return SHADOWTLS_ERR_IO;
        if (s_io_write_all(s, data + off, chunk) != SHADOWTLS_ERR_NONE) return SHADOWTLS_ERR_IO;
        s->first_packet = false;
        off += chunk;
    }
    return SHADOWTLS_ERR_NONE;
}

/* v3 read: ApplicationData [4B HMAC][data]. Residual handshake-server frames
 * (verified by the continued hmac_sr, no tag chaining) are dropped; our data is
 * verified by hmac_s (tag chained). Once a data frame matches, the residual
 * branch is disabled. A bad tag is treated as Alert bad_record_mac. */
static int s_v3_feed(shadowtls_session_t *s, const uint8_t *data, size_t len)
{
    uint8_t *nb = realloc(s->recbuf, s->recbuf_len + len);
    if (!nb) return SHADOWTLS_ERR_NOMEM;
    memcpy(nb + s->recbuf_len, data, len);
    s->recbuf = nb; s->recbuf_len += len;

    size_t off = 0;
    while (s->recbuf_len - off >= TLS_HDR) {
        size_t rl = ((size_t)s->recbuf[off + 3] << 8) | s->recbuf[off + 4];
        if (s->recbuf_len - off < TLS_HDR + rl) break;
        const uint8_t *rec = s->recbuf + off;
        if (rec[0] == 0x15) return SHADOWTLS_ERR_CLOSED;  /* Alert */
        if (rec[0] == 0x17 && rl >= HMAC_V3) {
            const uint8_t *tag = rec + TLS_HDR;
            const uint8_t *pl = rec + TLS_HDR + HMAC_V3;
            size_t pl_len = rl - HMAC_V3;
            uint8_t calc[EVP_MAX_MD_SIZE]; unsigned int cl = 0;
            if (s->v3_residual && s->tls.hmac_sr) {
                HMAC_Update(s->tls.hmac_sr, pl, pl_len);
                s_hmac_peek(s->tls.hmac_sr, calc, &cl);
                if (cl >= HMAC_V3 && memcmp(tag, calc, HMAC_V3) == 0) {
                    off += TLS_HDR + rl;   /* residual handshake data: drop */
                    continue;
                }
                s->v3_residual = false;
            }
            HMAC_Update(s->hmac_s, pl, pl_len);
            s_hmac_peek(s->hmac_s, calc, &cl);
            if (cl >= HMAC_V3 && memcmp(tag, calc, HMAC_V3) == 0) {
                HMAC_Update(s->hmac_s, calc, HMAC_V3);
                uint8_t *db = realloc(s->downbuf, s->downbuf_len + pl_len);
                if (!db) return SHADOWTLS_ERR_NOMEM;
                memcpy(db + s->downbuf_len, pl, pl_len);
                s->downbuf = db; s->downbuf_len += pl_len;
            } else {
                return SHADOWTLS_ERR_CLOSED;  /* bad_record_mac */
            }
        }
        off += TLS_HDR + rl;
    }
    if (off) {
        memmove(s->recbuf, s->recbuf + off, s->recbuf_len - off);
        s->recbuf_len -= off;
    }
    return SHADOWTLS_ERR_NONE;
}

/* v2 read: strip ApplicationData framing, return payload */
static int s_data_feed(shadowtls_session_t *s, const uint8_t *data, size_t len)
{
    if (s->v3) return s_v3_feed(s, data, len);
    uint8_t *nb = realloc(s->recbuf, s->recbuf_len + len);
    if (!nb) return SHADOWTLS_ERR_NOMEM;
    memcpy(nb + s->recbuf_len, data, len);
    s->recbuf = nb; s->recbuf_len += len;

    size_t off = 0;
    while (s->recbuf_len - off >= TLS_HDR) {
        size_t rl = ((size_t)s->recbuf[off + 3] << 8) | s->recbuf[off + 4];
        if (s->recbuf_len - off < TLS_HDR + rl) break;
        const uint8_t *rec = s->recbuf + off;
        if (rec[0] == 0x15) return SHADOWTLS_ERR_CLOSED;  /* Alert */
        if (rec[0] == 0x17 && rl > 0) {
            const uint8_t *pl = rec + TLS_HDR;
            uint8_t *db = realloc(s->downbuf, s->downbuf_len + rl);
            if (!db) return SHADOWTLS_ERR_NOMEM;
            memcpy(db + s->downbuf_len, pl, rl);
            s->downbuf = db; s->downbuf_len += rl;
        }
        off += TLS_HDR + rl;
    }
    if (off) {
        memmove(s->recbuf, s->recbuf + off, s->recbuf_len - off);
        s->recbuf_len -= off;
    }
    return SHADOWTLS_ERR_NONE;
}

shadowtls_session_t *shadowtls_session_connect(const shadowtls_config_t *a_cfg,
                                               shadowtls_io_ops_t *a_io)
{
    if (!a_cfg || !a_io || !a_io->read || !a_io->write || !a_io->close || !a_cfg->password[0])
        return NULL;
    shadowtls_session_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->io = a_io;
    s->tls.io = a_io;
    s->first_packet = true;
    snprintf(s->password, sizeof(s->password), "%s", a_cfg->password);

    int rc = stls_tls_handshake(&s->tls, a_cfg->sni, s->password);
    if (rc != SHADOWTLS_ERR_NONE) goto fail;

    if (s->tls.authorized) {
        /* v3: keep hmac_sr (continued) to filter residual handshake data;
         * set up the C/S streaming data HMACs. */
        s->v3 = true;
        s->v3_residual = true;
        s->hmac_c = s_data_hmac_new(s, "C");
        s->hmac_s = s_data_hmac_new(s, "S");
        if (!s->hmac_c || !s->hmac_s) goto fail;
    } else if (s->tls.hmac_sr) {
        /* v2: challenge is computed lazily at first write from hs_capture */
        HMAC_CTX_free(s->tls.hmac_sr); s->tls.hmac_sr = NULL;
    }
    /* feed bytes the handshake read past the server Finished (e.g. NST) */
    if (s->tls.inbuf_len) {
        int frc = s_data_feed(s, s->tls.inbuf, s->tls.inbuf_len);
        s->tls.inbuf_len = 0;
        if (frc != SHADOWTLS_ERR_NONE) goto fail;
    }
    return s;

fail:
    if (s->tls.hmac_sr) HMAC_CTX_free(s->tls.hmac_sr);
    if (s->hmac_c) HMAC_CTX_free(s->hmac_c);
    if (s->hmac_s) HMAC_CTX_free(s->hmac_s);
    free(s->tls.hs_capture);
    free(s->recbuf); free(s->downbuf); free(s);
    return NULL;
}

int shadowtls_session_read(shadowtls_session_t *s, uint8_t *a_buf, size_t a_len, size_t *a_out)
{
    if (!s || !a_buf || !a_out) return SHADOWTLS_ERR_PARAM;
    *a_out = 0;
    if (s->closed) return SHADOWTLS_ERR_CLOSED;
    for (int i = 0; i < 2000; ++i) {
        if (s->downbuf_len > s->downbuf_off) {
            size_t avail = s->downbuf_len - s->downbuf_off;
            size_t n = avail < a_len ? avail : a_len;
            memcpy(a_buf, s->downbuf + s->downbuf_off, n);
            s->downbuf_off += n;
            if (s->downbuf_off == s->downbuf_len) { s->downbuf_off = s->downbuf_len = 0; }
            *a_out = n;
            return SHADOWTLS_ERR_NONE;
        }
        uint8_t rbuf[16384]; size_t got = 0;
        int r = s->io->read(s->io->user, rbuf, sizeof(rbuf), &got);
        if (r == SHADOWTLS_ERR_AGAIN) return SHADOWTLS_ERR_AGAIN;
        if (r == SHADOWTLS_ERR_CLOSED || got == 0) return SHADOWTLS_ERR_CLOSED;
        if (r != SHADOWTLS_ERR_NONE) return SHADOWTLS_ERR_IO;
        /* v2 challenge = HMAC over the bytes read DURING the handshake only
         * (reference hashes at end of connect()). Post-handshake reads (NST
         * drain) must NOT extend the capture: the server's hash boundary at
         * challenge-check time is racy, and matching the handshake boundary is
         * what the reference client does. */
        if (!s->tls.handshake_done && got) {
            uint8_t *nb = realloc(s->tls.hs_capture, s->tls.hs_capture_len + got);
            if (nb) { memcpy(nb + s->tls.hs_capture_len, rbuf, got); s->tls.hs_capture = nb; s->tls.hs_capture_len += got; }
        }
        int rc = s_data_feed(s, rbuf, got);
        if (rc != SHADOWTLS_ERR_NONE) return rc;
    }
    return SHADOWTLS_ERR_AGAIN;
}

int shadowtls_session_write(shadowtls_session_t *s, const uint8_t *a_buf, size_t a_len)
{
    if (!s || !a_buf || !a_len || s->closed) return SHADOWTLS_ERR_PARAM;
    return s_data_write(s, a_buf, a_len);
}

bool shadowtls_session_has_pending(shadowtls_session_t *s)
{
    if (!s || s->closed) return false;
    return s->downbuf_len > s->downbuf_off;
}

void shadowtls_session_close(shadowtls_session_t *s)
{
    if (!s) return;
    if (!s->closed) {
        s->closed = true;
        if (s->io && s->io->close) s->io->close(s->io->user);
    }
    if (s->tls.hmac_sr) HMAC_CTX_free(s->tls.hmac_sr);
    if (s->hmac_c) HMAC_CTX_free(s->hmac_c);
    if (s->hmac_s) HMAC_CTX_free(s->hmac_s);
    free(s->tls.hs_capture);
    free(s->recbuf); free(s->downbuf); free(s);
}