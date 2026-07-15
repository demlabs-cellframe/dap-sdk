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
 * @brief Reed-Solomon encode: degree-511 polynomial → 2048 evaluations.
 *
 * Takes 512 coefficients (poly[0] + poly[1]·x + ... + poly[511]·x^511),
 * pads to 2048 with zeros, applies coset shift, and evaluates via NTT.
 *
 * Output (codeword[k] for k=0..2047) = f(g·ω^k) in natural order,
 * where g = CHIPMUNK_RS_COSET_G, ω = omega_2048.
 *
 * Requires chipmunk_fri_ntt_init() and chipmunk_field_init() to have
 * been called.
 *
 * @param codeword  Output array of CHIPMUNK_RS_CODE_LEN elements (may alias poly).
 * @param poly      Input array of CHIPMUNK_RS_MSG_LEN coefficients.
 * @return          0 on success, negative on error.
 */
int chipmunk_rs_encode(int32_t codeword[CHIPMUNK_RS_CODE_LEN],
                        const int32_t poly[CHIPMUNK_RS_MSG_LEN]);

/**
 * @brief Reed-Solomon interpolate: 2048 coset-domain evaluations → degree-511 poly.
 *
 * Inverse of chipmunk_rs_encode.  Takes 2048 evaluations f(g·ω^k),
 * applies inverse coset NTT to recover the 512 low-degree coefficients.
 * The 1536 high-degree coefficients must be zero (enforced by the
 * commitment scheme; this function does not check them).
 *
 * @param poly      Output array of CHIPMUNK_RS_MSG_LEN coefficients.
 * @param codeword  Input array of CHIPMUNK_RS_CODE_LEN evaluations.
 * @return          0 on success, negative on error.
 */
int chipmunk_rs_interpolate(int32_t poly[CHIPMUNK_RS_MSG_LEN],
                              const int32_t codeword[CHIPMUNK_RS_CODE_LEN]);

/**
 * @brief Evaluate a single polynomial at a point (naive Horner's method).
 *
 * Not used in the encoding hot path, but useful for verification and testing.
 * Computes f(x) = poly[0] + poly[1]·x + ... + poly[n-1]·x^{n-1} mod q.
 *
 * @param poly  Coefficient array.
 * @param n     Number of coefficients (degree + 1).
 * @param x     Evaluation point in F_q.
 * @return      f(x) mod q.
 */
int32_t chipmunk_rs_eval(const int32_t *poly, uint32_t n, int32_t x);

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
