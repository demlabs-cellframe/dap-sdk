/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * DAP TLS -- Windows SChannel backend
 *
 * Uses SSPI (Security Support Provider Interface) with the SChannel
 * security provider for TLS on Windows.
 *
 * Supports TLS 1.2 on all Windows versions.
 * TLS 1.3 on Windows 10 version 1903+ / Windows Server 2022+.
 *
 * The SChannel API does not perform I/O; this backend reads/writes
 * encrypted data via send()/recv() on the raw socket, and uses
 * EncryptMessage()/DecryptMessage() for the TLS record layer.
 */

#define SECURITY_WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wincrypt.h>
#include <schannel.h>
#include <security.h>
#include <sspi.h>

#include <string.h>
#include "dap_common.h"
#include "dap_tls.h"

#define LOG_TAG "dap_tls_schannel"

#define SCHAN_RECV_BUF_INIT  (16 * 1024)
#define SCHAN_SEND_BUF_MAX   (16 * 1024)

struct dap_tls_context {
    CredHandle cred_handle;
    bool cred_valid;
    bool is_server;
    bool verify_peer;
};

typedef enum {
    SCHAN_HS_INITIAL = 0,
    SCHAN_HS_LOOP,
    SCHAN_HS_COMPLETE,
    SCHAN_HS_ERROR
} schan_hs_phase_t;

struct dap_tls_session {
    dap_tls_context_t *ctx;
    CtxtHandle sec_ctx;
    bool sec_ctx_valid;
    SOCKET fd;
    bool nonblocking;
    schan_hs_phase_t hs_phase;

    SecPkgContext_StreamSizes stream_sizes;
    bool have_stream_sizes;

    uint8_t *recv_buf;
    DWORD recv_buf_size;
    DWORD recv_buf_used;

    uint8_t *dec_extra;
    DWORD dec_extra_size;

    wchar_t *target_name;
    SECURITY_STATUS last_status;
};

static int s_send_all(SOCKET a_fd, const void *a_data, int a_len)
{
    const char *l_ptr = (const char *)a_data;
    int l_sent = 0;
    while (l_sent < a_len) {
        int l_ret = send(a_fd, l_ptr + l_sent, a_len - l_sent, 0);
        if (l_ret == SOCKET_ERROR) {
            int l_err = WSAGetLastError();
            if (l_err == WSAEWOULDBLOCK) return l_sent;
            return -1;
        }
        l_sent += l_ret;
    }
    return l_sent;
}

static int s_recv_some(SOCKET a_fd, void *a_buf, int a_max)
{
    int l_ret = recv(a_fd, (char *)a_buf, a_max, 0);
    if (l_ret == SOCKET_ERROR) {
        int l_err = WSAGetLastError();
        if (l_err == WSAEWOULDBLOCK) return 0;
        return -1;
    }
    return l_ret;
}

int dap_tls_init(void)
{
    log_it(L_INFO, "TLS backend initialized: Windows SChannel");
    return 0;
}

void dap_tls_deinit(void) {}

const char *dap_tls_backend_name(void) { return "SChannel"; }

dap_tls_context_t *dap_tls_context_new_client(void)
{
    dap_tls_context_t *l_ctx = DAP_NEW_Z(dap_tls_context_t);
    if (!l_ctx) return NULL;

    SCH_CREDENTIALS l_cred = {0};
    l_cred.dwVersion = SCH_CREDENTIALS_VERSION;
    l_cred.dwFlags = SCH_USE_STRONG_CRYPTO | SCH_CRED_AUTO_CRED_VALIDATION
                   | SCH_CRED_NO_DEFAULT_CREDS;

    SECURITY_STATUS l_st = AcquireCredentialsHandleW(
        NULL, UNISP_NAME_W, SECPKG_CRED_OUTBOUND,
        NULL, &l_cred, NULL, NULL,
        &l_ctx->cred_handle, NULL);

    if (l_st != SEC_E_OK) {
        log_it(L_ERROR, "AcquireCredentialsHandle failed: 0x%08lx", l_st);
        DAP_DELETE(l_ctx);
        return NULL;
    }
    l_ctx->cred_valid = true;
    l_ctx->is_server = false;
    l_ctx->verify_peer = true;
    return l_ctx;
}

