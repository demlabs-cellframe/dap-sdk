/**
 * @file trojan.h
 * @brief Trojan transport — public API + dependency-injection ports.
 *
 * Trojan (trojan-gfw): TLS + a fixed request header
 *   hex(SHA224(password)) CRLF CMD ATYP ADDR PORT CRLF
 * then raw bidirectional payload. The server (Xray trojan inbound behind a
 * front) forwards the payload to ADDR:PORT (the node's HTTP-DAP listener).
 *
 * Isolation mirrors REALITY (plan §2): the core is SDK-free and depends only
 * on the injected io/tls ports below; the adapter is the only SDK-aware file.
 * The password is provisioned (never hardcoded); the adapter hashes it with
 * SHA224 and passes password_hash_hex to the core.
 */

#ifndef TROJAN_H
#define TROJAN_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>
#ifdef _MSC_VER
typedef intptr_t ssize_t;   /* MSVC lacks ssize_t; MinGW/POSIX provide it */
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define TROJAN_ERR_NONE     0
#define TROJAN_ERR_PARAM  (-1)
#define TROJAN_ERR_IO     (-2)
#define TROJAN_ERR_TLS    (-3)
#define TROJAN_ERR_CLOSED (-4)
#define TROJAN_ERR_AGAIN  (-5)
#define TROJAN_ERR_NOMEM  (-6)
#define TROJAN_ERR_VERIFY (-7)

#define TROJAN_HASH_HEX   56     /* SHA224 = 28 bytes = 56 hex chars */
#define TROJAN_HOST_MAX   256

/* IO port: blocking byte transport (TCP). Same shape as carrier_io_ops. */
typedef struct trojan_io_ops {
    void *user;
    int  (*read)(void *a_user, uint8_t *a_buf, size_t a_len, size_t *a_out_len);
    int  (*write)(void *a_user, const uint8_t *a_buf, size_t a_len);
    void (*close)(void *a_user);
    int  (*fd)(void *a_user);
    void (*shutdown)(void *a_user);
} trojan_io_ops_t;

/* TLS port: blocking TLS handshake + record IO over a connected fd.
 * connect returns an opaque TLS handle or NULL on failure. read/write return
 * >0 bytes, 0 EOF, negative TROJAN_ERR_* (AGAIN on want-read/write). */
typedef struct trojan_tls_cfg {
    const char *sni;
    const uint8_t *spki_sha256;   /* 32 raw bytes or NULL */
    bool have_pin;
    int hello_profile;            /* dap_tls_hello_profile_t value */
    const char *alpn;             /* e.g. "http/1.1" */
} trojan_tls_cfg_t;

typedef struct trojan_tls_ops {
    void *user;
    void *(*connect)(void *a_user, int a_fd, const trojan_tls_cfg_t *a_cfg);
    ssize_t (*read)(void *a_user, void *a_tls, void *a_buf, size_t a_len);
    ssize_t (*write)(void *a_user, void *a_tls, const void *a_buf, size_t a_len);
    int (*pending)(void *a_user, void *a_tls);
    void (*close)(void *a_user, void *a_tls);
} trojan_tls_ops_t;

typedef struct trojan_config {
    char password_hash_hex[TROJAN_HASH_HEX + 1];  /* hex(SHA224(password)) */
    char sni[TROJAN_HOST_MAX];
    uint8_t spki_sha256[32];
    bool have_pin;
    int hello_profile;
    char dest_host[TROJAN_HOST_MAX];   /* CONNECT target (node listener) */
    uint16_t dest_port;
} trojan_config_t;

typedef struct trojan_session trojan_session_t;

/* Establish TLS over io, send the Trojan request header. NULL on failure. */
trojan_session_t *trojan_session_connect(const trojan_config_t *a_cfg,
                                         trojan_io_ops_t *a_io,
                                         const trojan_tls_ops_t *a_tls);
/* Read decrypted payload. 0 ok (*out bytes, 0 on EOF), TROJAN_ERR_* otherwise. */
int trojan_session_read(trojan_session_t *a_s, uint8_t *a_buf, size_t a_len, size_t *a_out);
int trojan_session_write(trojan_session_t *a_s, const uint8_t *a_buf, size_t a_len);
bool trojan_session_has_pending(trojan_session_t *a_s);
void trojan_session_close(trojan_session_t *a_s);

/* ---- codec (SDK-free, unit-testable) ---- */
/* Build the Trojan request header into a_buf (>= 56+2+1+1+256+2+2 bytes).
 * Returns header length, or negative TROJAN_ERR_*. dest_host is IPv4 or domain. */
int trojan_codec_build_request(const char *a_hash_hex, const char *a_dest_host,
                               uint16_t a_dest_port, uint8_t *a_buf, size_t a_buf_size);

#ifdef __cplusplus
}
#endif

#endif /* TROJAN_H */