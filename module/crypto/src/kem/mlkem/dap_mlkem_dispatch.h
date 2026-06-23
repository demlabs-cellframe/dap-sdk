/**
 * @file dap_mlkem_dispatch.h
 * @brief Aggregated SIMD dispatch init for all ML-KEM variants.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

/**
 * One-time SIMD dispatch init for ML-KEM-512, 768, 1024.
 * Called from dap_enc_init() before any crypto hot path.
 */
void dap_mlkem_dispatch_init(void);
