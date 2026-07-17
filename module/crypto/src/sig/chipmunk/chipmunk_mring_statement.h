/*
 * CR-11.G Phase 7.7 — MRNG statement layer (G2 v2 §A1, §A6).
 *
 * Internal API.  All functions in this header are intra-module; they are
 * NOT part of the public chipmunk_ring_* surface (see chipmunk_ring.h).
 *
 * The statement layer implements the algebraic primitives that the M3
 * sign / verify path uses to construct and check the unified inner
 * product   ⟨b̃, P̃(c)⟩ = ρ(c)   over R_q (G2 v2 §A1.1):
 *
 *   • Vector-commitment generators (g, h, H′) derived from ring_hash.
 *   • Vector commitment   C_b = a·b + ⟨H′, r_b⟩   (G2 v2 REL-6).
 *   • Augmented witness   b̃ = (b, b∘(b−1), Y_pk-cross-bind slots)
 *     of length 2N + K_pk (G2 v2 §A1.1, §A1.2 cross-fold attachment).
 *   • Evaluation of the public vector P̃(c) ∈ R_q^{2N+K_pk} and the
 *     public scalar ρ(c) ∈ R_q for a transcript-derived challenge c.
 *   • Bind-block helpers: mask sampling, z_x assembly, norm checks,
 *     and the verifier-side Schnorr reconstruction
 *     M_pk = A_pk·z_x − c*·Y_pk   and   M_T = A_T·z_x − c*·T.
 *
 * The fold-tree itself (recursive halving with counter-mode Fiat-Shamir
 * challenges) is M4 and lives in chipmunk_mring_fold.c; this layer only
 * exposes the round-zero inputs and the final-round outputs.
 *
 * Every cryptographic primitive used here is reused from chipmunk_lrs_*
 * or chipmunk_poly_* — no self-rolled NTT, sampler, or hash (G5).
 */

#pragma once
#ifndef _CHIPMUNK_MRING_STATEMENT_H_
#define _CHIPMUNK_MRING_STATEMENT_H_

#include <stddef.h>
#include <stdint.h>

#include "chipmunk.h"
#include "chipmunk_lrs.h"
#include "chipmunk_mring_params.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ *
 *  Vector-commitment generators
 *
 *  The verifier and prover both recompute   (a, H′)   from  ring_hash
 *  via SHAKE256 with stable domain separators.  `a` is a single uniform
 *  R_q polynomial used as the projection of the bit-poly b (R_q packs 512
 *  coefficients ≥ CHIPMUNK_MRING_N_MAX = 256, so a single poly suffices).
 *  `H′` is a CHIPMUNK_MRING_K_PK-element uniform vector used as the
 *  randomness lane for  ⟨H′, r_b⟩  where  r_b ∈ R_q^{K_pk}.
 *
 *  All three generators are deterministic functions of ring_hash, so two
 *  parties with the same ring derive byte-identical generators.
 * ------------------------------------------------------------------------ */

typedef struct chipmunk_mring_vcom_gens {
    chipmunk_poly_t a;                              /* projection of b */
    chipmunk_poly_t H_prime[CHIPMUNK_MRING_K_PK];   /* randomness lane */
} chipmunk_mring_vcom_gens_t;

int chipmunk_mring_derive_vcom_generators(chipmunk_mring_vcom_gens_t *a_out,
                                          const uint8_t a_ring_hash[32]);

/* ------------------------------------------------------------------------ *
 *  Vector commitment   C_b = a · b_poly + ⟨H′, r_b⟩
 *
 *  • `b` is the indicator vector encoded as the low-N coefficients of a
 *    single R_q polynomial; the high (n − N) coefficients MUST be zero
 *    (the prover sets them so).  `chipmunk_mring_vcom_pack_b` packs an
 *    explicit uint8 indicator array into the poly form.
 *  • `r_b` is a short MLWE-bounded vector in R_q^{K_pk} with
 *    ‖r_b‖∞ ≤ β_w (= CHIPMUNK_MRING_BETA_W = 13).  The caller is
 *    responsible for sampling r_b via `chipmunk_lrs_h_to_short_poly`
 *    or an equivalent rejection sampler.
 *  • All polynomial multiplications use chipmunk_poly_mul_ntt; inputs
 *    are normalised to time-domain on entry and exit.
 * ------------------------------------------------------------------------ */

/*  Pack b ∈ {0,1}^N into the low-N coefficients of `a_b_poly`.
 *  `a_b_indicator` MUST have a_n_ring entries, each strictly 0 or 1; any
 *  other value yields -EINVAL.  High coefficients are zeroed. */
int chipmunk_mring_vcom_pack_b(chipmunk_poly_t *a_b_poly,
                               const uint8_t *a_b_indicator,
                               uint32_t a_n_ring);

