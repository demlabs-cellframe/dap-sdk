/**
 * @file dap_mlkem_cbd.h
 * @brief Centered Binomial Distribution sampling for ML-KEM.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "dap_mlkem_params.h"

void MLKEM_NAMESPACE(_cbd_eta1)(dap_mlkem_poly *a_r,
                                 const uint8_t a_buf[MLKEM_ETA1 * MLKEM_N / 4]);
void MLKEM_NAMESPACE(_cbd_eta2)(dap_mlkem_poly *a_r,
                                 const uint8_t a_buf[MLKEM_ETA2 * MLKEM_N / 4]);
