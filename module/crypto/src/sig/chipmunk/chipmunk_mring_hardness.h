/*
 * Chipmunk MRNG — lattice hardness estimator (G1 gate).
 *
 * CR-11.G Phase 7.7 / task_ac273cea.  G1 (amendment v2 §6) requires that
 * the MSIS instance underlying MRV1 binding has classical core-SVP cost
 * ≥ 128 bits.  This header pins the floor and exposes the estimator.
 *
 * The estimator follows the standard core-SVP cost model
 *   bits ≈ 0.292 · β_BKZ
 * where β_BKZ is the smallest BKZ block size at which the Module-SIS
 * problem becomes feasible.  Reduction to Integer-SIS uses the rank
 *   n_int = CHIPMUNK_MRING_N · CHIPMUNK_MRING_K_PK = 512 · 6 = 3072
 * and the root-Hermite factor δ_β = (β/(2πe))^(1/(2(β-1))) (Chen-Nguyen).
 *
 * The binding norm bound is derived from MRV1 amendment v2 §5.1:
 *   β_∞ = 2 · β_w     // worst-case  ||Δ(b, r_b)||_∞  for two openings
 *   β_2  = √(n_int) · β_∞
 *
 * The estimator is deliberately *conservative*: it returns the SMALLEST
 * BKZ block that admits an attack, so the reported bit count is a lower
 * bound on adversarial work.  Reality is harder.
 */

#pragma once
#ifndef _CHIPMUNK_MRING_HARDNESS_H_
#define _CHIPMUNK_MRING_HARDNESS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minimum acceptable bit-security floors for MRV1 (G1 + G2 v2 gates).
 * Tests assert each estimator returns >= the matching floor.
 *   MSIS_BITS_MIN          — binding of C_b and bind-block cross-binding.
 *   MLWE_BITS_MIN          — hiding of C_b (MLWE secret recovery cost).
 *   INVERTIBILITY_BITS_MIN — Lyubashevsky-Seiler 2018 challenge-space
 *                            invertibility / Schwartz-Zippel root bound.
 */
#define CHIPMUNK_MRING_MSIS_BITS_MIN           128u
#define CHIPMUNK_MRING_MLWE_BITS_MIN           128u
#define CHIPMUNK_MRING_INVERTIBILITY_BITS_MIN  128u

/*
 * Classical core-SVP bit-security of the MRV1 Module-SIS binding
 * instance (G1).  See chipmunk_mring_hardness.c for the model.
 */
uint32_t chipmunk_mring_hardness_msis_bits(void);

/*
 * RELAXED Module-SIS binding security after the fold (G3.1 §9.4 / M4.0b).
 * The fold extractor returns a witness with coefficient norm inflated by
 * the soundness slack 2^D over D = a_fold_depth rounds (β* = 2^D·β), so a
 * binding collision need only reach norm 2^D·β_2.  Returns the core-SVP
 * bit-security at that relaxed target; must still clear
 * CHIPMUNK_MRING_MSIS_BITS_MIN at a_fold_depth = CHIPMUNK_MRING_FOLD_DEPTH_MAX.
 * a_fold_depth is clamped to CHIPMUNK_MRING_FOLD_DEPTH_MAX.
 */
uint32_t chipmunk_mring_hardness_msis_bits_relaxed(uint32_t a_fold_depth);

/*
 * Classical core-SVP bit-security of the MLWE instance that underpins
 * the statistical hiding of C_b (G2 v2 §A5).  Secret r_b is sampled
 * uniformly in [-β_w, β_w]^{n·K_PK}; attacker recovers r_b from the
 * public (H', H'·r_b) view.
 */
uint32_t chipmunk_mring_hardness_mlwe_bits(void);

/*
 * Subtractive-set size of the fold challenge space (G3.1 §9.5; SUPERSEDES
 * the stale G2 v2 §A3 partial-splitting model — see the .c for history).
 *
 * The corrected MRNG fold draws challenges from S = F_{qᵉ} \ {0} in the
 * ring extension R_q^{(e)} = R_q[Y]/Φ₉ (e = CHIPMUNK_MRING_EXT_DEG = 6),
 * whose nonzero differences are all invertible.  Returns
 *   log₂|S| = log₂(qᵉ − 1) ≈ e·log₂ q ≈ 129.6 bits,
 * which bounds the single-shot fold knowledge error κ ≤ D·2/|S|
 * (MRNG_G3_1_NOGAP_LEMMA.md §5).  Compared against
 * CHIPMUNK_MRING_INVERTIBILITY_BITS_MIN by the gate.
 */
uint32_t chipmunk_mring_hardness_invertibility_bits(void);

#ifdef __cplusplus
}
#endif

#endif /* _CHIPMUNK_MRING_HARDNESS_H_ */
