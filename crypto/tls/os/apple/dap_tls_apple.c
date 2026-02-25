/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * DAP TLS -- Apple Secure Transport backend
 *
 * Uses the (deprecated but functional) Secure Transport API for
 * macOS 10.8+ / iOS 5+. Supports TLS 1.0-1.2.
 *
 * NOTE: TLS 1.3 is NOT supported by Secure Transport.
 * Apple deprecated this API in macOS 10.15 / iOS 13 in favour of
 * Network.framework, but it remains available and is the only
 * low-level C TLS API that works with raw file descriptors on Apple.
 *
 * For TLS 1.3 on Apple platforms, build with DAP_TLS_BACKEND=openssl
 * and link against Homebrew OpenSSL or a bundled BoringSSL.
 */

#include <Security/Security.h>
#include <Security/SecureTransport.h>
#include <CoreFoundation/CoreFoundation.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "dap_common.h"
#include "dap_tls.h"

#define LOG_TAG "dap_tls_apple"

struct dap_tls_context {
    SSLProtocol min_version;
    SSLProtocol max_version;
    bool verify_peer;
    bool is_server;
    CFArrayRef identity_certs;
    CFArrayRef alpn_protocols;
};

struct dap_tls_session {
    dap_tls_context_t *ctx;
    SSLContextRef ssl;
    int fd;
    bool nonblocking;
    bool last_io_was_write;
    OSStatus last_os_status;
};

/* I/O callbacks for Secure Transport.
   SSLConnectionRef is a pointer to our dap_tls_session_t. */

static OSStatus s_st_read_cb(SSLConnectionRef a_conn, void *a_data, size_t *a_len)
{
    dap_tls_session_t *l_sess = (dap_tls_session_t *)a_conn;
    l_sess->last_io_was_write = false;

    size_t l_requested = *a_len;
    size_t l_total = 0;
    uint8_t *l_dst = (uint8_t *)a_data;

    while (l_total < l_requested) {
        ssize_t l_ret = read(l_sess->fd, l_dst + l_total, l_requested - l_total);
        if (l_ret > 0) {
            l_total += (size_t)l_ret;
            continue;
        }
        if (l_ret == 0) {
            *a_len = l_total;
            return l_total > 0 ? (OSStatus)errSSLWouldBlock : (OSStatus)errSSLClosedGraceful;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            *a_len = l_total;
            return errSSLWouldBlock;
        }
        *a_len = l_total;
        return errSSLClosedAbort;
    }
    *a_len = l_total;
    return noErr;
}

static OSStatus s_st_write_cb(SSLConnectionRef a_conn, const void *a_data, size_t *a_len)
{
    dap_tls_session_t *l_sess = (dap_tls_session_t *)a_conn;
    l_sess->last_io_was_write = true;

    size_t l_requested = *a_len;
    size_t l_total = 0;
    const uint8_t *l_src = (const uint8_t *)a_data;

    while (l_total < l_requested) {
        ssize_t l_ret = write(l_sess->fd, l_src + l_total, l_requested - l_total);
        if (l_ret > 0) {
            l_total += (size_t)l_ret;
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            *a_len = l_total;
            return errSSLWouldBlock;
        }
        *a_len = l_total;
        return errSSLClosedAbort;
    }
    *a_len = l_total;
    return noErr;
}

int dap_tls_init(void)
{
    log_it(L_INFO, "TLS backend initialized: Apple Secure Transport (TLS 1.0-1.2)");
    log_it(L_WARNING, "Secure Transport does NOT support TLS 1.3. "
                       "Use DAP_TLS_BACKEND=openssl for TLS 1.3 on Apple.");
    return 0;
}

void dap_tls_deinit(void)
{
    log_it(L_DEBUG, "TLS backend deinitialized");
}

const char *dap_tls_backend_name(void)
{
    return "Secure Transport";
}

dap_tls_context_t *dap_tls_context_new_client(void)
{
    dap_tls_context_t *l_ctx = DAP_NEW_Z(dap_tls_context_t);
    if (!l_ctx) return NULL;
    l_ctx->min_version = kTLSProtocol12;
    l_ctx->max_version = kTLSProtocol12;
    l_ctx->verify_peer = false;
    l_ctx->is_server = false;
    return l_ctx;
}

