/*
 * chipmunk_deep.h — DEEP composition polynomial for FRI-DEEP PCS.
 *
 * DEEP (Differentiable Evaluation and Proximity) technique:
 *   For each committed polynomial f_i, compute:
 *     deep_q_i(X) = [f_i(X) - f_i(z)] / (X - z)
 *   where z ∈ F_q \ D is a DEEP point from the transcript (outside the
 *   FRI evaluation domain D).
 *
 *   Then compose:
 *     H(X) = Σ γ_i · deep_q_i(X)
 *   where γ_i ∈ F_q are random combination weights from the transcript.
 *
 *   H(X) has degree ≤ max_i(deg(f_i) - 1) ≤ 510.
 *   H(X) is committed via FRI-PCS (Reed-Solomon encoded to 2048 evals).
 *
 * Soundness (DEEP-ALI):
 *   (N + 5) / |F_{q^6}| ≈ 517 / 2^{129.6} ≈ 120.6 bits per query.
 *   Combined with FRI proximity (8 bits) and grinding (16 bits):
 *   total ≈ 145 bits >> 128-bit post-quantum target.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "chipmunk.h"
#include "chipmunk_poly.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of polynomials in the DEEP composition. */
#define CHIPMUNK_DEEP_MAX_POLYS    4u

/* DEEP opening: evaluation of each polynomial at the DEEP point z,
 * plus the committed composition polynomial H(X). */
typedef struct chipmunk_deep_opening {
    int32_t z_point;                              /**< DEEP evaluation point z ∈ F_q \ D */
    int32_t evals[CHIPMUNK_DEEP_MAX_POLYS];      /**< f_i(z) for each committed poly */
    uint32_t num_polys;                           /**< Number of polynomials (≤ MAX) */
} chipmunk_deep_opening_t;

/* DEEP prover state. */
typedef struct chipmunk_deep_prover {
    int32_t composition[CHIPMUNK_N];             /**< H(X) = Σ γ_i · deep_q_i(X), deg ≤ 510 */
    int32_t gammas[CHIPMUNK_DEEP_MAX_POLYS];      /**< Random combination weights */
    int32_t z;                                    /**< DEEP point */
    int32_t evals[CHIPMUNK_DEEP_MAX_POLYS];      /**< f_i(z) evaluations */
    uint32_t num_polys;                           /**< Number of polynomials */
    bool composed;                                /**< True after deep_compose() */
} chipmunk_deep_prover_t;

/**
 * Initialize DEEP prover state.
 * @param prov DEEP prover to initialize.
 * @return 0 on success, negative on error.
 */
int chipmunk_deep_prover_init(chipmunk_deep_prover_t *prov);

/**
 * Free DEEP prover resources (no-op for now, placeholder for future).
 * @param prov DEEP prover.
 */
void chipmunk_deep_prover_free(chipmunk_deep_prover_t *prov);

/**
 * Compute DEEP composition polynomial.
 *
 * For each polynomial f_i (degree ≤ 511):
 *   1. Evaluate f_i(z) using Horner's method
 *   2. Compute synthetic division: deep_q_i(X) = [f_i(X) - f_i(z)] / (X - z)
 *   3. Accumulate: H(X) += γ_i · deep_q_i(X)
 *
 * The composition polynomial H(X) has degree ≤ 510 and is stored in
 * prov->composition[0..511] (coefficients 511 = 0 for degree ≤ 510).
 *
 * @param prov        DEEP prover state (must be initialized).
 * @param polys       Array of polynomials to compose (each degree ≤ 511).
 * @param num_polys   Number of polynomials (1..CHIPMUNK_DEEP_MAX_POLYS).
 * @param z_point     DEEP evaluation point z ∈ F_q (must be nonzero).
 * @param gammas      Random combination weights γ_i ∈ F_q.
 * @return 0 on success, negative on error.
 */
int chipmunk_deep_compose(chipmunk_deep_prover_t *prov,
                          const chipmunk_poly_t polys[],
                          uint32_t num_polys,
                          int32_t z_point,
                          const int32_t gammas[]);

/**
 * Verify DEEP composition at a single F_q test point x.
 *
 * Checks that:
 *   H(x) = Σ γ_i · [f_i(x) - f_i(z)] / (x - z)
 *
 * @param opening     DEEP opening (z_point, evals f_i(z)).
 * @param polys       The original committed polynomials.
 * @param num_polys   Number of polynomials.
 * @param gammas      Random combination weights.
 * @param composition The committed composition polynomial H(X).
 * @param x           Test point in F_q (must differ from z_point).
 * @return true if the relation holds, false otherwise.
 */
bool chipmunk_deep_verify(const chipmunk_deep_opening_t *opening,
                          const chipmunk_poly_t polys[],
                          uint32_t num_polys,
                          const int32_t gammas[],
                          const int32_t composition[CHIPMUNK_N],
                          int32_t x);

/**
 * Get the composition polynomial from the prover.
 * @param prov DEEP prover (must be composed).
 * @return Pointer to composition[CHIPMUNK_N], or NULL on error.
 */
const int32_t *chipmunk_deep_prover_composition(const chipmunk_deep_prover_t *prov);

/**
 * Build a DEEP opening struct from the prover state.
 * @param opening Output opening.
 * @param prov    DEEP prover (must be composed).
 * @return 0 on success, negative on error.
 */
int chipmunk_deep_build_opening(chipmunk_deep_opening_t *opening,
                               const chipmunk_deep_prover_t *prov);

#ifdef __cplusplus
}
#endif
