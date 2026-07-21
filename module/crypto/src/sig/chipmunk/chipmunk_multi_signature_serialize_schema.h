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

#include "dap_serialize.h"
#include "chipmunk.h"
#include "chipmunk_hots.h"
#include "chipmunk_tree.h"
#include "chipmunk_aggregation.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------- *
 *  CR-D10 / Round-2 — Schema-driven wire layer for multi-signatures      *
 *  ------------------------------------------------------------------    *
 *  This header defines wire-mirror structures and dap_serialize schemas  *
 *  whose byte-level output is *identical* to the original hand-rolled    *
 *  CHMA body produced by chipmunk_multi_signature_codec.c. The migration *
 *  intentionally avoided introducing a new wire version: the schema     *
 *  re-implements the existing layout, so deployed nodes never need to    *
 *  understand more than one format.                                      *
 *                                                                        *
 *  Wire body layout (post 24-byte CHMA header — same for encode/decode): *
 *      32B   message_hash                                                *
 *      32B   hvc_hasher_seed                                             *
 *       1B   aggregated_hots.is_randomized   (0/1)                       *
 *       3B   reserved                          (must be zero)            *
 *       N*4 B    tree_root.coeffs                (int32 LE)              *
 *       GAMMA*N*4 B aggregated_hots.sigma[GAMMA] (int32 LE)              *
 *                                                                        *
 *      Per signer i ∈ [0, signer_count) — array-of-structs, NO inner    *
 *      count prefix (the count is carried in the CHMA header):           *
 *          N*4 B   public_key_roots[i].coeffs                            *
 *          N*4 B   hots_pks[i].v0.coeffs                                 *
 *          N*4 B   hots_pks[i].v1.coeffs                                 *
 *          32 B    rho_seeds[i]                                          *
 *           4 B    leaf_indices[i]               (uint32 LE)             *
 *           8 B    proofs[i].index               (uint64 LE)             *
 *           4 B    proofs[i].path_length         (uint32 LE)             *
 *           path_length * (2 * N * 4) B  proofs[i].nodes[*].{left,right} *
 *                                                                        *
 *  Two runtime structures (chipmunk_path_t and chipmunk_multi_signature  *
 *  _t) embed `size_t` count fields that the schema engine cannot encode  *
 *  directly (uint32_t length prefixes only).  The wire mirrors below     *
 *  demote those fields to uint32_t and reorder the fields when needed,   *
 *  but every leaf byte still maps 1:1 onto the canonical CHMA layout.    *
 * ---------------------------------------------------------------------- */

/**
 * @brief Per-signer wire record — one element of the top-level signers[] array.
 *
 * The on-wire layout matches the canonical CHMA per-signer block byte-for-byte;
 * the schema ordering MUST therefore stay {pk_root, hots_pk, rho_seed,
 * leaf_index, proof}.  `proof.nodes` retains its 4-byte length prefix
 * because the original codec also wrote `path_length` immediately before the
 * nodes block — so for the proof sub-record only `nodes` is a *normal*
 * ARRAY_DYNAMIC, not a NO_COUNT_PREFIX one.
 */
typedef struct chipmunk_signer_record_wire {
    chipmunk_hvc_poly_t pk_root;        ///< [N*4] public key root
    chipmunk_hots_pk_t  hots_pk;        ///< [2*N*4] HOTS public key (v0 + v1)
    uint8_t             rho_seed[32];   ///< [32] per-signer randomness seed
    uint32_t            leaf_index;     ///< [4] leaf index, demoted from uint32 alias
    uint64_t            index;          ///< [8] proof index (size_t in runtime)
    uint32_t            path_length;    ///< [4] proof path length (size_t in runtime)
    chipmunk_path_node_t *nodes;        ///< [path_length * 2*N*4] proof nodes
} chipmunk_signer_record_wire_t;

/**
 * @brief Top-level wire mirror of chipmunk_multi_signature_t.
 *
 * Layout matches the canonical CHMA body byte-for-byte: fixed prefix, then
 * an array-of-records with NO inner count prefix (signer_count comes
 * from the outer CHMA header set by the codec before deserialise).
 */