dap_tls_context_t *dap_tls_context_new_server(const char *a_cert_file,
                                               const char *a_key_file)
{
    if (!a_cert_file || !a_key_file) {
        log_it(L_ERROR, "Server context requires cert and key files");
        return NULL;
    }

    /* On macOS, loading PEM into Secure Transport requires SecItemImport
       which imports into a keychain. Read the PKCS#12 or PEM file. */
    FILE *l_fp = fopen(a_cert_file, "r");
    if (!l_fp) {
        log_it(L_ERROR, "Cannot open cert file: %s", a_cert_file);
        return NULL;
    }
    fseek(l_fp, 0, SEEK_END);
    long l_cert_size = ftell(l_fp);
    fseek(l_fp, 0, SEEK_SET);
    uint8_t *l_cert_buf = DAP_NEW_SIZE(uint8_t, l_cert_size);
    fread(l_cert_buf, 1, l_cert_size, l_fp);
    fclose(l_fp);

    CFDataRef l_cert_data = CFDataCreate(NULL, l_cert_buf, l_cert_size);
    DAP_DELETE(l_cert_buf);

    SecExternalFormat l_format = kSecFormatPEMSequence;
    SecExternalItemType l_type = kSecItemTypeAggregate;
    CFArrayRef l_items = NULL;
    OSStatus l_status = SecItemImport(l_cert_data, CFSTR(".pem"),
                                      &l_format, &l_type, 0, NULL, NULL, &l_items);
    CFRelease(l_cert_data);

    if (l_status != noErr || !l_items || CFArrayGetCount(l_items) == 0) {
        log_it(L_ERROR, "SecItemImport failed for cert: OSStatus %d", (int)l_status);
        if (l_items) CFRelease(l_items);
        return NULL;
    }

    /* Try to also import the key file */
    l_fp = fopen(a_key_file, "r");
    if (l_fp) {
        fseek(l_fp, 0, SEEK_END);
        long l_key_size = ftell(l_fp);
        fseek(l_fp, 0, SEEK_SET);
        uint8_t *l_key_buf = DAP_NEW_SIZE(uint8_t, l_key_size);
        fread(l_key_buf, 1, l_key_size, l_fp);
        fclose(l_fp);

        CFDataRef l_key_data = CFDataCreate(NULL, l_key_buf, l_key_size);
        DAP_DELETE(l_key_buf);

        SecExternalFormat l_key_format = kSecFormatPEMSequence;
        SecExternalItemType l_key_type = kSecItemTypeAggregate;
        CFArrayRef l_key_items = NULL;
        SecItemImport(l_key_data, CFSTR(".pem"),
                      &l_key_format, &l_key_type, 0, NULL, NULL, &l_key_items);
        CFRelease(l_key_data);
        if (l_key_items) CFRelease(l_key_items);
    }

    dap_tls_context_t *l_ctx = DAP_NEW_Z(dap_tls_context_t);
    if (!l_ctx) {
        CFRelease(l_items);
        return NULL;
    }
    l_ctx->min_version = kTLSProtocol12;
    l_ctx->max_version = kTLSProtocol12;
    l_ctx->verify_peer = false;
    l_ctx->is_server = true;
    l_ctx->identity_certs = l_items;

    log_it(L_INFO, "Server TLS context created with cert: %s", a_cert_file);
    return l_ctx;
}

void dap_tls_context_free(dap_tls_context_t *a_ctx)
{
    if (!a_ctx) return;
    if (a_ctx->identity_certs) CFRelease(a_ctx->identity_certs);
    if (a_ctx->alpn_protocols) CFRelease(a_ctx->alpn_protocols);
    DAP_DELETE(a_ctx);
}

int dap_tls_context_set_verify(dap_tls_context_t *a_ctx, bool a_verify)
{
    if (!a_ctx) return -1;
    a_ctx->verify_peer = a_verify;
    return 0;
}

int dap_tls_context_set_ciphers(dap_tls_context_t *a_ctx, const char *a_ciphers)
{
    UNUSED(a_ctx); UNUSED(a_ciphers);
    log_it(L_DEBUG, "Secure Transport uses system cipher configuration");
    return 0;
}

int dap_tls_context_load_ca(dap_tls_context_t *a_ctx,
                             const char *a_ca_file, const char *a_ca_dir)
{
    UNUSED(a_ctx); UNUSED(a_ca_file); UNUSED(a_ca_dir);
    log_it(L_DEBUG, "Secure Transport uses system Keychain for CA verification");
    return 0;
}

