/**
 * @file shadowtls.h
 * @brief ShadowTLS v3 transport — public API + DI io port.
 *
 * ShadowTLS v3 (ihciah/shadow-tls): the TLS handshake is relayed by the server
 * to a real "mask" site (e.g. www.samsung.com), so the client sees a genuine
 * public certificate. The client signs its ClientHello SessionID with
 * HMAC-SHA1(password); the server authenticates it and hijacks the return
 * stream. After the handshake, data flows as raw TLS ApplicationData records
 * carrying [4-byte streaming HMAC-SHA1][plaintext] (C/S keyed by ServerRandom).
 *
 * Resists traffic signature detection, active probing and hijacking. The
 * password is provisioned, never hardcoded. TLS 1.3 (strict) is used.
 *
 * The session uses OpenSSL directly (memory-BIO record pump); the adapter is
 * the SDK-aware glue (carrier bridge + carrier_dap).
 */

#ifndef SHADOWTLS_H
#define SHADOWTLS_H

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

#define SHADOWTLS_ERR_NONE     0
#define SHADOWTLS_ERR_PARAM  (-1)
#define SHADOWTLS_ERR_IO     (-2)
#define SHADOWTLS_ERR_TLS    (-3)
#define SHADOWTLS_ERR_CLOSED (-4)
#define SHADOWTLS_ERR_AGAIN  (-5)
#define SHADOWTLS_ERR_NOMEM  (-6)
#define SHADOWTLS_ERR_AUTH   (-7)

#define SHADOWTLS_HOST_MAX 256

typedef struct shadowtls_io_ops {
    void *user;
    int  (*read)(void *a_user, uint8_t *a_buf, size_t a_len, size_t *a_out_len);
    int  (*write)(void *a_user, const uint8_t *a_buf, size_t a_len);
    void (*close)(void *a_user);
    int  (*fd)(void *a_user);
    void (*shutdown)(void *a_user);
} shadowtls_io_ops_t;

typedef struct shadowtls_config {
    char password[128];
    char sni[SHADOWTLS_HOST_MAX];      /* mask site SNI (real cert) */
    char dest_host[SHADOWTLS_HOST_MAX];/* proxy target (node listener) */
    uint16_t dest_port;
} shadowtls_config_t;

typedef struct shadowtls_session shadowtls_session_t;

shadowtls_session_t *shadowtls_session_connect(const shadowtls_config_t *a_cfg,
                                               shadowtls_io_ops_t *a_io);
int shadowtls_session_read(shadowtls_session_t *a_s, uint8_t *a_buf, size_t a_len, size_t *a_out);
int shadowtls_session_write(shadowtls_session_t *a_s, const uint8_t *a_buf, size_t a_len);
bool shadowtls_session_has_pending(shadowtls_session_t *a_s);
void shadowtls_session_close(shadowtls_session_t *a_s);

#ifdef __cplusplus
}
#endif

#endif /* SHADOWTLS_H */