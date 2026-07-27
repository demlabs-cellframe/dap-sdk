/*
 * chipmunk_bdlop.h — BDLOP lattice commitment scheme + ABDLOP opening proof.
 *
 * Implements the commit-and-prove framework from:
 *   Baum, Damgård, Lyubashevsky, Osheter, Peikert,
 *   "More Efficient Commitments from Structured Lattice Assumptions",
 *   TCC 2020 (ePrint 2019/278).
 *
 * Protocol design follows the Lantern tutorial:
 *   Heimberger, Lugstein, Rechberger,
 *   "Studying Lattice-Based Zero-Knowledge Proofs:
 *    A Tutorial and an Implementation of Lantern",
 *   ePrint 2024/457.
 *
 * Ring:  R_q = Z_q[X]/(X^512 + 1),  q = 3168257  (~22 bits)
 * Module dimension k = 6, randomness dimension ℓ = 6.
 *
 * The commitment reuses the existing chipmunk Pedersen matrix A (6×6,
 * stored in NTT/Montgomery domain).  The BDLOP message occupies row 0:
 *
 *   C[0]   =  A[0]·r + m          (message row)
 *   C[i]   =  A[i]·r              (Ajtai hash rows, i = 1..k-1)
 *
 * The ABDLOP opening proof is a Lyubashevsky-style 3-move Σ-protocol
 * made non-interactive via Fiat-Shamir (SHAKE256):
 *
 *   Prover:
 *     1.  Sample masking:  y_m ∈ R_q (small),  y_r ∈ R_q^ℓ (small)
 *     2.  Compute hints:   W[i] = A[i]·y_r + (i==0 ? y_m : 0)
 *     3.  Challenge:       c = H(C, W, transcript)  — sparse ternary, weight τ
 *     4.  Responses:       z_m = c·m + y_m,  z_r = c·r + y_r
 *     5.  Rejection-sample z_m, z_r  (Lyubashevsky rej1)
 *
 *   Verifier:
 *     1.  Norm checks:  ||z_m||_∞ ≤ B_m,  ||z_r[j]||_∞ ≤ B_r
 *     2.  Linear equations:
 *           A[i]·z_r + (i==0 ? z_m : 0) − c·C[i]  =?  W[i]   for all i
 *
 * Soundness: Module-LWE + rejection-sampling (target 128-bit PQ security).
 * Zero-knowledge: rejection sampling makes z_m, z_r independent of m, r.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2025 Cellframe Project
 */
#ifndef CHIPMUNK_BDLOP_H
#define CHIPMUNK_BDLOP_H

#include "chipmunk.h"
#include "chipmunk_poly.h"
#include "chipmunk_pedersen.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =======================================================================
 *  Parameters
 * ======================================================================= */

/* Ring / module dimensions — reuse chipmunk globals */
#define CHIPMUNK_BDLOP_N          CHIPMUNK_N              /* 512 */
#define CHIPMUNK_BDLOP_Q          CHIPMUNK_Q              /* 3168257 */
#define CHIPMUNK_BDLOP_K          CHIPMUNK_PEDERSEN_K     /* 6 commitment rows */
#define CHIPMUNK_BDLOP_L          CHIPMUNK_LRS_K          /* 6 randomness polynomials */

/* Message: a single polynomial (row 0 of the commitment).
 * For range proofs this carries the bit-decomposition polynomial. */
#define CHIPMUNK_BDLOP_MSG_POLYS  1

/* Challenge: sparse ternary c ∈ {-1, 0, 1}^N with Hamming weight τ.
 *
 * TAU is defined below in the rejection sampling section — it must be 1
 * for practical rejection sampling with our modulus Q.
 */

/* Witness coefficient bound (matching LRS / Pedersen blinding). */
#define CHIPMUNK_BDLOP_WBOUND     CHIPMUNK_LRS_WITNESS_BOUND   /* 13 */

