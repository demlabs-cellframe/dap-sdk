/**
 * @file anytls.h
 * @brief AnyTLS transport — public API + DI ports (SDK-free core).
 *
 * AnyTLS (anytls-go reference): TLS + auth(sha256(password)+padding0) + a
 * session frame layer (command/streamId/len/data) with multiplexing and a
 * flexible padding scheme to mitigate TLS-in-TLS fingerprinting.
 *
 * This implementation carries a SINGLE stream (streamId=1) for the VPN DAP
 * carrier (the carrier_dap phase machine drives one byte stream). It speaks
 * protocol version 2 (cmdSettings/SYNACK/heartbeat/serverSettings) and handles
 * the server padding-scheme update. The password is provisioned, never
 * hardcoded; the adapter hashes it (SHA256) into password_hash.
 *
 * Isolation mirrors REALITY/Trojan: core is SDK-free behind io/tls ports;
 * the adapter is the only SDK-aware glue.
 */

#ifndef ANYTLS_H
#define ANYTLS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>
#ifdef _MSC_VER
typedef intptr_t ssize_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define ANYTLS_ERR_NONE     0
#define ANYTLS_ERR_PARAM  (-1)
#define ANYTLS_ERR_IO     (-2)
#define ANYTLS_ERR_TLS    (-3)
#define ANYTLS_ERR_CLOSED (-4)
#define ANYTLS_ERR_AGAIN  (-5)
#define ANYTLS_ERR_NOMEM  (-6)
#define ANYTLS_ERR_PROTO  (-7)

#define ANYTLS_HOST_MAX 256

/* IO port (blocking TCP) — same shape as carrier_io_ops_t. */
typedef struct anytls_io_ops {
    void *user;
    int  (*read)(void *a_user, uint8_t *a_buf, size_t a_len, size_t *a_out_len);
    int  (*write)(void *a_user, const uint8_t *a_buf, size_t a_len);
    void (*close)(void *a_user);
    int  (*fd)(void *a_user);
    void (*shutdown)(void *a_user);
} anytls_io_ops_t;

/* TLS port (blocking) — same shape as trojan_tls_ops_t. */
typedef struct anytls_tls_cfg {
    const char *sni;
    const uint8_t *spki_sha256;
    bool have_pin;
    int hello_profile;
    const char *alpn;   /* AnyTLS: NULL (no ALPN) */
} anytls_tls_cfg_t;

typedef struct anytls_tls_ops {
    void *user;
    void *(*connect)(void *a_user, int a_fd, const anytls_tls_cfg_t *a_cfg);
    ssize_t (*read)(void *a_user, void *a_tls, void *a_buf, size_t a_len);
    ssize_t (*write)(void *a_user, void *a_tls, const void *a_buf, size_t a_len);
    int (*pending)(void *a_user, void *a_tls);
    void (*close)(void *a_user, void *a_tls);
} anytls_tls_ops_t;

typedef struct anytls_config {
    uint8_t password_hash[32];        /* SHA256(password), raw */
    char sni[ANYTLS_HOST_MAX];
    uint8_t spki_sha256[32];
    bool have_pin;
    int hello_profile;
    char dest_host[ANYTLS_HOST_MAX];  /* CONNECT target (SocksAddr) */
    uint16_t dest_port;
    char padding_md5[33];             /* md5 of default padding scheme (hex) */
} anytls_config_t;

typedef struct anytls_session anytls_session_t;

/* TLS + auth + cmdSettings + cmdSYN(stream 1) + wait SYNACK. NULL on failure. */
anytls_session_t *anytls_session_connect(const anytls_config_t *a_cfg,
                                         anytls_io_ops_t *a_io,
                                         const anytls_tls_ops_t *a_tls);
/* Read stream-1 payload (control frames handled internally). */
int anytls_session_read(anytls_session_t *a_s, uint8_t *a_buf, size_t a_len, size_t *a_out);
int anytls_session_write(anytls_session_t *a_s, const uint8_t *a_buf, size_t a_len);
bool anytls_session_has_pending(anytls_session_t *a_s);
void anytls_session_close(anytls_session_t *a_s);

/* ---- codec (SDK-free, unit-testable) ---- */
/* Default padding scheme (protocol constant, from the AnyTLS spec). */
extern const char *ANYTLS_DEFAULT_PADDING_SCHEME;

/* Build auth blob: sha256(32) + padding0_len(u16 BE) + padding0. Returns len. */
int anytls_codec_build_auth(const uint8_t a_hash[32], uint16_t a_pad0_len,
                            uint8_t *a_buf, size_t a_buf_size);
/* Build a session frame: cmd(u8) + streamId(u32 BE) + len(u16 BE) + data. */
int anytls_codec_build_frame(uint8_t a_cmd, uint32_t a_stream_id,
                             const uint8_t *a_data, size_t a_data_len,
                             uint8_t *a_buf, size_t a_buf_size);
/* Build SocksAddr (ATYP + addr + port BE) for dest. Returns len, <0 on error. */
int anytls_codec_build_socksaddr(const char *a_host, uint16_t a_port,
                                 uint8_t *a_buf, size_t a_buf_size);

#ifdef __cplusplus
}
#endif

#endif /* ANYTLS_H */