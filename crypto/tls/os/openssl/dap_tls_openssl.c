/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * DAP TLS -- OpenSSL backend
 *
 * Implements the dap_tls.h API using OpenSSL 1.1+ / 3.x.
 * Used on: Linux, FreeBSD, Android (BoringSSL is API-compatible), generic Unix.
 */

#include <string.h>
#include <errno.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

#include "dap_common.h"
#include "dap_tls.h"

#define LOG_TAG "dap_tls_openssl"

static bool s_debug_more = false;
struct dap_tls_context {
    SSL_CTX *ssl_ctx;
};

struct dap_tls_session {
    dap_tls_context_t *ctx;
    SSL *ssl;
    bool nonblocking;
};

/* ---- Global lifecycle --------------------------------------------------- */

int dap_tls_init(void)
{
    if (OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS
                         | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL) == 0) {
        log_it(L_ERROR, "OPENSSL_init_ssl failed");
        return -1;
    }
    log_it(L_INFO, "TLS backend initialized: %s", OpenSSL_version(OPENSSL_VERSION));
    return 0;
}

void dap_tls_deinit(void)
{
    debug_if(s_debug_more, L_DEBUG, "TLS backend deinitialized");
}

const char *dap_tls_backend_name(void)
{
    return "OpenSSL";
}

/* ---- Context ------------------------------------------------------------ */

dap_tls_context_t *dap_tls_context_new_client(void)
{
    SSL_CTX *l_ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!l_ssl_ctx) {
        log_it(L_ERROR, "SSL_CTX_new(TLS_client_method) failed");
        return NULL;
    }
    SSL_CTX_set_min_proto_version(l_ssl_ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(l_ssl_ctx, TLS1_3_VERSION);
    SSL_CTX_set_verify(l_ssl_ctx, SSL_VERIFY_NONE, NULL);
    SSL_CTX_set_session_cache_mode(l_ssl_ctx, SSL_SESS_CACHE_CLIENT);

    dap_tls_context_t *l_ctx = DAP_NEW_Z(dap_tls_context_t);
    if (!l_ctx) {
        SSL_CTX_free(l_ssl_ctx);
        return NULL;
    }
    l_ctx->ssl_ctx = l_ssl_ctx;
    return l_ctx;
}

dap_tls_context_t *dap_tls_context_new_server(const char *a_cert_file,
                                               const char *a_key_file)
{
    if (!a_cert_file || !a_key_file) {
        log_it(L_ERROR, "Server context requires cert and key files");
        return NULL;
    }
    SSL_CTX *l_ssl_ctx = SSL_CTX_new(TLS_server_method());
    if (!l_ssl_ctx) {
        log_it(L_ERROR, "SSL_CTX_new(TLS_server_method) failed");
        return NULL;
    }
    SSL_CTX_set_min_proto_version(l_ssl_ctx, TLS1_2_VERSION);
    SSL_CTX_set_max_proto_version(l_ssl_ctx, TLS1_3_VERSION);

    if (SSL_CTX_use_certificate_chain_file(l_ssl_ctx, a_cert_file) != 1) {
        log_it(L_ERROR, "Failed to load certificate: %s", a_cert_file);
        SSL_CTX_free(l_ssl_ctx);
        return NULL;
    }
    if (SSL_CTX_use_PrivateKey_file(l_ssl_ctx, a_key_file, SSL_FILETYPE_PEM) != 1) {
        log_it(L_ERROR, "Failed to load private key: %s", a_key_file);
        SSL_CTX_free(l_ssl_ctx);
        return NULL;
    }
    if (SSL_CTX_check_private_key(l_ssl_ctx) != 1) {
        log_it(L_ERROR, "Certificate and private key do not match");
        SSL_CTX_free(l_ssl_ctx);
        return NULL;
    }

    dap_tls_context_t *l_ctx = DAP_NEW_Z(dap_tls_context_t);
    if (!l_ctx) {
        SSL_CTX_free(l_ssl_ctx);
        return NULL;
    }
    l_ctx->ssl_ctx = l_ssl_ctx;
    return l_ctx;
}

void dap_tls_context_free(dap_tls_context_t *a_ctx)
{
    if (!a_ctx)
        return;
    if (a_ctx->ssl_ctx)
        SSL_CTX_free(a_ctx->ssl_ctx);
    DAP_DELETE(a_ctx);
}

int dap_tls_context_set_verify(dap_tls_context_t *a_ctx, bool a_verify)
{
    if (!a_ctx || !a_ctx->ssl_ctx)
        return -1;
    SSL_CTX_set_verify(a_ctx->ssl_ctx,
                       a_verify ? (SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT)
                                : SSL_VERIFY_NONE,
                       NULL);
    return 0;
}

