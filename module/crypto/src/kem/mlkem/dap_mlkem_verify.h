/**
 * @file dap_mlkem_verify.h
 * @brief Constant-time comparison and conditional move for ML-KEM.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

static inline int dap_mlkem_verify(const uint8_t *a_a, const uint8_t *a_b, size_t a_len)
{
    uint8_t l_r = 0;
    for (size_t i = 0; i < a_len; i++)
        l_r |= a_a[i] ^ a_b[i];
    return (int)((-(uint64_t)l_r) >> 63);
}

static inline void dap_mlkem_cmov(uint8_t *a_r, const uint8_t *a_x, size_t a_len, uint8_t a_b)
{
    a_b = (uint8_t)(-(int8_t)a_b);
    for (size_t i = 0; i < a_len; i++)
        a_r[i] ^= a_b & (a_r[i] ^ a_x[i]);
}
