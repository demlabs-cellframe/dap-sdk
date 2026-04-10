/**
 * @file dap_mlkem_indcpa.h
 * @brief IND-CPA public-key encryption for ML-KEM.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdint.h>
#include "dap_mlkem_params.h"

struct dap_mlkem_ctx;

void MLKEM_NAMESPACE(_indcpa_keypair)(uint8_t a_pk[MLKEM_INDCPA_PUBLICKEYBYTES],
                                       uint8_t a_sk[MLKEM_INDCPA_SECRETKEYBYTES]);
void MLKEM_NAMESPACE(_indcpa_enc)(uint8_t a_c[MLKEM_INDCPA_BYTES],
                                   const uint8_t a_m[MLKEM_INDCPA_MSGBYTES],
                                   const uint8_t a_pk[MLKEM_INDCPA_PUBLICKEYBYTES],
                                   const uint8_t a_coins[MLKEM_SYMBYTES]);
void MLKEM_NAMESPACE(_indcpa_dec)(uint8_t a_m[MLKEM_INDCPA_MSGBYTES],
                                   const uint8_t a_c[MLKEM_INDCPA_BYTES],
                                   const uint8_t a_sk[MLKEM_INDCPA_SECRETKEYBYTES]);

void MLKEM_NAMESPACE(_indcpa_ctx_init_pk)(struct dap_mlkem_ctx *a_ctx,
                                           const uint8_t a_pk[MLKEM_INDCPA_PUBLICKEYBYTES]);
void MLKEM_NAMESPACE(_indcpa_ctx_init_sk)(struct dap_mlkem_ctx *a_ctx,
                                           const uint8_t a_sk[MLKEM_INDCPA_SECRETKEYBYTES]);
void MLKEM_NAMESPACE(_indcpa_enc_ctx)(uint8_t a_c[MLKEM_INDCPA_BYTES],
                                       const uint8_t a_m[MLKEM_INDCPA_MSGBYTES],
                                       const struct dap_mlkem_ctx *a_ctx,
                                       const uint8_t a_coins[MLKEM_SYMBYTES]);
void MLKEM_NAMESPACE(_indcpa_dec_ctx)(uint8_t a_m[MLKEM_INDCPA_MSGBYTES],
                                       const uint8_t a_c[MLKEM_INDCPA_BYTES],
                                       const struct dap_mlkem_ctx *a_ctx);
