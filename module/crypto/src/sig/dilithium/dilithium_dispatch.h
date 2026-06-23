/**
 * @file dilithium_dispatch.h
 * @brief Aggregated SIMD dispatch init for Dilithium.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

/**
 * One-time SIMD dispatch init for Dilithium poly and polyvec.
 * Called from dap_enc_init() before any crypto hot path.
 */
void dilithium_dispatch_init(void);
