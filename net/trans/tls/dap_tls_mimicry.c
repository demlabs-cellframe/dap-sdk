/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * TLS 1.3 Mimicry Engine -- implementation
 *
 * Generates realistic TLS 1.3 handshake on the wire so DPI sees
 * standard TLS traffic. No actual TLS crypto is performed; the
 * DAP stream layer with dap_enc does all real encryption on top.
 *
 * Wire layout produced:
 *   Client -> Server:  TLS Record(Handshake/ClientHello)
 *   Server -> Client:  TLS Record(Handshake/ServerHello)
 *                    + TLS Record(ChangeCipherSpec)
 *                    + TLS Record(ApplicationData) [fake enc extensions]
 *   Client -> Server:  TLS Record(ChangeCipherSpec)
 *                    + TLS Record(ApplicationData) [fake Finished]
 *
 * After handshake, all data is wrapped in Application Data records:
 *   [0x17][0x03 0x03][length_be16][payload]
 */

#include <string.h>
#include <stdlib.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "rand/dap_rand.h"
#include "dap_tls_mimicry.h"

#define LOG_TAG "dap_tls_mimicry"

/* TLS content types */
#define TLS_CT_CHANGE_CIPHER_SPEC  0x14
#define TLS_CT_HANDSHAKE           0x16
#define TLS_CT_APPLICATION_DATA    0x17

/* TLS handshake types */
#define TLS_HT_CLIENT_HELLO  0x01
#define TLS_HT_SERVER_HELLO  0x02

/* TLS versions on wire */
#define TLS_VER_1_0_HI  0x03
#define TLS_VER_1_0_LO  0x01
#define TLS_VER_1_2_HI  0x03
#define TLS_VER_1_2_LO  0x03

/* TLS extension types */
#define TLS_EXT_SERVER_NAME           0x0000
#define TLS_EXT_SUPPORTED_GROUPS      0x000A
#define TLS_EXT_SIGNATURE_ALGORITHMS  0x000D
#define TLS_EXT_SESSION_TICKET        0x0023
#define TLS_EXT_ENCRYPT_THEN_MAC      0x0016
#define TLS_EXT_EXTENDED_MASTER_SECRET 0x0017
#define TLS_EXT_PSK_KEY_EXCHANGE_MODES 0x002D
#define TLS_EXT_SUPPORTED_VERSIONS    0x002B
#define TLS_EXT_KEY_SHARE             0x0033

/* Named groups */
#define TLS_GROUP_X25519     0x001D
#define TLS_GROUP_SECP256R1  0x0017
#define TLS_GROUP_SECP384R1  0x0018

/* Signature algorithms */
#define TLS_SIG_RSA_PSS_RSAE_SHA256      0x0804
#define TLS_SIG_ECDSA_SECP256R1_SHA256   0x0403
#define TLS_SIG_ED25519                  0x0807

/* Cipher suites (TLS 1.3) */
#define TLS_CS_AES_256_GCM_SHA384       0x1302
#define TLS_CS_AES_128_GCM_SHA256       0x1301
#define TLS_CS_CHACHA20_POLY1305_SHA256 0x1303

#define FAKE_ENC_EXT_MIN  1500
#define FAKE_ENC_EXT_MAX  2500
#define FAKE_FINISHED_MIN  48
#define FAKE_FINISHED_MAX  64

struct dap_tls_mimicry {
    bool is_server;
    dap_tls_mimicry_state_t state;
    char *sni_hostname;
    uint8_t session_id[32];
    uint8_t client_random[32];
    uint8_t server_random[32];
};

/* ---- Helper: write big-endian ------------------------------------------- */

static inline void s_put_u16be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

static inline void s_put_u24be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 16) & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)(v & 0xFF);
}

