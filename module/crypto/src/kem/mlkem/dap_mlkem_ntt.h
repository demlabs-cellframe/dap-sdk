/**
 * @file dap_mlkem_ntt.h
 * @brief NTT wrapper for ML-KEM — delegates to dap_ntt16 (with SIMD dispatch).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "dap_mlkem_params.h"
#include "dap_ntt.h"

const int16_t *MLKEM_NAMESPACE(_get_zetas)(void);

void MLKEM_NAMESPACE(_ntt)(int16_t a_coeffs[MLKEM_N]);
void MLKEM_NAMESPACE(_invntt)(int16_t a_coeffs[MLKEM_N]);
void MLKEM_NAMESPACE(_basemul)(int16_t a_r[2], const int16_t a_a[2],
                                const int16_t a_b[2], int16_t a_zeta);