int dap_tls_context_set_ciphers(dap_tls_context_t *a_ctx, const char *a_ciphers)
{
    if (!a_ctx || !a_ctx->ssl_ctx || !a_ciphers)
        return -1;
    if (SSL_CTX_set_ciphersuites(a_ctx->ssl_ctx, a_ciphers) != 1) {
        log_it(L_WARNING, "Failed to set TLS 1.3 ciphersuites, trying cipher_list");
        if (SSL_CTX_set_cipher_list(a_ctx->ssl_ctx, a_ciphers) != 1) {
            log_it(L_ERROR, "Failed to set cipher list: %s", a_ciphers);
            return -1;
        }
    }
    return 0;
}

int dap_tls_context_load_ca(dap_tls_context_t *a_ctx,
                             const char *a_ca_file, const char *a_ca_dir)
{
    if (!a_ctx || !a_ctx->ssl_ctx)
        return -1;
    if (!a_ca_file && !a_ca_dir) {
        if (SSL_CTX_set_default_verify_paths(a_ctx->ssl_ctx) != 1) {
            log_it(L_WARNING, "Failed to load default CA paths");
            return -1;
        }
        return 0;
    }
    if (SSL_CTX_load_verify_locations(a_ctx->ssl_ctx, a_ca_file, a_ca_dir) != 1) {
        log_it(L_ERROR, "Failed to load CA: file=%s dir=%s",
               a_ca_file ? a_ca_file : "(null)",
               a_ca_dir ? a_ca_dir : "(null)");
        return -1;
    }
    return 0;
}

int dap_tls_context_set_alpn(dap_tls_context_t *a_ctx,
                              const uint8_t *a_protos, uint32_t a_protos_len)
{
    if (!a_ctx || !a_ctx->ssl_ctx || !a_protos || a_protos_len == 0)
        return -1;
    if (SSL_CTX_set_alpn_protos(a_ctx->ssl_ctx, a_protos, a_protos_len) != 0) {
        log_it(L_WARNING, "Failed to set ALPN protocols");
        return -1;
    }
    return 0;
}

int dap_tls_context_set_min_version(dap_tls_context_t *a_ctx, uint16_t a_version)
{
    if (!a_ctx || !a_ctx->ssl_ctx)
        return -1;
    long l_ver;
    switch (a_version) {
        case 0x0303: l_ver = TLS1_2_VERSION; break;
        case 0x0304: l_ver = TLS1_3_VERSION; break;
        default:
            log_it(L_ERROR, "Unsupported TLS version: 0x%04x", a_version);
            return -1;
    }
    if (!SSL_CTX_set_min_proto_version(a_ctx->ssl_ctx, l_ver)) {
        log_it(L_ERROR, "Failed to set min TLS version 0x%04x", a_version);
        return -1;
    }
    return 0;
}

/* ---- Session ------------------------------------------------------------ */

dap_tls_session_t *dap_tls_session_new(dap_tls_context_t *a_ctx)
{
    if (!a_ctx || !a_ctx->ssl_ctx)
        return NULL;
    SSL *l_ssl = SSL_new(a_ctx->ssl_ctx);
    if (!l_ssl) {
        log_it(L_ERROR, "SSL_new failed");
        return NULL;
    }
    dap_tls_session_t *l_s = DAP_NEW_Z(dap_tls_session_t);
    if (!l_s) {
        SSL_free(l_ssl);
        return NULL;
    }
    l_s->ctx = a_ctx;
    l_s->ssl = l_ssl;
    return l_s;
}

void dap_tls_session_free(dap_tls_session_t *a_session)
{
    if (!a_session)
        return;
    if (a_session->ssl)
        SSL_free(a_session->ssl);
    DAP_DELETE(a_session);
}

int dap_tls_session_set_fd(dap_tls_session_t *a_session, int a_fd)
{
    if (!a_session || !a_session->ssl || a_fd < 0)
        return -1;
    if (SSL_set_fd(a_session->ssl, a_fd) != 1) {
        log_it(L_ERROR, "SSL_set_fd failed for fd=%d", a_fd);
        return -1;
    }
    return 0;
}

int dap_tls_session_set_hostname(dap_tls_session_t *a_session,
                                  const char *a_hostname)
{
    if (!a_session || !a_session->ssl || !a_hostname)
        return -1;
    if (SSL_set_tlsext_host_name(a_session->ssl, a_hostname) != 1) {
        log_it(L_WARNING, "Failed to set SNI hostname: %s", a_hostname);
        return -1;
    }
    SSL_set1_host(a_session->ssl, a_hostname);
    return 0;
}

void dap_tls_session_set_nonblocking(dap_tls_session_t *a_session,
                                      bool a_nonblocking)
{
    if (a_session)
        a_session->nonblocking = a_nonblocking;
}

/* ---- Handshake ---------------------------------------------------------- */

