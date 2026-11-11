/**
 * @file reality_session.c
 * @brief Public session API: connect (REALITY TLS + VLESS + Vision), payload IO.
 *
 * VLESS wire phases (Xray proxy/vless/encoding v26.10.31):
 *   up:   request header RAW -> body through Vision writer (uuid-prefixed
 *         first frame, padded until filter window ends)
 *   down: response header RAW ([ver][addons]) -> body through Vision reader
 */

#include "reality_internal.h"
#include <stdlib.h>

#define VLESS_CHUNK 2048u

/* Shared token bucket for BOTH directions (a single flow-rate cap is what the
 * DPI measurements call for). Blocking here is safe: the session is consumed
 * either by the bridge pump thread or by a blocking test/CLI reader. */
static void rate_limit_wait(reality_session_t *s, size_t a_len)
{
    if (!s->rl.rate_bps || !s->time->sleep_ms || !a_len)
        return;
    for (;;) {
        uint64_t now = s->time->mono_ms(s->time->user);
        if (rate_limiter_allow(&s->rl, now, a_len))
            break;
        uint64_t d = rate_limiter_delay_ms(&s->rl, now, a_len);
        if (d > 1000) d = 1000;
        s->time->sleep_ms(s->time->user, d);
    }
}

/* raw (non-vision) read of exactly need bytes into dst; leftovers stay in rawbuf */
static int read_raw_exact(reality_session_t *s, uint8_t *dst, size_t need)
{
    size_t have = 0;
    while (have < need) {
        if (s->rawbuf_len > s->rawbuf_off) {
            size_t n = s->rawbuf_len - s->rawbuf_off;
            if (n > need - have) n = need - have;
            memcpy(dst + have, s->rawbuf + s->rawbuf_off, n);
            s->rawbuf_off += n;
            have += n;
            continue;
        }
        size_t got = 0;
        int rc = rtls_read_app(s, s->rawbuf, sizeof(s->rawbuf), &got);
        if (rc) return rc;
        if (got == 0) continue;  /* empty app-data record (REALITY post-handshake
                                  * mimicry / keepalive) -- NOT an EOF. Real EOF
                                  * arrives as rc == REALITY_ERR_CLOSED. */
        s->rawbuf_len = got;
        s->rawbuf_off = 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ */

reality_session_t *reality_session_connect(const reality_config_t *a_cfg,
                                           const reality_io_ops_t *a_io,
                                           const reality_crypto_ops_t *a_crypto,
                                           const reality_time_ops_t *a_time,
                                           const reality_log_ops_t *a_log,
                                           const char *a_dest_host, uint16_t a_dest_port)
{
    if (!a_cfg || !a_io || !a_crypto || !a_time || !a_dest_host) return NULL;
    if (!a_cfg->sni[0] || !a_cfg->public_key_b64url[0] || !a_cfg->uuid[0]) return NULL;
    if (!a_io->read || !a_io->write || !a_crypto->rand_bytes || !a_crypto->x25519_keypair ||
        !a_crypto->x25519_shared || !a_crypto->hkdf || !a_crypto->aead_seal ||
        !a_crypto->aead_open || !a_crypto->hmac || !a_crypto->hash ||
        !a_crypto->ed25519_verify || !a_crypto->mlkem_keypair || !a_crypto->mlkem_decaps ||
        !a_time->unix_now || !a_time->mono_ms)
        return NULL;

    reality_session_t *s = (reality_session_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;

    s->cfg = *a_cfg;
    s->io = a_io;
    s->crypto = a_crypto;
    s->time = a_time;
    s->log = a_log;

    uint8_t uuid[16];
    if (reality_uuid_parse(a_cfg->uuid, uuid) != 0) {
        snprintf(s->errmsg, sizeof(s->errmsg), "bad uuid");
        free(s);
        return NULL;
    }
    rate_limiter_init(&s->rl, a_cfg->rate_limit_bytes_per_sec);

    if (rtls_handshake(s, a_cfg->sni) != 0) {
        RLOGE(s, "handshake failed: %s", s->errmsg);
        free(s);
        return NULL;
    }

    /* --- VLESS request header: RAW TLS app data (NOT vision-wrapped) --- */
    {
        uint8_t hdr[512];
        size_t hdr_len = 0;
        if (vless_build_request(uuid, a_dest_host, a_dest_port, 1,
                                hdr, sizeof(hdr), &hdr_len) != 0) {
            snprintf(s->errmsg, sizeof(s->errmsg), "vless header build failed");
            free(s);
            return NULL;
        }
        if (rtls_write_app(s, hdr, hdr_len) != 0) {
            free(s);
            return NULL;
        }
    }

    /* --- VLESS response header is LAZY: xray server flushes it only after
     * the first client body bytes reach the target (SetFlushNext), so we must
     * NOT block on it here -- consume it on first reality_session_read(). --- */

    /* body phase: vision framing both directions */
    vision_writer_init(&s->vw, uuid);
    vision_reader_init(&s->vr, uuid);
    s->resp_hdr_done = false;

    RLOGI(s, "REALITY session established -> %s:%u", a_dest_host, a_dest_port);
    return s;
}

/* ------------------------------------------------------------------ */

int reality_session_write(reality_session_t *s, const uint8_t *buf, size_t len)
{
    if (!s || !buf) return REALITY_ERR_PARAM;
    if (!s->handshake_done) RFAIL(s, REALITY_ERR_PROTOCOL, "write before handshake");

    size_t off = 0;
    uint8_t frame[VLESS_CHUNK + 21 + 16];
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > VLESS_CHUNK) chunk = VLESS_CHUNK;

        if (s->rl.rate_bps && s->time->sleep_ms)
            rate_limit_wait(s, chunk);

        size_t flen = vision_write(&s->vw, buf + off, chunk, frame, sizeof(frame));
        if (flen == 0) RFAIL(s, REALITY_ERR_PROTOCOL, "vision_write overflow");
        int rc = rtls_write_app(s, frame, flen);
        if (rc) return rc;
        off += chunk;
    }
    return 0;
}

int reality_session_read(reality_session_t *s, uint8_t *buf, size_t cap, size_t *out_len)
{
    if (!s || !buf || !out_len) return REALITY_ERR_PARAM;
    *out_len = 0;
    if (!s->handshake_done) RFAIL(s, REALITY_ERR_PROTOCOL, "read before handshake");

    /* lazy VLESS response header: [version][addons_len][addons...] raw */
    if (!s->resp_hdr_done) {
        uint8_t rh[2];
        int rc = read_raw_exact(s, rh, 2);
        if (rc) return rc;
        if (rh[0] != 0)
            RFAILF(s, REALITY_ERR_PROTOCOL, "bad vless response version %u", rh[0]);
        if (rh[1] > 0) {
            uint8_t addons[255];
            rc = read_raw_exact(s, addons, rh[1]);
            if (rc) return rc;
        }
        s->resp_hdr_done = true;
        RLOGI(s, "vless response header ok");
    }

    /* leftover decoded payload */
    if (s->appbuf_len > s->appbuf_off) {
        size_t n = s->appbuf_len - s->appbuf_off;
        if (n > cap) n = cap;
        rate_limit_wait(s, n);
        memcpy(buf, s->appbuf + s->appbuf_off, n);
        s->appbuf_off += n;
        *out_len = n;
        return 0;
    }

    for (;;) {
        /* refill raw TLS payload */
        if (s->rawbuf_len <= s->rawbuf_off) {
            size_t got = 0;
            int rc = rtls_read_app(s, s->rawbuf, sizeof(s->rawbuf), &got);
            if (rc) return rc;
            if (got == 0) continue;
            s->rawbuf_len = got;
            s->rawbuf_off = 0;
        }
        size_t avail = s->rawbuf_len - s->rawbuf_off;
        size_t consumed = 0;
        int64_t n = vision_read(&s->vr, s->rawbuf + s->rawbuf_off, avail,
                                s->appbuf, sizeof(s->appbuf), &consumed);
        if (n < 0) RFAIL(s, REALITY_ERR_PROTOCOL, "vision framing violation");
        s->rawbuf_off += consumed;
        s->appbuf_len = (size_t)n;
        s->appbuf_off = 0;
        if (s->appbuf_len == 0) continue; /* pure padding frame */

        size_t take = s->appbuf_len;
        if (take > cap) take = cap;
        rate_limit_wait(s, take);
        memcpy(buf, s->appbuf, take);
        s->appbuf_off = take;
        *out_len = take;
        return 0;
    }
}

bool reality_session_has_pending(const reality_session_t *s)
{
    if (!s) return false;
    /* decoded payload not yet handed to the caller */
    if (s->appbuf_len > s->appbuf_off) return true;
    /* decrypted TLS payload not yet vision-decoded */
    if (s->rawbuf_len > s->rawbuf_off) return true;
    /* at least one COMPLETE TLS record buffered (header 5B + length) */
    if (s->inbuf_len >= 5) {
        size_t rlen = ((size_t)s->inbuf[3] << 8) | (size_t)s->inbuf[4];
        if (s->inbuf_len >= rlen + 5) return true;
    }
    return false;
}

void reality_session_close(reality_session_t *s){
    if (!s) return;
    if (s->handshake_done)
        rtls_close_notify(s);
    if (s->io && s->io->close) s->io->close(s->io->user);
    memset(s->auth_key, 0, sizeof(s->auth_key));
    memset(s->x25519_priv, 0, sizeof(s->x25519_priv));
    memset(s->hybrid_priv, 0, sizeof(s->hybrid_priv));
    memset(s->mlkem_dk, 0, sizeof(s->mlkem_dk));
    memset(s->c_key, 0, sizeof(s->c_key));
    memset(s->s_key, 0, sizeof(s->s_key));
    free(s);
}

const char *reality_last_error(reality_session_t *s)
{
    return s ? s->errmsg : "null session";
}
