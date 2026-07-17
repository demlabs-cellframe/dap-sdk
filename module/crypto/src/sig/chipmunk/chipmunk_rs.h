/*
 * chipmunk_rs.h — Reed-Solomon encoding via 2048-point NTT for FRI-DEEP PCS.
 *
 * Extends a degree-511 polynomial (512 coefficients) to 2048 field-element
 * evaluations over the coset-shifted FRI domain, providing 4× rate redundancy.
 *
 * Coset generator: g = 3 (a primitive root of F_q*, order = q-1 = 3168256).
 * Evaluation domain: { g·ω⁰, g·ω¹, ..., g·ω²⁰⁴⁷ }
 *   where ω = omega_2048 (primitive 2048-th root of unity).
 *
 * Encoding algorithm:
 *   1. Pad poly[0..511] with 1536 zero coefficients → coeff[0..2047]
 *   2. Apply coset shift: coeff[i] *= g^i for i = 0..2047
 *   3. Forward NTT → evaluations at coset domain (natural order)
 *
 * This is an injective map: distinct degree-511 polynomials yield distinct
 * 2048-evaluation codewords (a property the FRI verifier relies on).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#ifndef _CHIPMUNK_RS_H_
#define _CHIPMUNK_RS_H_

#include <stdint.h>
#include <stdbool.h>
#include "chipmunk_fri_ntt.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Number of message coefficients (degree < 512). */
#define CHIPMUNK_RS_MSG_LEN   512u

/* Codeword length = FRI domain size = 4 × message length (rate ρ = 1/4). */
#define CHIPMUNK_RS_CODE_LEN  CHIPMUNK_FRI_NTT_SIZE  /* 2048 */

/**
 * @brief Coset generator for RS encoding.
 *
 * g = 3, a primitive root of F_q*.  Order = q-1 = 3168256 >> 2048,
 * guaranteeing the coset shift is non-degenerate (no two distinct
 * degree-511 polys evaluate identically on the coset domain).
 */
#define CHIPMUNK_RS_COSET_G   3

/**
 * @brief RS encode using a per-q NTT context (Phase 9.13h).
 *
 * Takes 512 coefficients, pads to 2048 with zeros, applies coset shift,
 * and evaluates via per-q NTT. ntt_ctx must be valid.
 */
int chipmunk_rs_encode_q(int32_t codeword[CHIPMUNK_RS_CODE_LEN],
                          const int32_t poly[CHIPMUNK_RS_MSG_LEN],
                          const chipmunk_fri_ntt_ctx_t *ntt_ctx);

/** @brief Per-q RS interpolate (Phase 9.13h). ntt_ctx may be NULL for q==CHIPMUNK_Q. */
int chipmunk_rs_interpolate_q(int32_t poly[CHIPMUNK_RS_MSG_LEN],
                                const int32_t codeword[CHIPMUNK_RS_CODE_LEN],
                                uint64_t q,
                                const chipmunk_fri_ntt_ctx_t *ntt_ctx);

/** @brief Per-q polynomial evaluation via Horner (Phase 9.13h). */
int32_t chipmunk_rs_eval_q(const int32_t *poly, uint32_t n, int32_t x, uint64_t q);

/**
 * @brief Get the coset generator constant.
 */
static inline int32_t chipmunk_rs_coset_g(void)
{
    return CHIPMUNK_RS_COSET_G;
}

#ifdef __cplusplus
}
#endif

#endif /* _CHIPMUNK_RS_H_ */
