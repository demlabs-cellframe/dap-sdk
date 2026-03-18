/*
 * ChaCha20-Poly1305 AEAD (RFC 8439).
 * Reference implementation — constant-time on platforms without
 * variable-time multiply.
 */

#include <stdlib.h>
#include <string.h>
#include "dap_chacha20_poly1305.h"

/* ─── helpers ──────────────────────────────────────────────────────── */

static inline uint32_t s_rotl32(uint32_t v, int n)
{
    return (v << n) | (v >> (32 - n));
}

static inline uint32_t s_load32_le(const uint8_t *p)
{
    return (uint32_t)p[0]
         | (uint32_t)p[1] << 8
         | (uint32_t)p[2] << 16
         | (uint32_t)p[3] << 24;
}

static inline void s_store32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static inline void s_store64_le(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

/* ─── ChaCha20 (RFC 8439 §2.3) ────────────────────────────────────── */

#define QR(a, b, c, d) do { \
    a += b; d ^= a; d = s_rotl32(d, 16); \
    c += d; b ^= c; b = s_rotl32(b, 12); \
    a += b; d ^= a; d = s_rotl32(d, 8);  \
    c += d; b ^= c; b = s_rotl32(b, 7);  \
} while (0)

void dap_chacha20_block(uint32_t a_out[16], const uint32_t a_in[16])
{
    uint32_t x[16];
    memcpy(x, a_in, 64);
    for (int i = 0; i < 10; i++) {
        QR(x[0], x[4], x[ 8], x[12]);
        QR(x[1], x[5], x[ 9], x[13]);
        QR(x[2], x[6], x[10], x[14]);
        QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]);
        QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[ 8], x[13]);
        QR(x[3], x[4], x[ 9], x[14]);
    }
    for (int i = 0; i < 16; i++)
        a_out[i] = x[i] + a_in[i];
}

void dap_chacha20_encrypt(uint8_t *a_out, const uint8_t *a_in, size_t a_len,
        const uint8_t a_key[DAP_CHACHA20_KEY_SIZE],
        const uint8_t a_nonce[DAP_CHACHA20_NONCE_SIZE], uint32_t a_counter)
{
    uint32_t state[16];
    state[0] = 0x61707865; state[1] = 0x3320646e;
    state[2] = 0x79622d32; state[3] = 0x6b206574;
    for (int i = 0; i < 8; i++)
        state[4 + i] = s_load32_le(a_key + 4 * i);
    state[13] = s_load32_le(a_nonce);
    state[14] = s_load32_le(a_nonce + 4);
    state[15] = s_load32_le(a_nonce + 8);

    uint32_t ks[16];
    while (a_len > 0) {
        state[12] = a_counter++;
        dap_chacha20_block(ks, state);

        uint8_t ks_bytes[DAP_CHACHA20_BLOCK_SIZE];
        for (int i = 0; i < 16; i++)
            s_store32_le(ks_bytes + 4 * i, ks[i]);

        size_t todo = a_len < DAP_CHACHA20_BLOCK_SIZE ? a_len : DAP_CHACHA20_BLOCK_SIZE;
        for (size_t i = 0; i < todo; i++)
            a_out[i] = a_in[i] ^ ks_bytes[i];
        a_out += todo;
        a_in  += todo;
        a_len -= todo;
    }
}

/* ─── Poly1305 (RFC 8439 §2.5) — 26-bit limb arithmetic ─────────── */

