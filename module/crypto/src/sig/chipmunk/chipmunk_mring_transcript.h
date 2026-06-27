/*
 * CR-11.G Phase 7.7 — MRNG Fiat-Shamir transcript (G4 / G2 v2 §7).
 *
 * Byte-exact SHA3-256 domain-separated hash chain for sign/verify glue.
 * Internal API only; wire layout in README_MRNG.md §5.
 */

#pragma once
#ifndef _CHIPMUNK_MRING_TRANSCRIPT_H_
#define _CHIPMUNK_MRING_TRANSCRIPT_H_

#include <stddef.h>
#include <stdint.h>

#include "chipmunk_lrs.h"
#include "chipmunk_mring_fold.h"
#include "chipmunk_mring_params.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Canonicalise ring members: validate CLPKs, lex-sort by qpacked P bytes,
 * reject duplicates (-EEXIST).  Output order is canonical for hashing.
 */
int chipmunk_mring_canonicalise_ring(chipmunk_lrs_public_key_t *a_sorted_out,
                                     uint32_t a_n_ring,
                                     const chipmunk_lrs_public_key_t *a_ring);

/* Fixed header hashes (32 B each). */
int chipmunk_mring_hash_ring(uint8_t a_out[CHIPMUNK_MRING_HASH_BYTES],
                             const chipmunk_lrs_public_key_t *a_ring,
                             uint32_t a_n_ring);

/* Same as hash_ring but assumes a_ring is already canonical-sorted. */
int chipmunk_mring_hash_sorted_ring(uint8_t a_out[CHIPMUNK_MRING_HASH_BYTES],
                                    const chipmunk_lrs_public_key_t *a_sorted,
                                    uint32_t a_n_ring);

int chipmunk_mring_hash_ctx(uint8_t a_out[CHIPMUNK_MRING_HASH_BYTES],
                            uint32_t a_params_id,
                            const void *a_ctx, size_t a_ctx_len);

int chipmunk_mring_hash_msg(uint8_t a_out[CHIPMUNK_MRING_HASH_BYTES],
                            uint32_t a_params_id,
                            const uint8_t *a_msg, size_t a_msg_len);

/*
 * fs_seed = SHA3-256("chipmunk-mring-fs-v1" ‖ ring_hash ‖ ctx_hash ‖
 *                     msg_hash ‖ qpack(T) ‖ qpack(C_b)).
 */
int chipmunk_mring_fs_seed(uint8_t a_out[CHIPMUNK_MRING_HASH_BYTES],
                           const uint8_t a_ring_hash[CHIPMUNK_MRING_HASH_BYTES],
                           const uint8_t a_ctx_hash[CHIPMUNK_MRING_HASH_BYTES],
                           const uint8_t a_msg_hash[CHIPMUNK_MRING_HASH_BYTES],
                           const uint8_t
                               a_T_qpack[CHIPMUNK_MRING_POLY_QPACK],
                           const uint8_t
                               a_Cb_qpack[CHIPMUNK_MRING_POLY_QPACK]);

/* Unified statement challenge c (sparse ternary, G2 v2 §A1.1). */
int chipmunk_mring_transcript_sample_c(
    chipmunk_poly_t *a_c_out,
    const uint8_t a_fs_seed[CHIPMUNK_MRING_HASH_BYTES]);

/*
 * Per-round fold FS anchor (G3.1 / M4): SHAKE256 domain
 * "MRNG-M4-fold-round-fs-v1" ‖ fs_seed ‖ round ‖ qpack(CL) ‖ qpack(CR).
 */
int chipmunk_mring_transcript_fold_round_fs(
    uint8_t a_out[CHIPMUNK_MRING_HASH_BYTES],
    const uint8_t a_fs_seed[CHIPMUNK_MRING_HASH_BYTES],
    uint32_t a_round,
    const chipmunk_mring_ext_t *a_CL,
    const chipmunk_mring_ext_t *a_CR);

/*
 * Bind-block FS digest absorbing fold commitments + scalars (on-wire b*).
 * c* = sparse_ternary(SHA3-256("chipmunk-mring-bind-fs-v1" ‖ …)).
 */
int chipmunk_mring_transcript_bind_fs(
    uint8_t a_out[CHIPMUNK_MRING_HASH_BYTES],
    const uint8_t a_fs_seed[CHIPMUNK_MRING_HASH_BYTES],
    const chipmunk_poly_t *a_c,
    const chipmunk_poly_t *a_M_pk,
    const chipmunk_poly_t *a_M_T,
    const chipmunk_mring_fold_proof_t *a_proof,
    uint32_t a_fold_depth);

int chipmunk_mring_transcript_sample_c_star(
    chipmunk_poly_t *a_c_star_out,
    const uint8_t a_bind_fs[CHIPMUNK_MRING_HASH_BYTES]);

#ifdef __cplusplus
}
#endif

#endif /* _CHIPMUNK_MRING_TRANSCRIPT_H_ */
