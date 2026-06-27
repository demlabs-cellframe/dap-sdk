/*
 * Chipmunk MRNG — lattice hardness estimator (G1 gate).
 *
 * CR-11.G Phase 7.7 / task_ac273cea.  See chipmunk_mring_hardness.h for
 * the contract and the math reference.  This translation unit deliberately
 * has zero crypto-state and zero allocations — it is pure double-precision
 * arithmetic and may be called from tests at any point.
 */

#include <math.h>
#include <stdint.h>

#include "chipmunk_mring_hardness.h"
#include "chipmunk_mring_params.h"
#include "chipmunk_mring_ext.h"   /* CHIPMUNK_MRING_EXT_DEG (e) */

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#  define M_E  2.71828182845904523536
#endif

/*
 * Chen-Nguyen root-Hermite factor for BKZ block size β:
 *
 *   δ_β = (β / (2πe))^(1/(2(β-1)))
 *
 * Returned in log2 form for the feasibility comparison.
 * Undefined for β ≤ 2·π·e ≈ 17.08 (where the log term is non-positive);
 * caller skips those.
 */
static double s_log2_delta_for_bkz(uint32_t a_bkz)
{
    const double b = (double)a_bkz;
    const double inside = b / (2.0 * M_PI * M_E);
    if (inside <= 1.0) {
        return -1.0;  /* sentinel: estimator skips */
    }
    return log2(inside) / (2.0 * (b - 1.0));
}

/*
 * Core Module-SIS core-SVP cost: smallest BKZ block β (mapped to bits via
 * the classical 0.292·β exponent) at which the primal lattice's shortest
 * vector falls below the collision-norm target log2 β_2.
 *
 *   1. Compute log2 δ_β (Chen-Nguyen).
 *   2. Project to Integer-SIS at the asymptotic optimal sub-lattice
 *      dimension m* = √(n_int · log2 q / log2 δ).
 *   3. Shortest vector achievable by BKZ-β at m* has length
 *        log2 ‖v‖ ≈ 2 · √(n_int · log2 q · log2 δ).
 *   4. Attack succeeds iff ‖v‖ ≤ β_2, i.e. lhs ≤ log2 β_2.
 */
static uint32_t s_msis_core(double a_n_int, double a_q_bits, double a_log2_beta)
{
    for (uint32_t l_bkz = 60u; l_bkz <= 20000u; ++l_bkz) {
        const double l_log2_delta = s_log2_delta_for_bkz(l_bkz);
        if (l_log2_delta <= 0.0) {
            continue;
        }
        const double l_lhs = 2.0 * sqrt(a_n_int * a_q_bits * l_log2_delta);
        if (l_lhs <= a_log2_beta) {
            const double l_bits = 0.292 * (double)l_bkz;
            if (l_bits >= (double)UINT32_MAX) {
                return UINT32_MAX;
            }
            return (uint32_t)floor(l_bits);
        }
    }
    return UINT32_MAX;
}

uint32_t chipmunk_mring_hardness_msis_bits(void)
{
    /*
     * MRV1 binding instance.
     *   n_int  = N · K_PK         — integer rank of the Module-SIS
     *                                instance after projection from R_q.
     *   q_bits = log2 q
     *   β_∞    = 2 · β_w          — worst-case |Δ| for a binding collision
     *                                (b ∈ {0,1} contributes ≤ 2 · 1;
     *                                 r_b ∈ [−β_w, β_w] contributes 2·β_w,
     *                                 take the max).
     *   β_2    = √n_int · β_∞
     */
    const double n_int   = (double)CHIPMUNK_MRING_N * (double)CHIPMUNK_MRING_K_PK;
    const double q_bits  = log2((double)CHIPMUNK_MRING_Q);
    const double beta_inf = 2.0 * (double)CHIPMUNK_MRING_BETA_W;
    const double beta_2   = sqrt(n_int) * beta_inf;
    const double log2_beta = log2(beta_2);

    return s_msis_core(n_int, q_bits, log2_beta);
}

uint32_t chipmunk_mring_hardness_msis_bits_relaxed(uint32_t a_fold_depth)
{
    /*
     * G3.1 §9.4 / M4.0b — RELAXED binding norm after the fold.
     *
     * The fold extractor (NOGAP_LEMMA §3) returns a witness whose
     * coefficient norm is inflated by the Bulletproofs-style soundness
     * slack 2^D over D = a_fold_depth rounds: β* = 2^D · β_∞.  A malicious
     * prover therefore needs only a Module-SIS collision of norm β*_2 =
     * 2^D · β_2, so binding security must be evaluated at this larger
     * target.  In log2 space:  log2 β*_2 = D + log2 β_2.
     *
     * This is the obligation flagged in MRNG_G3_1_NOGAP_LEMMA.md §6: the
     * G1 MSIS floor must still hold at the relaxed bound for the gate to
     * stand.  a_fold_depth is clamped to CHIPMUNK_MRING_FOLD_DEPTH_MAX.
     */
    uint32_t l_depth = a_fold_depth;
    if (l_depth > CHIPMUNK_MRING_FOLD_DEPTH_MAX) {
        l_depth = CHIPMUNK_MRING_FOLD_DEPTH_MAX;
    }
    const double n_int   = (double)CHIPMUNK_MRING_N * (double)CHIPMUNK_MRING_K_PK;
    const double q_bits  = log2((double)CHIPMUNK_MRING_Q);
    const double beta_inf = 2.0 * (double)CHIPMUNK_MRING_BETA_W;
    const double beta_2   = sqrt(n_int) * beta_inf;
    const double log2_beta_relaxed = log2(beta_2) + (double)l_depth;

    return s_msis_core(n_int, q_bits, log2_beta_relaxed);
}

