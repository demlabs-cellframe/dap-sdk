/**
 * @file reality_auth.c
 * @brief REALITY auth: seal client credentials into ClientHello session_id and
 *        verify the server's ephemeral certificate evidence.
 *
 * Wire format mirror of Xray v26.10.31:
 *   authPlain[16] = {Version_x 26, Version_y 10, Version_z 31, 0,
 *                    unix_time BE32, shortId...}
 *   shared  = X25519(auth_keypair_priv, server_static_pub)   [PLAIN share]
 *   authKey = HKDF-SHA256(ikm=shared, salt=random[:20], info="REALITY", 32)
 *   session_id = AES-256-GCM(authKey, nonce=random[20:32], pt=authPlain,
 *                            aad=clienthello_with_session_id_zeroed)  -> 32B
 *
 * Server evidence: DER ed25519 cert where signature VALUE (last 64 bytes of
 * the DER) equals HMAC-SHA512(authKey, ed25519_public_key_bytes).
 */

#include "reality_internal.h"
#include <stdlib.h>
#include <stdio.h>

static const uint8_t REALITY_INFO[] = { 'R','E','A','L','I','T','Y' };
static const uint8_t REALITY_VER[3] = { 26, 10, 31 }; /* pinned server version */

int reality_b64url_decode(const char *a_in, uint8_t *a_out, size_t a_out_cap, size_t *a_out_len)
{
    if (!a_in || !a_out || !a_out_len) return REALITY_ERR_PARAM;
    static const int8_t rev[256] = {
        ['A']=0, ['B']=1, ['C']=2, ['D']=3, ['E']=4, ['F']=5, ['G']=6, ['H']=7,
        ['I']=8, ['J']=9, ['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,['P']=15,
        ['Q']=16,['R']=17,['S']=18,['T']=19,['U']=20,['V']=21,['W']=22,['X']=23,
        ['Y']=24,['Z']=25,['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,
        ['g']=32,['h']=33,['i']=34,['j']=35,['k']=36,['l']=37,['m']=38,['n']=39,
        ['o']=40,['p']=41,['q']=42,['r']=43,['s']=44,['t']=45,['u']=46,['v']=47,
        ['w']=48,['x']=49,['y']=50,['z']=51,['0']=52,['1']=53,['2']=54,['3']=55,
        ['4']=56,['5']=57,['6']=58,['7']=59,['8']=60,['9']=61,['-']=62,['_']=63
    };
    size_t len = strlen(a_in), out = 0, acc = 0, bits = 0;
    for (size_t i = 0; i < len; i++) {
        char c = a_in[i];
        if (c == '=') break;
        if (rev[(uint8_t)c] < 0 && !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                     (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return REALITY_ERR_PARAM;
        int8_t v = -1;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '-' || c == '_') {
            /* re-lookup through the table */
            v = rev[(uint8_t)c];
        }
        if (v < 0) return REALITY_ERR_PARAM;
        acc = (acc << 6) | (uint32_t)v; bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (out >= a_out_cap) return REALITY_ERR_PARAM;
            a_out[out++] = (uint8_t)(acc >> bits);
        }
    }
    *a_out_len = out;
    return REALITY_ERR_NONE;
}

int reality_hex_decode(const char *a_in, uint8_t *a_out, size_t a_out_cap, size_t *a_out_len)
{
    if (!a_in || !a_out || !a_out_len) return REALITY_ERR_PARAM;
    size_t len = strlen(a_in);
    if (len % 2) return REALITY_ERR_PARAM;
    if (len / 2 > a_out_cap) return REALITY_ERR_PARAM;
    for (size_t i = 0; i < len; i += 2) {
        unsigned v;
        if (sscanf(a_in + i, "%2x", &v) != 1) return REALITY_ERR_PARAM;
        a_out[i / 2] = (uint8_t)v;
    }
    *a_out_len = len / 2;
    return REALITY_ERR_NONE;
}

int reality_uuid_parse(const char *a_str, uint8_t a_out[16])
{
    if (!a_str || strlen(a_str) != 36) return REALITY_ERR_PARAM;
    unsigned b[16];
    if (sscanf(a_str, "%2x%2x%2x%2x-%2x%2x-%2x%2x-%2x%2x-%2x%2x%2x%2x%2x%2x",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7],
               &b[8], &b[9], &b[10], &b[11], &b[12], &b[13], &b[14], &b[15]) != 16)
        return REALITY_ERR_PARAM;
    for (int i = 0; i < 16; i++) a_out[i] = (uint8_t)b[i];
    return REALITY_ERR_NONE;
}

int reality_auth_seal(reality_session_t *a_s,
                      const uint8_t a_eph_priv[32],
                      const uint8_t a_server_static_pub[32],
                      const char *a_short_id_hex,
                      const uint8_t *a_ch_zero_sid, size_t a_ch_len,
                      const uint8_t a_random[32],
                      uint8_t a_out_session_id[32])
{
    if (!a_s || !a_eph_priv || !a_server_static_pub || !a_ch_zero_sid || !a_random)
        return REALITY_ERR_PARAM;

    uint8_t shared[32];
    if (a_s->crypto->x25519_shared(a_s->crypto->user, shared, a_eph_priv, a_server_static_pub) != 0)
        RFAIL(a_s, REALITY_ERR_CRYPTO, "auth: x25519_shared failed");
    static const uint8_t zero[32] = {0};
    if (memcmp(shared, zero, 32) == 0)
        RFAIL(a_s, REALITY_ERR_CRYPTO, "auth: all-zero shared key");

    uint8_t auth_key[32];
    if (a_s->crypto->hkdf(a_s->crypto->user, /*sha384=*/0,
                          a_random /*salt[:20] handled below*/, 20,
                          shared, 32, REALITY_INFO, sizeof(REALITY_INFO), auth_key, 32) != 0)
        RFAIL(a_s, REALITY_ERR_CRYPTO, "auth: hkdf failed");
    memcpy(a_s->auth_key, auth_key, 32);

    /* plaintext[16] */
    uint8_t pt[16] = {0};
    pt[0] = REALITY_VER[0]; pt[1] = REALITY_VER[1]; pt[2] = REALITY_VER[2]; pt[3] = 0;
    int64_t now = a_s->time->unix_now(a_s->time->user);
    pt[4] = (uint8_t)(now >> 24); pt[5] = (uint8_t)(now >> 16);
    pt[6] = (uint8_t)(now >> 8);  pt[7] = (uint8_t)now;
    size_t sid_len = 0;
    uint8_t sid_bin[REALITY_SHORTID_MAX];
    if (reality_hex_decode(a_short_id_hex, sid_bin, sizeof(sid_bin), &sid_len) != 0)
        RFAIL(a_s, REALITY_ERR_PARAM, "auth: bad short_id hex");
    if (sid_len > 8) sid_len = 8;
    memcpy(pt + 8, sid_bin, sid_len);

    /* seal: ct(16) + tag(16) = 32 == session_id */
    size_t out_len = 0;
    if (a_s->crypto->aead_seal(a_s->crypto->user, /*algo 0 = AES-GCM*/ 0,
                               auth_key, 32, a_random + 20,
                               a_ch_zero_sid, a_ch_len, pt, 16,
                               a_out_session_id, &out_len) != 0 || out_len != 32)
        RFAIL(a_s, REALITY_ERR_CRYPTO, "auth: aead_seal failed");
    return REALITY_ERR_NONE;
}

/* minimal DER scan: find ed25519 SPKI public key + signature value.
 * The REALITY server cert is a fixed empty ed25519 self-signed cert; its DER:
 * ... SubjectPublicKeyInfo(44B: SEQ{SEQ{OID 1.3.101.112}, BIT STRING 0x00||32B}) ...
 * ... signatureAlgorithm || BIT STRING(0x00||64B sig) at the tail. */
static int cert_extract_ed25519_pub(const uint8_t *a_der, size_t a_len, const uint8_t **a_pub)
{
    /* locate OID 1.3.101.112 ed25519: 06 03 2B 65 70 */
    static const uint8_t oid[] = {0x06, 0x03, 0x2B, 0x65, 0x70};
    for (size_t i = 0; i + 5 + 34 <= a_len; i++) {
        if (memcmp(a_der + i, oid, sizeof(oid)) == 0) {
            /* expect: OID, then BIT STRING tag 03 len 21 00 || 32B */
            size_t j = i + 5;
            if (a_der[j] == 0x03 && a_der[j + 1] == 0x21 && a_der[j + 2] == 0x00) {
                *a_pub = a_der + j + 3;
                return 0;
            }
        }
    }
    return -1;
}

int reality_auth_verify_server_cert(reality_session_t *a_s,
                                    const uint8_t *a_cert_der, size_t a_cert_len)
{
    if (!a_s || !a_cert_der || a_cert_len < 128) return REALITY_ERR_PARAM;

    const uint8_t *pub = NULL;
    if (cert_extract_ed25519_pub(a_cert_der, a_cert_len, &pub) != 0)
        RFAIL(a_s, REALITY_ERR_VERIFY, "cert: no ed25519 key in server cert");

    /* evidence = HMAC-SHA512(authKey, ed25519_pub) placed into the signature
     * value: last 64 bytes of the DER */
    uint8_t mac[64];
    if (a_s->crypto->hmac(a_s->crypto->user, /*2=SHA512*/ 2,
                          a_s->auth_key, 32, pub, 32, mac) != 0)
        RFAIL(a_s, REALITY_ERR_CRYPTO, "cert: hmac-sha512 failed");

    if (memcmp(mac, a_cert_der + a_cert_len - 64, 64) != 0)
        RFAIL(a_s, REALITY_ERR_VERIFY,
              "REALITY: received real certificate (potential MITM or redirection)");
    return REALITY_ERR_NONE;
}