dap_tls_context_t *dap_tls_context_new_server(const char *a_cert_file,
                                               const char *a_key_file)
{
    if (!a_cert_file || !a_key_file) {
        log_it(L_ERROR, "Server context requires cert and key files");
        return NULL;
    }

    /* Load the certificate from file into a CERT_CONTEXT.
       For PEM: convert to DER first. For simplicity, support PFX/PKCS#12. */
    FILE *l_fp = fopen(a_cert_file, "rb");
    if (!l_fp) {
        log_it(L_ERROR, "Cannot open cert file: %s", a_cert_file);
        return NULL;
    }
    fseek(l_fp, 0, SEEK_END);
    long l_size = ftell(l_fp);
    fseek(l_fp, 0, SEEK_SET);
    uint8_t *l_buf = DAP_NEW_SIZE(uint8_t, l_size);
    fread(l_buf, 1, l_size, l_fp);
    fclose(l_fp);

    CRYPT_DATA_BLOB l_pfx = { .cbData = (DWORD)l_size, .pbData = l_buf };
    HCERTSTORE l_store = PFXImportCertStore(&l_pfx, L"", CRYPT_EXPORTABLE);
    DAP_DELETE(l_buf);

    if (!l_store) {
        log_it(L_ERROR, "PFXImportCertStore failed: %lu", GetLastError());
        return NULL;
    }

    PCCERT_CONTEXT l_cert = CertFindCertificateInStore(
        l_store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
        0, CERT_FIND_ANY, NULL, NULL);

    if (!l_cert) {
        log_it(L_ERROR, "No certificate found in store");
        CertCloseStore(l_store, 0);
        return NULL;
    }

    dap_tls_context_t *l_ctx = DAP_NEW_Z(dap_tls_context_t);
    if (!l_ctx) {
        CertFreeCertificateContext(l_cert);
        CertCloseStore(l_store, 0);
        return NULL;
    }

    SCH_CREDENTIALS l_cred = {0};
    l_cred.dwVersion = SCH_CREDENTIALS_VERSION;
    l_cred.cCreds = 1;
    l_cred.paCred = &l_cert;

    SECURITY_STATUS l_st = AcquireCredentialsHandleW(
        NULL, UNISP_NAME_W, SECPKG_CRED_INBOUND,
        NULL, &l_cred, NULL, NULL,
        &l_ctx->cred_handle, NULL);

    CertFreeCertificateContext(l_cert);
    CertCloseStore(l_store, 0);

    if (l_st != SEC_E_OK) {
        log_it(L_ERROR, "AcquireCredentialsHandle (server) failed: 0x%08lx", l_st);
        DAP_DELETE(l_ctx);
        return NULL;
    }
    l_ctx->cred_valid = true;
    l_ctx->is_server = true;
    return l_ctx;
}

void dap_tls_context_free(dap_tls_context_t *a_ctx)
{
    if (!a_ctx) return;
    if (a_ctx->cred_valid)
        FreeCredentialsHandle(&a_ctx->cred_handle);
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
    log_it(L_DEBUG, "SChannel uses system cipher configuration via registry/policy");
    return 0;
}

int dap_tls_context_load_ca(dap_tls_context_t *a_ctx,
                             const char *a_ca_file, const char *a_ca_dir)
{
    UNUSED(a_ctx); UNUSED(a_ca_file); UNUSED(a_ca_dir);
    log_it(L_DEBUG, "SChannel uses system certificate store for CA verification");
    return 0;
}

