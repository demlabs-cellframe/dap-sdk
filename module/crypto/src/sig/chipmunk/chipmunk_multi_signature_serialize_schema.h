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
 *  This header turns the bespoke byte-level codec previously used for    *
 *  chipmunk_multi_signature_t into a set of dap_serialize schemas.       *
 *                                                                        *
 *  Two runtime structures (chipmunk_path_t and chipmunk_multi_signature  *
 *  _t) embed `size_t` count fields that the framework cannot serialise   *
 *  directly (the schema engine requires uint32_t length prefixes).  We   *
 *  therefore introduce thin "wire" mirrors that own the same data        *
 *  pointers but carry uint32_t counts.  Conversion is purely shallow —   *
 *  no array data is copied; only the per-signer path-length / index     *
 *  values are demoted from size_t to uint32_t.                          *
 *                                                                        *
 *  Wire format ("CHMA" v2)                                              *
 *  ------------------------                                             *
 *  Header (16 bytes, written manually by the codec):                     *
 *      [0..3]    magic         = "CHMA"                                  *
 *      [4..5]    version       = 2          (u16 LE)                     *
 *      [6..7]    reserved      = 0          (u16 LE, must be zero)       *
 *      [8..15]   payload_len   = sizeof(body)  (u64 LE)                  *
 *                                                                        *
 *  Body (laid out by chipmunk_multi_signature_wire_schema):              *
 *      32B       message_hash                                             *
 *      32B       hvc_hasher_seed                                          *
 *      NESTED    aggregated_hots                                          *
 *                  1B   is_randomized                                     *
 *                  3B   reserved (zero)                                   *
 *                  GAMMA poly[]   (each: N * INT32 LE)                    *
 *      NESTED    tree_root         (poly: N * INT32 LE)                   *
 *      ARRAY_DYN public_key_roots (4B count + count * poly)               *
 *      ARRAY_DYN hots_pks         (4B count + count * 2 * poly)           *
 *      ARRAY_DYN rho_seeds        (4B count + count * 32 raw bytes)       *
 *      ARRAY_DYN leaf_indices     (4B count + count * UINT32 LE)          *
 *      ARRAY_DYN proofs           (4B count + count * proof_wire)         *
 *                                                                        *
 *      proof_wire:                                                        *
 *          8B   index            (u64 LE — runtime size_t demoted)        *
 *          ARRAY_DYN nodes       (4B count + count * 2 * poly)            *
 *                                                                        *
 *  v1 blobs produced by the legacy codec are *not* schema-readable —     *
 *  the codec keeps a dedicated v1 path for backward compatibility.       *
 * ---------------------------------------------------------------------- */

/**
 * @brief Wire mirror of chipmunk_aggregated_hots_sig_t with a stable layout.
 *
 * The runtime type stores `sigma` first and `is_randomized` last (after a
 * compiler-chosen alignment slot).  The wire format demands the opposite
 * order plus three explicit reserved bytes; mirroring it in a dedicated
 * struct lets the schema declare offsets that match the wire byte-for-byte.
 */
typedef struct chipmunk_aggregated_hots_sig_wire {
    uint8_t         is_randomized;            ///< 0 or 1, validated by codec
    uint8_t         reserved[3];              ///< Must be zero on the wire
    chipmunk_poly_t sigma[CHIPMUNK_GAMMA];    ///< Aggregated signature polys
} chipmunk_aggregated_hots_sig_wire_t;

/**
 * @brief Wire mirror of chipmunk_path_t with uint32_t counts.
 *
 * `nodes` is shared with the runtime path (shallow copy on serialise,
 * fresh allocation on deserialise — the codec hands ownership over to
 * the deserialised chipmunk_multi_signature_t).
 */
typedef struct chipmunk_path_wire {
    chipmunk_path_node_t *nodes;              ///< Path nodes (shared on encode)
    uint32_t              path_length;        ///< == nodes element count
    uint64_t              index;              ///< Leaf index, demoted from size_t
} chipmunk_path_wire_t;

/**
 * @brief Wire mirror of chipmunk_multi_signature_t.
 *
 * Only the count fields and the proof array are reshaped; every other
 * pointer is copied by reference, so the wire mirror is cheap to build.
 */
