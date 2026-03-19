/**
 * @file dap_mlkem_reduce.h
 * @brief Inline Montgomery/Barrett reduction for ML-KEM.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdint.h>
#include "dap_mlkem_params.h"

static inline int16_t dap_mlkem_montgomery_reduce(int32_t a)
{
    int16_t u = (int16_t)(a * MLKEM_QINV);
    return (int16_t)((a - (int32_t)u * MLKEM_Q) >> 16);
}

static inline int16_t dap_mlkem_barrett_reduce(int16_t a)
{
    int16_t t = (int16_t)(((int32_t)((1U << 26) + MLKEM_Q / 2) / MLKEM_Q * a) >> 26);
    return a - t * MLKEM_Q;
}

static inline int16_t dap_mlkem_csubq(int16_t a)
{
    a -= MLKEM_Q;
    a += (a >> 15) & MLKEM_Q;
    return a;
}

static inline int16_t dap_mlkem_fqmul(int16_t a, int16_t b)
{
    return dap_mlkem_montgomery_reduce((int32_t)a * b);
}
