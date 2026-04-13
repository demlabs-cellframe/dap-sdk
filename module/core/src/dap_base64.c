/**
 * @file dap_base64.c
 * @brief Base64 encoding/decoding implementation
 *
 * Copyright (c) 2017-2026 Demlabs
 * License: GNU GPL v3
 */

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dap_common.h"
#include "dap_base64.h"

static const char s_b64_table_standard[] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
    'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
    'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z', '0', '1', '2', '3',
    '4', '5', '6', '7', '8', '9', '+', '/'
};

static const char s_b64_table_urlsafe[] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
    'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
    'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
    'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z', '0', '1', '2', '3',
    '4', '5', '6', '7', '8', '9', '-', '_'
};

static const char *s_b64_get_table(dap_data_type_t a_type)
{
    switch (a_type) {
    case DAP_DATA_TYPE_B64:
        return s_b64_table_standard;
    case DAP_DATA_TYPE_B64_URLSAFE:
        return s_b64_table_urlsafe;
    default:
        return s_b64_table_standard;
    }
}

size_t dap_base64_decode(const char *a_in, size_t a_in_size, void *a_out, dap_data_type_t a_type)
{
    uint8_t *l_out_bytes = (uint8_t*)a_out;
    int j = 0;
    int8_t l = 0, i = 0;
    size_t l_size = 0;
    unsigned char buf[3] = {0};
    unsigned char tmp[4] = {0};
    const char *l_b64_table = s_b64_get_table(a_type);

    if (!a_out)
        return 0;

    while (a_in_size--) {
        if ('=' == a_in[j])
            break;

        if (!(isalnum(a_in[j]) || a_in[j] == l_b64_table[62] || a_in[j] == l_b64_table[63]))
            break;

        tmp[i++] = a_in[j++];

        if (4 == i) {
            for (i = 0; i < 4; ++i) {
                for (l = 0; l < 64; ++l) {
                    if (tmp[i] == l_b64_table[l]) {
                        tmp[i] = l;
                        break;
                    }
                }
            }

            buf[0] = (tmp[0] << 2) + ((tmp[1] & 0x30) >> 4);
            buf[1] = ((tmp[1] & 0xf) << 4) + ((tmp[2] & 0x3c) >> 2);
            buf[2] = ((tmp[2] & 0x3) << 6) + tmp[3];

            for (i = 0; i < 3; ++i)
                l_out_bytes[l_size++] = buf[i];

            i = 0;
        }
    }

    if (i > 0) {
        for (j = i; j < 4; ++j)
            tmp[j] = '\0';

        for (j = 0; j < 4; ++j) {
            for (l = 0; l < 64; ++l) {
                if (tmp[j] == l_b64_table[l]) {
                    tmp[j] = l;
                    break;
                }
            }
        }

        buf[0] = (tmp[0] << 2) + ((tmp[1] & 0x30) >> 4);
        buf[1] = ((tmp[1] & 0xf) << 4) + ((tmp[2] & 0x3c) >> 2);
        buf[2] = ((tmp[2] & 0x3) << 6) + tmp[3];

        for (j = 0; j < i - 1; ++j)
            l_out_bytes[l_size++] = buf[j];
    }

    return l_size;
}

size_t dap_base64_encode(const void *a_in, size_t a_in_size, char *a_out, dap_data_type_t a_type)
{
    uint8_t i = 0;
    int j = 0;
    size_t size = 0;
    unsigned char buf[4];
    unsigned char tmp[3];
    const unsigned char *l_in_bytes = (const unsigned char*)a_in;
    const char *l_b64_table = s_b64_get_table(a_type);

    if (!a_out)
        return 0;

    while (a_in_size--) {
        tmp[i++] = *(l_in_bytes++);

        if (3 == i) {
            buf[0] = (tmp[0] & 0xfc) >> 2;
            buf[1] = ((tmp[0] & 0x03) << 4) + ((tmp[1] & 0xf0) >> 4);
            buf[2] = ((tmp[1] & 0x0f) << 2) + ((tmp[2] & 0xc0) >> 6);
            buf[3] = tmp[2] & 0x3f;

            for (i = 0; i < 4; ++i)
                a_out[size++] = l_b64_table[buf[i]];

            i = 0;
        }
    }

    if (i > 0) {
        for (j = i; j < 3; ++j)
            tmp[j] = '\0';

        buf[0] = (tmp[0] & 0xfc) >> 2;
        buf[1] = ((tmp[0] & 0x03) << 4) + ((tmp[1] & 0xf0) >> 4);
        buf[2] = ((tmp[1] & 0x0f) << 2) + ((tmp[2] & 0xc0) >> 6);
        buf[3] = tmp[2] & 0x3f;

        for (j = 0; j < i + 1; ++j)
            a_out[size++] = l_b64_table[buf[j]];

        while (i++ < 3)
            a_out[size++] = '=';
    }

    return size;
}

char *dap_strdup_to_base64(const char *a_string)
{
    size_t l_string_len = strlen(a_string);
    size_t l_string_base64_len = DAP_BASE64_ENCODE_SIZE(l_string_len) + 1;
    char *l_string_base64 = DAP_NEW_SIZE(char, l_string_base64_len);
    if (!l_string_base64)
        return NULL;
    size_t l_res_len = dap_base64_encode(a_string, l_string_len, l_string_base64, DAP_DATA_TYPE_B64);
    l_string_base64[l_res_len] = '\0';
    return l_string_base64;
}

char *dap_strdup_from_base64(const char *a_string_base64)
{
    if (!a_string_base64 || !*a_string_base64)
        return NULL;
    size_t l_string_base64_len = strlen(a_string_base64);
    size_t l_decoded_len = DAP_BASE64_DECODE_SIZE(l_string_base64_len);
    if (!l_decoded_len)
        return NULL;
    char *l_string = DAP_NEW_Z_SIZE(char, l_decoded_len + 1);
    if (!l_string)
        return NULL;
    dap_base64_decode(a_string_base64, l_string_base64_len, l_string, DAP_DATA_TYPE_B64_URLSAFE);
    return l_string;
}
