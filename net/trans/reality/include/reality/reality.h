/**
 * @file reality.h
 * @brief VLESS+Reality client transport -- public API (Track B, dap-sdk net/trans).
 *
 * Wire-compatible with Xray-core v26.10.31 REALITY server (pinned tag,
 * commit a4d3dff5b20c2faae567b742c172c7b95e75c083).
 *
 * SOLID isolation: the core depends ONLY on the injectable ports below
 * (io / crypto / time / log). No dap-sdk or cellframe headers anywhere in
 * core/ or port/. The only SDK-aware code lives in adapter/.
 */

#ifndef REALITY_H
#define REALITY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REALITY_ERR_NONE        0
#define REALITY_ERR_PARAM     (-1)
#define REALITY_ERR_IO        (-2)
#define REALITY_ERR_CRYPTO    (-3)
#define REALITY_ERR_PROTOCOL  (-4)
#define REALITY_ERR_VERIFY    (-5)   /* server identity check failed (MITM/fallback) */
#define REALITY_ERR_CLOSED    (-6)
#define REALITY_ERR_NOMEM     (-7)
#define REALITY_ERR_LIMITED   (-8)   /* rate limiter */
#define REALITY_ERR_AGAIN     (-9)   /* io port: no data now (SO_RCVTIMEO/EAGAIN) */

#define REALITY_SNI_MAX       255
#define REALITY_SHORTID_MAX    8
#define REALITY_KEY_B64URL    64     /* base64url-encoded X25519 public key, NUL-terminated */
#define REALITY_UUID_STR      36     /* "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" */

/* ------------------------------------------------------------------ */
/* Ports (Dependency Inversion)                                        */
/* ------------------------------------------------------------------ */

/** IO port. Blocking socket semantics; tests replace with memory buffers. */
typedef struct reality_io_ops {
    void *user;                                   /* passed to every call */
    /* returns bytes read (>=0) or REALITY_ERR_IO; 0 with *a_out_len==0 means EOF */
    int  (*read)(void *a_user, uint8_t *a_buf, size_t a_len, size_t *a_out_len);
    int  (*write)(void *a_user, const uint8_t *a_buf, size_t a_len);
    void (*close)(void *a_user);
} reality_io_ops_t;

/** Crypto port. All primitives needed by the protocol (OpenSSL impl in port/). */
typedef struct reality_crypto_ops {
    void *user;
    int  (*rand_bytes)(void *a_user, uint8_t *a_out, size_t a_len);
    /* X25519: ephemeral keypair / shared secret */
    int  (*x25519_keypair)(void *a_user, uint8_t a_out_pub[32], uint8_t a_out_priv[32]);
    int  (*x25519_shared)(void *a_user, uint8_t a_out_shared[32],
                          const uint8_t a_priv[32], const uint8_t a_peer_pub[32]);
    /* HKDF-SHA256/SHA384: extract+expand in one shot (RFC 5869) */
    int  (*hkdf)(void *a_user, int a_sha384,
                 const uint8_t *a_salt, size_t a_salt_len,
                 const uint8_t *a_ikm, size_t a_ikm_len,
                 const uint8_t *a_info, size_t a_info_len,
                 uint8_t *a_out, size_t a_out_len);
    /* AEAD for TLS 1.3 records: a_algo 0=AES-GCM(key 16/32B), 1=ChaCha20-Poly1305(32B).
     * a_dst may alias a_src. */
    int  (*aead_seal)(void *a_user, int a_algo,
                      const uint8_t *a_key, size_t a_key_len,
                      const uint8_t a_nonce[12],
                      const uint8_t *a_aad, size_t a_aad_len,
                      const uint8_t *a_src, size_t a_src_len,
                      uint8_t *a_dst, size_t *a_dst_len /* src_len+16 */);
    int  (*aead_open)(void *a_user, int a_algo,
                      const uint8_t *a_key, size_t a_key_len,
                      const uint8_t a_nonce[12],
                      const uint8_t *a_aad, size_t a_aad_len,
                      const uint8_t *a_src, size_t a_src_len,
                      uint8_t *a_dst, size_t *a_dst_len /* src_len-16 */);
    /* HMAC: a_hash 0=SHA256, 1=SHA384, 2=SHA512 (server identity evidence) */
    int  (*hmac)(void *a_user, int a_hash,
                 const uint8_t *a_key, size_t a_key_len,
                 const uint8_t *a_msg, size_t a_msg_len, uint8_t *a_out /* 64 max */);
    /* hash one shot: same a_hash codes */
    int  (*hash)(void *a_user, int a_hash,
                 const uint8_t *a_msg, size_t a_msg_len, uint8_t *a_out);
    /* ed25519 signature verify over raw message (cert CertificateVerify) */
    int  (*ed25519_verify)(void *a_user, const uint8_t a_pub[32],
                           const uint8_t *a_msg, size_t a_msg_len,
                           const uint8_t a_sig[64]);
    /* ML-KEM-768 (FIPS 203). sk 2400B(packed) for BoringSSL-parity sizes:
     * PQClean uses 1184/1088/32/2400 -- enc/dec via packed keys. */
    int  (*mlkem_keypair)(void *a_user,
                          uint8_t a_out_ek[1184], uint8_t a_out_dk[2400]);
    int  (*mlkem_encaps)(void *a_user, const uint8_t a_ek[1184],
                         uint8_t a_out_ct[1088], uint8_t a_out_ss[32]);
    int  (*mlkem_decaps)(void *a_user, const uint8_t a_ct[1088],
                         const uint8_t a_dk[2400], uint8_t a_out_ss[32]);
} reality_crypto_ops_t;

