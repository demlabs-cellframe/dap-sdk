/**
 * @file reality_internal.h
 * @brief Internal shared declarations for the reality transport core.
 *        NOT part of the public API -- core/ and port/ only.
 */

#ifndef REALITY_INTERNAL_H
#define REALITY_INTERNAL_H

#include "reality/reality.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define RLOG(s, lvl, ...) \
    do { if ((s) && (s)->log && (s)->log->log) (s)->log->log((s)->log->user, (lvl), __VA_ARGS__); } while (0)
#define RLOGD(s, ...) RLOG(s, 0, __VA_ARGS__)
#define RLOGI(s, ...) RLOG(s, 1, __VA_ARGS__)
#define RLOGW(s, ...) RLOG(s, 2, __VA_ARGS__)
#define RLOGE(s, ...) RLOG(s, 3, __VA_ARGS__)

#define RFAIL(s, code, msg) \
    do { if (s) { (s)->err = (code); snprintf((s)->errmsg, sizeof((s)->errmsg), "%s", (msg)); RLOGE(s, "%s", msg); } return code; } while (0)
#define RFAILF(s, code, fmt, ...) \
    do { if (s) { (s)->err = (code); snprintf((s)->errmsg, sizeof((s)->errmsg), fmt, __VA_ARGS__); RLOGE(s, fmt, __VA_ARGS__); } return code; } while (0)

/* ------------------------------------------------------------------ */
/* Fingerprint template (generated: core/reality_fp_chrome.c)          */
/* ------------------------------------------------------------------ */

typedef struct reality_fp_template {
    const char    *name;
    const uint8_t *clienthello;
    size_t         clienthello_size;
    /* static offsets inside the template */
    size_t random_off;        /* 32 bytes, fresh per connection */
    size_t session_id_len_off;/* byte == 0x20 */
    size_t session_id_off;    /* 32 bytes, sealed REALITY auth */
    size_t sni_name_off;      /* SNI hostname bytes */
    size_t sni_name_len;
    size_t ks_hybrid_off;     /* ML-KEM ek(1184) || X25519 pub(32) */
    size_t ks_hybrid_len;
    size_t ks_x25519_off;     /* plain X25519 pub(32) */
    size_t ks_x25519_len;
} reality_fp_template_t;

extern const reality_fp_template_t reality_fp_chrome;

/**
 * Build a ClientHello handshake message from the template with per-connection
 * values patched: random, session_id(auth), key shares, SNI.
 * Caller frees *a_out. SNI shifts post-SNI offsets -- handled internally.
 */
int reality_fp_build_clienthello(const reality_fp_template_t *a_fp,
                                 const uint8_t a_random[32],
                                 const uint8_t a_session_id[32],
                                 const uint8_t a_mlkem_ek[1184],
                                 const uint8_t a_hybrid_x25519_pub[32],  /* hybrid tail: TLS ECDH */
                                 const uint8_t a_auth_x25519_pub[32],    /* plain share: REALITY auth */
                                 const char *a_sni,
                                 uint8_t **a_out, size_t *a_out_size);

/* ------------------------------------------------------------------ */
/* Auth (core/reality_auth.c)                                          */
/* ------------------------------------------------------------------ */

/* Base64url decode / hex parse helpers (port-free) */
int reality_b64url_decode(const char *a_in, uint8_t *a_out, size_t a_out_cap, size_t *a_out_len);
int reality_hex_decode(const char *a_in, uint8_t *a_out, size_t a_out_cap, size_t *a_out_len);
int reality_uuid_parse(const char *a_str, uint8_t a_out[16]);

/**
 * Seal the auth data into session_id[32]:
 *   authPlain[0:16] = {ver_x, ver_y, ver_z, 0, time32BE, shortId...}
 *   shared   = X25519(a_eph_priv, server_static_pub)
 *   authKey  = HKDF-SHA256(ikm=shared, salt=hello.random[:20], info="REALITY", 32)
 *   sealed   = AES-256-GCM(authKey, nonce=hello.random[20:32], pt=authPlain,
 *                          aad=clienthello_with_session_id_zeroed)
 */
int reality_auth_seal(reality_session_t *a_s,
                      const uint8_t a_eph_priv[32],
                      const uint8_t a_server_static_pub[32],
                      const char *a_short_id_hex,
                      const uint8_t *a_clienthello_with_zero_sid,
                      size_t a_ch_len,
                      const uint8_t a_random[32],
                      uint8_t a_out_session_id[32]);

/** Verify server certificate evidence: HMAC-SHA512(authKey, ed25519_pub) == sig. */
int reality_auth_verify_server_cert(reality_session_t *a_s,
                                    const uint8_t *a_cert_der, size_t a_cert_len);

/* ------------------------------------------------------------------ */
/* VLESS codec (core/vless_codec.c)                                    */
/* ------------------------------------------------------------------ */

int vless_build_request(const uint8_t a_uuid[16], const char *a_host, uint16_t a_port,
                        int a_command_tcp /*1*/, uint8_t *a_out, size_t a_cap, size_t *a_out_len);

