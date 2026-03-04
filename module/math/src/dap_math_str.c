/**
 * @file dap_math_str.c
 * @brief String conversion functions for 128-bit integers
 *
 * Copyright (c) 2024-2026 Demlabs
 * License: GNU GPL v3
 */

#include <ctype.h>
#include <errno.h>
#include <string.h>

#include "dap_math_str.h"

#ifdef DAP_GLOBAL_IS_INT128

/**
 * @brief Convert character to digit value
 */
static int s_strdigit(char c)
{
    /* ASCII / UTF-8 specific */
    return (c >= '0' && c <= '9') ? c - '0'
        :  (c >= 'a' && c <= 'z') ? c - 'a' + 10
        :  (c >= 'A' && c <= 'Z') ? c - 'A' + 10
        :  255;
}

/**
 * @brief Internal string to uint128 conversion
 */
static uint128_t s_strtou128(const char *p, char **endp, int base)
{
    uint128_t v = 0;
    int digit;

    if (base == 0) {    /* handle octal and hexadecimal syntax */
        base = 10;
        if (*p == '0') {
            base = 8;
            if ((p[1] == 'x' || p[1] == 'X') && s_strdigit(p[2]) < 16) {
                p += 2;
                base = 16;
            }
        }
    }
    if (base < 2 || base > 36) {
        errno = EINVAL;
    } else
    if ((digit = s_strdigit(*p)) < base) {
        v = digit;
        /* convert to unsigned 128 bit with overflow control */
        while ((digit = s_strdigit(*++p)) < base) {
            uint128_t v0 = v;
            v = v * base + digit;
            if (v < v0) {
                v = ~(uint128_t)0;
                errno = ERANGE;
            }
        }
        if (endp) {
            *endp = (char *)p;
        }
    }
    return v;
}

uint128_t dap_strtou128(const char *p, char **endp, int base)
{
    if (endp) {
        *endp = (char *)p;
    }
    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (*p == '-') {
        p++;
        return -s_strtou128(p, endp, base);
    } else {
        if (*p == '+')
            p++;
        return s_strtou128(p, endp, base);
    }
}

int128_t dap_strtoi128(const char *p, char **endp, int base)
{
    uint128_t v;

    if (endp) {
        *endp = (char *)p;
    }
    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (*p == '-') {
        p++;
        v = s_strtou128(p, endp, base);
        if (v >= (uint128_t)1 << 127) {
            if (v > (uint128_t)1 << 127)
                errno = ERANGE;
            return -(int128_t)(((uint128_t)1 << 127) - 1) - 1;
        }
        return -(int128_t)v;
    } else {
        if (*p == '+')
            p++;
        v = s_strtou128(p, endp, base);
        if (v >= (uint128_t)1 << 127) {
            errno = ERANGE;
            return (int128_t)(((uint128_t)1 << 127) - 1);
        }
        return (int128_t)v;
    }
}

char *dap_utoa128(char *dest, uint128_t v, int base)
{
    char buf[129], *p = buf + 128;
    const char *digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    *p = '\0';
    if (base >= 2 && base <= 36) {
        while (v > (unsigned)base - 1) {
            *--p = digits[v % base];
            v /= base;
        }
        *--p = digits[v];
    }
    return strcpy(dest, p);
}

char *dap_itoa128(char *a_str, int128_t a_value, int a_base)
{
    char *p = a_str;
    uint128_t uv = (uint128_t)a_value;
    if (a_value < 0) {
        *p++ = '-';
        uv = -uv;
    }
    if (a_base == 10)
        dap_utoa128(p, uv, 10);
    else
    if (a_base == 16)
        dap_utoa128(p, uv, 16);
    else
        dap_utoa128(p, uv, a_base);
    return a_str;
}

#endif // DAP_GLOBAL_IS_INT128
