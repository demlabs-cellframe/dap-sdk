/*
 * Chipmunk MRNG — parameter profile MRV1 (M1 pinning).
 *
 * CR-11.G Phase 7.7 / task_ac273cea.  Per amendment v2
 * (documentation_0539a9f3f1b8ae5d) MRNG reuses the Chipmunk substrate
 * (q = 3 168 257, n = 512, sparse-ternary challenges with weight 37,
 * 22-bit qpack, 20-bit zpack).  This header pins only the MRV1
 * profile-level constants; per-component hardness arguments live in
 * chipmunk_mring_hardness.c (M2 / G1 gate).
 */

#pragma once
#ifndef _CHIPMUNK_MRING_PARAMS_H_
#define _CHIPMUNK_MRING_PARAMS_H_

#include <stdint.h>

#include "chipmunk.h"
#include "chipmunk_lrs.h"

/* -------------------------------------------------------------------------
 * Protocol identity.
 * ---------------------------------------------------------------------- */

#define CHIPMUNK_MRING_MAGIC       0x474E524Du  /* 'MRNG' little-endian */
#define CHIPMUNK_MRING_VERSION     1u
#define CHIPMUNK_MRING_PARAMS_ID   0x31565252u  /* 'MRV1' little-endian */

/* -------------------------------------------------------------------------
 * Algebraic substrate (inherited from Chipmunk).
 * ---------------------------------------------------------------------- */

#define CHIPMUNK_MRING_N           CHIPMUNK_N                       /* 512 */
#define CHIPMUNK_MRING_Q           CHIPMUNK_Q                       /* 3 168 257 */
#define CHIPMUNK_MRING_Q_BITS      CHIPMUNK_LRS_Q_BITS              /* 22 */
#define CHIPMUNK_MRING_Z_BITS      CHIPMUNK_LRS_Z_BITS              /* 20 */
#define CHIPMUNK_MRING_K_PK        CHIPMUNK_LRS_K                   /* 6 (aggregated-pk dimension) */
#define CHIPMUNK_MRING_K_T         CHIPMUNK_LRS_K                   /* 6 (link-tag dimension) */
#define CHIPMUNK_MRING_WC          CHIPMUNK_LRS_CHALLENGE_WEIGHT    /* 37 (sparse ternary weight) */
#define CHIPMUNK_MRING_BETA_W      CHIPMUNK_LRS_WITNESS_BOUND       /* 13 (||x||∞ bound) */

/*
 * Bind-block bounds inherited from the Chipmunk LRS substrate (G2 v2.1 §4).
 *
 *   BETA          = WC · BETA_W   = 481      (worst-case |c·X| coefficient)
 *   RESPONSE_BOUND = 2^19         = 524 288  (half-open zpack range)
 *   MASK_BOUND    = RESPONSE_BOUND + BETA = 524 769
 *
 * Mask sampling is uniform in [-MASK_BOUND, +MASK_BOUND]; the abort
 * rejects whenever any coefficient of z_x = ρ_x + c·X falls outside
 * [-RESPONSE_BOUND, RESPONSE_BOUND-1].  These are EXACTLY the LRS
 * constants — we reuse them so the Lyubashevsky bounded-uniform abort
 * proof from LRS transfers verbatim.
 */
#define CHIPMUNK_MRING_BETA            ((int32_t)CHIPMUNK_LRS_BETA)
#define CHIPMUNK_MRING_RESPONSE_BOUND  ((int32_t)CHIPMUNK_LRS_RESPONSE_BOUND)
#define CHIPMUNK_MRING_MASK_BOUND      ((int32_t)CHIPMUNK_LRS_MASK_BOUND)
#define CHIPMUNK_MRING_MAX_ATTEMPTS    CHIPMUNK_LRS_MAX_ATTEMPTS    /* 2048 */

#define CHIPMUNK_MRING_POLY_QPACK  CHIPMUNK_LRS_POLY_QPACK_BYTES    /* 1408 */
#define CHIPMUNK_MRING_POLY_ZPACK  CHIPMUNK_LRS_POLY_ZPACK_BYTES    /* 1280 */

/* -------------------------------------------------------------------------
 * Ring / threshold envelope (matches public chipmunk_ring.h).
 * ---------------------------------------------------------------------- */

#define CHIPMUNK_MRING_N_MIN       2u
#define CHIPMUNK_MRING_N_MAX       256u
#define CHIPMUNK_MRING_T_MIN       1u
#define CHIPMUNK_MRING_T_MAX       CHIPMUNK_MRING_N_MAX

/*
 * Fold depth — G2 v2 §A1.1.  The fold operates on an augmented vector
 * of length 2N (binary check appended as the second half), giving
 *   fold_depth = 1 + ceil(log2 N)
 * For N_MAX = 256: fold_depth_max = 1 + 8 = 9.
 */
#define CHIPMUNK_MRING_FOLD_DEPTH_MAX 9u

/* G3 §7 / M4 — Fiat-Shamir and seed-compression pins (wire in M4.2+). */
#define CHIPMUNK_MRING_FOLD_SEED_BYTES   32u
#define CHIPMUNK_MRING_FOLD_OPENING_BYTES CHIPMUNK_MRING_FOLD_SEED_BYTES
#define CHIPMUNK_MRING_FS_OUT_BITS       384u

/*
 * G3 §6.1 C2 / M4.3 — final-round leaf-mask ω ∈ R_q (MatRiCT+ Lemma 4.2).
 * Wire: 49-bit biased encoding per coefficient, bound WC^{D_MAX} = 37^9.
 */
