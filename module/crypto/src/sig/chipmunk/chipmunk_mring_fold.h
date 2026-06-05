/*
 * CR-11.G Phase 7.7 — MRNG halving fold over R_q^{(e)} (M4 / G3.1 §4).
 *
 * Internal API.  Implements in-memory prove/verify of the log-N fold
 * compressing ⟨b̃, P̃(c)⟩ = ρ(c) to a base scalar check.  Wire packing
 * in M4.1; seed-compressed VCom openings in M4.2 — see MRNG_M4_FOLD.md.
 */

#pragma once
#ifndef _CHIPMUNK_MRING_FOLD_H_
#define _CHIPMUNK_MRING_FOLD_H_

#include <stdint.h>

#include "chipmunk.h"
#include "chipmunk_mring_ext.h"
#include "chipmunk_mring_params.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct chipmunk_mring_fold_round {
    chipmunk_mring_ext_t C_L; /* VCom commitment to cross-term L_r */
    chipmunk_mring_ext_t C_R; /* VCom commitment to cross-term R_r */
} chipmunk_mring_fold_round_t;

typedef struct chipmunk_mring_fold_proof {
    uint32_t fold_depth;
    chipmunk_mring_fold_round_t *rounds; /* [fold_depth], heap */
    chipmunk_mring_ext_t a_star;           /* final P̃ scalar */
    chipmunk_mring_ext_t b_star;           /* final b̃ scalar (β = b̃+ω on wire) */
    int64_t *leaf_mask;                    /* ω ∈ R_q, [N] heap (M4.3) */
    uint8_t fold_opening_seed[CHIPMUNK_MRING_FOLD_OPENING_BYTES];
} chipmunk_mring_fold_proof_t;

/*  padded_dim = 2 · 2^{⌈log₂ N⌉}  (zero-pad augmented witness/public vec). */
uint32_t chipmunk_mring_fold_padded_dim(uint32_t a_n_ring);

/*  WC^fold_depth — leaf-mask sampling bound (G3 §1.3). */
int64_t chipmunk_mring_leaf_bound_for_depth(uint32_t a_fold_depth);

/*
 *  Sample ω ∈ R_q uniformly in [-a_bound, +a_bound] (int64 lift).
 *  a_leaf_mask_seed is domain-separated (MRNG-M4-leaf-mask-v1).
 */
int chipmunk_mring_leaf_mask_sample(int64_t a_out[CHIPMUNK_MRING_N],
                                    const uint8_t
                                        a_leaf_mask_seed[CHIPMUNK_MRING_HASH_BYTES],
                                    int64_t a_bound);

/*  49-bit biased pack/unpack; a_pack_bound is typically LEAF_BOUND_MAX. */
int chipmunk_mring_leaf_mask_pack(uint8_t *a_out, size_t a_out_size,
                                  const int64_t a_coeffs[CHIPMUNK_MRING_N],
                                  int64_t a_pack_bound);

int chipmunk_mring_leaf_mask_unpack(int64_t a_out[CHIPMUNK_MRING_N],
                                    const uint8_t *a_in, size_t a_in_size,
                                    int64_t a_pack_bound);

int  chipmunk_mring_fold_proof_alloc(chipmunk_mring_fold_proof_t *a_proof,
                                     uint32_t a_fold_depth);
void chipmunk_mring_fold_proof_free(chipmunk_mring_fold_proof_t *a_proof);

/*
 *  Derive one K_pk opening vector for fold round (r, side, y_deg) from the
 *  32-byte aggregated seed (G3 §6.1 C1).  side: 0 = L, 1 = R.
 */
int chipmunk_mring_fold_derive_opening(
    chipmunk_poly_t a_r_out[CHIPMUNK_MRING_K_PK],
    const uint8_t a_fold_opening_seed[CHIPMUNK_MRING_FOLD_OPENING_BYTES],
    uint32_t a_round, uint32_t a_side, uint32_t a_y_deg);

/*
 *  Prover: materialise b̃, P̃(c), ρ(c), run D halving rounds with
 *  seed-compressed VCom commitments.  fs_seed anchors per-round FS hashes
 *  over (C_L, C_R); fold_opening_seed derives all opening randomness.
 */
int chipmunk_mring_fold_prove(chipmunk_mring_fold_proof_t *a_proof,
                              const uint8_t *a_b_indicator,
                              uint32_t a_n_ring,
                              const chipmunk_poly_t *a_pks,
                              const chipmunk_poly_t *a_c,
                              uint32_t a_t,
                              const chipmunk_poly_t *a_Y_pk,
                              const uint8_t a_ring_hash[CHIPMUNK_MRING_HASH_BYTES],
                              const uint8_t a_fs_seed[CHIPMUNK_MRING_HASH_BYTES],
                              const uint8_t a_fold_opening_seed
                                  [CHIPMUNK_MRING_FOLD_OPENING_BYTES]);

/*
 *  Verifier: open C_L/C_R via seed-derived openings, re-derive challenges,
 *  recompute public P̃ side, check INV at base.
 */
int chipmunk_mring_fold_verify(const chipmunk_mring_fold_proof_t *a_proof,
                               uint32_t a_n_ring,
                               const chipmunk_poly_t *a_pks,
                               const chipmunk_poly_t *a_c,
                               uint32_t a_t,
                               const chipmunk_poly_t *a_Y_pk,
                               const uint8_t a_ring_hash[CHIPMUNK_MRING_HASH_BYTES],
                               const uint8_t a_fs_seed[CHIPMUNK_MRING_HASH_BYTES]);

/* ------------------------------------------------------------------------ *
 *  M4.1 wire pack/unpack (G3.1 §8 / M4.2 VCom commitments / M4.3 leaf-mask).
 *
 *  fold_opening_seed at chipmunk_mring_section_off_fold_opening_seed().
 *  Per round r: C_L ‖ C_R (each EXT_QPACK_BYTES).
 *  Final scalars: a* ‖ b* at chipmunk_mring_section_off_final().
 *  Leaf mask ω at chipmunk_mring_section_off_leaf_mask().
 * ------------------------------------------------------------------------ */

int chipmunk_mring_ext_qpack(uint8_t *a_out, size_t a_out_size,
                             const chipmunk_mring_ext_t *a_x);

int chipmunk_mring_ext_qunpack(chipmunk_mring_ext_t *a_out,
                               const uint8_t *a_in, size_t a_in_size);

int chipmunk_mring_fold_write(uint8_t *a_buf, size_t a_buf_size,
                              uint32_t a_fold_depth,
                              const chipmunk_mring_fold_proof_t *a_proof);

int chipmunk_mring_fold_read(chipmunk_mring_fold_proof_t *a_proof,
                             uint32_t a_fold_depth,
                             const uint8_t *a_buf, size_t a_buf_size);

#ifdef __cplusplus
}
#endif

#endif /* _CHIPMUNK_MRING_FOLD_H_ */
