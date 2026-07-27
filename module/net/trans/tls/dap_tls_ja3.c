/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * JA3 fingerprint calculator -- implementation (TL.3)
 */

#include <string.h>
#include <stdio.h>

#include "dap_common.h"
#include "dap_tls_ja3.h"

#define LOG_TAG "dap_tls_ja3"

/* ------------------------------------------------------------------ *
 *  Minimal RFC 1321 MD5 implementation (self-contained, static).     *
 * ------------------------------------------------------------------ */

#define MD5_F(x,y,z)  (((x) & (y)) | (~(x) & (z)))
#define MD5_G(x,y,z)  (((x) & (z)) | ((y) & ~(z)))
#define MD5_H(x,y,z)  ((x) ^ (y) ^ (z))
#define MD5_I(x,y,z)  ((y) ^ ((x) | ~(z)))

#define MD5_ROL(v,n)   (((v) << (n)) | ((v) >> (32 - (n))))

#define MD5_FF(a,b,c,d,x,s,ac) { \
    (a) += MD5_F(b,c,d) + (x) + (uint32_t)(ac); \
    (a) = MD5_ROL(a,s) + (b); }
#define MD5_GG(a,b,c,d,x,s,ac) { \
    (a) += MD5_G(b,c,d) + (x) + (uint32_t)(ac); \
    (a) = MD5_ROL(a,s) + (b); }
#define MD5_HH(a,b,c,d,x,s,ac) { \
    (a) += MD5_H(b,c,d) + (x) + (uint32_t)(ac); \
    (a) = MD5_ROL(a,s) + (b); }
#define MD5_II(a,b,c,d,x,s,ac) { \
    (a) += MD5_I(b,c,d) + (x) + (uint32_t)(ac); \
    (a) = MD5_ROL(a,s) + (b); }

