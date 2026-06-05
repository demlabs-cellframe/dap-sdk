/*
 * CR-11.G Phase 7.7 — MRNG halving fold over R_q^{(e)} (M4 / G3.1 §4).
 *
 * Internal API.  Implements in-memory prove/verify of the log-N fold
 * compressing ⟨b̃, P̃(c)⟩ = ρ(c) to a base scalar check.  Wire packing
 * lands in M4.1; see MRNG_M4_FOLD.md.
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
    chipmunk_mring_ext_t L;
    chipmunk_mring_ext_t R;
} chipmunk_mring_fold_round_t;

typedef struct chipmunk_mring_fold_proof {
    uint32_t fold_depth;
    chipmunk_mring_fold_round_t *rounds; /* [fold_depth], heap */
    chipmunk_mring_ext_t a_star;           /* final P̃ scalar */
    chipmunk_mring_ext_t b_star;           /* final b̃ scalar */
} chipmunk_mring_fold_proof_t;

/*  padded_dim = 2 · 2^{⌈log₂ N⌉}  (zero-pad augmented witness/public vec). */
uint32_t chipmunk_mring_fold_padded_dim(uint32_t a_n_ring);

int  chipmunk_mring_fold_proof_alloc(chipmunk_mring_fold_proof_t *a_proof,
                                     uint32_t a_fold_depth);
void chipmunk_mring_fold_proof_free(chipmunk_mring_fold_proof_t *a_proof);

/*
 *  Prover: materialise b̃, P̃(c), ρ(c) from secret b and public ring data,
 *  run D halving rounds, fill a_proof.  fs_seed anchors per-round FS hashes.
 *
 *  Returns 0 on success, negative errno on failure.
 */
int chipmunk_mring_fold_prove(chipmunk_mring_fold_proof_t *a_proof,
                              const uint8_t *a_b_indicator,
                              uint32_t a_n_ring,
                              const chipmunk_poly_t *a_pks,
                              const chipmunk_poly_t *a_c,
                              uint32_t a_t,
                              const chipmunk_poly_t *a_Y_pk,
                              const uint8_t a_fs_seed[CHIPMUNK_MRING_HASH_BYTES]);

/*
 *  Verifier: re-derive challenges, recompute public P̃ side, check INV at
 *  base and Galois consistency σ(b*) = b*.
 */
int chipmunk_mring_fold_verify(const chipmunk_mring_fold_proof_t *a_proof,
                               uint32_t a_n_ring,
                               const chipmunk_poly_t *a_pks,
                               const chipmunk_poly_t *a_c,
                               uint32_t a_t,
                               const chipmunk_poly_t *a_Y_pk,
                               const uint8_t a_fs_seed[CHIPMUNK_MRING_HASH_BYTES]);

/* ------------------------------------------------------------------------ *
 *  M4.1 wire pack/unpack (G3.1 §8).
 *
 *  One R_q^{(e)} element = e consecutive qpacks (Y^0 .. Y^{e-1}).
 *  Fold section layout (relative to chipmunk_mring_section_off_fold()):
 *    round r: L_r ‖ R_r   (each EXT_QPACK_BYTES)
 *  Final scalars (chipmunk_mring_section_off_final()):
 *    a* ‖ b*              (each EXT_QPACK_BYTES)
 * ------------------------------------------------------------------------ */

int chipmunk_mring_ext_qpack(uint8_t *a_out, size_t a_out_size,
                             const chipmunk_mring_ext_t *a_x);

int chipmunk_mring_ext_qunpack(chipmunk_mring_ext_t *a_out,
                               const uint8_t *a_in, size_t a_in_size);

/*
 *  Serialise a_proof into the fold + final-scalar wire slots of a_buf.
 *  a_buf must be at least chipmunk_mring_wire_size(a_fold_depth).
 */
int chipmunk_mring_fold_write(uint8_t *a_buf, size_t a_buf_size,
                              uint32_t a_fold_depth,
                              const chipmunk_mring_fold_proof_t *a_proof);

/*
 *  Parse fold + final-scalar slots into a_proof (caller must alloc proof
 *  with matching fold_depth first).
 */
int chipmunk_mring_fold_read(chipmunk_mring_fold_proof_t *a_proof,
                             uint32_t a_fold_depth,
                             const uint8_t *a_buf, size_t a_buf_size);

#ifdef __cplusplus
}
#endif

#endif /* _CHIPMUNK_MRING_FOLD_H_ */