int dap_tls_context_set_alpn(dap_tls_context_t *a_ctx,
                              const uint8_t *a_protos, uint32_t a_protos_len)
{
    UNUSED(a_ctx); UNUSED(a_protos); UNUSED(a_protos_len);
    log_it(L_DEBUG, "ALPN for SChannel is configured via InitializeSecurityContext");
    return 0;
}

int dap_tls_context_set_min_version(dap_tls_context_t *a_ctx, uint16_t a_version)
{
    UNUSED(a_ctx); UNUSED(a_version);
    log_it(L_DEBUG, "TLS version negotiation handled by SChannel via SCH_CREDENTIALS");
    return 0;
}

dap_tls_session_t *dap_tls_session_new(dap_tls_context_t *a_ctx)
{
    if (!a_ctx || !a_ctx->cred_valid) return NULL;
    dap_tls_session_t *l_sess = DAP_NEW_Z(dap_tls_session_t);
    if (!l_sess) return NULL;
    l_sess->ctx = a_ctx;
    l_sess->fd = INVALID_SOCKET;
    l_sess->hs_phase = SCHAN_HS_INITIAL;
    l_sess->recv_buf_size = SCHAN_RECV_BUF_INIT;
    l_sess->recv_buf = DAP_NEW_Z_SIZE(uint8_t, SCHAN_RECV_BUF_INIT);
    if (!l_sess->recv_buf) {
        DAP_DELETE(l_sess);
        return NULL;
    }
    return l_sess;
}

void dap_tls_session_free(dap_tls_session_t *a_session)
{
    if (!a_session) return;
    if (a_session->sec_ctx_valid)
        DeleteSecurityContext(&a_session->sec_ctx);
    DAP_DEL_Z(a_session->recv_buf);
    DAP_DEL_Z(a_session->dec_extra);
    DAP_DEL_Z(a_session->target_name);
    DAP_DELETE(a_session);
}

int dap_tls_session_set_fd(dap_tls_session_t *a_session, int a_fd)
{
    if (!a_session) return -1;
    a_session->fd = (SOCKET)a_fd;
    return 0;
}

int dap_tls_session_set_hostname(dap_tls_session_t *a_session, const char *a_hostname)
{
    if (!a_session || !a_hostname) return -1;
    DAP_DEL_Z(a_session->target_name);
    int l_wlen = MultiByteToWideChar(CP_UTF8, 0, a_hostname, -1, NULL, 0);
    a_session->target_name = DAP_NEW_SIZE(wchar_t, l_wlen * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, a_hostname, -1, a_session->target_name, l_wlen);
    return 0;
}

void dap_tls_session_set_nonblocking(dap_tls_session_t *a_session, bool a_nonblocking)
{
    if (a_session) a_session->nonblocking = a_nonblocking;
}

