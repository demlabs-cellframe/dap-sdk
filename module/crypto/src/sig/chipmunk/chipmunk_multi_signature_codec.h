/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net/cellframe
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * This file is part of DAP (Distributed Applications Platform).
 * DAP SDK is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "chipmunk_aggregation.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------- *
 *  CR-D10 — Schema-driven wire codec for chipmunk_multi_signature_t      *
 *  ------------------------------------------------------------------    *
 *  The historical aggregate blob handed out by dap_sign was a raw        *
 *  `memcpy(&multi_sig, …, sizeof(chipmunk_multi_signature_t))`: it       *
 *  serialised heap-owned pointers (proofs[i].nodes, public_key_roots,    *
 *  hots_pks, rho_seeds, leaf_indices) whose address-space meaning        *
 *  was lost as soon as the blob crossed a process boundary.  Reading     *
 *  such a blob back was textbook undefined behaviour and effectively     *
 *  let a remote attacker substitute any byte pattern for a valid        *
 *  aggregate.                                                            *
 *                                                                        *
 *  This codec replaces that path with a canonical, versioned,            *
 *  self-describing binary representation:                                *
 *                                                                        *
 *    Header (24 bytes)                                                   *
 *      [0..3]   magic       = "CHMA"           (4 bytes, ASCII)          *
 *      [4..5]   version     = 1                (u16 LE)                  *
 *      [6..7]   reserved    = 0                (u16 LE, must be zero)    *
 *      [8..11]  signer_count                   (u32 LE)                  *
 *      [12..15] path_length                    (u32 LE, common across    *
 *                                                all signers)            *
 *      [16..23] payload_length                 (u64 LE, full blob size)  *
 *                                                                        *
 *    Body                                                                *
 *      32B  message_hash                                                 *
 *      32B  hvc_hasher_seed                                              *
 *      1B   aggregated_hots.is_randomized      (0 or 1)                  *
 *      3B   reserved (0)                                                 *
 *      N*4 B       tree_root.coeffs            (int32 LE each)           *
 *      GAMMA*N*4 B aggregated_hots.sigma[GAMMA]                          *
 *                                                                        *
 *    Per signer i in [0, signer_count):                                  *
 *      N*4 B   public_key_roots[i].coeffs                                *
 *      N*4 B   hots_pks[i].v0.coeffs                                     *
 *      N*4 B   hots_pks[i].v1.coeffs                                     *
 *      32 B    rho_seeds[i]                                              *
 *      4 B     leaf_indices[i]                 (u32 LE)                  *
 *      8 B     proofs[i].index                 (u64 LE)                  *
 *      4 B     proofs[i].path_length           (u32 LE, ==header)        *
 *      path_length * (2 * N * 4) B  proofs[i].nodes[*].{left,right}      *
 *                                                                        *
 *  All int32 coefficients ride their native two's-complement encoding    *
 *  packed LSB-first; no modular lift is performed here, so the codec     *
 *  is a pure transport — it does not re-interpret or normalise           *
 *  coefficient values.  Verification (range, reduction mod q) remains    *
 *  the responsibility of chipmunk_verify_multi_signature().              *
 *                                                                        *
 *  Hard limits (enforced on deserialise):                                *
 *    signer_count ∈ [1, CHIPMUNK_MULTI_SIG_MAX_SIGNERS]                  *
 *    path_length  ∈ [1, CHIPMUNK_TREE_HEIGHT_MAX - 1]                    *
 *    reserved fields must be zero                                        *
 *    payload_length must match a_buffer_size (no trailing / missing      *
 *                                             bytes)                     *
 * ---------------------------------------------------------------------- */

#define CHIPMUNK_MULTI_SIG_MAGIC0          'C'
#define CHIPMUNK_MULTI_SIG_MAGIC1          'H'
#define CHIPMUNK_MULTI_SIG_MAGIC2          'M'
#define CHIPMUNK_MULTI_SIG_MAGIC3          'A'
#define CHIPMUNK_MULTI_SIG_VERSION_V1      0x0001
#define CHIPMUNK_MULTI_SIG_HEADER_SIZE     24U
#define CHIPMUNK_MULTI_SIG_MAX_SIGNERS     CHIPMUNK_TREE_MAX_PARTICIPANTS

