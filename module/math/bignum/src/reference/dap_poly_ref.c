/*
 * Reference polynomial arithmetic over Z_q[X]/(X^n+1).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dap_poly.h"

static inline int32_t s_centered_mod(int64_t a_val, int32_t a_q)
{
    int32_t l_r = (int32_t)(a_val % a_q);
    if (l_r < 0) l_r += a_q;
    return l_r;
}

void dap_poly_add(int32_t *a_r, const int32_t *a_a, const int32_t *a_b,
                  uint32_t a_n, int32_t a_q)
{
    for (uint32_t i = 0; i < a_n; i++)
        a_r[i] = s_centered_mod((int64_t)a_a[i] + a_b[i], a_q);
}

void dap_poly_sub(int32_t *a_r, const int32_t *a_a, const int32_t *a_b,
                  uint32_t a_n, int32_t a_q)
{
    for (uint32_t i = 0; i < a_n; i++)
        a_r[i] = s_centered_mod((int64_t)a_a[i] - a_b[i], a_q);
}

void dap_poly_reduce(int32_t *a_r, uint32_t a_n, int32_t a_q)
{
    for (uint32_t i = 0; i < a_n; i++)
        a_r[i] = s_centered_mod(a_r[i], a_q);
}