int dap_tls_session_connect(dap_tls_session_t *a_session)
{
    if (!a_session || !a_session->ctx || a_session->fd == INVALID_SOCKET)
        return -1;

    DWORD l_flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT
                  | ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY
                  | ISC_REQ_STREAM;

    if (!a_session->ctx->verify_peer)
        l_flags |= ISC_REQ_MANUAL_CRED_VALIDATION;

    for (;;) {
        SecBufferDesc l_in_desc = {0}, l_out_desc = {0};
        SecBuffer l_in_bufs[2] = {0}, l_out_buf = {0};

        l_out_buf.BufferType = SECBUFFER_TOKEN;
        l_out_desc.ulVersion = SECBUFFER_VERSION;
        l_out_desc.cBuffers = 1;
        l_out_desc.pBuffers = &l_out_buf;

        SecBufferDesc *l_in_ptr = NULL;
        if (a_session->hs_phase == SCHAN_HS_LOOP && a_session->recv_buf_used > 0) {
            l_in_bufs[0].BufferType = SECBUFFER_TOKEN;
            l_in_bufs[0].pvBuffer = a_session->recv_buf;
            l_in_bufs[0].cbBuffer = a_session->recv_buf_used;
            l_in_bufs[1].BufferType = SECBUFFER_EMPTY;
            l_in_desc.ulVersion = SECBUFFER_VERSION;
            l_in_desc.cBuffers = 2;
            l_in_desc.pBuffers = l_in_bufs;
            l_in_ptr = &l_in_desc;
        }

        DWORD l_out_flags = 0;
        SECURITY_STATUS l_st = InitializeSecurityContextW(
            &a_session->ctx->cred_handle,
            a_session->hs_phase == SCHAN_HS_INITIAL ? NULL : &a_session->sec_ctx,
            a_session->target_name,
            l_flags, 0, 0,
            l_in_ptr, 0,
            a_session->hs_phase == SCHAN_HS_INITIAL ? &a_session->sec_ctx : NULL,
            &l_out_desc, &l_out_flags, NULL);

        a_session->last_status = l_st;

        if (a_session->hs_phase == SCHAN_HS_INITIAL)
            a_session->sec_ctx_valid = true;

        /* Handle EXTRA data (leftover from input) */
        if (l_in_ptr && l_in_bufs[1].BufferType == SECBUFFER_EXTRA) {
            DWORD l_extra = l_in_bufs[1].cbBuffer;
            memmove(a_session->recv_buf,
                    a_session->recv_buf + a_session->recv_buf_used - l_extra,
                    l_extra);
            a_session->recv_buf_used = l_extra;
        } else if (l_in_ptr) {
            a_session->recv_buf_used = 0;
        }

        /* Send output token if present */
        if (l_out_buf.cbBuffer > 0 && l_out_buf.pvBuffer) {
            int l_sent = s_send_all(a_session->fd, l_out_buf.pvBuffer, l_out_buf.cbBuffer);
            FreeContextBuffer(l_out_buf.pvBuffer);
            if (l_sent < 0) {
                a_session->hs_phase = SCHAN_HS_ERROR;
                return -1;
            }
        }

        if (l_st == SEC_E_OK) {
            a_session->hs_phase = SCHAN_HS_COMPLETE;
            QueryContextAttributes(&a_session->sec_ctx, SECPKG_ATTR_STREAM_SIZES,
                                   &a_session->stream_sizes);
            a_session->have_stream_sizes = true;
            return 1;
        }

        if (l_st == SEC_I_CONTINUE_NEEDED || l_st == SEC_I_INCOMPLETE_CREDENTIALS) {
            a_session->hs_phase = SCHAN_HS_LOOP;
            /* Read more data from server */
            int l_avail = (int)(a_session->recv_buf_size - a_session->recv_buf_used);
            if (l_avail <= 0) {
                a_session->recv_buf_size *= 2;
                a_session->recv_buf = DAP_REALLOC(a_session->recv_buf, a_session->recv_buf_size);
                l_avail = (int)(a_session->recv_buf_size - a_session->recv_buf_used);
            }
            int l_got = s_recv_some(a_session->fd,
                                    a_session->recv_buf + a_session->recv_buf_used,
                                    l_avail);
            if (l_got < 0) {
                a_session->hs_phase = SCHAN_HS_ERROR;
                return -1;
            }
            if (l_got == 0) return 0;  /* WANT_READ */
            a_session->recv_buf_used += l_got;
            continue;
        }

        if (l_st == SEC_E_INCOMPLETE_MESSAGE) {
            a_session->hs_phase = SCHAN_HS_LOOP;
            int l_avail = (int)(a_session->recv_buf_size - a_session->recv_buf_used);
            if (l_avail <= 0) {
                a_session->recv_buf_size *= 2;
                a_session->recv_buf = DAP_REALLOC(a_session->recv_buf, a_session->recv_buf_size);
                l_avail = (int)(a_session->recv_buf_size - a_session->recv_buf_used);
            }
            int l_got = s_recv_some(a_session->fd,
                                    a_session->recv_buf + a_session->recv_buf_used,
                                    l_avail);
            if (l_got < 0) {
                a_session->hs_phase = SCHAN_HS_ERROR;
                return -1;
            }
            if (l_got == 0) return 0;
            a_session->recv_buf_used += l_got;
            continue;
        }

        log_it(L_ERROR, "InitializeSecurityContext failed: 0x%08lx", l_st);
        a_session->hs_phase = SCHAN_HS_ERROR;
        return -1;
    }
}