/* ------------------------------------------------------------------ */
/* Vision flow (core/vless_vision.c)                                   */
/* ------------------------------------------------------------------ */

typedef struct vision_writer {
    uint8_t  uuid[16];
    bool     uuid_pending;      /* prepend once */
    bool     padding;           /* padding phase active */
    int      packets_to_filter; /* 8 initially */
    bool     inner_tls_seen;
} vision_writer_t;

typedef struct vision_reader {
    uint8_t  uuid[16];
    bool     uuid_checked;      /* initial state */
    int32_t  remaining_command; /* -1 initial */
    int32_t  remaining_content;
    int32_t  remaining_padding;
    uint8_t  current_command;
    bool     direct;            /* after command 0x02 */
} vision_reader_t;

void vision_writer_init(vision_writer_t *a_w, const uint8_t a_uuid[16]);
void vision_reader_init(vision_reader_t *a_r, const uint8_t a_uuid[16]);

/**
 * Wrap payload -> vision frames. a_out must hold a_len + 21 + 16.
 * Returns wrapped length.
 */
size_t vision_write(vision_writer_t *a_w, const uint8_t *a_src, size_t a_len,
                    uint8_t *a_dst, size_t a_dst_cap);

/**
 * Extract payload from vision frames (stateful, partial-input safe).
 * Writes consumed bytes to *a_consumed, payload to a_dst.
 * Returns payload length >= 0, or -1 on protocol violation.
 */
int64_t vision_read(vision_reader_t *a_r, const uint8_t *a_src, size_t a_len,
                    uint8_t *a_dst, size_t a_dst_cap, size_t *a_consumed);

/* ------------------------------------------------------------------ */
/* Rate limiter (core/rate_limiter.c)                                  */
/* ------------------------------------------------------------------ */

typedef struct rate_limiter {
    uint64_t rate_bps;      /* bytes per second, 0 = off */
    uint64_t tokens;
    uint64_t last_mono_ms;
} rate_limiter_t;

void    rate_limiter_init(rate_limiter_t *a_rl, uint64_t a_rate_bps);
bool    rate_limiter_allow(rate_limiter_t *a_rl, uint64_t a_now_mono_ms, size_t a_len);
uint64_t rate_limiter_delay_ms(rate_limiter_t *a_rl, uint64_t a_now_mono_ms, size_t a_len);

/* ------------------------------------------------------------------ */
/* Session internals (core/reality_tls13.c)                            */
/* ------------------------------------------------------------------ */

struct reality_session {
    reality_config_t       cfg;
    const reality_io_ops_t    *io;
    const reality_crypto_ops_t *crypto;
    const reality_time_ops_t  *time;
    const reality_log_ops_t   *log;

    /* auth-derived */
    uint8_t  auth_key[32];

    /* tls 1.3 state */
    uint8_t  x25519_priv[32];         /* our plain-X25519 share priv (auth ECDH) */
    uint8_t  hybrid_priv[32];         /* hybrid-tail X25519 priv (session ECDH) */
    uint8_t  mlkem_dk[2400];
    uint8_t  client_random[32];
    int      cipher;                  /* 0x1301/0x1302/0x1303 */
    int      hash384;
    /* secrets (max 48) */
    uint8_t  c_hs_secret[48], s_hs_secret[48];
    uint8_t  c_ap_secret[48], s_ap_secret[48];
    uint8_t  c_key[32], s_key[32];
    size_t   c_key_len, s_key_len;
    int      c_algo, s_algo;          /* 0=aes-gcm, 1=chacha */
    uint8_t  c_iv[12],  s_iv[12];
    uint64_t c_seq, s_seq;
    bool     handshake_done;

    /* vless/vision */
    vision_writer_t vw;
    vision_reader_t vr;
    rate_limiter_t  rl;
    bool     resp_hdr_done;         /* VLESS response header consumed lazily */

    /* buffering */
    uint8_t  inbuf[16384 + 256];
    size_t   inbuf_len;
    uint8_t  rawbuf[16384 + 64];      /* decrypted TLS payload, not yet consumed */
    size_t   rawbuf_len, rawbuf_off;
    uint8_t  appbuf[16384 + 64];      /* vision-decoded payload leftover */
    size_t   appbuf_len, appbuf_off;

    int      err;
    char     errmsg[256];
};

/* TLS helpers used by the session flow */
int rtls_handshake(reality_session_t *a_s, const char *a_sni);
int rtls_write_app(reality_session_t *a_s, const uint8_t *a_buf, size_t a_len);
int rtls_read_app(reality_session_t *a_s, uint8_t *a_buf, size_t a_cap, size_t *a_out_len);
int rtls_close_notify(reality_session_t *a_s);   /* best effort */
int record_read_raw_hs(reality_session_t *a_s, uint8_t *a_buf, size_t a_cap, size_t *a_len);

#endif /* REALITY_INTERNAL_H */