typedef struct chipmunk_multi_signature_wire {
    uint8_t             message_hash[32];            ///< [32]
    uint8_t             hvc_hasher_seed[32];         ///< [32]
    uint8_t             is_randomized;               ///< [1] 0/1
    uint8_t             reserved[3];                 ///< [3] zero-filled
    chipmunk_hvc_poly_t tree_root;                   ///< [N*4]
    chipmunk_poly_t     sigma[CHIPMUNK_GAMMA];       ///< [GAMMA*N*4]
    uint32_t            signer_count;                ///< Carried via header on wire
    chipmunk_signer_record_wire_t *signers;          ///< [signer_count] AoS records
} chipmunk_multi_signature_wire_t;

/* ---------------------------------------------------------------------- *
 *  Wire-format constants                                                 *
 * ---------------------------------------------------------------------- */

/* Custom magic for the schema (non-zero so dap_serialize accepts it). The
 * actual wire blob carries the codec's CHMA framing, not this value — the
 * magic is only meaningful when calling dap_serialize_calc_size() on the
 * top-level schema, which the codec never does (it always uses _raw). */
#define CHIPMUNK_MULTI_SIG_SCHEMA_MAGIC     0x43484D41u  /* "CHMA" packed BE */

/* ---------------------------------------------------------------------- *
 *  Public schemas (defined in the .c file)                               *
 * ---------------------------------------------------------------------- */

extern const dap_serialize_schema_t chipmunk_hvc_poly_schema;
extern const dap_serialize_schema_t chipmunk_poly_schema;
extern const dap_serialize_schema_t chipmunk_path_node_schema;
extern const dap_serialize_schema_t chipmunk_hots_pk_schema;
extern const dap_serialize_schema_t chipmunk_signer_record_wire_schema;
extern const dap_serialize_schema_t chipmunk_multi_signature_wire_schema;

/* ---------------------------------------------------------------------- *
 *  Wire ↔ runtime adapters                                               *
 * ---------------------------------------------------------------------- */

/**
 * @brief Build a wire mirror from a runtime multi-signature.
 *
 * Allocates a fresh signers[] array and shallow-copies every fixed
 * sub-structure (pk_root, hots_pk, rho_seed) into it.  The proof nodes
 * pointer is shared with the runtime owner (no deep copy of node data).
 * The caller MUST release the mirror via
 * chipmunk_multi_signature_wire_release().
 *
 * Returns 0 on success, negative on validation / allocation failure.
 */
int chipmunk_multi_signature_to_wire(
        const chipmunk_multi_signature_t *a_runtime,
        chipmunk_multi_signature_wire_t  *a_out_wire);

/**
 * @brief Free transient buffers attached to the wire mirror (encode side).
 *
 * Safe on NULL and on partially initialised mirrors.  Does NOT free the
 * proof node buffers (they still belong to the runtime owner).
 */
void chipmunk_multi_signature_wire_release(
        chipmunk_multi_signature_wire_t *a_wire);

/**
 * @brief Release wire struct INCLUDING serializer-owned proof nodes.
 *
 * Use on deserialization error paths where the serializer allocated
 * proof node buffers that were never transferred to the runtime struct.
 * Do NOT use on serialization paths where nodes are borrowed from runtime.
 */
void chipmunk_multi_signature_wire_release_with_nodes(
        chipmunk_multi_signature_wire_t *a_wire);

/**
 * @brief Take an owned wire mirror (post-deserialise) and lift it into
 *        the runtime struct.
 *
 * Pointer ownership is transferred from @p a_wire to @p a_out_runtime;
 * after a successful call the wire struct is zeroed and must not be
 * released via the helper above.  On failure the wire struct is left
 * untouched and the caller must release it (the codec then deep-frees
 * the partially populated runtime struct).
 *
 * Cross-signer invariants enforced here on top of the schema decode:
 *   - every proof.path_length must equal the common header value;
 *   - aggregated_hots.is_randomized must be 0 or 1;
 *   - reserved bytes must be zero.
 * Returns 0 on success, negative on validation failure.
 */
int chipmunk_multi_signature_from_wire(
        chipmunk_multi_signature_wire_t  *a_wire,
        chipmunk_multi_signature_t       *a_out_runtime);

#ifdef __cplusplus
}
#endif