int dap_tls_session_connect(dap_tls_session_t *a_session)
{
    if (!a_session || !a_session->ssl)
        return -1;
    int l_ret = SSL_connect(a_session->ssl);
    if (l_ret == 1)
        return 1;
    int l_err = SSL_get_error(a_session->ssl, l_ret);
    if (l_err == SSL_ERROR_WANT_READ || l_err == SSL_ERROR_WANT_WRITE)
        return 0;
    unsigned long l_ossl_err = ERR_peek_last_error();
    log_it(L_ERROR, "SSL_connect failed: %s", ERR_error_string(l_ossl_err, NULL));
    return -1;
}

int dap_tls_session_accept(dap_tls_session_t *a_session)
{
    if (!a_session || !a_session->ssl)
        return -1;
    int l_ret = SSL_accept(a_session->ssl);
    if (l_ret == 1)
        return 1;
    int l_err = SSL_get_error(a_session->ssl, l_ret);
    if (l_err == SSL_ERROR_WANT_READ || l_err == SSL_ERROR_WANT_WRITE)
        return 0;
    unsigned long l_ossl_err = ERR_peek_last_error();
    log_it(L_ERROR, "SSL_accept failed: %s", ERR_error_string(l_ossl_err, NULL));
    return -1;
}

/* ---- Data transfer ------------------------------------------------------ */

ssize_t dap_tls_session_read(dap_tls_session_t *a_session,
                              void *a_buf, size_t a_size)
{
    if (!a_session || !a_session->ssl || !a_buf || a_size == 0)
        return -1;
    return (ssize_t)SSL_read(a_session->ssl, a_buf, (int)a_size);
}

ssize_t dap_tls_session_write(dap_tls_session_t *a_session,
                               const void *a_data, size_t a_size)
{
    if (!a_session || !a_session->ssl || !a_data || a_size == 0)
        return -1;
    return (ssize_t)SSL_write(a_session->ssl, a_data, (int)a_size);
}

/* ---- Shutdown ----------------------------------------------------------- */

int dap_tls_session_shutdown(dap_tls_session_t *a_session)
{
    if (!a_session || !a_session->ssl)
        return -1;
    int l_ret = SSL_shutdown(a_session->ssl);
    if (l_ret == 1)
        return 1;
    if (l_ret == 0)
        return 0;
    int l_err = SSL_get_error(a_session->ssl, l_ret);
    if (l_err == SSL_ERROR_WANT_READ || l_err == SSL_ERROR_WANT_WRITE)
        return 0;
    return -1;
}

/* ---- Error / Info ------------------------------------------------------- */

dap_tls_error_t dap_tls_session_get_error(dap_tls_session_t *a_session, int a_ret)
{
    if (!a_session || !a_session->ssl)
        return DAP_TLS_ERR_INVAL;
    int l_err = SSL_get_error(a_session->ssl, a_ret);
    switch (l_err) {
        case SSL_ERROR_NONE:        return DAP_TLS_ERR_NONE;
        case SSL_ERROR_WANT_READ:   return DAP_TLS_ERR_WANT_READ;
        case SSL_ERROR_WANT_WRITE:  return DAP_TLS_ERR_WANT_WRITE;
        case SSL_ERROR_ZERO_RETURN: return DAP_TLS_ERR_CLOSED;
        case SSL_ERROR_SYSCALL:     return DAP_TLS_ERR_SYSCALL;
        case SSL_ERROR_SSL:         return DAP_TLS_ERR_SSL;
        default:                    return DAP_TLS_ERR_SSL;
    }
}

const char *dap_tls_error_string(dap_tls_error_t a_err)
{
    switch (a_err) {
        case DAP_TLS_ERR_NONE:       return "no error";
        case DAP_TLS_ERR_WANT_READ:  return "want read";
        case DAP_TLS_ERR_WANT_WRITE: return "want write";
        case DAP_TLS_ERR_SYSCALL:    return "system call error";
        case DAP_TLS_ERR_SSL:        return "SSL protocol error";
        case DAP_TLS_ERR_CLOSED:     return "connection closed";
        case DAP_TLS_ERR_NOMEM:      return "out of memory";
        case DAP_TLS_ERR_INVAL:      return "invalid argument";
        case DAP_TLS_ERR_NOT_IMPL:   return "not implemented";
        default:                     return "unknown error";
    }
}

const char *dap_tls_session_get_cipher(dap_tls_session_t *a_session)
{
    if (!a_session || !a_session->ssl)
        return NULL;
    return SSL_get_cipher_name(a_session->ssl);
}

const char *dap_tls_session_get_version(dap_tls_session_t *a_session)
{
    if (!a_session || !a_session->ssl)
        return NULL;
    return SSL_get_version(a_session->ssl);
}