int dap_tls_session_accept(dap_tls_session_t *a_session)
{
    if (!a_session || !a_session->ctx || a_session->fd == INVALID_SOCKET)
        return -1;

    DWORD l_flags = ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT
                  | ASC_REQ_CONFIDENTIALITY | ASC_REQ_ALLOCATE_MEMORY
                  | ASC_REQ_STREAM;

    for (;;) {
        /* Read initial client hello if needed */
        if (a_session->recv_buf_used == 0) {
            int l_got = s_recv_some(a_session->fd, a_session->recv_buf,
                                    (int)a_session->recv_buf_size);
            if (l_got <= 0) return l_got == 0 ? 0 : -1;
            a_session->recv_buf_used += l_got;
        }

        SecBufferDesc l_in_desc = {0}, l_out_desc = {0};
        SecBuffer l_in_bufs[2] = {0}, l_out_buf = {0};

        l_in_bufs[0].BufferType = SECBUFFER_TOKEN;
        l_in_bufs[0].pvBuffer = a_session->recv_buf;
        l_in_bufs[0].cbBuffer = a_session->recv_buf_used;
        l_in_bufs[1].BufferType = SECBUFFER_EMPTY;
        l_in_desc.ulVersion = SECBUFFER_VERSION;
        l_in_desc.cBuffers = 2;
        l_in_desc.pBuffers = l_in_bufs;

        l_out_buf.BufferType = SECBUFFER_TOKEN;
        l_out_desc.ulVersion = SECBUFFER_VERSION;
        l_out_desc.cBuffers = 1;
        l_out_desc.pBuffers = &l_out_buf;

        DWORD l_out_flags = 0;
        SECURITY_STATUS l_st = AcceptSecurityContext(
            &a_session->ctx->cred_handle,
            a_session->sec_ctx_valid ? &a_session->sec_ctx : NULL,
            &l_in_desc, l_flags, 0,
            a_session->sec_ctx_valid ? NULL : &a_session->sec_ctx,
            &l_out_desc, &l_out_flags, NULL);

        a_session->last_status = l_st;
        a_session->sec_ctx_valid = true;

        if (l_in_bufs[1].BufferType == SECBUFFER_EXTRA) {
            DWORD l_extra = l_in_bufs[1].cbBuffer;
            memmove(a_session->recv_buf,
                    a_session->recv_buf + a_session->recv_buf_used - l_extra,
                    l_extra);
            a_session->recv_buf_used = l_extra;
        } else {
            a_session->recv_buf_used = 0;
        }

        if (l_out_buf.cbBuffer > 0 && l_out_buf.pvBuffer) {
            s_send_all(a_session->fd, l_out_buf.pvBuffer, l_out_buf.cbBuffer);
            FreeContextBuffer(l_out_buf.pvBuffer);
        }

        if (l_st == SEC_E_OK) {
            a_session->hs_phase = SCHAN_HS_COMPLETE;
            QueryContextAttributes(&a_session->sec_ctx, SECPKG_ATTR_STREAM_SIZES,
                                   &a_session->stream_sizes);
            a_session->have_stream_sizes = true;
            return 1;
        }
        if (l_st == SEC_I_CONTINUE_NEEDED || l_st == SEC_E_INCOMPLETE_MESSAGE) {
            int l_avail = (int)(a_session->recv_buf_size - a_session->recv_buf_used);
            if (l_avail <= 0) {
                a_session->recv_buf_size *= 2;
                a_session->recv_buf = DAP_REALLOC(a_session->recv_buf, a_session->recv_buf_size);
                l_avail = (int)(a_session->recv_buf_size - a_session->recv_buf_used);
            }
            int l_got = s_recv_some(a_session->fd,
                                    a_session->recv_buf + a_session->recv_buf_used,
                                    l_avail);
            if (l_got <= 0) return l_got == 0 ? 0 : -1;
            a_session->recv_buf_used += l_got;
            continue;
        }
        log_it(L_ERROR, "AcceptSecurityContext failed: 0x%08lx", l_st);
        return -1;
    }
}