static void s_md5_transform(uint32_t state[4], const uint8_t block[64])
{
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t M[16];

    for (int i = 0; i < 16; i++)
        M[i] = (uint32_t)block[i*4]
             | ((uint32_t)block[i*4+1] << 8)
             | ((uint32_t)block[i*4+2] << 16)
             | ((uint32_t)block[i*4+3] << 24);

    MD5_FF(a,b,c,d, M[ 0],  7, 0xd76aa478); MD5_FF(d,a,b,c, M[ 1], 12, 0xe8c7b756);
    MD5_FF(c,d,a,b, M[ 2], 17, 0x242070db); MD5_FF(b,c,d,a, M[ 3], 22, 0xc1bdceee);
    MD5_FF(a,b,c,d, M[ 4],  7, 0xf57c0faf); MD5_FF(d,a,b,c, M[ 5], 12, 0x4787c62a);
    MD5_FF(c,d,a,b, M[ 6], 17, 0xa8304613); MD5_FF(b,c,d,a, M[ 7], 22, 0xfd469501);
    MD5_FF(a,b,c,d, M[ 8],  7, 0x698098d8); MD5_FF(d,a,b,c, M[ 9], 12, 0x8b44f7af);
    MD5_FF(c,d,a,b, M[10], 17, 0xffff5bb1); MD5_FF(b,c,d,a, M[11], 22, 0x895cd7be);
    MD5_FF(a,b,c,d, M[12],  7, 0x6b901122); MD5_FF(d,a,b,c, M[13], 12, 0xfd987193);
    MD5_FF(c,d,a,b, M[14], 17, 0xa679438e); MD5_FF(b,c,d,a, M[15], 22, 0x49b40821);

    MD5_GG(a,b,c,d, M[ 1],  5, 0xf61e2562); MD5_GG(d,a,b,c, M[ 6],  9, 0xc040b340);
    MD5_GG(c,d,a,b, M[11], 14, 0x265e5a51); MD5_GG(b,c,d,a, M[ 0], 20, 0xe9b6c7aa);
    MD5_GG(a,b,c,d, M[ 5],  5, 0xd62f105d); MD5_GG(d,a,b,c, M[10],  9, 0x02441453);
    MD5_GG(c,d,a,b, M[15], 14, 0xd8a1e681); MD5_GG(b,c,d,a, M[ 4], 20, 0xe7d3fbc8);
    MD5_GG(a,b,c,d, M[ 9],  5, 0x21e1cde6); MD5_GG(d,a,b,c, M[14],  9, 0xc33707d6);
    MD5_GG(c,d,a,b, M[ 3], 14, 0xf4d50d87); MD5_GG(b,c,d,a, M[ 8], 20, 0x455a14ed);
    MD5_GG(a,b,c,d, M[13],  5, 0xa9e3e905); MD5_GG(d,a,b,c, M[ 2],  9, 0xfcefa3f8);
    MD5_GG(c,d,a,b, M[ 7], 14, 0x676f02d9); MD5_GG(b,c,d,a, M[12], 20, 0x8d2a4c8a);

    MD5_HH(a,b,c,d, M[ 5],  4, 0xfffa3942); MD5_HH(d,a,b,c, M[ 8], 11, 0x8771f681);
    MD5_HH(c,d,a,b, M[11], 16, 0x6d9d6122); MD5_HH(b,c,d,a, M[14], 23, 0xfde5380c);
    MD5_HH(a,b,c,d, M[ 1],  4, 0xa4beea44); MD5_HH(d,a,b,c, M[ 4], 11, 0x4bdecfa9);
    MD5_HH(c,d,a,b, M[ 7], 16, 0xf6bb4b60); MD5_HH(b,c,d,a, M[10], 23, 0xbebfbc70);
    MD5_HH(a,b,c,d, M[13],  4, 0x289b7ec6); MD5_HH(d,a,b,c, M[ 0], 11, 0xeaa127fa);
    MD5_HH(c,d,a,b, M[ 3], 16, 0xd4ef3085); MD5_HH(b,c,d,a, M[ 6], 23, 0x04881d05);
    MD5_HH(a,b,c,d, M[ 9],  4, 0xd9d4d039); MD5_HH(d,a,b,c, M[12], 11, 0xe6db99e5);
    MD5_HH(c,d,a,b, M[15], 16, 0x1fa27cf8); MD5_HH(b,c,d,a, M[ 2], 23, 0xc4ac5665);

    MD5_II(a,b,c,d, M[ 0],  6, 0xf4292244); MD5_II(d,a,b,c, M[ 7], 10, 0x432aff97);
    MD5_II(c,d,a,b, M[14], 15, 0xab9423a7); MD5_II(b,c,d,a, M[ 5], 21, 0xfc93a039);
    MD5_II(a,b,c,d, M[12],  6, 0x655b59c3); MD5_II(d,a,b,c, M[ 3], 10, 0x8f0ccc92);
    MD5_II(c,d,a,b, M[10], 15, 0xffeff47d); MD5_II(b,c,d,a, M[ 1], 21, 0x85845dd1);
    MD5_II(a,b,c,d, M[ 8],  6, 0x6fa87e4f); MD5_II(d,a,b,c, M[15], 10, 0xfe2ce6e0);
    MD5_II(c,d,a,b, M[ 6], 15, 0xa3014314); MD5_II(b,c,d,a, M[13], 21, 0x4e0811a1);
    MD5_II(a,b,c,d, M[ 4],  6, 0xf7537e82); MD5_II(d,a,b,c, M[11], 10, 0xbd3af235);
    MD5_II(c,d,a,b, M[ 2], 15, 0x2ad7d2bb); MD5_II(b,c,d,a, M[ 9], 21, 0xeb86d391);

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
}

static void s_md5(const uint8_t *msg, size_t len, uint8_t digest[16])
{
    uint32_t state[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };

    size_t new_len = len + 8;
    while (new_len % 64 != 0)
        new_len++;

    uint8_t *buf = (uint8_t *)calloc(new_len, 1);
    if (!buf) {
        memset(digest, 0, 16);
        return;
    }
    memcpy(buf, msg, len);
    buf[len] = 0x80;

    uint64_t bit_len = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++)
        buf[new_len - 8 + i] = (uint8_t)(bit_len >> (i * 8));

    for (size_t off = 0; off < new_len; off += 64)
        s_md5_transform(state, buf + off);

    for (int i = 0; i < 4; i++) {
        digest[i*4+0] = (uint8_t)(state[i]);
        digest[i*4+1] = (uint8_t)(state[i] >> 8);
        digest[i*4+2] = (uint8_t)(state[i] >> 16);
        digest[i*4+3] = (uint8_t)(state[i] >> 24);
    }
    free(buf);
}

