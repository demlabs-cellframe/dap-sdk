/**
 * @file dap_base58.c
 * @brief Base58 encoding/decoding implementation
 *
 * Copyright (c) 2017-2026 Demlabs
 * License: GNU GPL v3
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_base58.h"

#define LOG_TAG "dap_base58"

static const char s_b58digits_ordered[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
static const int8_t s_b58digits_map[] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8,-1,-1,-1,-1,-1,-1,
    -1, 9,10,11,12,13,14,15,16,-1,17,18,19,20,21,-1,
    22,23,24,25,26,27,28,29,30,31,32,-1,-1,-1,-1,-1,
    -1,33,34,35,36,37,38,39,40,41,42,43,-1,44,45,46,
    47,48,49,50,51,52,53,54,55,56,57,-1,-1,-1,-1,-1,
};

size_t dap_base58_decode(const char *a_in, void *a_out)
{
    size_t l_out_size_max = DAP_BASE58_DECODE_SIZE(strlen(a_in));
    size_t l_out_size = l_out_size_max;

    const unsigned char *l_in_u8 = (const unsigned char*)a_in;
    size_t l_outi_size = (l_out_size_max + 3) / 4;

    uint32_t l_outi[l_outi_size];
    memset(l_outi, 0, l_outi_size * sizeof(uint32_t));
    uint64_t t;
    uint32_t c;
    size_t i, j;
    uint8_t bytesleft = l_out_size_max % 4;
    uint32_t zeromask = bytesleft ? (0xffffffff << (bytesleft * 8)) : 0;
    unsigned zerocount = 0;
    size_t l_in_len = strlen(a_in);

    // Leading zeros, just count
    for (i = 0; i < l_in_len && l_in_u8[i] == '1'; ++i)
        ++zerocount;

    for (; i < l_in_len; ++i) {
        if (l_in_u8[i] & 0x80)
            return 0;  // High-bit set on invalid digit
        if (s_b58digits_map[l_in_u8[i]] == -1)
            return 0;  // Invalid base58 digit
        c = (unsigned)s_b58digits_map[l_in_u8[i]];
        for (j = l_outi_size; j--;) {
            t = ((uint64_t)l_outi[j]) * 58 + c;
            c = (t & 0x3f00000000) >> 32;
            l_outi[j] = t & 0xffffffff;
        }
        if (c)
            return 0;  // Output number too big
        if (l_outi[0] & zeromask)
            return 0;  // Output number too big
    }

    unsigned char l_out_u80[l_out_size_max];
    memset(l_out_u80, 0, l_out_size_max);
    unsigned char *l_out_u8 = l_out_u80;
    j = 0;
    switch (bytesleft) {
        case 3:
            *(l_out_u8++) = (l_outi[0] & 0xff0000) >> 16;
            //-fallthrough
        case 2:
            *(l_out_u8++) = (l_outi[0] & 0xff00) >> 8;
            //-fallthrough
        case 1:
            *(l_out_u8++) = (l_outi[0] & 0xff);
            ++j;
            //-fallthrough
        default:
            break;
    }

    for (; j < l_outi_size; ++j) {
        *(l_out_u8++) = (l_outi[j] >> 0x18) & 0xff;
        *(l_out_u8++) = (l_outi[j] >> 0x10) & 0xff;
        *(l_out_u8++) = (l_outi[j] >> 8) & 0xff;
        *(l_out_u8++) = (l_outi[j] >> 0) & 0xff;
    }

    // Count canonical base58 byte count
    l_out_u8 = l_out_u80;
    for (i = 0; i < l_out_size_max; ++i) {
        if (l_out_u8[i]) {
            if (zerocount > i)
                return 0;  // Result too large
            break;
        }
        --l_out_size;
    }

    unsigned char *l_out = a_out;
    memset(l_out, 0, zerocount);
    for (j = 0; j < l_out_size; j++)
        l_out[j + zerocount] = l_out_u8[j + i];
    l_out[j + zerocount] = 0;
    l_out_size += zerocount;

    return l_out_size;
}

size_t dap_base58_encode(const void *a_in, size_t a_in_size, char *a_out)
{
    const uint8_t *l_in_u8 = a_in;
    int carry;
    ssize_t i, j, high, zcount = 0;
    size_t size;
    size_t l_out_size = DAP_BASE58_ENCODE_SIZE(a_in_size);

    while (zcount < (ssize_t)a_in_size && !l_in_u8[zcount])
        ++zcount;

    size = (a_in_size - zcount) * 138 / 100 + 1;
    uint8_t buf[size];
    memset(buf, 0, size);

    for (i = zcount, high = size - 1; i < (ssize_t)a_in_size; ++i, high = j) {
        for (carry = l_in_u8[i], j = size - 1; (j > high) || carry; --j) {
            carry += 256 * buf[j];
            buf[j] = carry % 58;
            carry /= 58;
        }
    }

    for (j = 0; j < (ssize_t)size && !buf[j]; ++j);

    if (l_out_size <= (zcount + size - j)) {
        l_out_size = (zcount + size - j + 1);
        return l_out_size;
    }

    if (zcount)
        memset(a_out, '1', zcount);
    for (i = zcount; j < (ssize_t)size; ++i, ++j)
        a_out[i] = s_b58digits_ordered[buf[j]];
    a_out[i] = '\0';
    l_out_size = i;

    return l_out_size;
}

char *dap_base58_encode_to_str(const void *a_in, size_t a_in_size)
{
    size_t l_out_size = DAP_BASE58_ENCODE_SIZE(a_in_size);
    char *l_out = DAP_NEW_Z_SIZE(char, l_out_size + 1);
    if (!l_out) return NULL;
    dap_base58_encode(a_in, a_in_size, l_out);
    return l_out;
}

char *dap_base58_from_hex_str(const char *a_in_str)
{
    size_t l_in_len = dap_strlen(a_in_str);
    if (l_in_len < 3 || dap_strncmp(a_in_str, "0x", 2))
        return NULL;
    char *l_out_str = DAP_NEW_STACK_SIZE(char, l_in_len / 2 + 1);
    size_t len = dap_hex2bin((uint8_t*)l_out_str, a_in_str + 2, l_in_len - 2);
    return dap_base58_encode_to_str(l_out_str, len / 2);
}

char *dap_base58_to_hex_str(const char *a_in_str)
{
    size_t l_in_len = dap_strlen(a_in_str);
    if (l_in_len < 8)
        return NULL;
    size_t l_out_size_max = DAP_BASE58_DECODE_SIZE(l_in_len);
    void *l_out = DAP_NEW_STACK_SIZE(char, l_out_size_max + 1);
    size_t l_out_size = dap_base58_decode(a_in_str, l_out);
    if (l_out_size < 8 || l_out_size % 8)
        return NULL;
    size_t l_out_str_size = l_out_size * 2 + 3;
    char *l_out_str = DAP_NEW_Z_SIZE(char, l_out_str_size);
    if (!l_out_str) return NULL;
    l_out_str[0] = '0';
    l_out_str[1] = 'x';
    dap_htoa64((l_out_str + 2), l_out, l_out_size);
    return l_out_str;
}
