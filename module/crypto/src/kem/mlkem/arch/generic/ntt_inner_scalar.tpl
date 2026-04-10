/*
 * ML-KEM NTT scalar inner-layer stub.
 *
 * Does NOT define MLKEM_HAS_NTT_INNER — the universal template falls back
 * to the generic scalar loop for sub-VEC_LANES butterfly layers.
 * Does NOT define MLKEM_HAS_NTTPACK — the universal template uses the
 * portable scalar deinterleave.
 *
 * Included by dap_mlkem_ntt_simd.c.tpl as NTT_INNER_FILE — do not compile standalone.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* No SIMD inner-layer or nttpack optimizations on this architecture. */