/*  Compute  C_b = a · b_poly + Σ_j H′_j · r_b[j]   in R_q.
 *  Both inputs MUST be in the time-domain; on return C_b is in the
 *  time-domain.  Returns 0 on success, -EINVAL on parameter mismatch. */
int chipmunk_mring_vcom_commit(chipmunk_poly_t *a_Cb,
                               const chipmunk_mring_vcom_gens_t *a_gens,
                               const chipmunk_poly_t *a_b_poly,
                               const chipmunk_poly_t a_r_b[CHIPMUNK_MRING_K_PK],
                               uint64_t q);

/*  Open a single-poly commitment:  v = a⁻¹ · (C − ⟨H′, r⟩).  Returns 0
 *  on success, -EDOM if the projection generator a is non-invertible. */
int chipmunk_mring_vcom_open(chipmunk_poly_t *a_v_out,
                             const chipmunk_poly_t *a_C,
                             const chipmunk_mring_vcom_gens_t *a_gens,
                             const chipmunk_poly_t a_r[CHIPMUNK_MRING_K_PK],
                             uint64_t q);

/* ------------------------------------------------------------------------ *
 *  Norm check helper.  Returns 0 if every coefficient of `a_poly` is in
 *  the centered interval [-a_bound, +a_bound]; returns -ERANGE otherwise.
 *  Mirrors chipmunk_lrs_poly_chknorm_centered but accepts the (large)
 *  CHIPMUNK_MRING_BETA_Z bound and exposes it to the verifier-side
 *  reconstruction of z_x.
 * ------------------------------------------------------------------------ */

int chipmunk_mring_chknorm(const chipmunk_poly_t *a_poly, int32_t a_bound,
                           uint64_t q);

/* ------------------------------------------------------------------------ *
 *  Unified inner-product statement (G2 v2.1 §3) — M3.2 surface.
 *
 *  Glossary (matches G2 v2.1):
 *    N       — ring size, [N_MIN, N_MAX]
 *    K_PK    — Chipmunk witness dimension (= 6)
 *    b ∈ {0,1}^N — secret indicator of the signer subset
 *    x_i ∈ R_q^{K_PK} — secret short witness for ring member i
 *    X = Σ_i b_i x_i ∈ R_q^{K_PK} — aggregated witness
 *    pk_i = chipmunk_lrs_relation_eval(A_pk, x_i) ∈ R_q — public key
 *    Y_pk = chipmunk_lrs_relation_eval(A_pk, X) ∈ R_q  — aggregated pk
 *    t = Σ b_i — Hamming weight (= subset cardinality)
 *
 *  The augmented witness  b̃ ∈ R_q^{2N}  has
 *    b̃[i]    = b_i              (i ∈ [0, N))
 *    b̃[N+i]  = b_i (b_i − 1)    (i ∈ [0, N))   — identically 0 for honest b
 *
 *  The augmented public vector at challenge c ∈ R_q,
 *    P̃[i](c)    = c + c³ · pk_i      (i ∈ [0, N))
 *    P̃[N+i](c)  = c²                 (i ∈ [0, N))
 *
 *  The public target,
 *    ρ(c) = c · t + c³ · Y_pk
 *
 *  Claim 1 v2.1 (G2 v2.1 §3):
 *    ⟨b̃, P̃(c)⟩ = ρ(c)   in R_q   for an honest witness, for every c.
 *
 *  The fold (M4) reduces this to a single scalar in log₂(2N) = 1 + ⌈log₂ N⌉
 *  rounds; this header exposes only round-zero materialisers + the inner
 *  product as a test/sanity helper.
 * ------------------------------------------------------------------------ */

/*  Length of the augmented vectors  b̃, P̃.  Always  2 · N. */
uint32_t chipmunk_mring_augmented_dim(uint32_t a_n_ring);

/*
 *  Heap-allocated R_q-vector buffer.  Uses dap_new_z so each slot is
 *  zero-initialised; callers must release via chipmunk_mring_polyvec_free.
 *  Length is taken at allocation time and cannot be re-sized.
 */
typedef struct chipmunk_mring_polyvec {
    chipmunk_poly_t *slots;
    uint32_t length;
} chipmunk_mring_polyvec_t;

int  chipmunk_mring_polyvec_alloc(chipmunk_mring_polyvec_t *a_vec,
                                  uint32_t a_length);
void chipmunk_mring_polyvec_free(chipmunk_mring_polyvec_t *a_vec);

/*
 *  Materialise the augmented witness b̃ from the indicator vector b.
 *  The vector MUST be allocated with length = augmented_dim(n_ring).
 *  For an honest b ∈ {0,1}^N the second half is identically zero; for
 *  validation purposes we ENFORCE the bit constraint and reject any
 *  indicator byte ≥ 2 with -EINVAL.
 */
