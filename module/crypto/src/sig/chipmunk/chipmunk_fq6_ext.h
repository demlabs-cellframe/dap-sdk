/*
 * CR-11.G Phase 7.7 — MRNG ring-extension arithmetic (G3.1 §9.2).
 *
 * Internal API.  Implements arithmetic in the degree-e extension
 *
 *     R_q^{(e)} := R_q[Y] / (g(Y)),   g(Y) = Φ₉(Y) = Y⁶ + Y³ + 1,  e = 6,
 *
 * where R_q = Z_q[X]/(X⁵¹²+1) is the Chipmunk ring (q = 3 168 257).
 *
 * Because R_q fully splits (M4.0a finding), R_q ≅ F_q⁵¹² and hence
 *     R_q^{(e)}  ≅  (F_q[Y]/(Φ₉))⁵¹²  =  (F_{q⁶})⁵¹².
 * This extension is introduced ONLY to obtain a large subtractive
 * (exceptional) challenge set of size qᵉ ≈ 2¹²⁹·⁶ for the fold, closing
 * the soundness gap exposed in MRNG_M4_INVERTIBILITY.md (Option B,
 * MRNG_G3_1_EXTENSION_SOUNDNESS.md).
 *
 * Representation: an R_q^{(e)} element is a degree-<e polynomial in Y
 * with R_q coefficients,  a = Σ_{j=0}^{e-1} a.c[j] · Yʲ.  Each a.c[j] is
 * a standard chipmunk_poly_t (time-domain).
 *
 * "Scalar" elements (the challenge subtractive set) are those whose six
 * Y-coefficients are CONSTANT R_q polynomials; they are isomorphic to
 * F_{q⁶} and are the only elements that ever need inversion (their
 * differences are invertible by construction).  General-element
 * inversion (per-slot F_{q⁶}) is provided for completeness/tests.
 *
 * Every primitive reuses chipmunk_poly_* (NTT, add, sub) — no
 * self-rolled R_q multiplication (G5).
 */

#pragma once
#ifndef _CHIPMUNK_FQ6_EXT_H_
#define _CHIPMUNK_FQ6_EXT_H_

#include <stdbool.h>
#include <stdint.h>

#include "chipmunk.h"
#include "chipmunk_mring_params.h"

/* Forward declaration for per-q NTT context (Phase 9.14). */
struct chipmunk_ntt_ctx;
typedef struct chipmunk_ntt_ctx chipmunk_ntt_ctx_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Extension degree e is pinned in chipmunk_mring_params.h (CHIPMUNK_FQ6_EXT_DEG). */

/* Element of R_q^{(e)}: coefficients of a degree-<e polynomial in Y. */
typedef struct chipmunk_fq6_ext {
    chipmunk_poly_t c[CHIPMUNK_FQ6_EXT_DEG];
} chipmunk_fq6_ext_t;

/* ---- constructors -------------------------------------------------- */

/*  a := 0  (additive identity). */
void chipmunk_fq6_ext_zero(chipmunk_fq6_ext_t *a_out);

/*  a := 1  (multiplicative identity, the constant polynomial 1·Y⁰). */
void chipmunk_fq6_ext_one(chipmunk_fq6_ext_t *a_out);

/*  Embed an R_q element into R_q^{(e)} at Y-degree 0:  out := a · Y⁰. */
void chipmunk_fq6_ext_embed(chipmunk_fq6_ext_t *a_out,
                              const chipmunk_poly_t *a_base);

/*  Project the Y-degree-0 component:  out := a.c[0]. */
void chipmunk_fq6_ext_project(chipmunk_poly_t *a_out,
                                const chipmunk_fq6_ext_t *a);

/*  Normalise every R_q coefficient into canonical [0, q).  Wire qpack and
 *  FS transcript absorption both require this representation. */
void chipmunk_fq6_ext_canonicalize_q(chipmunk_fq6_ext_t *a, uint64_t q);

/*  True iff a lies in the base ring R_q (Y-components 1..e-1 all zero).
 *  Used by the verifier-side consistency lane (G3.1 §7). */
bool chipmunk_fq6_ext_is_in_base_q(const chipmunk_fq6_ext_t *a, uint64_t q);

/* ---- ring operations ----------------------------------------------- */

/*  out := a + b   (coefficient-wise R_q add). */
int chipmunk_fq6_ext_add(chipmunk_fq6_ext_t *a_out,
                           const chipmunk_fq6_ext_t *a,
                           const chipmunk_fq6_ext_t *b);
int chipmunk_fq6_ext_add_q(chipmunk_fq6_ext_t *a_out,
                             const chipmunk_fq6_ext_t *a,
                             const chipmunk_fq6_ext_t *b,
                             uint64_t q);

/*  out := a − b. */
int chipmunk_fq6_ext_sub(chipmunk_fq6_ext_t *a_out,
                           const chipmunk_fq6_ext_t *a,
                           const chipmunk_fq6_ext_t *b);
int chipmunk_fq6_ext_sub_q(chipmunk_fq6_ext_t *a_out,
                             const chipmunk_fq6_ext_t *a,
                             const chipmunk_fq6_ext_t *b,
                             uint64_t q);

/*  out := a · b  mod Φ₉  (schoolbook in Y over R_q, reduced by
 *  Y⁶ ≡ −Y³ − 1).  out may NOT alias a or b. */
int chipmunk_fq6_ext_mul(chipmunk_fq6_ext_t *a_out,
                           const chipmunk_fq6_ext_t *a,
                           const chipmunk_fq6_ext_t *b);
int chipmunk_fq6_ext_mul_q(chipmunk_fq6_ext_t *a_out,
                             const chipmunk_fq6_ext_t *a,
                             const chipmunk_fq6_ext_t *b,
                             uint64_t q);