/** Time port (tests substitute for replay-window checks). */
typedef struct reality_time_ops {
    void *user;
    int64_t (*unix_now)(void *a_user);            /* seconds */
    uint64_t (*mono_ms)(void *a_user);            /* monotonic milliseconds */
    void (*sleep_ms)(void *a_user, uint64_t a_ms);/* optional (rate limiter) */
} reality_time_ops_t;

/** Log port. */
typedef struct reality_log_ops {
    void *user;
    void (*log)(void *a_user, int a_level /*0 dbg,1 info,2 warn,3 err*/, const char *a_fmt, ...);
} reality_log_ops_t;

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

typedef struct reality_config {
    char     sni[REALITY_SNI_MAX + 1];            /* e.g. "www.samsung.com" */
    char     public_key_b64url[REALITY_KEY_B64URL + 1];
    char     short_id_hex[2 * REALITY_SHORTID_MAX + 1]; /* e.g. "b7598d95" */
    char     uuid[REALITY_UUID_STR + 1];
    int64_t  max_time_diff;                       /* sec; server-side window mirror, 0 = default 90 */
    uint32_t rate_limit_bytes_per_sec;            /* 0 = unlimited; DPI data: 5-6 MB/s */
    size_t   max_record;                          /* 0 = default 16384 */
} reality_config_t;

/* ------------------------------------------------------------------ */
/* Session                                                             */
/* ------------------------------------------------------------------ */

typedef struct reality_session reality_session_t;

/**
 * @brief Full client connect: TCP(assume connected io) -> REALITY TLS 1.3 ->
 *        VLESS (flow xtls-rprx-vision) -> ready for payload IO.
 * @param a_dest_host / a_dest_port  VLESS destination (e.g. "127.0.0.1", 8079).
 * @return session or NULL (invalid params / handshake failure).
 */
reality_session_t *reality_session_connect(const reality_config_t *a_cfg,
                                           const reality_io_ops_t *a_io,
                                           const reality_crypto_ops_t *a_crypto,
                                           const reality_time_ops_t *a_time,
                                           const reality_log_ops_t *a_log,
                                           const char *a_dest_host, uint16_t a_dest_port);

/** Payload IO after connect. Vision framing handled inside. */
int  reality_session_write(reality_session_t *a_s, const uint8_t *a_buf, size_t a_len);
int  reality_session_read(reality_session_t *a_s, uint8_t *a_buf, size_t a_cap, size_t *a_out_len);
void reality_session_close(reality_session_t *a_s);   /* graceful close_notify + io close */

/**
 * @brief True when payload can be produced WITHOUT new socket IO.
 *
 * The record layer reads as much as the socket offers into an internal buffer,
 * so one recv() may contain several TLS records while a single
 * reality_session_read() returns only the first one. Consumers that gate reads
 * on socket readiness (poll/epoll/kqueue) MUST also check this flag, otherwise
 * the already-buffered records are stranded until an unrelated socket event
 * arrives (observed as a multi-second stall in the middle of a response).
 *
 * Reports: undelivered decoded payload, undecoded plaintext, or at least one
 * complete TLS record sitting in the input buffer. A partial record is NOT
 * reported (reading it would block).
 */
bool reality_session_has_pending(const reality_session_t *a_s);

/* diagnostics */
const char *reality_last_error(reality_session_t *a_s);

#ifdef __cplusplus
}
#endif

#endif /* REALITY_H */
