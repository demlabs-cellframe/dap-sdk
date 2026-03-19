/**
 * @file dap_mlkem_poly.h
 * @brief Polynomial operations for ML-KEM.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "dap_mlkem_params.h"

void MLKEM_NAMESPACE(_poly_compress)(uint8_t *a_r, dap_mlkem_poly *a_a);
void MLKEM_NAMESPACE(_poly_decompress)(dap_mlkem_poly *a_r, const uint8_t *a_a);
void MLKEM_NAMESPACE(_poly_tobytes)(uint8_t *a_r, dap_mlkem_poly *a_a);
void MLKEM_NAMESPACE(_poly_frombytes)(dap_mlkem_poly *a_r, const uint8_t *a_a);
void MLKEM_NAMESPACE(_poly_frommsg)(dap_mlkem_poly *a_r, const uint8_t a_msg[MLKEM_INDCPA_MSGBYTES]);
void MLKEM_NAMESPACE(_poly_tomsg)(uint8_t a_msg[MLKEM_INDCPA_MSGBYTES], dap_mlkem_poly *a_a);
void MLKEM_NAMESPACE(_poly_getnoise_eta1)(dap_mlkem_poly *a_r,
                                           const uint8_t a_seed[MLKEM_SYMBYTES], uint8_t a_nonce);
void MLKEM_NAMESPACE(_poly_getnoise_eta2)(dap_mlkem_poly *a_r,
                                           const uint8_t a_seed[MLKEM_SYMBYTES], uint8_t a_nonce);
void MLKEM_NAMESPACE(_poly_ntt)(dap_mlkem_poly *a_r);
void MLKEM_NAMESPACE(_poly_invntt_tomont)(dap_mlkem_poly *a_r);
void MLKEM_NAMESPACE(_poly_basemul_montgomery)(dap_mlkem_poly *a_r,
                                                const dap_mlkem_poly *a_a,
                                                const dap_mlkem_poly *a_b);
void MLKEM_NAMESPACE(_poly_tomont)(dap_mlkem_poly *a_r);
void MLKEM_NAMESPACE(_poly_reduce)(dap_mlkem_poly *a_r);
void MLKEM_NAMESPACE(_poly_csubq)(dap_mlkem_poly *a_r);
void MLKEM_NAMESPACE(_poly_add)(dap_mlkem_poly *a_r, const dap_mlkem_poly *a_a,
                                 const dap_mlkem_poly *a_b);
void MLKEM_NAMESPACE(_poly_sub)(dap_mlkem_poly *a_r, const dap_mlkem_poly *a_a,
                                 const dap_mlkem_poly *a_b);