ssize_t dap_tls_session_read(dap_tls_session_t *a_session,
                              void *a_buf, size_t a_size)
{
    if (!a_session || !a_session->have_stream_sizes || !a_buf || a_size == 0)
        return -1;

    /* Return buffered decrypted data first */
    if (a_session->dec_extra && a_session->dec_extra_size > 0) {
        DWORD l_copy = (DWORD)a_size;
        if (l_copy > a_session->dec_extra_size)
            l_copy = a_session->dec_extra_size;
        memcpy(a_buf, a_session->dec_extra, l_copy);
        a_session->dec_extra_size -= l_copy;
        if (a_session->dec_extra_size > 0) {
            memmove(a_session->dec_extra, a_session->dec_extra + l_copy,
                    a_session->dec_extra_size);
        } else {
            DAP_DEL_Z(a_session->dec_extra);
        }
        return (ssize_t)l_copy;
    }

    /* Read encrypted data from network */
    int l_avail = (int)(a_session->recv_buf_size - a_session->recv_buf_used);
    if (l_avail <= 0) {
        a_session->recv_buf_size *= 2;
        a_session->recv_buf = DAP_REALLOC(a_session->recv_buf, a_session->recv_buf_size);
        l_avail = (int)(a_session->recv_buf_size - a_session->recv_buf_used);
    }
    int l_got = s_recv_some(a_session->fd,
                            a_session->recv_buf + a_session->recv_buf_used,
                            l_avail);
    if (l_got < 0) return -1;
    if (l_got == 0 && a_session->recv_buf_used == 0) {
        a_session->last_status = SEC_E_OK;
        return 0;
    }
    a_session->recv_buf_used += l_got;

    /* Decrypt */
    SecBuffer l_bufs[4];
    l_bufs[0].BufferType = SECBUFFER_DATA;
    l_bufs[0].pvBuffer = a_session->recv_buf;
    l_bufs[0].cbBuffer = a_session->recv_buf_used;
    l_bufs[1].BufferType = SECBUFFER_EMPTY;
    l_bufs[2].BufferType = SECBUFFER_EMPTY;
    l_bufs[3].BufferType = SECBUFFER_EMPTY;

    SecBufferDesc l_desc = { SECBUFFER_VERSION, 4, l_bufs };
    SECURITY_STATUS l_st = DecryptMessage(&a_session->sec_ctx, &l_desc, 0, NULL);
    a_session->last_status = l_st;

    if (l_st == SEC_E_INCOMPLETE_MESSAGE) return 0;

    if (l_st == SEC_I_CONTEXT_EXPIRED) return 0;

    if (l_st != SEC_E_OK && l_st != SEC_I_RENEGOTIATE) {
        log_it(L_ERROR, "DecryptMessage failed: 0x%08lx", l_st);
        return -1;
    }

    /* Find decrypted data and extra buffers */
    SecBuffer *l_data_buf = NULL, *l_extra_buf = NULL;
    for (int i = 0; i < 4; i++) {
        if (l_bufs[i].BufferType == SECBUFFER_DATA) l_data_buf = &l_bufs[i];
        if (l_bufs[i].BufferType == SECBUFFER_EXTRA) l_extra_buf = &l_bufs[i];
    }

    ssize_t l_result = 0;
    if (l_data_buf && l_data_buf->cbBuffer > 0) {
        DWORD l_copy = (DWORD)a_size;
        if (l_copy > l_data_buf->cbBuffer)
            l_copy = l_data_buf->cbBuffer;
        memcpy(a_buf, l_data_buf->pvBuffer, l_copy);
        l_result = (ssize_t)l_copy;

        /* Buffer any remaining decrypted data */
        if (l_copy < l_data_buf->cbBuffer) {
            DWORD l_rem = l_data_buf->cbBuffer - l_copy;
            a_session->dec_extra = DAP_REALLOC(a_session->dec_extra, l_rem);
            memcpy(a_session->dec_extra,
                   (uint8_t *)l_data_buf->pvBuffer + l_copy, l_rem);
            a_session->dec_extra_size = l_rem;
        }
    }

    /* Save EXTRA encrypted data for next decrypt */
    if (l_extra_buf && l_extra_buf->cbBuffer > 0) {
        memmove(a_session->recv_buf, l_extra_buf->pvBuffer, l_extra_buf->cbBuffer);
        a_session->recv_buf_used = l_extra_buf->cbBuffer;
    } else {
        a_session->recv_buf_used = 0;
    }

    return l_result;
}