/* ---- Rejection sampling parameters (Lyubashevsky) ----
 *
 * The prover samples masking polynomials y ~ Uniform[-SAMP, SAMP] and
 * computes responses z = c·s + y.  The verifier checks ||z||_∞ ≤ RESP.
 *
 * For PERFECT zero-knowledge, the sampling bound must exceed the response
 * bound by the maximum possible secret contribution:
 *
 *   SAMP = RESP + τ · WBOUND
 *
 * Then for any z ∈ [-RESP, RESP], the probability P(z | s) is uniform
 * (independent of s), giving perfect ZK.
 *
 * The rejection rate per polynomial is approximately:
 *   P(reject) ≈ (τ · WBOUND) / (2 · SAMP)
 *
 * For τ=37, WBOUND=13, RESP=2^19:
 *   P(reject per poly) ≈ 481 / (2 · 524769) ≈ 0.046%
 * Over 7 polynomials × 512 coeffs = 3584 coefficients:
 *   P(all accept) ≈ (1 - 0.000046)^3584 ≈ 0.85  → ~1.2 retries avg
 */

/* ---- Rejection sampling parameters (Lyubashevsky) ----
 *
 * The prover samples masking polynomials y ~ Uniform[-SAMP, SAMP] and
 * computes responses z = c·s + y.  The verifier checks ||z||_∞ ≤ RESP.
 *
 * For PERFECT zero-knowledge, the sampling bound must exceed the response
 * bound by the maximum possible secret contribution:
 *   SAMP = RESP + τ · WBOUND
 *
 * The rejection probability PER COEFFICIENT is approximately:
 *   P(reject) ≈ τ·WBOUND / SAMP
 *
 * Over all (1+L)·N = 7·512 = 3584 coefficients, the probability that ALL
 * pass is approximately:
 *   P(all pass) ≈ (1 - τ·WBOUND/SAMP)^3584
 *
 * ---- OPTIMAL PARAMETER SELECTION (Phase 2.6b) ----
 *
 * We want: single round (ROUNDS=1) for compact proof, τ high enough for
 * 128-bit soundness, rejection rate manageable.
 *
 * Soundness per round: C(N,τ) × 2^τ.
 *   τ=18: C(512,18)×2^18 ≈ 2^131  →  131 bits ≥ 128  ✓
 *
 * Rejection probability per coefficient:
 *   P(reject) ≈ τ·WBOUND / SAMP = 18×13 / SAMP = 234 / SAMP
 *
 * With SAMP = Q/4 ≈ 792064:
 *   P(reject/coeff) ≈ 234/792064 ≈ 0.030%
 *   P(all pass 3584) ≈ (1-0.0003)^3584 ≈ 0.34 → 34% per attempt
 *   Need ~3 attempts on average. REJ_MAX_ROUNDS=16 gives P(fail) ≈ 10^{-8}.
 *
 * Proof size (single round, 3-byte packed):
 *   14 polys × 512 coeffs × 3 bytes = 21,504 bytes ≈ 21 KB
 *   vs previous 13-round version: 372 KB (18× reduction!)
 */
#define CHIPMUNK_BDLOP_TAU        18  /* Challenge weight: C(512,18)×2^18 ≈ 2^131 */

/* Sampling bound for masking polynomials.
 * Must be < Q/2 for correct modular arithmetic. */
#define CHIPMUNK_BDLOP_SAMP_M     (CHIPMUNK_Q / 4)   /* ≈ 792064 */
#define CHIPMUNK_BDLOP_SAMP_R     (CHIPMUNK_Q / 4)   /* ≈ 792064 */

/* Response bound: what the verifier checks.
 * RESP = SAMP - τ·(secret bound) ensures perfect ZK.
 * For the message polynomial (ternary, bound 1): RESP_M = SAMP - τ·1
 * For the randomness (bound WBOUND): RESP_R = SAMP - τ·WBOUND */