typedef struct chipmunk_multi_signature_wire {
    uint8_t                              message_hash[32];
    uint8_t                              hvc_hasher_seed[32];
    chipmunk_aggregated_hots_sig_wire_t  aggregated_hots;
    chipmunk_hvc_poly_t                  tree_root;
    chipmunk_hvc_poly_t                 *public_key_roots;     ///< [signer_count]
    chipmunk_hots_pk_t                  *hots_pks;             ///< [signer_count]
    uint8_t                            (*rho_seeds)[32];        ///< [signer_count]
    uint32_t                            *leaf_indices;         ///< [signer_count]
    chipmunk_path_wire_t                *proofs;               ///< [signer_count]
    uint32_t                             signer_count;         ///< Top-level count
} chipmunk_multi_signature_wire_t;

/* ---------------------------------------------------------------------- *
 *  Wire-format constants                                                 *
 * ---------------------------------------------------------------------- */

/** Magic bytes "CHMA" at offset 0 of the wire blob. */
#define CHIPMUNK_MULTI_SIG_MAGIC_BYTES { 'C', 'H', 'M', 'A' }

/** Wire version emitted by the schema-driven writer. */
#define CHIPMUNK_MULTI_SIG_WIRE_VERSION_V2  2

/** Header size in bytes (4 magic + 2 version + 2 reserved + 8 payload_len). */
#define CHIPMUNK_MULTI_SIG_WIRE_HEADER_SIZE 16U

/* Custom magic for the schema (non-zero so dap_serialize accepts it as
 * a custom-magic schema even though the wire blob carries CHMA, not the
 * default DAP_SERIALIZE_MAGIC_NUMBER). */
#define CHIPMUNK_MULTI_SIG_SCHEMA_MAGIC     0x43484D41u  /* "CHMA" packed BE */

/* ---------------------------------------------------------------------- *
 *  Public schemas (defined in the .c file)                               *
 *                                                                        *
 *  Use the _raw variants of dap_serialize for top-level encoding/decoding *
 *  because the codec writes its own 16-byte CHMA header up-front.        *
 * ---------------------------------------------------------------------- */

extern const dap_serialize_schema_t chipmunk_hvc_poly_schema;
extern const dap_serialize_schema_t chipmunk_poly_schema;
extern const dap_serialize_schema_t chipmunk_path_node_schema;
extern const dap_serialize_schema_t chipmunk_hots_pk_schema;
extern const dap_serialize_schema_t chipmunk_aggregated_hots_sig_wire_schema;
extern const dap_serialize_schema_t chipmunk_path_wire_schema;
extern const dap_serialize_schema_t chipmunk_multi_signature_wire_schema;

/* ---------------------------------------------------------------------- *
 *  Wire ↔ runtime adapters                                               *
 * ---------------------------------------------------------------------- */

/**
 * @brief Build a shallow wire mirror from a runtime multi-signature.
 *
 * Allocates two transient arrays (proofs[] and aggregated_hots reordered
 * sigma copy is in-place) — the caller MUST release them via
 * chipmunk_multi_signature_wire_release().  Pointer arrays inside the
 * wire mirror reference the same heap blocks as the runtime struct, so
 * the caller must keep the runtime struct alive while the mirror is in
 * use.
 *
 * Returns 0 on success, negative on validation / allocation failure.
 */
int chipmunk_multi_signature_to_wire(
        const chipmunk_multi_signature_t *a_runtime,
        chipmunk_multi_signature_wire_t  *a_out_wire);

/**
 * @brief Free transient buffers attached to the wire mirror.
 *
 * Safe on NULL and on partially initialised mirrors.  Does NOT free the
 * pointer arrays shared with the runtime struct (those still belong to
 * the runtime owner).
 */
void chipmunk_multi_signature_wire_release(
        chipmunk_multi_signature_wire_t *a_wire);

/**
 * @brief Take a fully owned wire mirror (post-deserialise) and lift it
 *        into the runtime struct.
 *
 * Pointer ownership is transferred from @p a_wire to @p a_out_runtime;
 * after a successful call the wire struct is zeroed and must not be
 * released via the helper above.  On failure the wire struct is left
 * untouched and the caller must release it.
 *
 * Returns 0 on success, negative if cross-signer invariants fail
 * (e.g. proofs[i].path_length not all equal — historical CHMA invariant
 * preserved on top of the schema decode).
 */
int chipmunk_multi_signature_from_wire(
        chipmunk_multi_signature_wire_t  *a_wire,
        chipmunk_multi_signature_t       *a_out_runtime);

#ifdef __cplusplus
}
#endif