static inline uint16_t s_get_u16be(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline uint32_t s_get_u24be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

/* ---- Helper: write TLS record header ------------------------------------ */

static void s_write_record_header(uint8_t *p, uint8_t content_type, uint16_t length)
{
    p[0] = content_type;
    p[1] = TLS_VER_1_2_HI;
    p[2] = TLS_VER_1_2_LO;
    s_put_u16be(p + 3, length);
}

/* ---- Create / Destroy --------------------------------------------------- */

dap_tls_mimicry_t *dap_tls_mimicry_new(bool a_is_server)
{
    dap_tls_mimicry_t *l_m = DAP_NEW_Z(dap_tls_mimicry_t);
    if (!l_m)
        return NULL;
    l_m->is_server = a_is_server;
    l_m->state = DAP_TLS_MIMICRY_STATE_INIT;
    return l_m;
}

void dap_tls_mimicry_free(dap_tls_mimicry_t *a_m)
{
    if (!a_m)
        return;
    DAP_DEL_Z(a_m->sni_hostname);
    DAP_DELETE(a_m);
}

void dap_tls_mimicry_set_sni(dap_tls_mimicry_t *a_m, const char *a_hostname)
{
    if (!a_m)
        return;
    DAP_DEL_Z(a_m->sni_hostname);
    if (a_hostname)
        a_m->sni_hostname = dap_strdup(a_hostname);
}

dap_tls_mimicry_state_t dap_tls_mimicry_get_state(const dap_tls_mimicry_t *a_m)
{
    return a_m ? a_m->state : DAP_TLS_MIMICRY_STATE_INIT;
}

/* ========================================================================== */
/*  ClientHello generation                                                    */
/* ========================================================================== */

static size_t s_build_extension_sni(uint8_t *buf, const char *hostname)
{
    if (!hostname || !hostname[0])
        return 0;
    size_t l_name_len = strlen(hostname);
    uint8_t *p = buf;

    s_put_u16be(p, TLS_EXT_SERVER_NAME); p += 2;
    uint16_t l_ext_len = (uint16_t)(2 + 1 + 2 + l_name_len);
    s_put_u16be(p, l_ext_len); p += 2;
    s_put_u16be(p, (uint16_t)(l_ext_len - 2)); p += 2;
    *p++ = 0x00; /* host_name type */
    s_put_u16be(p, (uint16_t)l_name_len); p += 2;
    memcpy(p, hostname, l_name_len); p += l_name_len;
    return (size_t)(p - buf);
}

static size_t s_build_extension_supported_groups(uint8_t *buf)
{
    uint8_t *p = buf;
    s_put_u16be(p, TLS_EXT_SUPPORTED_GROUPS); p += 2;
    s_put_u16be(p, 8); p += 2;
    s_put_u16be(p, 6); p += 2;
    s_put_u16be(p, TLS_GROUP_X25519); p += 2;
    s_put_u16be(p, TLS_GROUP_SECP256R1); p += 2;
    s_put_u16be(p, TLS_GROUP_SECP384R1); p += 2;
    return (size_t)(p - buf);
}

static size_t s_build_extension_sig_algorithms(uint8_t *buf)
{
    uint8_t *p = buf;
    s_put_u16be(p, TLS_EXT_SIGNATURE_ALGORITHMS); p += 2;
    s_put_u16be(p, 8); p += 2;
    s_put_u16be(p, 6); p += 2;
    s_put_u16be(p, TLS_SIG_RSA_PSS_RSAE_SHA256); p += 2;
    s_put_u16be(p, TLS_SIG_ECDSA_SECP256R1_SHA256); p += 2;
    s_put_u16be(p, TLS_SIG_ED25519); p += 2;
    return (size_t)(p - buf);
}

static size_t s_build_extension_supported_versions_ch(uint8_t *buf)
{
    uint8_t *p = buf;
    s_put_u16be(p, TLS_EXT_SUPPORTED_VERSIONS); p += 2;
    s_put_u16be(p, 3); p += 2; /* ext data len */
    *p++ = 2; /* list length */
    s_put_u16be(p, 0x0304); p += 2; /* TLS 1.3 */
    return (size_t)(p - buf);
}

static size_t s_build_extension_key_share_ch(uint8_t *buf)
{
    uint8_t *p = buf;
    uint8_t l_fake_pubkey[32];
    randombytes(l_fake_pubkey, 32);

    s_put_u16be(p, TLS_EXT_KEY_SHARE); p += 2;
    s_put_u16be(p, 2 + 2 + 2 + 32); p += 2; /* ext data len */
    s_put_u16be(p, 2 + 2 + 32); p += 2; /* client_shares len */
    s_put_u16be(p, TLS_GROUP_X25519); p += 2;
    s_put_u16be(p, 32); p += 2;
    memcpy(p, l_fake_pubkey, 32); p += 32;
    return (size_t)(p - buf);
}

static size_t s_build_extension_session_ticket(uint8_t *buf)
{
    uint8_t *p = buf;
    s_put_u16be(p, TLS_EXT_SESSION_TICKET); p += 2;
    s_put_u16be(p, 0); p += 2;
    return 4;
}

static size_t s_build_extension_encrypt_then_mac(uint8_t *buf)
{
    uint8_t *p = buf;
    s_put_u16be(p, TLS_EXT_ENCRYPT_THEN_MAC); p += 2;
    s_put_u16be(p, 0); p += 2;
    return 4;
}

static size_t s_build_extension_extended_master_secret(uint8_t *buf)
{
    uint8_t *p = buf;
    s_put_u16be(p, TLS_EXT_EXTENDED_MASTER_SECRET); p += 2;
    s_put_u16be(p, 0); p += 2;
    return 4;
}

static size_t s_build_extension_psk_kex_modes(uint8_t *buf)
{
    uint8_t *p = buf;
    s_put_u16be(p, TLS_EXT_PSK_KEY_EXCHANGE_MODES); p += 2;
    s_put_u16be(p, 2); p += 2;
    *p++ = 1; /* list length */
    *p++ = 1; /* psk_dhe_ke */
    return (size_t)(p - buf);
}

int dap_tls_mimicry_create_client_hello(dap_tls_mimicry_t *a_m,
                                        void **a_out, size_t *a_out_size)
{
    if (!a_m || !a_out || !a_out_size || a_m->is_server)
        return -1;

    randombytes(a_m->client_random, 32);
    randombytes(a_m->session_id, 32);

    /* Build ClientHello body into a temp buffer (max ~1KB is plenty) */
    uint8_t l_body[2048];
    uint8_t *p = l_body;

    /* client_version: TLS 1.2 on wire (real version in supported_versions ext) */
    *p++ = TLS_VER_1_2_HI;
    *p++ = TLS_VER_1_2_LO;

    /* random */
    memcpy(p, a_m->client_random, 32); p += 32;

    /* session_id */
    *p++ = 32;
    memcpy(p, a_m->session_id, 32); p += 32;

    /* cipher_suites */
    s_put_u16be(p, 6); p += 2;
    s_put_u16be(p, TLS_CS_AES_256_GCM_SHA384); p += 2;
    s_put_u16be(p, TLS_CS_AES_128_GCM_SHA256); p += 2;
    s_put_u16be(p, TLS_CS_CHACHA20_POLY1305_SHA256); p += 2;

    /* compression_methods: null only */
    *p++ = 1;
    *p++ = 0x00;

    /* extensions - build into sub-buffer, then prepend length */
    uint8_t l_ext_buf[1024];
    size_t l_ext_len = 0;
    l_ext_len += s_build_extension_sni(l_ext_buf + l_ext_len, a_m->sni_hostname);
    l_ext_len += s_build_extension_supported_groups(l_ext_buf + l_ext_len);
    l_ext_len += s_build_extension_sig_algorithms(l_ext_buf + l_ext_len);
    l_ext_len += s_build_extension_supported_versions_ch(l_ext_buf + l_ext_len);
    l_ext_len += s_build_extension_key_share_ch(l_ext_buf + l_ext_len);
    l_ext_len += s_build_extension_session_ticket(l_ext_buf + l_ext_len);
    l_ext_len += s_build_extension_encrypt_then_mac(l_ext_buf + l_ext_len);
    l_ext_len += s_build_extension_extended_master_secret(l_ext_buf + l_ext_len);
    l_ext_len += s_build_extension_psk_kex_modes(l_ext_buf + l_ext_len);

    s_put_u16be(p, (uint16_t)l_ext_len); p += 2;
    memcpy(p, l_ext_buf, l_ext_len); p += l_ext_len;

    size_t l_body_len = (size_t)(p - l_body);

    /* Wrap in Handshake message header (type + uint24 length) */
    size_t l_hs_len = 1 + 3 + l_body_len;

    /* Wrap in TLS record: header(5) + handshake */
    size_t l_total = 5 + l_hs_len;
    uint8_t *l_out = DAP_NEW_SIZE(uint8_t, l_total);
    if (!l_out)
        return -1;

    /* TLS record header: Handshake, TLS 1.0 in record layer (standard for ClientHello) */
    l_out[0] = TLS_CT_HANDSHAKE;
    l_out[1] = TLS_VER_1_0_HI;
    l_out[2] = TLS_VER_1_0_LO;
    s_put_u16be(l_out + 3, (uint16_t)l_hs_len);

    /* Handshake header */
    l_out[5] = TLS_HT_CLIENT_HELLO;
    s_put_u24be(l_out + 6, (uint32_t)l_body_len);

    /* Handshake body */
    memcpy(l_out + 9, l_body, l_body_len);

    *a_out = l_out;
    *a_out_size = l_total;
    a_m->state = DAP_TLS_MIMICRY_STATE_CLIENT_HELLO_SENT;
    return 0;
}

/* ========================================================================== */
/*  ServerHello + CCS + fake EncryptedExtensions                              */
/* ========================================================================== */

int dap_tls_mimicry_process_client_hello(dap_tls_mimicry_t *a_m,
                                         const void *a_data, size_t a_size,
                                         void **a_response, size_t *a_response_size)
{
    if (!a_m || !a_data || !a_response || !a_response_size || !a_m->is_server)
        return -1;

    const uint8_t *d = (const uint8_t *)a_data;

    /* Minimal validation: TLS record header */
    if (a_size < 5 || d[0] != TLS_CT_HANDSHAKE)
        return -1;
    uint16_t l_rec_len = s_get_u16be(d + 3);
    if (a_size < (size_t)(5 + l_rec_len))
        return -1;

    /* Parse just enough: handshake type + skip to extract session_id & random */
    const uint8_t *hs = d + 5;
    if (hs[0] != TLS_HT_CLIENT_HELLO)
        return -1;

    const uint8_t *body = hs + 4; /* skip type(1) + length(3) */
    /* body: version(2) + random(32) + session_id_len(1) + session_id(...) */
    if (body + 2 + 32 + 1 > d + a_size)
        return -1;

    memcpy(a_m->client_random, body + 2, 32);
    uint8_t l_sid_len = body[34];
    if (l_sid_len > 32 || body + 35 + l_sid_len > d + a_size)
        return -1;
    if (l_sid_len > 0)
        memcpy(a_m->session_id, body + 35, l_sid_len);
    else
        randombytes(a_m->session_id, 32);

    /* Generate server random */
    randombytes(a_m->server_random, 32);

    /* ---- Build ServerHello ---- */
    uint8_t l_sh_body[256];
    uint8_t *p = l_sh_body;

    /* server_version: TLS 1.2 on wire */
    *p++ = TLS_VER_1_2_HI;
    *p++ = TLS_VER_1_2_LO;

    memcpy(p, a_m->server_random, 32); p += 32;

    /* Echo session_id */
    *p++ = l_sid_len > 0 ? l_sid_len : 32;
    memcpy(p, a_m->session_id, l_sid_len > 0 ? l_sid_len : 32);
    p += (l_sid_len > 0 ? l_sid_len : 32);

    /* Selected cipher suite */
    s_put_u16be(p, TLS_CS_AES_256_GCM_SHA384); p += 2;

    /* Compression: null */
    *p++ = 0x00;

    /* Extensions: supported_versions + key_share */
    uint8_t l_sh_ext[128];
    size_t l_sh_ext_len = 0;

    /* supported_versions (server) */
    s_put_u16be(l_sh_ext + l_sh_ext_len, TLS_EXT_SUPPORTED_VERSIONS);
    l_sh_ext_len += 2;
    s_put_u16be(l_sh_ext + l_sh_ext_len, 2);
    l_sh_ext_len += 2;
    s_put_u16be(l_sh_ext + l_sh_ext_len, 0x0304); /* TLS 1.3 */
    l_sh_ext_len += 2;

    /* key_share (server) */
    uint8_t l_fake_pubkey[32];
    randombytes(l_fake_pubkey, 32);
    s_put_u16be(l_sh_ext + l_sh_ext_len, TLS_EXT_KEY_SHARE);
    l_sh_ext_len += 2;
    s_put_u16be(l_sh_ext + l_sh_ext_len, 2 + 2 + 32);
    l_sh_ext_len += 2;
    s_put_u16be(l_sh_ext + l_sh_ext_len, TLS_GROUP_X25519);
    l_sh_ext_len += 2;
    s_put_u16be(l_sh_ext + l_sh_ext_len, 32);
    l_sh_ext_len += 2;
    memcpy(l_sh_ext + l_sh_ext_len, l_fake_pubkey, 32);
    l_sh_ext_len += 32;

    s_put_u16be(p, (uint16_t)l_sh_ext_len); p += 2;
    memcpy(p, l_sh_ext, l_sh_ext_len); p += l_sh_ext_len;

    size_t l_sh_body_len = (size_t)(p - l_sh_body);
    size_t l_sh_hs_len = 1 + 3 + l_sh_body_len;

    /* ---- Build ChangeCipherSpec record ---- */
    /* CCS: 1 byte payload = 0x01 */
    size_t l_ccs_record_len = 5 + 1;

    /* ---- Build fake EncryptedExtensions (random bytes in AppData record) ---- */
    uint16_t l_fake_len = (uint16_t)(FAKE_ENC_EXT_MIN +
        (m_dap_random_u16() % (FAKE_ENC_EXT_MAX - FAKE_ENC_EXT_MIN + 1)));
    size_t l_fake_record_len = 5 + l_fake_len;

    /* Total response */
    size_t l_total = (5 + l_sh_hs_len) + l_ccs_record_len + l_fake_record_len;
    uint8_t *l_out = DAP_NEW_SIZE(uint8_t, l_total);
    if (!l_out)
        return -1;

    uint8_t *w = l_out;

    /* ServerHello record */
    s_write_record_header(w, TLS_CT_HANDSHAKE, (uint16_t)l_sh_hs_len);
    w += 5;
    *w++ = TLS_HT_SERVER_HELLO;
    s_put_u24be(w, (uint32_t)l_sh_body_len); w += 3;
    memcpy(w, l_sh_body, l_sh_body_len); w += l_sh_body_len;

    /* ChangeCipherSpec record */
    s_write_record_header(w, TLS_CT_CHANGE_CIPHER_SPEC, 1);
    w += 5;
    *w++ = 0x01;

    /* Fake EncryptedExtensions (Application Data record with random payload) */
    s_write_record_header(w, TLS_CT_APPLICATION_DATA, l_fake_len);
    w += 5;
    randombytes(w, l_fake_len);
    w += l_fake_len;

    *a_response = l_out;
    *a_response_size = l_total;
    a_m->state = DAP_TLS_MIMICRY_STATE_ESTABLISHED;
    return 0;
}

/* ========================================================================== */
/*  Client: process ServerHello response                                      */
/* ========================================================================== */

int dap_tls_mimicry_process_server_hello(dap_tls_mimicry_t *a_m,
                                         const void *a_data, size_t a_size,
                                         void **a_response, size_t *a_response_size)
{
    if (!a_m || !a_data || !a_response || !a_response_size || a_m->is_server)
        return -1;

    const uint8_t *d = (const uint8_t *)a_data;
    size_t l_pos = 0;

    /* Consume ServerHello record */
    if (l_pos + 5 > a_size || d[l_pos] != TLS_CT_HANDSHAKE)
        return -1;
    uint16_t l_sh_rec_len = s_get_u16be(d + l_pos + 3);
    l_pos += 5 + l_sh_rec_len;

    /* Consume ChangeCipherSpec record (optional but expected) */
    if (l_pos + 5 <= a_size && d[l_pos] == TLS_CT_CHANGE_CIPHER_SPEC) {
        uint16_t l_ccs_len = s_get_u16be(d + l_pos + 3);
        l_pos += 5 + l_ccs_len;
    }

    /* Consume fake EncryptedExtensions (Application Data records) */
    while (l_pos + 5 <= a_size && d[l_pos] == TLS_CT_APPLICATION_DATA) {
        uint16_t l_ad_len = s_get_u16be(d + l_pos + 3);
        if (l_pos + 5 + l_ad_len > a_size)
            break;
        l_pos += 5 + l_ad_len;
    }

    /* Build client response: CCS + fake Finished (Application Data) */
    uint16_t l_fin_len = (uint16_t)(FAKE_FINISHED_MIN +
        (m_dap_random_u16() % (FAKE_FINISHED_MAX - FAKE_FINISHED_MIN + 1)));
    size_t l_total = (5 + 1) + (5 + l_fin_len);

    uint8_t *l_out = DAP_NEW_SIZE(uint8_t, l_total);
    if (!l_out)
        return -1;

    uint8_t *w = l_out;

    /* ChangeCipherSpec */
    s_write_record_header(w, TLS_CT_CHANGE_CIPHER_SPEC, 1);
    w += 5;
    *w++ = 0x01;

    /* Fake Finished */
    s_write_record_header(w, TLS_CT_APPLICATION_DATA, l_fin_len);
    w += 5;
    randombytes(w, l_fin_len);
    w += l_fin_len;

    *a_response = l_out;
    *a_response_size = l_total;
    a_m->state = DAP_TLS_MIMICRY_STATE_ESTABLISHED;
    return 0;
}

/* ========================================================================== */
/*  Record Layer: wrap / unwrap                                               */
/* ========================================================================== */

int dap_tls_mimicry_wrap(dap_tls_mimicry_t *a_m,
                         const void *a_data, size_t a_size,
                         void **a_out, size_t *a_out_size)
{
    if (!a_m || !a_data || !a_out || !a_out_size || a_size == 0)
        return -1;

    /* Calculate number of records needed */
    size_t l_num_records = (a_size + DAP_TLS_MIMICRY_MAX_RECORD_PAYLOAD - 1)
                         / DAP_TLS_MIMICRY_MAX_RECORD_PAYLOAD;
    size_t l_total = l_num_records * DAP_TLS_MIMICRY_RECORD_HDR_SIZE + a_size;

    uint8_t *l_out = DAP_NEW_SIZE(uint8_t, l_total);
    if (!l_out)
        return -1;

    const uint8_t *src = (const uint8_t *)a_data;
    uint8_t *dst = l_out;
    size_t l_remaining = a_size;

    while (l_remaining > 0) {
        uint16_t l_chunk = (l_remaining > DAP_TLS_MIMICRY_MAX_RECORD_PAYLOAD)
                         ? DAP_TLS_MIMICRY_MAX_RECORD_PAYLOAD
                         : (uint16_t)l_remaining;
        s_write_record_header(dst, TLS_CT_APPLICATION_DATA, l_chunk);
        dst += DAP_TLS_MIMICRY_RECORD_HDR_SIZE;
        memcpy(dst, src, l_chunk);
        dst += l_chunk;
        src += l_chunk;
        l_remaining -= l_chunk;
    }

    *a_out = l_out;
    *a_out_size = l_total;
    return 0;
}

int dap_tls_mimicry_unwrap(dap_tls_mimicry_t *a_m,
                           const void *a_data, size_t a_size,
                           void **a_out, size_t *a_out_size,
                           size_t *a_consumed)
{
    if (!a_m || !a_data || !a_out || !a_out_size || !a_consumed)
        return -1;

    *a_out = NULL;
    *a_out_size = 0;
    *a_consumed = 0;

    if (a_size < DAP_TLS_MIMICRY_RECORD_HDR_SIZE)
        return 1; /* need more data */

    const uint8_t *d = (const uint8_t *)a_data;

    /* Collect all complete records from the input */
    size_t l_pos = 0;
    size_t l_payload_total = 0;

    /* First pass: count total payload */
    while (l_pos + DAP_TLS_MIMICRY_RECORD_HDR_SIZE <= a_size) {
        if (d[l_pos] != TLS_CT_APPLICATION_DATA)
            break;
        uint16_t l_rec_len = s_get_u16be(d + l_pos + 3);
        if (l_pos + DAP_TLS_MIMICRY_RECORD_HDR_SIZE + l_rec_len > a_size)
            break; /* partial record */
        l_payload_total += l_rec_len;
        l_pos += DAP_TLS_MIMICRY_RECORD_HDR_SIZE + l_rec_len;
    }

    if (l_payload_total == 0) {
        if (l_pos == 0)
            return 1; /* need more data */
        *a_consumed = l_pos;
        return 0;
    }

    uint8_t *l_out = DAP_NEW_SIZE(uint8_t, l_payload_total);
    if (!l_out)
        return -1;

    /* Second pass: copy payloads */
    size_t l_pos2 = 0;
    size_t l_out_pos = 0;
    while (l_pos2 < l_pos) {
        uint16_t l_rec_len = s_get_u16be(d + l_pos2 + 3);
        memcpy(l_out + l_out_pos, d + l_pos2 + DAP_TLS_MIMICRY_RECORD_HDR_SIZE, l_rec_len);
        l_out_pos += l_rec_len;
        l_pos2 += DAP_TLS_MIMICRY_RECORD_HDR_SIZE + l_rec_len;
    }

    *a_out = l_out;
    *a_out_size = l_payload_total;
    *a_consumed = l_pos;
    return 0;
}