#define CHIPMUNK_BDLOP_MSG_BOUND   1   /* Ternary message: coefficients ∈ {0, 1} */
#define CHIPMUNK_BDLOP_RESP_M     (CHIPMUNK_BDLOP_SAMP_M - (uint32_t)(CHIPMUNK_BDLOP_TAU * CHIPMUNK_BDLOP_MSG_BOUND))
#define CHIPMUNK_BDLOP_RESP_R     (CHIPMUNK_BDLOP_SAMP_R - (uint32_t)(CHIPMUNK_BDLOP_TAU * CHIPMUNK_BDLOP_WBOUND))

/* Maximum rejection sampling retries before giving up.
 * With P(success) ≈ 0.34 per attempt, P(all fail 16) ≈ 0.66^16 ≈ 10^{-3}.
 * In practice, most proofs succeed within 3-5 attempts. */
#define CHIPMUNK_BDLOP_REJ_MAX_ROUNDS  16

/* Number of protocol repetitions for soundness amplification.
 * With τ=18: C(512,18)×2^18 ≈ 2^131 ≥ 2^128 → single round suffices. */
#define CHIPMUNK_BDLOP_ROUNDS     1

/* =======================================================================
 *  Structures
 * ======================================================================= */

/*
 * BDLOP commitment.
 *
 * Identical in memory layout to chipmunk_pedersen_commit_t:
 *   C[0]  =  A[0]·r + m   (message row)
 *   C[i]  =  A[i]·r        (Ajtai hash rows, i = 1 .. K-1)
 *
 * The message polynomial m is arbitrary (not restricted to scalar encoding).
 */
typedef chipmunk_pedersen_commit_t chipmunk_bdlop_commit_t;

/*
 * BDLOP opening secret (what the prover knows).
 */
typedef struct chipmunk_bdlop_secret {
    chipmunk_poly_t  message[CHIPMUNK_BDLOP_MSG_POLYS];   /* m: committed polynomial */
    chipmunk_poly_t  randomness[CHIPMUNK_BDLOP_L];         /* r: commitment randomness */
} chipmunk_bdlop_secret_t;

/*
 * BDLOP opening proof (repeated ROUNDS times for soundness amplification).
 *
 * Each round contains: K (W) + 1 (c) + 1 (z_m) + L (z_r) = 14 polynomials.
 * With ROUNDS=13: total = 13 × 14 = 182 polynomials.
 *
 * Uncompressed: 182 × 2048 bytes = 372 736 bytes ≈ 364 KB.
 * This is large but CORRECT and SECURE (128-bit PQ).
 *
 * Size reduction (Phase 2.6b): Gaussian masking → single round (τ=37)
 * reduces to ~28 KB.
 */
typedef struct chipmunk_bdlop_proof_round {
    chipmunk_poly_t  W[CHIPMUNK_BDLOP_K];           /* W[i] = A[i]·y_r + (i==0?y_m:0) */
    chipmunk_poly_t  challenge;                      /* c ∈ {-1,0,1}^N, weight τ */
    chipmunk_poly_t  z_m[CHIPMUNK_BDLOP_MSG_POLYS]; /* z_m = c·m + y_m */
    chipmunk_poly_t  z_r[CHIPMUNK_BDLOP_L];          /* z_r = c·r + y_r */
} chipmunk_bdlop_proof_round_t;

typedef struct chipmunk_bdlop_proof {
    uint32_t num_rounds;
    chipmunk_bdlop_proof_round_t rounds[CHIPMUNK_BDLOP_ROUNDS];
} chipmunk_bdlop_proof_t;

/* =======================================================================
 *  API — Commitment
 * ======================================================================= */

/*
 * Create a BDLOP commitment with an ARBITRARY message polynomial.
 *
 *   C[0]  =  A[0]·r + m
 *   C[i]  =  A[i]·r     (i = 1 .. K-1)
 *
 * \param a_commit      Output commitment (K polynomials).
 * \param a_params      Pedersen/BDLOP parameters (matrix A in NTT domain).
 * \param a_message     Message polynomial m (1 polynomial).
 * \param a_randomness  Randomness r (L polynomials, short coefficients).
 * \return 0 on success, negative errno on error.
 */
