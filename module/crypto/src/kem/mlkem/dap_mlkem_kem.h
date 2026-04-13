/**
 * @file dap_mlkem_kem.h
 * @brief CCA-secure KEM for ML-KEM (FIPS 203).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdint.h>
#include "dap_mlkem_params.h"

int MLKEM_NAMESPACE(_kem_keypair)(uint8_t *a_pk, uint8_t *a_sk);
int MLKEM_NAMESPACE(_kem_enc)(uint8_t *a_ct, uint8_t *a_ss, const uint8_t *a_pk);
int MLKEM_NAMESPACE(_kem_dec)(uint8_t *a_ss, const uint8_t *a_ct, const uint8_t *a_sk);
