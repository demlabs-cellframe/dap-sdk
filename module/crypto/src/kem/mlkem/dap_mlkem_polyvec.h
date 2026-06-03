/**
 * @file dap_mlkem_polyvec.h
 * @brief Polynomial vector operations for ML-KEM.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "dap_mlkem_params.h"
#include "dap_mlkem_poly.h"

void MLKEM_NAMESPACE(_polyvec_compress)(uint8_t *a_r, dap_mlkem_polyvec *a_a);
void MLKEM_NAMESPACE(_polyvec_decompress)(dap_mlkem_polyvec *a_r, const uint8_t *a_a);
void MLKEM_NAMESPACE(_polyvec_tobytes)(uint8_t *a_r, dap_mlkem_polyvec *a_a);
void MLKEM_NAMESPACE(_polyvec_frombytes)(dap_mlkem_polyvec *a_r, const uint8_t *a_a);
void MLKEM_NAMESPACE(_polyvec_ntt)(dap_mlkem_polyvec *a_r);
void MLKEM_NAMESPACE(_polyvec_invntt_tomont)(dap_mlkem_polyvec *a_r);
void MLKEM_NAMESPACE(_polyvec_pointwise_acc_montgomery)(dap_mlkem_poly *a_r,
                                                         const dap_mlkem_polyvec *a_a,
                                                         const dap_mlkem_polyvec *a_b);
void MLKEM_NAMESPACE(_polyvec_reduce)(dap_mlkem_polyvec *a_r);
void MLKEM_NAMESPACE(_polyvec_csubq)(dap_mlkem_polyvec *a_r);
void MLKEM_NAMESPACE(_polyvec_add)(dap_mlkem_polyvec *a_r,
                                    const dap_mlkem_polyvec *a_a,
                                    const dap_mlkem_polyvec *a_b);
void MLKEM_NAMESPACE(_polyvec_mulcache_compute)(dap_mlkem_polyvec_mulcache *a_cache,
                                                 const dap_mlkem_polyvec *a_b);
void MLKEM_NAMESPACE(_polyvec_basemul_acc_montgomery_cached)(
    dap_mlkem_poly *a_r,
    const dap_mlkem_polyvec *a_a,
    const dap_mlkem_polyvec *a_b,
    const dap_mlkem_polyvec_mulcache *a_b_cache);
void MLKEM_NAMESPACE(_polyvec_nttpack)(dap_mlkem_polyvec *a_r);
void MLKEM_NAMESPACE(_polyvec_nttunpack)(dap_mlkem_polyvec *a_r);