int chipmunk_bdlop_commit_poly(chipmunk_bdlop_commit_t *a_commit,
                                const chipmunk_pedersen_params_t *a_params,
                                const chipmunk_poly_t *a_message,
                                const chipmunk_poly_t a_randomness[CHIPMUNK_BDLOP_L]);

/* =======================================================================
 *  API — Opening proof
 * ======================================================================= */

/*
 * Create a BDLOP opening proof (non-interactive, Fiat-Shamir).
 *
 * Proves knowledge of (m, r) such that a_commit = Com(m, r).
 * Additionally proves ||m||_∞ ≤ a_msg_bound (approximate shortness).
 *
 * \param a_proof       Output proof.
 * \param a_params      Pedersen/BDLOP parameters.
 * \param a_commit      The commitment being opened.
 * \param a_message     Secret message polynomial m.
 * \param a_randomness  Secret randomness r.
 * \param a_msg_bound   Infinity-norm bound on m (for approximate shortness).
 *                      Pass INT32_MAX to skip the shortness constraint.
 * \param a_seed        32-byte seed for masking polynomial derivation.
 * \return 0 on success, negative errno on error.
 */
int chipmunk_bdlop_opening_prove(chipmunk_bdlop_proof_t *a_proof,
                                  const chipmunk_pedersen_params_t *a_params,
                                  const chipmunk_bdlop_commit_t *a_commit,
                                  const chipmunk_poly_t *a_message,
                                  const chipmunk_poly_t a_randomness[CHIPMUNK_BDLOP_L],
                                  int32_t a_msg_bound,
                                  const uint8_t a_seed[32]);

/*
 * Verify a BDLOP opening proof.
 *
 * Checks:
 *   1.  Response norms:  ||z_m||_∞ ≤ σ_m,  ||z_r[j]||_∞ ≤ σ_r
 *   2.  Linear equations:  A[i]·z_r + δ_{i0}·z_m − c·C[i] = W[i]
 *
 * \param a_proof   The proof to verify.
 * \param a_params  Pedersen/BDLOP parameters.
 * \param a_commit  The commitment being opened.
 * \return 1 if valid, 0 if invalid, negative errno on error.
 */
int chipmunk_bdlop_opening_verify(const chipmunk_bdlop_proof_t *a_proof,
                                   const chipmunk_pedersen_params_t *a_params,
                                   const chipmunk_bdlop_commit_t *a_commit);

/* =======================================================================
 *  API — Serialization
 * ======================================================================= */

/* Wire size of one proof round (bytes). */
size_t chipmunk_bdlop_proof_serialized_size(void);

/*
 * Serialize a proof to a compact byte buffer.
 * Uses coefficient packing where beneficial.
 *
 * \param a_out      Output buffer.
 * \param a_out_size Buffer capacity (must be ≥ chipmunk_bdlop_proof_serialized_size()).
 * \param a_proof    Proof to serialize.
 * \return Bytes written on success, negative errno on error.
 */
int chipmunk_bdlop_proof_serialize(uint8_t *a_out, size_t a_out_size,
                                    const chipmunk_bdlop_proof_t *a_proof);

/*
 * Deserialize a proof from a byte buffer.
 *
 * \param a_proof   Output proof.
 * \param a_in      Input buffer.
 * \param a_in_size Buffer size.
 * \return 0 on success, negative errno on error.
 */
int chipmunk_bdlop_proof_deserialize(chipmunk_bdlop_proof_t *a_proof,
                                      const uint8_t *a_in, size_t a_in_size);

/*
 * Wipe secret material from a proof (responses may leak info if masking fails).
 */
void chipmunk_bdlop_proof_wipe(chipmunk_bdlop_proof_t *a_proof);

#ifdef __cplusplus
}
#endif

#endif /* CHIPMUNK_BDLOP_H */