int dap_tls_context_set_alpn(dap_tls_context_t *a_ctx,
                              const uint8_t *a_protos, uint32_t a_protos_len)
{
    if (!a_ctx || !a_protos || a_protos_len == 0)
        return -1;

#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 101304
    CFMutableArrayRef l_arr = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    uint32_t l_pos = 0;
    while (l_pos < a_protos_len) {
        uint8_t l_plen = a_protos[l_pos++];
        if (l_pos + l_plen > a_protos_len) break;
        CFStringRef l_str = CFStringCreateWithBytes(NULL, a_protos + l_pos,
                                                     l_plen, kCFStringEncodingASCII, false);
        if (l_str) {
            CFArrayAppendValue(l_arr, l_str);
            CFRelease(l_str);
        }
        l_pos += l_plen;
    }
    if (a_ctx->alpn_protocols) CFRelease(a_ctx->alpn_protocols);
    a_ctx->alpn_protocols = l_arr;
    return 0;
#else
    log_it(L_WARNING, "ALPN not available on this macOS SDK version");
    return -1;
#endif
}

int dap_tls_context_set_min_version(dap_tls_context_t *a_ctx, uint16_t a_version)
{
    if (!a_ctx) return -1;
    switch (a_version) {
        case 0x0303: a_ctx->min_version = kTLSProtocol12; break;
        case 0x0304:
            log_it(L_WARNING, "TLS 1.3 not supported by Secure Transport, using TLS 1.2");
            a_ctx->min_version = kTLSProtocol12;
            break;
        default:
            log_it(L_ERROR, "Unsupported TLS version: 0x%04x", a_version);
            return -1;
    }
    return 0;
}

dap_tls_session_t *dap_tls_session_new(dap_tls_context_t *a_ctx)
{
    if (!a_ctx) return NULL;

    SSLProtocolSide l_side = a_ctx->is_server ? kSSLServerSide : kSSLClientSide;
    SSLContextRef l_ssl = SSLCreateContext(NULL, l_side, kSSLStreamType);
    if (!l_ssl) {
        log_it(L_ERROR, "SSLCreateContext failed");
        return NULL;
    }

    SSLSetProtocolVersionMin(l_ssl, a_ctx->min_version);
    SSLSetProtocolVersionMax(l_ssl, a_ctx->max_version);

    if (!a_ctx->verify_peer) {
        SSLSetSessionOption(l_ssl, kSSLSessionOptionBreakOnServerAuth, true);
    }

    if (a_ctx->is_server && a_ctx->identity_certs) {
        SSLSetCertificate(l_ssl, a_ctx->identity_certs);
    }

#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 101304
    if (a_ctx->alpn_protocols) {
        SSLSetALPNProtocols(l_ssl, a_ctx->alpn_protocols);
    }
#endif

    dap_tls_session_t *l_sess = DAP_NEW_Z(dap_tls_session_t);
    if (!l_sess) {
        CFRelease(l_ssl);
        return NULL;
    }
    l_sess->ctx = a_ctx;
    l_sess->ssl = l_ssl;
    l_sess->fd = -1;
    return l_sess;
}

void dap_tls_session_free(dap_tls_session_t *a_session)
{
    if (!a_session) return;
    if (a_session->ssl) {
        SSLClose(a_session->ssl);
        CFRelease(a_session->ssl);
    }
    DAP_DELETE(a_session);
}

int dap_tls_session_set_fd(dap_tls_session_t *a_session, int a_fd)
{
    if (!a_session || !a_session->ssl || a_fd < 0)
        return -1;
    a_session->fd = a_fd;

    /* Set our session as the connection reference so callbacks can access fd */
    OSStatus l_st = SSLSetConnection(a_session->ssl, (SSLConnectionRef)a_session);
    if (l_st != noErr) {
        log_it(L_ERROR, "SSLSetConnection failed: %d", (int)l_st);
        return -1;
    }
    l_st = SSLSetIOFuncs(a_session->ssl, s_st_read_cb, s_st_write_cb);
    if (l_st != noErr) {
        log_it(L_ERROR, "SSLSetIOFuncs failed: %d", (int)l_st);
        return -1;
    }
    return 0;
}

int dap_tls_session_set_hostname(dap_tls_session_t *a_session, const char *a_hostname)
{
    if (!a_session || !a_session->ssl || !a_hostname)
        return -1;
    OSStatus l_st = SSLSetPeerDomainName(a_session->ssl, a_hostname, strlen(a_hostname));
    if (l_st != noErr) {
        log_it(L_WARNING, "SSLSetPeerDomainName failed: %d", (int)l_st);
        return -1;
    }
    return 0;
}

void dap_tls_session_set_nonblocking(dap_tls_session_t *a_session, bool a_nonblocking)
{
    if (a_session) a_session->nonblocking = a_nonblocking;
}

