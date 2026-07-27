/*
 * chipmunk_range_proof_bdlop.h — Lattice-based range proof via BDLOP.
 *
 * Replaces the broken Stern-like range proof (chipmunk_range_proof.c).
 * Uses the BDLOP commitment + ABDLOP opening proof as building blocks.
 *
 * Strategy (Lantern-style approximate shortness):
 *
 *   Given a Pedersen commitment C = Com(v, r) to a value v ∈ [0, 2^BITS):
 *
 *   1. Decompose v = Σ b_i · 2^i  (b_i ∈ {0, 1})
 *   2. Define the bit-polynomial poly_b with ternary coefficients:
 *        poly_b.coeffs[k] = b_{k mod BITS}   for k = 0..N-1
 *      (bits are tiled across polynomial coefficients)
 *   3. The "bit-ness" constraint b_i ∈ {0,1} is equivalent to:
 *        b_i · (1 - b_i) = 0  ⟹  b_i² = b_i
 *      In polynomial form: poly_b² = poly_b  (mod X^N+1) — NO! This is
 *      not right because negacyclic convolution wraps. Instead we use
 *      the standard approach:
 *
 *   CORRECTED approach (following Bulletproofs / ENS20):
 *   We commit to each bit individually and aggregate:
 *
 *   For range [0, 2^BITS), create BITS individual commitments:
 *     C_i = Com(b_i, r_i)  for i = 0..BITS-1
 *   such that:
 *     a) b_i ∈ {0, 1}     (bit-ness)
 *     b) Σ 2^i · b_i = v  (aggregation)
 *
 *   Bit-ness proof: b_i ∈ {0,1} ⟺ b_i · (b_i - 1) = 0
 *   This is a degree-2 relation, handled by the ABDLOP protocol's
 *   quadratic constraint extension.
 *
 *   For simplicity in the first implementation, we use a different approach:
 *   the "short polynomial" encoding. Instead of per-bit commitments, we
 *   commit to a SINGLE polynomial whose coefficients encode the bits:
 *
 *     m = (b_0, b_1, ..., b_{BITS-1}, 0, 0, ..., 0)  ∈ {0,1}^N
 *
 *   Then ||m||_∞ ≤ 1 (approximate shortness with bound 1).
 *   The BDLOP opening proof with a_msg_bound = 1 proves ||m||_∞ ≤ 1.
 *
 *   The aggregation Σ 2^i · b_i = v is verified by checking:
 *     <powers_of_2, m> = v  mod q
 *   using a linear relation on the commitment.
 *
 *   Total proof: one BDLOP commitment + one BDLOP opening proof.
 *   Proof size: ~28 KB per range proof (per output).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2025 Cellframe Project
 */
#ifndef CHIPMUNK_RANGE_PROOF_BDLOP_H
#define CHIPMUNK_RANGE_PROOF_BDLOP_H

#include "chipmunk.h"
#include "chipmunk_poly.h"
#include "chipmunk_pedersen.h"
#include "chipmunk_bdlop.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =======================================================================
 *  Parameters
 * ======================================================================= */

/* Range: [0, 2^BITS).  64 bits covers amounts up to ~18.4 × 10^18. */
#define CHIPMUNK_RANGE_BDLOP_BITS   64

/* =======================================================================
 *  Structures
 * ======================================================================= */

/*
 * Aggregated range proof for a single value.
 *
 * Contains:
 *   - The BDLOP commitment to the bit-polynomial
 *   - The BDLOP opening proof (proves ||m||_∞ ≤ 1)
 *   - The committed value (for the verifier's aggregation check)
 *   - The blinding for the Pedersen commitment (needed to link to the
 *     original output commitment)
 *
 * Size: sizeof(chipmunk_bdlop_proof_t) + a few hundred bytes overhead
 *      ≈ 28 KB + ~400 B = ~29 KB
 */
typedef struct chipmunk_range_proof_bdlop {
    chipmunk_bdlop_commit_t   commit;      /* BDLOP commitment to bit-poly */
    chipmunk_bdlop_proof_t    proof;       /* BDLOP opening proof */
    uint8_t                   value[32];   /* The value v (LE uint256, only low 64 bits used) */
    uint32_t                  bits;        /* Number of range bits (CHIPMUNK_RANGE_BDLOP_BITS) */
} chipmunk_range_proof_bdlop_t;

/* =======================================================================
 *  API
 * ======================================================================= */

/*
 * Create a range proof proving that a_value ∈ [0, 2^BITS).
 *
 * The proof commits to the bit-decomposition of a_value and proves:
 *   1. Each coefficient of the committed polynomial is ∈ {0, 1}
 *      (via BDLOP opening with norm bound 1)
 *   2. The weighted sum Σ b_i · 2^i = a_value
 *
 * \param a_proof      Output proof.
 * \param a_params     Pedersen/BDLOP parameters (shared global).
 * \param a_value      The value to prove range of (32-byte LE uint256).
 * \param a_seed       32-byte seed for commitment blinding derivation.
 * \return 0 on success, negative errno on error.
 */
int chipmunk_range_proof_bdlop_prove(chipmunk_range_proof_bdlop_t *a_proof,
                                      const chipmunk_pedersen_params_t *a_params,
                                      const uint8_t a_value[CHIPMUNK_PEDERSEN_VALUE_BYTES],
                                      const uint8_t a_seed[32]);

/*
 * Create a range proof with explicit blinding polynomials.
 * Used for the anchor output whose blinding must close the Pedersen gap.
 *
 * \param a_proof      Output proof.
 * \param a_params     Pedersen/BDLOP parameters.
 * \param a_value      The value to prove range of.
 * \param a_r          Pre-derived blinding polynomials (for Pedersen commit).
 * \param a_seed       32-byte seed for BDLOP proof masking derivation.
 * \return 0 on success, negative errno on error.
 */
int chipmunk_range_proof_bdlop_prove_explicit(chipmunk_range_proof_bdlop_t *a_proof,
                                                const chipmunk_pedersen_params_t *a_params,
                                                const uint8_t a_value[CHIPMUNK_PEDERSEN_VALUE_BYTES],
                                                const chipmunk_poly_t a_r[CHIPMUNK_BDLOP_L],
                                                const uint8_t a_seed[32]);

/*
 * Verify a range proof.
 *
 * Checks:
 *   1. BDLOP opening proof is valid (linear equations + norm bounds)
 *   2. ||committed polynomial||_∞ ≤ 1 (bit-ness)
 *   3. Weighted sum of committed bits equals the claimed value
 *
 * \param a_proof   The proof to verify.
 * \param a_params  Pedersen/BDLOP parameters.
 * \return 1 if valid, 0 if invalid, negative errno on error.
 */
int chipmunk_range_proof_bdlop_verify(const chipmunk_range_proof_bdlop_t *a_proof,
                                       const chipmunk_pedersen_params_t *a_params);

/*
 * Serialize a range proof to bytes.
 */
size_t chipmunk_range_proof_bdlop_serialized_size(void);

int chipmunk_range_proof_bdlop_serialize(uint8_t *a_out, size_t a_out_size,
                                          const chipmunk_range_proof_bdlop_t *a_proof);

int chipmunk_range_proof_bdlop_deserialize(chipmunk_range_proof_bdlop_t *a_proof,
                                            const uint8_t *a_in, size_t a_in_size);

void chipmunk_range_proof_bdlop_wipe(chipmunk_range_proof_bdlop_t *a_proof);

#ifdef __cplusplus
}
#endif

#endif /* CHIPMUNK_RANGE_PROOF_BDLOP_H */