/*
 * Error codes returned by the codec.  The caller should treat every
 * negative return as a fatal parse failure — no partial state is
 * consumed, no output is produced.
 */
typedef enum chipmunk_multi_sig_codec_error {
    CHIPMUNK_MULTI_SIG_CODEC_OK                    =  0,
    CHIPMUNK_MULTI_SIG_CODEC_ERR_NULL              = -1,
    CHIPMUNK_MULTI_SIG_CODEC_ERR_BUFFER_TOO_SMALL  = -2,
    CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_MAGIC         = -3,
    CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_VERSION       = -4,
    CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_RESERVED      = -5,
    CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_SIGNER_COUNT  = -6,
    CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_PATH_LENGTH   = -7,
    CHIPMUNK_MULTI_SIG_CODEC_ERR_SIZE_MISMATCH     = -8,
    CHIPMUNK_MULTI_SIG_CODEC_ERR_PATH_LENGTH_MISMATCH = -9,
    CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_FLAG          = -10,
    CHIPMUNK_MULTI_SIG_CODEC_ERR_ALLOC             = -11
} chipmunk_multi_sig_codec_error_t;

/**
 * @brief Compute the exact serialised size for a multi-signature.
 *
 * @param  a_multi_sig         Multi-signature to measure (read-only).
 * @param  a_out_size          OUT: required byte count (only written on
 *                             success).  May be NULL, in which case the
 *                             return code is still authoritative.
 * @return CHIPMUNK_MULTI_SIG_CODEC_OK or a negative error code.
 */
int chipmunk_multi_signature_serialized_size(
        const chipmunk_multi_signature_t *a_multi_sig,
        size_t *a_out_size);

/**
 * @brief Serialise a multi-signature into canonical wire bytes.
 *
 * Writes at most a_buffer_size bytes and updates a_bytes_written on
 * success.  All `proofs[i]` must share the same `path_length`; otherwise
 * the codec refuses the request (CHIPMUNK_MULTI_SIG_CODEC_ERR_PATH_LENGTH_MISMATCH).
 *
 * @return CHIPMUNK_MULTI_SIG_CODEC_OK or a negative error code.  On any
 *         non-zero return nothing meaningful is written to a_buffer.
 */
int chipmunk_multi_signature_serialize(
        const chipmunk_multi_signature_t *a_multi_sig,
        uint8_t *a_buffer,
        size_t a_buffer_size,
        size_t *a_bytes_written);

/**
 * @brief Deserialise wire bytes into a multi-signature.
 *
 * On success, every pointer field of `a_multi_sig` (public_key_roots,
 * hots_pks, rho_seeds, proofs, proofs[i].nodes, leaf_indices) is
 * freshly allocated and owned by the caller; release exclusively via
 * chipmunk_multi_signature_deep_free().  Passing the struct to
 * chipmunk_multi_signature_free() (non-deep) would leak the proof
 * node buffers.
 *
 * On any negative return, a_multi_sig is reset to an all-zero state
 * and no allocations remain, so the caller can safely deep-free it
 * unconditionally.
 */
int chipmunk_multi_signature_deserialize(
        const uint8_t *a_buffer,
        size_t a_buffer_size,
        chipmunk_multi_signature_t *a_multi_sig);

/**
 * @brief Deep-free a multi-signature owned end-to-end by the caller.
 *
 * Safe on NULL and on zero-initialised structs.  Frees the five parallel
 * arrays AND the `nodes` buffer embedded in each proof.  After the call
 * the struct is zeroed.
 *
 * Use this instead of chipmunk_multi_signature_free() whenever the
 * multi-signature originated from chipmunk_multi_signature_deserialize()
 * or any other path where the caller owns the proof node buffers.
 */
void chipmunk_multi_signature_deep_free(
        chipmunk_multi_signature_t *a_multi_sig);

#ifdef __cplusplus
}
#endif