static int s_do_handshake(dap_tls_session_t *a_session)
{
    OSStatus l_st = SSLHandshake(a_session->ssl);
    a_session->last_os_status = l_st;

    if (l_st == noErr)
        return 1;

    if (l_st == errSSLWouldBlock)
        return 0;

    /* When verify is off, BreakOnServerAuth causes errSSLPeerAuthCompleted
       which means the server cert was received but not verified.
       We accept it and continue the handshake. */
    if (l_st == errSSLPeerAuthCompleted) {
        return s_do_handshake(a_session);
    }

    log_it(L_ERROR, "SSLHandshake failed: OSStatus %d", (int)l_st);
    return -1;
}

int dap_tls_session_connect(dap_tls_session_t *a_session)
{
    if (!a_session || !a_session->ssl) return -1;
    return s_do_handshake(a_session);
}

int dap_tls_session_accept(dap_tls_session_t *a_session)
{
    if (!a_session || !a_session->ssl) return -1;
    return s_do_handshake(a_session);
}

ssize_t dap_tls_session_read(dap_tls_session_t *a_session,
                              void *a_buf, size_t a_size)
{
    if (!a_session || !a_session->ssl || !a_buf || a_size == 0)
        return -1;

    size_t l_processed = 0;
    OSStatus l_st = SSLRead(a_session->ssl, a_buf, a_size, &l_processed);
    a_session->last_os_status = l_st;

    if (l_st == noErr || (l_st == errSSLWouldBlock && l_processed > 0))
        return (ssize_t)l_processed;

    if (l_st == errSSLWouldBlock)
        return 0;

    if (l_st == errSSLClosedGraceful || l_st == errSSLClosedNoNotify)
        return 0;

    return -1;
}

ssize_t dap_tls_session_write(dap_tls_session_t *a_session,
                               const void *a_data, size_t a_size)
{
    if (!a_session || !a_session->ssl || !a_data || a_size == 0)
        return -1;

    size_t l_processed = 0;
    OSStatus l_st = SSLWrite(a_session->ssl, a_data, a_size, &l_processed);
    a_session->last_os_status = l_st;

    if (l_st == noErr || (l_st == errSSLWouldBlock && l_processed > 0))
        return (ssize_t)l_processed;

    if (l_st == errSSLWouldBlock)
        return 0;

    return -1;
}

int dap_tls_session_shutdown(dap_tls_session_t *a_session)
{
    if (!a_session || !a_session->ssl) return -1;
    OSStatus l_st = SSLClose(a_session->ssl);
    if (l_st == noErr) return 1;
    if (l_st == errSSLWouldBlock) return 0;
    return -1;
}

dap_tls_error_t dap_tls_session_get_error(dap_tls_session_t *a_session, int a_ret)
{
    UNUSED(a_ret);
    if (!a_session) return DAP_TLS_ERR_INVAL;

    switch (a_session->last_os_status) {
        case noErr:                     return DAP_TLS_ERR_NONE;
        case errSSLWouldBlock:
            return a_session->last_io_was_write
                ? DAP_TLS_ERR_WANT_WRITE : DAP_TLS_ERR_WANT_READ;
        case errSSLClosedGraceful:
        case errSSLClosedNoNotify:      return DAP_TLS_ERR_CLOSED;
        case errSSLClosedAbort:         return DAP_TLS_ERR_SYSCALL;
        default:                        return DAP_TLS_ERR_SSL;
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
    if (!a_session || !a_session->ssl) return NULL;
    SSLCipherSuite l_cipher = 0;
    if (SSLGetNegotiatedCipher(a_session->ssl, &l_cipher) != noErr)
        return NULL;

    switch (l_cipher) {
        case TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384:    return "ECDHE-RSA-AES256-GCM-SHA384";
        case TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256:    return "ECDHE-RSA-AES128-GCM-SHA256";
        case TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384:  return "ECDHE-ECDSA-AES256-GCM-SHA384";
        case TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256:  return "ECDHE-ECDSA-AES128-GCM-SHA256";
        case TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256: return "ECDHE-RSA-CHACHA20-POLY1305";
        default: return "unknown";
    }
}

const char *dap_tls_session_get_version(dap_tls_session_t *a_session)
{
    if (!a_session || !a_session->ssl) return NULL;
    SSLProtocol l_proto = kSSLProtocolUnknown;
    SSLGetNegotiatedProtocolVersion(a_session->ssl, &l_proto);
    switch (l_proto) {
        case kTLSProtocol1:  return "TLSv1.0";
        case kTLSProtocol11: return "TLSv1.1";
        case kTLSProtocol12: return "TLSv1.2";
        default:             return "unknown";
    }
}