/* ---- Galois / Frobenius (R_q ↔ R_q^{(e)} consistency, NOGAP §4) ---- */

/*  Apply the R_q-algebra automorphism  σ : Y ↦ Y²  (mod Φ₉).  Since 2 is
 *  a primitive root mod 9, σ generates the full order-6 Galois group of
 *  R_q^{(e)}/R_q (≅ Gal(F_{q⁶}/F_q) per slot).  Used by the consistency
 *  lane: an element lies in the base ring R_q iff it is σ-fixed
 *      w ∈ ι(R_q)  ⟺  σ(w) = w,
 *  giving a succinct linear opening (NOGAP_LEMMA §4.1).  out may alias a.
 *  Returns 0, or -EINVAL on null args. */
int chipmunk_fq6_ext_frobenius(chipmunk_fq6_ext_t *a_out,
                                 const chipmunk_fq6_ext_t *a);

/*  Field trace  Tr_{R_q^{(e)}/R_q}(w) = Σ_{i=0}^{e-1} σⁱ(w).  The result
 *  always lies in the base ring R_q (it is σ-fixed); the R_q value is
 *  written to a_out.  For a base element ι(a) it equals e·a (NOGAP §4.1).
 *  Returns 0, or -EINVAL on null args. */
int chipmunk_fq6_ext_trace(chipmunk_poly_t *a_out,
                             const chipmunk_fq6_ext_t *a);

/* ---- scalar (F_{q⁶}) sub-API — challenges live here ---------------- */

/*  A scalar is given by its six F_q coordinates (a_0,…,a_5) representing
 *  Σ a_j Yʲ ∈ F_{q⁶}.  Materialise it as an ext element whose six
 *  Y-coefficients are the corresponding CONSTANT R_q polynomials. */

/** @brief Per-q variant: normalize coords into [0, q) before storing. */
void chipmunk_fq6_ext_scalar_set_q(chipmunk_fq6_ext_t *a_out,
                                     const int32_t a_coords[CHIPMUNK_FQ6_EXT_DEG],
                                     uint64_t q);

/*  Extract the six F_q coordinates from a scalar ext element (reads the
 *  degree-0 X-coefficient of each Y-component).  Returns -EINVAL if the
 *  element is not scalar (some Y-component is a non-constant R_q poly). */

/** @brief Per-q variant: normalize coords into [0, q). */
int chipmunk_fq6_ext_scalar_get_q(int32_t a_coords_out[CHIPMUNK_FQ6_EXT_DEG],
                                    const chipmunk_fq6_ext_t *a, uint64_t q);

/*  Invert a scalar (F_{q⁶}) element via extended Euclid in F_q[Y]/(Φ₉).
 *  Returns 0 on success (out is the scalar inverse), -EDOM if the scalar
 *  is zero / non-invertible, -EINVAL if a is not scalar. */

/** @brief Per-q variant of scalar inversion. */
int chipmunk_fq6_ext_scalar_invert_q(chipmunk_fq6_ext_t *a_out,
                                       const chipmunk_fq6_ext_t *a, uint64_t q);

/* ---- Fiat-Shamir challenge sampler over the subtractive set ------- */

/*  Deterministically sample a fold challenge from the subtractive set
 *
 *      S = F_{q⁶} \ {0}   (embedded diagonally as a scalar ext element).
 *
 *  Because F_{q⁶} is a field, every difference of two distinct elements
 *  of S is a nonzero field element and hence invertible in R_q^{(e)};
 *  thus S is subtractive with |S| = qᵉ − 1 ≈ 2¹²⁹·⁶, giving single-shot
 *  ≈128-bit fold soundness (G3.1 §2).
 *
 *  The challenge is a pure function of (a_fs_hash, a_counter) — the
 *  verifier re-derives it identically.  The all-zero element is
 *  rejection-resampled, so the output is always nonzero (invertible).
 *  Returns 0 on success, -EINVAL on null args. */

/** @brief Per-q variant: rejection-sample F_q coordinates in [0, q).
 *  Required for q != CHIPMUNK_Q so challenges live in the correct field. */
int chipmunk_fq6_ext_sample_challenge_q(chipmunk_fq6_ext_t *a_out,
                                          const uint8_t a_fs_hash[32],
                                          uint32_t a_counter, uint64_t q);

/* ---- general inversion (per-slot F_{q⁶}) — completeness/tests ------ */

/*  Invert a GENERAL element of R_q^{(e)} ≅ (F_{q⁶})⁵¹² by inverting each
 *  of the 512 NTT slots in F_{q⁶}.  Returns 0 on success, -EDOM if any
 *  slot is non-invertible.  Not used on the fold hot path (challenges
 *  are scalar); provided to validate the algebra and for the
 *  R_q↔R_q^{(e)} consistency tooling. */

/** @brief Per-q variant of chipmunk_fq6_ext_invert (Phase 9.14).
 *  Requires a per-q NTT context (no fallback to global NTT). */
int chipmunk_fq6_ext_invert_q(chipmunk_fq6_ext_t *a_out,
                                const chipmunk_fq6_ext_t *a,
                                uint64_t q,
                                const chipmunk_ntt_ctx_t *ntt_ctx);

/* ---- irreducibility self-check (Rabin) for CI --------------------- */

/** @brief Per-q variant of the Rabin irreducibility test for Φ₉ over F_q.
 *  Required for q != CHIPMUNK_Q — verifies Φ₉ is irreducible so R_q^{(e)}
 *  is actually a field and the subtractive-set soundness holds. */
bool chipmunk_fq6_ext_modulus_is_irreducible_q(uint64_t q);

#ifdef __cplusplus
}
#endif

#endif /* _CHIPMUNK_FQ6_EXT_H_ */