int chipmunk_mring_augment_witness(chipmunk_mring_polyvec_t *a_b_tilde,
                                   const uint8_t *a_b_indicator,
                                   uint32_t a_n_ring);

/*
 *  Materialise the round-zero public vector P̃(c) from the challenge c
 *  and the canonical ring (a_pks[0..N-1]).  Vector MUST be allocated
 *  with length = augmented_dim(n_ring).
 */
int chipmunk_mring_eval_public_P(chipmunk_mring_polyvec_t *a_P_tilde,
                                 const chipmunk_poly_t *a_c,
                                 const chipmunk_poly_t *a_pks,
                                 uint32_t a_n_ring,
                                 uint64_t q);

/*
 *  Public target ρ(c) = c · t + c³ · Y_pk  ∈ R_q.
 *  Caller-allocated  a_rho;  inputs c, Y_pk are time-domain polys.
 *  t is the Hamming weight; the lift Z → R_q is loss-free because
 *  N_MAX < q (G2 v2 §A2 / static assert in chipmunk_mring_params.h).
 */
int chipmunk_mring_eval_public_rho_q(chipmunk_poly_t *a_rho,
                                     const chipmunk_poly_t *a_c,
                                     uint32_t a_t,
                                     const chipmunk_poly_t *a_Y_pk,
                                     uint64_t q);

/*
 *  Aggregate witness X = Σ_i b_i · x_i ∈ R_q^{K_PK}.
 *  Each x_i is laid out as K_PK consecutive polynomials in a_x flat:
 *    a_x[i * K_PK + j] = x_i[j].
 *  Output X has K_PK polynomials.  Computed in the time domain; no NTT
 *  required because addition does not need NTT.
 */
int chipmunk_mring_aggregate_X(chipmunk_poly_t a_X_out[CHIPMUNK_MRING_K_PK],
                               const uint8_t *a_b_indicator,
                               const chipmunk_poly_t *a_x_flat,
                               uint32_t a_n_ring,
                               uint64_t q);

/*
 *  Inner product  ⟨b̃, P̃⟩ = Σ_i b̃_i · P̃_i  ∈ R_q.
 *  Both vectors MUST have identical length and contain time-domain polys.
 *  Used by tests / debug paths; the production fold (M4) consumes the
 *  same identity but processes it incrementally via halving rounds.
 */
int chipmunk_mring_inner_product(chipmunk_poly_t *a_out,
                                 const chipmunk_mring_polyvec_t *a_b_tilde,
                                 const chipmunk_mring_polyvec_t *a_P_tilde,
                                 uint64_t q);

/* ------------------------------------------------------------------------ *
 *  R_q inversion (M4.0a) — required by the halving fold to compute x_i⁻¹.
 *
 *  RING-SPLITTING NOTE (verified empirically, see
 *  test_chipmunk_mring_invert.c and MRNG_M4_INVERTIBILITY.md):
 *  The active Chipmunk NTT uses zetas_len = 1024 and
 *  chipmunk_poly_mul_ntt performs PLAIN coefficient-wise multiplication
 *  mod q (NOT Montgomery).  Because 1024 | (q − 1), the ring
 *  R_q = Z_q[X]/(X^n + 1) FULLY SPLITS into n = 512 linear factors over
 *  F_q.  Therefore inversion is the coefficient-wise modular inverse of
 *  the NTT representation, and x is invertible iff every one of its 512
 *  NTT coordinates is non-zero mod q.
 *
 *  Consequence for the fold: sparse-ternary challenges are non-invertible
 *  with probability ≈ n/q ≈ 2⁻¹²·⁶ per challenge (NOT 2⁻⁹⁸⁰ as the
 *  partial-splitting model in G2 v2 §A3 assumed).  The fold MUST use a
 *  deterministic verifier-mirrored retry loop on non-invertibility.
 *
 *  Returns:
 *    0        success; a_inv_out holds x⁻¹ in the time domain.
 *    -EDOM    x is non-invertible (some NTT coordinate is zero) — the
 *             caller (fold) must bump the FS nonce and resample x.
 *    -EINVAL  null args.
 *
 *  Challenges are PUBLIC Fiat-Shamir values, so the non-constant-time
 *  extended-Euclid modular inverse used internally is acceptable.
 * ------------------------------------------------------------------------ */
int chipmunk_mring_poly_invert_q(chipmunk_poly_t *a_inv_out,
                                 const chipmunk_poly_t *a_x,
                                 uint64_t q);

