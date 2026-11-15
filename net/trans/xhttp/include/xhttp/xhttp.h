/**
 * @file xhttp.h
 * @brief XHTTP (Xray splithttp) transport — public API + DI ports (SDK-free core
 *        except the nghttp2-backed session). Carries VLESS over HTTP/2 in
 *        "packet-up" mode: a GET download stream + POST upload packets, keyed
 *        by a session id, with browser-like headers and Referer padding.
 *
 * Single stream for the DAP carrier. The VLESS request header (dest) is sent
 * as the first upload packet; the VLESS response header is stripped from the
 * download stream before payload is delivered to the carrier.
 *
 * The uuid/password is provisioned, never hardcoded.
 */

#ifndef XHTTP_H
#define XHTTP_H

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

#define XHTTP_ERR_NONE     0
#define XHTTP_ERR_PARAM  (-1)
#define XHTTP_ERR_IO     (-2)
#define XHTTP_ERR_TLS    (-3)
#define XHTTP_ERR_CLOSED (-4)
#define XHTTP_ERR_AGAIN  (-5)
#define XHTTP_ERR_NOMEM  (-6)
#define XHTTP_ERR_PROTO  (-7)

#define XHTTP_HOST_MAX 256
#define XHTTP_PATH_MAX 256

typedef struct xhttp_io_ops {
    void *user;
    int  (*read)(void *a_user, uint8_t *a_buf, size_t a_len, size_t *a_out_len);
    int  (*write)(void *a_user, const uint8_t *a_buf, size_t a_len);
    void (*close)(void *a_user);
    int  (*fd)(void *a_user);
    void (*shutdown)(void *a_user);
} xhttp_io_ops_t;

typedef struct xhttp_tls_cfg {
    const char *sni;
    const uint8_t *spki_sha256;
    bool have_pin;
    int hello_profile;
    const char *alpn;   /* "h2" */
} xhttp_tls_cfg_t;

typedef struct xhttp_tls_ops {
    void *user;
    void *(*connect)(void *a_user, int a_fd, const xhttp_tls_cfg_t *a_cfg);
    ssize_t (*read)(void *a_user, void *a_tls, void *a_buf, size_t a_len);
    ssize_t (*write)(void *a_user, void *a_tls, const void *a_buf, size_t a_len);
    int (*pending)(void *a_user, void *a_tls);
    void (*close)(void *a_user, void *a_tls);
} xhttp_tls_ops_t;

typedef struct xhttp_config {
    uint8_t uuid[16];                 /* VLESS UUID (binary) */
    char host[XHTTP_HOST_MAX];        /* HTTP Host / :authority */
    char path[XHTTP_PATH_MAX];        /* base path, e.g. /api/v1/data */
    char sni[XHTTP_HOST_MAX];
    uint8_t spki_sha256[32];
    bool have_pin;
    int hello_profile;
    char dest_host[XHTTP_HOST_MAX];   /* VLESS CONNECT target */
    uint16_t dest_port;
} xhttp_config_t;

typedef struct xhttp_session xhttp_session_t;

xhttp_session_t *xhttp_session_connect(const xhttp_config_t *a_cfg,
                                       xhttp_io_ops_t *a_io,
                                       const xhttp_tls_ops_t *a_tls);
int xhttp_session_read(xhttp_session_t *a_s, uint8_t *a_buf, size_t a_len, size_t *a_out);
int xhttp_session_write(xhttp_session_t *a_s, const uint8_t *a_buf, size_t a_len);
bool xhttp_session_has_pending(xhttp_session_t *a_s);
void xhttp_session_close(xhttp_session_t *a_s);

/* ---- VLESS codec (SDK-free) ---- */
/* Build VLESS request header: ver(0) uuid(16) addons_len(0) cmd(1) port(2) atyp addr. */
int xhttp_codec_build_vless_request(const uint8_t a_uuid[16], const char *a_dest_host,
                                    uint16_t a_dest_port, uint8_t *a_buf, size_t a_buf_size);

#ifdef __cplusplus
}
#endif

#endif /* XHTTP_H */