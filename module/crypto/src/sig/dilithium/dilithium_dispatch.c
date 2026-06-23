/**
 * @file dilithium_dispatch.c
 * @brief Aggregated SIMD dispatch init for Dilithium.
 *
 * Called once from dap_enc_init() before any crypto hot path.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dilithium_poly.h"
#include "dilithium_polyvec.h"

void dilithium_dispatch_init(void)
{
    dilithium_poly_dispatch_init();
    dilithium_polyvec_dispatch_init();
}