void dap_poly1305_mac(uint8_t a_tag[DAP_POLY1305_TAG_SIZE],
        const uint8_t *a_msg, size_t a_msg_len,
        const uint8_t a_key[DAP_POLY1305_KEY_SIZE])
{
    /* Load and clamp r (RFC 8439 §2.5) */
    uint32_t rt0 = s_load32_le(a_key)      & 0x0fffffff;
    uint32_t rt1 = s_load32_le(a_key + 4)  & 0x0ffffffc;
    uint32_t rt2 = s_load32_le(a_key + 8)  & 0x0ffffffc;
    uint32_t rt3 = s_load32_le(a_key + 12) & 0x0ffffffc;

    /* Split clamped r into five 26-bit limbs */
    uint32_t r0 =  rt0                          & 0x03ffffff;
    uint32_t r1 = ((rt0 >> 26) | (rt1 <<  6))   & 0x03ffffff;
    uint32_t r2 = ((rt1 >> 20) | (rt2 << 12))   & 0x03ffffff;
    uint32_t r3 = ((rt2 >> 14) | (rt3 << 18))   & 0x03ffffff;
    uint32_t r4 =  (rt3 >>  8);

    uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;

    uint32_t h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;

    while (a_msg_len > 0) {
        /* Build padded 17-byte block: msg_bytes || 0x01 || zeros */
        uint8_t blk[17];
        memset(blk, 0, sizeof(blk));
        size_t blen = a_msg_len >= 16 ? 16 : a_msg_len;
        memcpy(blk, a_msg, blen);
        blk[blen] = 1;

        /* Load 130-bit block into 26-bit limbs */
        uint32_t b0 = s_load32_le(blk);
        uint32_t b1 = s_load32_le(blk + 4);
        uint32_t b2 = s_load32_le(blk + 8);
        uint32_t b3 = s_load32_le(blk + 12);
        uint32_t b4 = blk[16];

        h0 +=  b0                        & 0x03ffffff;
        h1 += ((b0 >> 26) | (b1 <<  6)) & 0x03ffffff;
        h2 += ((b1 >> 20) | (b2 << 12)) & 0x03ffffff;
        h3 += ((b2 >> 14) | (b3 << 18)) & 0x03ffffff;
        h4 +=  (b3 >>  8)               | (b4 << 24);

        /* h *= r mod 2^130 - 5 */
        uint64_t d0 = (uint64_t)h0*r0 + (uint64_t)h1*s4 + (uint64_t)h2*s3 + (uint64_t)h3*s2 + (uint64_t)h4*s1;
        uint64_t d1 = (uint64_t)h0*r1 + (uint64_t)h1*r0 + (uint64_t)h2*s4 + (uint64_t)h3*s3 + (uint64_t)h4*s2;
        uint64_t d2 = (uint64_t)h0*r2 + (uint64_t)h1*r1 + (uint64_t)h2*r0 + (uint64_t)h3*s4 + (uint64_t)h4*s3;
        uint64_t d3 = (uint64_t)h0*r3 + (uint64_t)h1*r2 + (uint64_t)h2*r1 + (uint64_t)h3*r0 + (uint64_t)h4*s4;
        uint64_t d4 = (uint64_t)h0*r4 + (uint64_t)h1*r3 + (uint64_t)h2*r2 + (uint64_t)h3*r1 + (uint64_t)h4*r0;

        uint32_t c;
        c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x03ffffff; d1 += c;
        c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x03ffffff; d2 += c;
        c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x03ffffff; d3 += c;
        c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x03ffffff; d4 += c;
        c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x03ffffff;
        h0 += c * 5;
        c = h0 >> 26; h0 &= 0x03ffffff; h1 += c;

        a_msg     += blen;
        a_msg_len -= blen;
    }

    /* Final carry + full reduction */
    uint32_t c;
    c = h1 >> 26; h1 &= 0x03ffffff; h2 += c;
    c = h2 >> 26; h2 &= 0x03ffffff; h3 += c;
    c = h3 >> 26; h3 &= 0x03ffffff; h4 += c;
    c = h4 >> 26; h4 &= 0x03ffffff; h0 += c * 5;
    c = h0 >> 26; h0 &= 0x03ffffff; h1 += c;

    /* Compute h - (2^130 - 5) = h + 5 - 2^130 */
    uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x03ffffff;
    uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x03ffffff;
    uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x03ffffff;
    uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x03ffffff;
    uint32_t g4 = h4 + c - (1u << 26);

    /* Select h or g: if g4 underflowed (bit 31 set), keep h */
    uint32_t mask = (g4 >> 31) - 1;
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0; h1 = (h1 & mask) | g1;
    h2 = (h2 & mask) | g2; h3 = (h3 & mask) | g3;
    h4 = (h4 & mask) | g4;

    /* Reassemble h into 4x32-bit words */
    uint64_t f;
    f = (uint64_t)h0 | ((uint64_t)h1 << 26);
    uint32_t w0 = (uint32_t)f; f >>= 32;
    f += (uint64_t)h2 << 20;
    uint32_t w1 = (uint32_t)f; f >>= 32;
    f += (uint64_t)h3 << 14;
    uint32_t w2 = (uint32_t)f; f >>= 32;
    f += (uint64_t)h4 << 8;
    uint32_t w3 = (uint32_t)f;

    /* tag = (h + s) mod 2^128 */
    uint32_t sk0 = s_load32_le(a_key + 16);
    uint32_t sk1 = s_load32_le(a_key + 20);
    uint32_t sk2 = s_load32_le(a_key + 24);
    uint32_t sk3 = s_load32_le(a_key + 28);

    f = (uint64_t)w0 + sk0;             s_store32_le(a_tag,      (uint32_t)f);
    f = (uint64_t)w1 + sk1 + (f >> 32); s_store32_le(a_tag + 4,  (uint32_t)f);
    f = (uint64_t)w2 + sk2 + (f >> 32); s_store32_le(a_tag + 8,  (uint32_t)f);
    f = (uint64_t)w3 + sk3 + (f >> 32); s_store32_le(a_tag + 12, (uint32_t)f);
}