#define CHIPMUNK_MRING_LEAF_BITS         49u
#define CHIPMUNK_MRING_LEAF_MASK_BYTES \
        ((uint32_t)(CHIPMUNK_MRING_N * CHIPMUNK_MRING_LEAF_BITS / 8u))
#define CHIPMUNK_MRING_LEAF_BOUND_MAX    ((int64_t)129961749712937LL) /* 37^9 */

/* Ring extension degree e = deg Φ₉ (G3.1 §3); kept here to avoid params↔ext cycle. */
#define CHIPMUNK_MRING_EXT_DEG           6

/*
 * G3.1 §8 — one R_q^{(e)} element serialises as e R_q polys (qpacked).
 */
#define CHIPMUNK_MRING_EXT_QPACK_BYTES \
        (CHIPMUNK_MRING_EXT_DEG * (uint32_t)CHIPMUNK_MRING_POLY_QPACK)

/* -------------------------------------------------------------------------
 * Fixed-size wire constants.
 * ---------------------------------------------------------------------- */

#define CHIPMUNK_MRING_HASH_BYTES        32u   /* ring_hash / ctx_hash / msg_hash / fs_seed */
#define CHIPMUNK_MRING_HEADER_BYTES      28u   /* see chipmunk_mring.h:chipmunk_mring_header_t */
#define CHIPMUNK_MRING_FIXED_HASH_BYTES  (4u * CHIPMUNK_MRING_HASH_BYTES) /* 128 */

/*
 * G2 v2.1 wire-size correction (documentation_fc6bb792148137c5).
 *
 * In chipmunk_lrs the matrices A_pk and A_T are themselves K_pk-vectors
 * of R_q elements (see chipmunk_lrs_derive_A_pk / _derive_A_I) and
 * chipmunk_lrs_relation_eval contracts them with a K_pk-vector witness
 * to produce a SINGLE R_q polynomial.  Therefore:
 *   T     = A_T  · X   ∈ R_q           (1 qpack = 1 408 B, not K_T·qpack)
 *   Y_pk  = A_pk · X   ∈ R_q           (1 qpack = 1 408 B, not K_PK·qpack)
 *   z_x   = ρ_x + c*·X  ∈ R_q^{K_pk}   (K_pk zpacks = 7 680 B, not 1 zpack)
 */

/* T block: 1 qpacked poly (link tag, G2 v2.1 §1, §4). */
#define CHIPMUNK_MRING_T_BYTES           ((uint32_t)CHIPMUNK_MRING_POLY_QPACK)

/* C_b block: 1 qpacked poly (vector commitment). */
#define CHIPMUNK_MRING_CB_BYTES          ((uint32_t)CHIPMUNK_MRING_POLY_QPACK)

/*
 * Single fold round: (L_r, R_r) ∈ (R_q^{(e)})² — G3.1 §8.
 * M4.1 will wire-pack; M4.0 uses the same byte budget for layout pins.
 */
#define CHIPMUNK_MRING_FOLD_ROUND_BYTES  (2u * CHIPMUNK_MRING_EXT_QPACK_BYTES)

/* Final folded scalars (a*, b*) — two R_q^{(e)} elements. */
#define CHIPMUNK_MRING_FINAL_SCALARS_BYTES (2u * CHIPMUNK_MRING_EXT_QPACK_BYTES)

/* Y_pk block: aggregated-pk claim (1 qpacked poly, G2 v2.1 §1, §4). */
#define CHIPMUNK_MRING_YPK_BYTES         ((uint32_t)CHIPMUNK_MRING_POLY_QPACK)

/* Bind block (G2 v2.1 §4): z_x (K_pk zpacks) + c* (qpack) for verify glue. */
#define CHIPMUNK_MRING_BIND_BYTES \
        ((uint32_t)CHIPMUNK_MRING_K_PK * (uint32_t)CHIPMUNK_MRING_POLY_ZPACK \
         + (uint32_t)CHIPMUNK_MRING_POLY_QPACK)

/* -------------------------------------------------------------------------
 * Static asserts: catch accidental drift of the substrate.
 * ---------------------------------------------------------------------- */

_Static_assert(CHIPMUNK_MRING_POLY_QPACK == 1408u,
               "MRNG: qpacked Chipmunk poly must be 1408 bytes");
_Static_assert(CHIPMUNK_MRING_POLY_ZPACK == 1280u,
               "MRNG: zpacked Chipmunk poly must be 1280 bytes");
_Static_assert(CHIPMUNK_MRING_K_PK == 6u && CHIPMUNK_MRING_K_T == 6u,
               "MRNG: K_PK and K_T are fixed at 6 to match chipmunk_lrs_K");
_Static_assert(CHIPMUNK_MRING_EXT_QPACK_BYTES ==
               CHIPMUNK_MRING_EXT_DEG * CHIPMUNK_MRING_POLY_QPACK,
               "MRNG: ext qpack = e R_q polys");
_Static_assert(CHIPMUNK_MRING_FOLD_ROUND_BYTES ==
               2u * CHIPMUNK_MRING_EXT_QPACK_BYTES,
               "MRNG: fold round = (L,R) ext elements");
_Static_assert(CHIPMUNK_MRING_LEAF_MASK_BYTES == 3136u,
               "MRNG: leaf-mask = 49 bits × 512 coeffs");
/* G2 v2 §A2: ensure REL-2 (\sum b_i = t) lifts losslessly Z -> R_q. */
_Static_assert((uint32_t)CHIPMUNK_MRING_N_MAX < (uint32_t)CHIPMUNK_MRING_Q,
               "MRNG: N_MAX must be < q for the Z->R_q lift of REL-2 to be loss-free");

#endif /* _CHIPMUNK_MRING_PARAMS_H_ */
