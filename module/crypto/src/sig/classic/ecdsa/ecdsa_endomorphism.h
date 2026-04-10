/*
 * GLV Endomorphism for secp256k1 scalar multiplication acceleration
 * 
 * secp256k1 has an efficiently computable endomorphism φ:
 *   φ(x, y) = (β*x, y) where β³ = 1 (mod p)
 *   φ(P) = λ*P where λ³ = 1 (mod n)
 * 
 * This allows decomposing k*P into k1*P + k2*φ(P) where k1, k2 are ~128-bit,
 * effectively halving the number of doublings required (~2x speedup for verify).
 */

#ifndef _ECDSA_ENDOMORPHISM_H_
#define _ECDSA_ENDOMORPHISM_H_

#include "ecdsa_field.h"
#include "ecdsa_scalar.h"
#include "ecdsa_group.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Constants (lazy-initialized getters)
// =============================================================================

/**
 * @brief Get β - cube root of unity in Fp
 * β = 0x7ae96a2b657c07106e64479eac3434e99cf0497512f58995c1396c28719501ee
 * @return Pointer to β constant (thread-safe, lazy-initialized)
 */
const ecdsa_field_t *ecdsa_get_beta(void);

/**
 * @brief Get λ - cube root of unity in Fn (scalar field)
 * λ = 0x5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72
 * @return Pointer to λ constant (thread-safe, lazy-initialized)
 */
const ecdsa_scalar_t *ecdsa_get_lambda(void);

// =============================================================================
// GLV Decomposition
// =============================================================================

/**
 * @brief Decompose scalar k into (k1, k2) such that k = k1 + k2*λ (mod n)
 * @param[out] a_k1 Output scalar k1 (~128 bits)
 * @param[out] a_k1_neg Set to true if k1 should be negated
 * @param[out] a_k2 Output scalar k2 (~128 bits)
 * @param[out] a_k2_neg Set to true if k2 should be negated
 * @param[in] a_k Input scalar to decompose
 * 
 * The decomposition ensures |k1|, |k2| < sqrt(n), giving ~128-bit scalars.
 * Uses precomputed lattice basis for efficient decomposition.
 */
void ecdsa_scalar_split_lambda(
    ecdsa_scalar_t *a_k1, bool *a_k1_neg,
    ecdsa_scalar_t *a_k2, bool *a_k2_neg,
    const ecdsa_scalar_t *a_k
);

// =============================================================================
// Endomorphism Application
// =============================================================================

/**
 * @brief Apply endomorphism to affine point: φ(x, y) = (β*x, y)
 * @param[out] a_result Result point φ(P)
 * @param[in] a_point Input point P
 */
void ecdsa_ge_mul_lambda(ecdsa_ge_t *a_result, const ecdsa_ge_t *a_point);

/**
 * @brief Apply endomorphism to Jacobian point: φ(X, Y, Z) = (β*X, Y, Z)
 * @param[out] a_result Result point φ(P)
 * @param[in] a_point Input point P
 */
void ecdsa_gej_mul_lambda(ecdsa_gej_t *a_result, const ecdsa_gej_t *a_point);

// =============================================================================
// Endomorphism-Accelerated Scalar Multiplication
// =============================================================================

/**
 * @brief Compute n*P using GLV endomorphism (for arbitrary point P)
 * @param[out] a_result Result point n*P
 * @param[in] a_point Input point P
 * @param[in] a_scalar Scalar n
 * 
 * Uses decomposition n = k1 + k2*λ to compute:
 *   n*P = k1*P + k2*λ*P = k1*P + k2*φ(P)
 * With ~128-bit k1, k2, this halves the number of doublings.
 */
void ecdsa_ecmult_endomorphism(
    ecdsa_gej_t *a_result,
    const ecdsa_ge_t *a_point,
    const ecdsa_scalar_t *a_scalar
);

/**
 * @brief Compute na*A + ng*G using GLV endomorphism (optimized for verify)
 * @param[out] a_result Result point na*A + ng*G
 * @param[in] a_point_a Point A (public key)
 * @param[in] a_scalar_a Scalar na
 * @param[in] a_scalar_g Scalar ng
 * 
 * Uses 4-way simultaneous multiplication with endomorphism:
 *   na*A + ng*G = (k1a*A + k2a*φ(A)) + (k1g*G + k2g*φ(G))
 * Where each ki is ~128 bits.
 */
void ecdsa_ecmult_strauss_endo(
    ecdsa_gej_t *a_result,
    const ecdsa_gej_t *a_point_a,
    const ecdsa_scalar_t *a_scalar_a,
    const ecdsa_scalar_t *a_scalar_g
);

#ifdef __cplusplus
}
#endif

#endif /* _ECDSA_ENDOMORPHISM_H_ */
