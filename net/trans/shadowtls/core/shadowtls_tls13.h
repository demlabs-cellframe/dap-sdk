/**
 * @file shadowtls_tls13.h
 * @brief Internal manual TLS 1.3 client state for ShadowTLS (core only).
 */

#ifndef SHADOWTLS_TLS13_H
#define SHADOWTLS_TLS13_H

#include "shadowtls/shadowtls.h"
#include <openssl/hmac.h>

typedef struct stls_tls {
    shadowtls_io_ops_t *io;
    uint8_t inbuf[16384 + 256];
    size_t inbuf_len;
    uint8_t th_buf[64 * 1024];
    size_t th_len;
    uint8_t client_random[32];
    uint8_t server_random[32];
    int cipher;
    int hash384;
    size_t hash_len;
    uint8_t c_key[32], s_key[32];
    size_t c_key_len, s_key_len;
    uint8_t c_iv[12], s_iv[12];
    uint64_t c_seq, s_seq;
    bool handshake_done;
    /* ShadowTLS residual-handshake filter (server XORs handshake AppData) */
    HMAC_CTX *hmac_sr;
    uint8_t xor_key[32];
    bool have_sr;
    bool authorized;
    /* v2: capture all raw server bytes during handshake (HMAC challenge) */
    uint8_t *hs_capture;
    size_t hs_capture_len;
} stls_tls_t;

/* Perform the relayed TLS 1.3 handshake (signed ClientHello SessionID).
 * Extracts server_random. Returns SHADOWTLS_ERR_NONE on success. */
int stls_tls_handshake(stls_tls_t *t, const char *sni, const char *password);

#endif /* SHADOWTLS_TLS13_H */