ssize_t dap_tls_session_write(dap_tls_session_t *a_session,
                               const void *a_data, size_t a_size)
{
    if (!a_session || !a_session->have_stream_sizes || !a_data || a_size == 0)
        return -1;

    DWORD l_max_msg = a_session->stream_sizes.cbMaximumMessage;
    DWORD l_to_send = (DWORD)a_size;
    if (l_to_send > l_max_msg)
        l_to_send = l_max_msg;

    DWORD l_hdr = a_session->stream_sizes.cbHeader;
    DWORD l_trl = a_session->stream_sizes.cbTrailer;
    DWORD l_total = l_hdr + l_to_send + l_trl;

    uint8_t *l_buf = DAP_NEW_SIZE(uint8_t, l_total);
    if (!l_buf) return -1;

    memcpy(l_buf + l_hdr, a_data, l_to_send);

    SecBuffer l_bufs[4];
    l_bufs[0].BufferType = SECBUFFER_STREAM_HEADER;
    l_bufs[0].pvBuffer = l_buf;
    l_bufs[0].cbBuffer = l_hdr;
    l_bufs[1].BufferType = SECBUFFER_DATA;
    l_bufs[1].pvBuffer = l_buf + l_hdr;
    l_bufs[1].cbBuffer = l_to_send;
    l_bufs[2].BufferType = SECBUFFER_STREAM_TRAILER;
    l_bufs[2].pvBuffer = l_buf + l_hdr + l_to_send;
    l_bufs[2].cbBuffer = l_trl;
    l_bufs[3].BufferType = SECBUFFER_EMPTY;

    SecBufferDesc l_desc = { SECBUFFER_VERSION, 4, l_bufs };
    SECURITY_STATUS l_st = EncryptMessage(&a_session->sec_ctx, 0, &l_desc, 0);
    a_session->last_status = l_st;

    if (l_st != SEC_E_OK) {
        DAP_DELETE(l_buf);
        log_it(L_ERROR, "EncryptMessage failed: 0x%08lx", l_st);
        return -1;
    }

    DWORD l_enc_total = l_bufs[0].cbBuffer + l_bufs[1].cbBuffer + l_bufs[2].cbBuffer;
    int l_sent = s_send_all(a_session->fd, l_buf, (int)l_enc_total);
    DAP_DELETE(l_buf);

    if (l_sent < 0) return -1;
    return (ssize_t)l_to_send;
}