/* ─── AEAD construction (RFC 8439 §2.8) ──────────────────────────── */

int dap_chacha20_poly1305_seal(uint8_t *a_ct, uint8_t a_tag[DAP_CHACHA20_POLY1305_TAG_SIZE],
        const uint8_t *a_pt, size_t a_pt_len,
        const uint8_t *a_aad, size_t a_aad_len,
        const uint8_t a_key[DAP_CHACHA20_KEY_SIZE],
        const uint8_t a_nonce[DAP_CHACHA20_NONCE_SIZE])
{
    uint8_t poly_key[64];
    memset(poly_key, 0, sizeof(poly_key));
    dap_chacha20_encrypt(poly_key, poly_key, sizeof(poly_key), a_key, a_nonce, 0);

    dap_chacha20_encrypt(a_ct, a_pt, a_pt_len, a_key, a_nonce, 1);

    /* Build MAC input: aad || pad16(aad) || ct || pad16(ct) || len64(aad) || len64(ct) */
    size_t aad_padded = a_aad_len + ((16 - (a_aad_len & 0xf)) & 0xf);
    size_t ct_padded  = a_pt_len  + ((16 - (a_pt_len  & 0xf)) & 0xf);
    size_t mac_len = aad_padded + ct_padded + 16;

    uint8_t stack_buf[512];
    uint8_t *mac_data = mac_len <= sizeof(stack_buf) ? stack_buf : (uint8_t *)malloc(mac_len);
    if (!mac_data) return -1;
    memset(mac_data, 0, mac_len);

    size_t pos = 0;
    if (a_aad_len) { memcpy(mac_data, a_aad, a_aad_len); }
    pos = aad_padded;
    memcpy(mac_data + pos, a_ct, a_pt_len);
    pos = aad_padded + ct_padded;
    s_store64_le(mac_data + pos, (uint64_t)a_aad_len);
    s_store64_le(mac_data + pos + 8, (uint64_t)a_pt_len);

    dap_poly1305_mac(a_tag, mac_data, mac_len, poly_key);

    if (mac_data != stack_buf) free(mac_data);
    memset(poly_key, 0, sizeof(poly_key));
    return 0;
}

int dap_chacha20_poly1305_open(uint8_t *a_pt,
        const uint8_t *a_ct, size_t a_ct_len,
        const uint8_t a_tag[DAP_CHACHA20_POLY1305_TAG_SIZE],
        const uint8_t *a_aad, size_t a_aad_len,
        const uint8_t a_key[DAP_CHACHA20_KEY_SIZE],
        const uint8_t a_nonce[DAP_CHACHA20_NONCE_SIZE])
{
    uint8_t poly_key[64];
    memset(poly_key, 0, sizeof(poly_key));
    dap_chacha20_encrypt(poly_key, poly_key, sizeof(poly_key), a_key, a_nonce, 0);

    size_t aad_padded = a_aad_len + ((16 - (a_aad_len & 0xf)) & 0xf);
    size_t ct_padded  = a_ct_len  + ((16 - (a_ct_len  & 0xf)) & 0xf);
    size_t mac_len = aad_padded + ct_padded + 16;

    uint8_t stack_buf[512];
    uint8_t *mac_data = mac_len <= sizeof(stack_buf) ? stack_buf : (uint8_t *)malloc(mac_len);
    if (!mac_data) return -1;
    memset(mac_data, 0, mac_len);

    if (a_aad_len) memcpy(mac_data, a_aad, a_aad_len);
    memcpy(mac_data + aad_padded, a_ct, a_ct_len);
    size_t pos = aad_padded + ct_padded;
    s_store64_le(mac_data + pos, (uint64_t)a_aad_len);
    s_store64_le(mac_data + pos + 8, (uint64_t)a_ct_len);

    uint8_t computed_tag[DAP_POLY1305_TAG_SIZE];
    dap_poly1305_mac(computed_tag, mac_data, mac_len, poly_key);
    if (mac_data != stack_buf) free(mac_data);
    memset(poly_key, 0, sizeof(poly_key));

    uint8_t diff = 0;
    for (int i = 0; i < DAP_POLY1305_TAG_SIZE; i++)
        diff |= computed_tag[i] ^ a_tag[i];
    if (diff)
        return -1;

    dap_chacha20_encrypt(a_pt, a_ct, a_ct_len, a_key, a_nonce, 1);
    return 0;
}
