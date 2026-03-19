/**
 * @file dap_mlkem_indcpa.h
 * @brief IND-CPA public-key encryption for ML-KEM.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdint.h>
#include "dap_mlkem_params.h"

void MLKEM_NAMESPACE(_indcpa_keypair)(uint8_t a_pk[MLKEM_INDCPA_PUBLICKEYBYTES],
                                       uint8_t a_sk[MLKEM_INDCPA_SECRETKEYBYTES]);
void MLKEM_NAMESPACE(_indcpa_enc)(uint8_t a_c[MLKEM_INDCPA_BYTES],
                                   const uint8_t a_m[MLKEM_INDCPA_MSGBYTES],
                                   const uint8_t a_pk[MLKEM_INDCPA_PUBLICKEYBYTES],
                                   const uint8_t a_coins[MLKEM_SYMBYTES]);
void MLKEM_NAMESPACE(_indcpa_dec)(uint8_t a_m[MLKEM_INDCPA_MSGBYTES],
                                   const uint8_t a_c[MLKEM_INDCPA_BYTES],
                                   const uint8_t a_sk[MLKEM_INDCPA_SECRETKEYBYTES]);