static void s_md5_hex(const uint8_t *msg, size_t len, char out[33])
{
    uint8_t digest[16];
    s_md5(msg, len, digest);
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        out[i*2+0] = hex[(digest[i] >> 4) & 0x0f];
        out[i*2+1] = hex[digest[i] & 0x0f];
    }
    out[32] = '\0';
}

/* ------------------------------------------------------------------ */

#define TLS_CT_HANDSHAKE           0x16
#define TLS_HT_CLIENT_HELLO        0x01
#define TLS_EXT_SUPPORTED_GROUPS   0x000A
#define TLS_EXT_EC_POINT_FORMATS   0x000B

static inline uint16_t s_get_u16be(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline uint32_t s_get_u24be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

int dap_tls_ja3_hash_string(const char *a_ja3_string, char *a_hash_hex, size_t a_hash_hex_size)
{
    if (!a_ja3_string || !a_hash_hex || a_hash_hex_size < DAP_TLS_JA3_HASH_HEX_SIZE)
        return -1;

    s_md5_hex((const uint8_t *)a_ja3_string, strlen(a_ja3_string), a_hash_hex);
    return 0;
}

/* ---- JA3 field builders ------------------------------------------------ */

static int s_append_decimal(char *a_buf, size_t a_buf_size, size_t *a_pos, uint32_t a_val, bool a_first)
{
    char l_tmp[16];
    int l_n = snprintf(l_tmp, sizeof(l_tmp), a_first ? "%u" : "-%u", a_val);
    if (l_n < 0 || (size_t)l_n >= sizeof(l_tmp))
        return -1;
    if (*a_pos + (size_t)l_n >= a_buf_size)
        return -1;
    memcpy(a_buf + *a_pos, l_tmp, (size_t)l_n);
    *a_pos += (size_t)l_n;
    return 0;
}

static int s_append_field_sep(char *a_buf, size_t a_buf_size, size_t *a_pos)
{
    if (*a_pos + 1 >= a_buf_size)
        return -1;
    a_buf[(*a_pos)++] = ',';
    a_buf[*a_pos] = '\0';
    return 0;
}

int dap_tls_ja3_from_client_hello_body(const uint8_t *a_body, size_t a_body_size,
                                       dap_tls_ja3_result_t *a_out)
{
    if (!a_body || !a_out || a_body_size < 34)
        return -1;

    memset(a_out, 0, sizeof(*a_out));
    size_t l_off = 0;

    uint16_t l_version = s_get_u16be(a_body + l_off);
    l_off += 2;
    l_off += 32; /* random */
    if (l_off >= a_body_size)
        return -1;

    uint8_t l_sid_len = a_body[l_off++];
    l_off += l_sid_len;
    if (l_off + 2 > a_body_size)
        return -1;

    uint16_t l_cs_len = s_get_u16be(a_body + l_off);
    l_off += 2;
    if (l_off + l_cs_len + 1 > a_body_size)
        return -1;

    char l_ja3[DAP_TLS_JA3_STRING_MAX];
    size_t l_pos = 0;
    if (s_append_decimal(l_ja3, sizeof(l_ja3), &l_pos, l_version, true) != 0)
        return -1;
    if (s_append_field_sep(l_ja3, sizeof(l_ja3), &l_pos) != 0)
        return -1;

    const uint8_t *l_cs = a_body + l_off;
    l_off += l_cs_len;
    bool l_first = true;
    for (uint16_t l_i = 0; l_i + 1 < l_cs_len; l_i += 2) {
        uint16_t l_cipher = s_get_u16be(l_cs + l_i);
        if (s_append_decimal(l_ja3, sizeof(l_ja3), &l_pos, l_cipher, l_first) != 0)
            return -1;
        l_first = false;
    }

    if (l_off >= a_body_size)
        return -1;
    uint8_t l_comp_len = a_body[l_off++];
    l_off += l_comp_len;
    if (l_off + 2 > a_body_size) {
        /* Truncated before extensions — treat as empty ext/curves/points */
        for (int l_i = 0; l_i < 3; l_i++) {
            if (s_append_field_sep(l_ja3, sizeof(l_ja3), &l_pos) != 0)
                return -1;
        }
        snprintf(a_out->ja3_string, sizeof(a_out->ja3_string), "%s", l_ja3);
        return dap_tls_ja3_hash_string(a_out->ja3_string, a_out->ja3_hash, sizeof(a_out->ja3_hash));
    }

    uint16_t l_ext_total = s_get_u16be(a_body + l_off);
    l_off += 2;
    if (l_off + l_ext_total > a_body_size)
        return -1;

    if (s_append_field_sep(l_ja3, sizeof(l_ja3), &l_pos) != 0)
        return -1;

    char l_curves[256] = {0};
    char l_points[64] = {0};
    size_t l_curves_pos = 0, l_points_pos = 0;
    bool l_curves_first = true, l_points_first = true;

    const uint8_t *l_ext = a_body + l_off;
    size_t l_ext_off = 0;
    l_first = true;

    while (l_ext_off + 4 <= l_ext_total) {
        uint16_t l_type = s_get_u16be(l_ext + l_ext_off);
        uint16_t l_len = s_get_u16be(l_ext + l_ext_off + 2);
        l_ext_off += 4;
        if (l_ext_off + l_len > l_ext_total)
            return -1;

        if (s_append_decimal(l_ja3, sizeof(l_ja3), &l_pos, l_type, l_first) != 0)
            return -1;
        l_first = false;

        if (l_type == TLS_EXT_SUPPORTED_GROUPS && l_len >= 2) {
            uint16_t l_gl = s_get_u16be(l_ext + l_ext_off);
            for (uint16_t l_gi = 0; l_gi + 1 < l_gl && 2 + l_gi + 2 <= l_len; l_gi += 2) {
                uint16_t l_g = s_get_u16be(l_ext + l_ext_off + 2 + l_gi);
                if (s_append_decimal(l_curves, sizeof(l_curves), &l_curves_pos, l_g, l_curves_first) != 0)
                    return -1;
                l_curves_first = false;
            }
        } else if (l_type == TLS_EXT_EC_POINT_FORMATS && l_len >= 1) {
            uint8_t l_pl = l_ext[l_ext_off];
            for (uint8_t l_pi = 0; l_pi < l_pl && 1 + l_pi < l_len; l_pi++) {
                if (s_append_decimal(l_points, sizeof(l_points), &l_points_pos,
                                     l_ext[l_ext_off + 1 + l_pi], l_points_first) != 0)
                    return -1;
                l_points_first = false;
            }
        }

        l_ext_off += l_len;
    }

    if (s_append_field_sep(l_ja3, sizeof(l_ja3), &l_pos) != 0)
        return -1;
    if (l_curves_pos > 0) {
        if (l_pos + l_curves_pos >= sizeof(l_ja3))
            return -1;
        memcpy(l_ja3 + l_pos, l_curves, l_curves_pos);
        l_pos += l_curves_pos;
    }

    if (s_append_field_sep(l_ja3, sizeof(l_ja3), &l_pos) != 0)
        return -1;
    if (l_points_pos > 0) {
        if (l_pos + l_points_pos >= sizeof(l_ja3))
            return -1;
        memcpy(l_ja3 + l_pos, l_points, l_points_pos);
        l_pos += l_points_pos;
    }

    snprintf(a_out->ja3_string, sizeof(a_out->ja3_string), "%s", l_ja3);
    return dap_tls_ja3_hash_string(a_out->ja3_string, a_out->ja3_hash, sizeof(a_out->ja3_hash));
}

int dap_tls_ja3_from_handshake(const uint8_t *a_data, size_t a_size,
                               dap_tls_ja3_result_t *a_out)
{
    if (!a_data || !a_out || a_size < 4)
        return -1;
    if (a_data[0] != TLS_HT_CLIENT_HELLO)
        return -1;
    uint32_t l_hs_len = s_get_u24be(a_data + 1);
    if ((size_t)4 + l_hs_len > a_size)
        return 1;
    return dap_tls_ja3_from_client_hello_body(a_data + 4, l_hs_len, a_out);
}

int dap_tls_ja3_from_tls_record(const uint8_t *a_data, size_t a_size,
                                dap_tls_ja3_result_t *a_out)
{
    if (!a_data || !a_out || a_size < 5)
        return -1;
    if (a_data[0] != TLS_CT_HANDSHAKE)
        return -1;
    uint16_t l_rec_len = s_get_u16be(a_data + 3);
    if ((size_t)5 + (size_t)l_rec_len > a_size)
        return 1;
    return dap_tls_ja3_from_handshake(a_data + 5, l_rec_len, a_out);
}
