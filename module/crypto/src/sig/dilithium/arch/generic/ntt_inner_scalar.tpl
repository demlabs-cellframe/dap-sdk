/*
 * Dilithium NTT scalar inner-layer stub.
 *
 * Does NOT define DIL_HAS_NTT_INNER — the universal template falls back
 * to the generic scalar loop for sub-VEC_LANES butterfly layers.
 *
 * Included by dap_dilithium_ntt_simd.c.tpl as NTT_INNER_FILE — do not compile standalone.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* No SIMD inner-layer optimizations on this architecture. */