/* ------------------------------------------------------------------------ *
 *  Bind-block helpers (G2 v2.1 §4) — M3.3.
 *
 *  The bind block proves "the prover knows a short K_pk-vector X such
 *  that Y_pk = chipmunk_lrs_relation_eval(A_pk, X) and T =
 *  chipmunk_lrs_relation_eval(A_T, X)" via a SINGLE same-witness
 *  response z_x = ρ_x + c*·X ∈ R_q^{K_pk}.  The verifier reconstructs
 *  M_pk and M_T from (A_pk, A_T, Y_pk, T, c*, z_x) and feeds them back
 *  into the Fiat-Shamir transcript to re-derive c* (which closes the
 *  loop and rejects any malicious deviation).
 *
 *  A_T is per-(ring, ctx) — it is derived from ring_hash and ctx_hash
 *  via SHAKE128 (chipmunk_poly_uniform with per-slot nonces).  Note
 *  this differs from chipmunk_lrs_derive_A_I which is per-PK; MRNG
 *  needs one A_T for the whole ring to make T = A_T·X linkable across
 *  signers in the same subset.
 * ------------------------------------------------------------------------ */

/*
 *  Derive the per-(ring, ctx) link-tag matrix-row A_T ∈ R_q^{K_pk}.
 *  Uses SHA3-256 over a domain-separator + ring_hash + ctx_hash to
 *  seed chipmunk_poly_uniform, distinct nonces 0..K_pk-1 for slots.
 *  Deterministic in (ring_hash, ctx_hash).
 */
int chipmunk_mring_derive_A_T(chipmunk_poly_t a_A_T_out[CHIPMUNK_MRING_K_PK],
                              const uint8_t a_ring_hash[32],
                              const uint8_t a_ctx_hash[32]);

/*
 *  Sample the prover-side mask ρ_x ∈ R_q^{K_pk} with every coefficient
 *  uniformly in [-MASK_BOUND, +MASK_BOUND].  Deterministic in
 *  (a_seed, a_attempt) — caller increments a_attempt on rejection.
 *  Reuses chipmunk_lrs_h_to_bounded_poly so the LRS bounded-uniform
 *  abort statistical distance bound transfers verbatim.
 */
int chipmunk_mring_bind_mask_sample(chipmunk_poly_t a_rho_x_out[CHIPMUNK_MRING_K_PK],
                                    const uint8_t a_seed[32],
                                    uint32_t a_attempt);

/*
 *  Prover-side bind response:  z_x = ρ_x + c*·X  ∈ R_q^{K_pk}.
 *
 *  Returns:
 *    0           on success and accepting abort (all coefficients in
 *                [-RESPONSE_BOUND, RESPONSE_BOUND));
 *    -EAGAIN     if any coefficient is out of range — caller MUST
 *                resample ρ_x with a fresh attempt and retry;
 *    -EINVAL     on argument errors.
 *
 *  c* is the bind-block Fiat-Shamir challenge (sparse-ternary R_q
 *  polynomial); X is the K_pk-vector aggregated witness.
 */
int chipmunk_mring_bind_prove_z_x(chipmunk_poly_t a_z_x_out[CHIPMUNK_MRING_K_PK],
                                  const chipmunk_poly_t a_rho_x[CHIPMUNK_MRING_K_PK],
                                  const chipmunk_poly_t *a_c_star,
                                  const chipmunk_poly_t a_X[CHIPMUNK_MRING_K_PK],
                                  uint64_t q);

/*
 *  Verifier-side Schnorr-style reconstruction (G2 v2.1 §4):
 *    M_pk := chipmunk_lrs_relation_eval(A_pk, z_x) − c*·Y_pk   ∈ R_q
 *    M_T  := chipmunk_lrs_relation_eval(A_T,  z_x) − c*·T      ∈ R_q
 *  These are then re-absorbed into the Fiat-Shamir transcript so that
 *  c* is recomputable; any malicious tampering of z_x / Y_pk / T flips
 *  the recomputed c* and the verifier rejects the signature.
 *
 *  Also performs the norm check ‖z_x‖∞ < RESPONSE_BOUND; returns
 *  -ERANGE if any coefficient is out of range (G2 v2 §A6: verifier
 *  recomputes Π_norm from the unpacked z_x, not from a wire field).
 */
int chipmunk_mring_bind_verify_reconstruct(chipmunk_poly_t *a_M_pk_out,
                                           chipmunk_poly_t *a_M_T_out,
                                           const chipmunk_poly_t a_A_pk[CHIPMUNK_MRING_K_PK],
                                           const chipmunk_poly_t a_A_T[CHIPMUNK_MRING_K_PK],
                                           const chipmunk_poly_t a_z_x[CHIPMUNK_MRING_K_PK],
                                           const chipmunk_poly_t *a_c_star,
                                           const chipmunk_poly_t *a_Y_pk,
                                           const chipmunk_poly_t *a_T,
                                           uint64_t q);

#ifdef __cplusplus
}
#endif
#endif /* _CHIPMUNK_MRING_STATEMENT_H_ */
