/*
 * Polynomial arithmetic over Z_q[X]/(X^n+1) using NTT.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "dap_ntt.h"

#ifdef __cplusplus
extern "C" {
#endif

void dap_poly_add(int32_t *a_r, const int32_t *a_a, const int32_t *a_b,
                  uint32_t a_n, int32_t a_q);

void dap_poly_sub(int32_t *a_r, const int32_t *a_a, const int32_t *a_b,
                  uint32_t a_n, int32_t a_q);

void dap_poly_reduce(int32_t *a_r, uint32_t a_n, int32_t a_q);

#ifdef __cplusplus
}
#endif