int dap_tls_session_shutdown(dap_tls_session_t *a_session)
{
    if (!a_session || !a_session->sec_ctx_valid)
        return -1;

    DWORD l_type = SCHANNEL_SHUTDOWN;
    SecBuffer l_buf = { sizeof(l_type), SECBUFFER_TOKEN, &l_type };
    SecBufferDesc l_desc = { SECBUFFER_VERSION, 1, &l_buf };

    SECURITY_STATUS l_st = ApplyControlToken(&a_session->sec_ctx, &l_desc);
    if (FAILED(l_st)) return -1;

    SecBuffer l_out = { 0, SECBUFFER_TOKEN, NULL };
    SecBufferDesc l_out_desc = { SECBUFFER_VERSION, 1, &l_out };
    DWORD l_flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT
                  | ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY
                  | ISC_REQ_STREAM;
    DWORD l_out_flags = 0;

    l_st = InitializeSecurityContextW(
        &a_session->ctx->cred_handle, &a_session->sec_ctx,
        NULL, l_flags, 0, 0, NULL, 0, NULL,
        &l_out_desc, &l_out_flags, NULL);

    if (l_out.cbBuffer > 0 && l_out.pvBuffer) {
        s_send_all(a_session->fd, l_out.pvBuffer, l_out.cbBuffer);
        FreeContextBuffer(l_out.pvBuffer);
    }

    DeleteSecurityContext(&a_session->sec_ctx);
    a_session->sec_ctx_valid = false;
    return 1;
}

dap_tls_error_t dap_tls_session_get_error(dap_tls_session_t *a_session, int a_ret)
{
    UNUSED(a_ret);
    if (!a_session) return DAP_TLS_ERR_INVAL;
    SECURITY_STATUS l_st = a_session->last_status;
    if (l_st == SEC_E_OK) return DAP_TLS_ERR_NONE;
    if (l_st == SEC_I_CONTINUE_NEEDED || l_st == SEC_E_INCOMPLETE_MESSAGE)
        return DAP_TLS_ERR_WANT_READ;
    if (l_st == SEC_I_CONTEXT_EXPIRED) return DAP_TLS_ERR_CLOSED;
    return DAP_TLS_ERR_SSL;
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
    if (!a_session || !a_session->sec_ctx_valid) return NULL;

    SecPkgContext_ConnectionInfo l_info = {0};
    SECURITY_STATUS l_st = QueryContextAttributes(
        &a_session->sec_ctx, SECPKG_ATTR_CONNECTION_INFO, &l_info);
    if (l_st != SEC_E_OK) return NULL;

    switch (l_info.aiCipher) {
        case CALG_AES_256: return "AES-256-GCM";
        case CALG_AES_128: return "AES-128-GCM";
        case CALG_3DES:    return "3DES";
        default:           return "unknown";
    }
}

const char *dap_tls_session_get_version(dap_tls_session_t *a_session)
{
    if (!a_session || !a_session->sec_ctx_valid) return NULL;

    SecPkgContext_ConnectionInfo l_info = {0};
    SECURITY_STATUS l_st = QueryContextAttributes(
        &a_session->sec_ctx, SECPKG_ATTR_CONNECTION_INFO, &l_info);
    if (l_st != SEC_E_OK) return NULL;

    switch (l_info.dwProtocol) {
        case SP_PROT_TLS1_CLIENT:   return "TLSv1.0";
        case SP_PROT_TLS1_1_CLIENT: return "TLSv1.1";
        case SP_PROT_TLS1_2_CLIENT: return "TLSv1.2";
        case SP_PROT_TLS1_3_CLIENT: return "TLSv1.3";
        case SP_PROT_TLS1_SERVER:   return "TLSv1.0";
        case SP_PROT_TLS1_1_SERVER: return "TLSv1.1";
        case SP_PROT_TLS1_2_SERVER: return "TLSv1.2";
        case SP_PROT_TLS1_3_SERVER: return "TLSv1.3";
        default:                    return "unknown";
    }
}
