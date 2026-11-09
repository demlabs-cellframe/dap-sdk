/**
 * @file reality_fp_build.c
 * @brief Patch a wire-captured ClientHello template with per-connection values.
 *
 * Only these fields change: random, session_id (sealed auth), ML-KEM ek,
 * X25519 pub, SNI hostname. Everything else stays byte-identical to the
 * capture (JA3/JA4 stable).
 */

#include "reality_internal.h"
#include <stdlib.h>

static void put16(uint8_t *a_p, unsigned a_v) { a_p[0] = (uint8_t)(a_v >> 8); a_p[1] = (uint8_t)a_v; }
static void put24(uint8_t *a_p, unsigned a_v)
{ a_p[0] = (uint8_t)(a_v >> 16); a_p[1] = (uint8_t)(a_v >> 8); a_p[2] = (uint8_t)a_v; }

int reality_fp_build_clienthello(const reality_fp_template_t *a_fp,
                                 const uint8_t a_random[32],
                                 const uint8_t a_session_id[32],
                                 const uint8_t a_mlkem_ek[1184],
                                 const uint8_t a_hybrid_x25519_pub[32],
                                 const uint8_t a_auth_x25519_pub[32],
                                 const char *a_sni,
                                 uint8_t **a_out, size_t *a_out_size)
{
    if (!a_fp || !a_out || !a_out_size || !a_random || !a_session_id ||
        !a_mlkem_ek || !a_hybrid_x25519_pub || !a_auth_x25519_pub || !a_sni || !a_sni[0])
        return REALITY_ERR_PARAM;

    size_t sni_len = strlen(a_sni);
    if (sni_len > 255) return REALITY_ERR_PARAM;

    long delta = (long)sni_len - (long)a_fp->sni_name_len;
    size_t total = a_fp->clienthello_size + (delta > 0 ? (size_t)delta : 0);

    uint8_t *b = (uint8_t *)malloc(total);
    if (!b) return REALITY_ERR_NOMEM;

    memcpy(b, a_fp->clienthello, a_fp->clienthello_size);

    /* SNI extension base offsets (static, before any shift) */
    size_t ext_off = a_fp->sni_name_off - 5;          /* ext header start */
    size_t ext_elen_off = ext_off + 2;                /* ext length field */
    size_t list_len_off = ext_off + 4;                /* server_name_list length */
    size_t name_len_off = a_fp->sni_name_off - 2;     /* hostname length field */
    size_t tail_off = a_fp->sni_name_off + a_fp->sni_name_len;

    if (delta != 0) {
        /* shift tail, patch the three lengths + handshake length + extensions length */
        memmove(b + tail_off + delta, b + tail_off, a_fp->clienthello_size - tail_off);
        put16(b + ext_elen_off, (unsigned)(5 + sni_len));
        put16(b + list_len_off, (unsigned)(3 + sni_len));
        put16(b + name_len_off, (unsigned)sni_len);
        put24(b + 1, (unsigned)(a_fp->clienthello_size + delta - 4));
        /* extensions_length: static offset for this template family (after
         * sid/ciphers/compression); recompute instead of hardcoding: it is the
         * 2 bytes right before ext region start. ext region starts at
         * (session_id_off + 32) + 2 (cs len field) + cs_len + 1 (comp len) + comp. */
        size_t p = a_fp->session_id_off + 32;
        size_t cs_len = (size_t)((b[p] << 8) | b[p + 1]); p += 2 + cs_len;
        p += 1 + b[p]; /* compression */
        put16(b + p, (unsigned)(a_fp->clienthello_size + delta - (p + 2)));
    }
    memcpy(b + a_fp->sni_name_off, a_sni, sni_len);

    /* per-connection fields (offsets < SNI are static; >= SNI shift by delta) */
    memcpy(b + a_fp->random_off, a_random, 32);
    memcpy(b + a_fp->session_id_off, a_session_id, 32);
    size_t hyb = a_fp->ks_hybrid_off + (size_t)delta;
    memcpy(b + hyb, a_mlkem_ek, 1184);
    memcpy(b + hyb + 1184, a_hybrid_x25519_pub, 32); /* hybrid tail: session ECDH */
    memcpy(b + a_fp->ks_x25519_off + (size_t)delta, a_auth_x25519_pub, 32); /* plain: auth ECDH */

    *a_out = b;
    *a_out_size = a_fp->clienthello_size + (size_t)delta;
    return REALITY_ERR_NONE;
}
