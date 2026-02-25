/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * DAP TLS Platform Abstraction Layer
 *
 * Provides a unified TLS API across platforms:
 *   Linux/FreeBSD:  OpenSSL / LibreSSL
 *   macOS/iOS:      Secure Transport
 *   Windows:        SChannel
 *   Android:        BoringSSL (OpenSSL-compatible)
 *
 * Architecture mirrors source/tun/ -- internal API + platform-specific backends.
 * CMake auto-detects the system SSL library and selects the correct backend.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef DAP_OS_UNIX
#include <sys/types.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dap_tls_context  dap_tls_context_t;
typedef struct dap_tls_session  dap_tls_session_t;

typedef enum dap_tls_error {
    DAP_TLS_ERR_NONE        = 0,
    DAP_TLS_ERR_WANT_READ   = 1,
    DAP_TLS_ERR_WANT_WRITE  = 2,
    DAP_TLS_ERR_SYSCALL     = 3,
    DAP_TLS_ERR_SSL         = 4,
    DAP_TLS_ERR_CLOSED      = 5,
    DAP_TLS_ERR_NOMEM       = 6,
    DAP_TLS_ERR_INVAL       = 7,
    DAP_TLS_ERR_NOT_IMPL    = 8
} dap_tls_error_t;

int dap_tls_init(void);
void dap_tls_deinit(void);
const char *dap_tls_backend_name(void);

dap_tls_context_t *dap_tls_context_new_client(void);
dap_tls_context_t *dap_tls_context_new_server(const char *a_cert_file,
                                               const char *a_key_file);
void dap_tls_context_free(dap_tls_context_t *a_ctx);

int dap_tls_context_set_verify(dap_tls_context_t *a_ctx, bool a_verify);
int dap_tls_context_set_ciphers(dap_tls_context_t *a_ctx, const char *a_ciphers);
int dap_tls_context_load_ca(dap_tls_context_t *a_ctx,
                             const char *a_ca_file, const char *a_ca_dir);
int dap_tls_context_set_alpn(dap_tls_context_t *a_ctx,
                              const uint8_t *a_protos, uint32_t a_protos_len);
int dap_tls_context_set_min_version(dap_tls_context_t *a_ctx, uint16_t a_version);

dap_tls_session_t *dap_tls_session_new(dap_tls_context_t *a_ctx);
void dap_tls_session_free(dap_tls_session_t *a_session);
int dap_tls_session_set_fd(dap_tls_session_t *a_session, int a_fd);
int dap_tls_session_set_hostname(dap_tls_session_t *a_session,
                                  const char *a_hostname);
void dap_tls_session_set_nonblocking(dap_tls_session_t *a_session,
                                      bool a_nonblocking);

int dap_tls_session_connect(dap_tls_session_t *a_session);
int dap_tls_session_accept(dap_tls_session_t *a_session);

ssize_t dap_tls_session_read(dap_tls_session_t *a_session,
                              void *a_buf, size_t a_size);
ssize_t dap_tls_session_write(dap_tls_session_t *a_session,
                               const void *a_data, size_t a_size);

int dap_tls_session_shutdown(dap_tls_session_t *a_session);

dap_tls_error_t dap_tls_session_get_error(dap_tls_session_t *a_session, int a_ret);
const char *dap_tls_error_string(dap_tls_error_t a_err);
const char *dap_tls_session_get_cipher(dap_tls_session_t *a_session);
const char *dap_tls_session_get_version(dap_tls_session_t *a_session);

#ifdef __cplusplus
}
#endif