/* -------------------------------------------------------------------------
 * G2 v2 §A5 — MLWE hiding of C_b.
 *
 * Instance:  given (H', H' · r_b mod q), recover r_b ∈ [-β_w, β_w]^{n·K_PK}.
 * Reduction to Integer-LWE with dimension n_int = n · K_PK, modulus q,
 * uniform short secret of bound β_w.  Core-SVP cost model identical to
 * MSIS — find the smallest BKZ block at which the primal lattice's
 * shortest vector falls below the LWE secret-norm threshold.
 *
 *   √n_int · β_w  (l2 norm of the secret)  ↔  short SVP target
 *
 * We reuse the same δ-vs-feasibility formula:
 *    2 · √(n_int · log2 q · log2 δ)  ≤  log2 (√n_int · β_w)
 * which gives the secret-recovery threshold.
 * ---------------------------------------------------------------------- */

uint32_t chipmunk_mring_hardness_mlwe_bits(void)
{
    const double n_int   = (double)CHIPMUNK_MRING_N * (double)CHIPMUNK_MRING_K_PK;
    const double q_bits  = log2((double)CHIPMUNK_MRING_Q);
    /* Secret l2 norm: √(n_int) · β_w  (uniform short ternary). */
    const double secret_l2 = sqrt(n_int) * (double)CHIPMUNK_MRING_BETA_W;
    const double log2_target = log2(secret_l2);

    for (uint32_t l_bkz = 60u; l_bkz <= 20000u; ++l_bkz) {
        const double l_log2_delta = s_log2_delta_for_bkz(l_bkz);
        if (l_log2_delta <= 0.0) {
            continue;
        }
        const double l_lhs = 2.0 * sqrt(n_int * q_bits * l_log2_delta);
        if (l_lhs <= log2_target) {
            const double l_bits = 0.292 * (double)l_bkz;
            if (l_bits >= (double)UINT32_MAX) {
                return UINT32_MAX;
            }
            return (uint32_t)floor(l_bits);
        }
    }
    return UINT32_MAX;
}

/* -------------------------------------------------------------------------
 * G3.1 §9.5 — subtractive-set size (REPLACES the stale G2 v2 §A3 model).
 *
 * HISTORY / SUPERSESSION.  The original G2 v2 §A3 estimator assumed a
 * PARTIALLY-splitting R_q (l = 8 factors of degree 64) and reported
 * λ_inv ≈ 980 bits via a Lyubashevsky-Seiler union bound.  The M4.0a
 * finding (`MRNG_M4_INVERTIBILITY.md`, empirically: non-invertibility
 * rate 8/50 000 ≈ n/q) PROVED R_q actually FULLY splits — so that model
 * is WRONG and is retired.  A short-challenge subtractive set in the bare
 * R_q has size ≤ q ≈ 2²¹·⁶, giving only ≈17-bit fold soundness.
 *
 * The corrected MRNG fold (Option B, `MRNG_G3_1_EXTENSION_SOUNDNESS.md`,
 * `MRNG_G3_1_NOGAP_LEMMA.md`) draws challenges from the subtractive set
 *
 *     S = F_{qᵉ} \ {0}   in the ring extension R_q^{(e)} = R_q[Y]/Φ₉,
 *
 * with e = CHIPMUNK_MRING_EXT_DEG = 6.  ALL nonzero differences in S are
 * invertible by construction (proven in test_chipmunk_mring_subtractive).
 * The relevant security quantity is therefore the SUBTRACTIVE-SET SIZE
 *
 *     log₂|S| = log₂(qᵉ − 1) ≈ e · log₂ q ≈ 6 · 21.595 ≈ 129.6 bits,
 *
 * which bounds the per-round knowledge error κ_round ≤ 2/|S| and (over
 * D ≤ 9 rounds) κ_total ≤ D·2/|S| ≈ 2⁻¹²⁵·⁴ (NOGAP_LEMMA §5).  This
 * function now reports log₂|S| so the G1/G3 gate compares the fold's true
 * single-shot soundness floor against CHIPMUNK_MRING_INVERTIBILITY_BITS_MIN.
 * ---------------------------------------------------------------------- */

uint32_t chipmunk_mring_hardness_invertibility_bits(void)
{
    /* log₂|S| = log₂(qᵉ − 1), e = extension degree.  Compute as
     * e·log₂ q + log₂(1 − q⁻ᵉ); the correction is ≈ −2⁻¹³⁰, negligible,
     * so e·log₂ q is exact to floor precision. */
    const double e_deg  = (double)CHIPMUNK_MRING_EXT_DEG;
    const double q_bits = log2((double)CHIPMUNK_MRING_Q);
    const double l_bits = e_deg * q_bits;

    if (l_bits <= 0.0) {
        return 0u;
    }
    if (l_bits >= (double)UINT32_MAX) {
        return UINT32_MAX;
    }
    return (uint32_t)floor(l_bits);
}
