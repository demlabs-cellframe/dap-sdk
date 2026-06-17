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
#include "dap_hash_md5.h"
#include "dap_tls_ja3.h"

#define LOG_TAG "dap_tls_ja3"

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

    return dap_hash_md5_hex(a_ja3_string, strlen(a_ja3_string), a_hash_hex, a_hash_hex_size);
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
