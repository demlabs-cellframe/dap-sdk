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

#include "chipmunk_multi_signature_serialize_schema.h"

#include <stdlib.h>
#include <string.h>

#include "dap_common.h"

#define LOG_TAG "chipmunk_multi_sig_schema"

/* ---------------------------------------------------------------------- *
 *  Leaf schemas — chipmunk_hvc_poly_t and chipmunk_poly_t share a layout *
 *  (a single int32_t coeffs[CHIPMUNK_N]) but live in separate types, so  *
 *  we expose two thin schemas that point at the same byte representation.*
 * ---------------------------------------------------------------------- */

static const dap_serialize_field_t s_hvc_poly_fields[] = {
    DAP_SERIALIZE_FIELD_FIXED_ARRAY(chipmunk_hvc_poly_t, coeffs,
                                    CHIPMUNK_N,
                                    DAP_SERIALIZE_TYPE_INT32),
};

const dap_serialize_schema_t chipmunk_hvc_poly_schema = {
    .name        = "chipmunk_hvc_poly",
    .version     = 1,
    .struct_size = sizeof(chipmunk_hvc_poly_t),
    .field_count = sizeof(s_hvc_poly_fields) / sizeof(s_hvc_poly_fields[0]),
    .fields      = s_hvc_poly_fields,
    .magic       = CHIPMUNK_MULTI_SIG_SCHEMA_MAGIC,
};

static const dap_serialize_field_t s_poly_fields[] = {
    DAP_SERIALIZE_FIELD_FIXED_ARRAY(chipmunk_poly_t, coeffs,
                                    CHIPMUNK_N,
                                    DAP_SERIALIZE_TYPE_INT32),
};

const dap_serialize_schema_t chipmunk_poly_schema = {
    .name        = "chipmunk_poly",
    .version     = 1,
    .struct_size = sizeof(chipmunk_poly_t),
    .field_count = sizeof(s_poly_fields) / sizeof(s_poly_fields[0]),
    .fields      = s_poly_fields,
    .magic       = CHIPMUNK_MULTI_SIG_SCHEMA_MAGIC,
};

/* ---------------------------------------------------------------------- *
 *  Composite leaf schemas                                                *
 * ---------------------------------------------------------------------- */

static const dap_serialize_field_t s_path_node_fields[] = {
    DAP_SERIALIZE_FIELD_NESTED(chipmunk_path_node_t, left,  &chipmunk_hvc_poly_schema),
    DAP_SERIALIZE_FIELD_NESTED(chipmunk_path_node_t, right, &chipmunk_hvc_poly_schema),
};

const dap_serialize_schema_t chipmunk_path_node_schema = {
    .name        = "chipmunk_path_node",
    .version     = 1,
    .struct_size = sizeof(chipmunk_path_node_t),
    .field_count = sizeof(s_path_node_fields) / sizeof(s_path_node_fields[0]),
    .fields      = s_path_node_fields,
    .magic       = CHIPMUNK_MULTI_SIG_SCHEMA_MAGIC,
};

static const dap_serialize_field_t s_hots_pk_fields[] = {
    DAP_SERIALIZE_FIELD_NESTED(chipmunk_hots_pk_t, v0, &chipmunk_poly_schema),
    DAP_SERIALIZE_FIELD_NESTED(chipmunk_hots_pk_t, v1, &chipmunk_poly_schema),
};

const dap_serialize_schema_t chipmunk_hots_pk_schema = {
    .name        = "chipmunk_hots_pk",
    .version     = 1,
    .struct_size = sizeof(chipmunk_hots_pk_t),
    .field_count = sizeof(s_hots_pk_fields) / sizeof(s_hots_pk_fields[0]),
    .fields      = s_hots_pk_fields,
    .magic       = CHIPMUNK_MULTI_SIG_SCHEMA_MAGIC,
};

/* ---------------------------------------------------------------------- *
 *  Aggregated HOTS signature (wire mirror)                               *
 * ---------------------------------------------------------------------- */

static const dap_serialize_field_t s_aggregated_hots_fields[] = {
    DAP_SERIALIZE_FIELD_SIMPLE(chipmunk_aggregated_hots_sig_wire_t,
                               is_randomized,
                               DAP_SERIALIZE_TYPE_UINT8),
    /* Three reserved bytes — emit them as a fixed byte array so the
     * deserialiser can verify they are actually zero through the
     * codec's preflight check (the schema engine itself only writes
     * whatever bytes happen to live at the offset, so the codec layer
     * still validates the value). */
    {
        .name    = "reserved",
        .type    = DAP_SERIALIZE_TYPE_BYTES_FIXED,
        .flags   = DAP_SERIALIZE_FLAG_NONE,
        .offset  = offsetof(chipmunk_aggregated_hots_sig_wire_t, reserved),
        .size    = 3,
    },
    DAP_SERIALIZE_FIELD_FIXED_ARRAY_NESTED(chipmunk_aggregated_hots_sig_wire_t,
                                           sigma,
                                           CHIPMUNK_GAMMA,
                                           &chipmunk_poly_schema),
};

const dap_serialize_schema_t chipmunk_aggregated_hots_sig_wire_schema = {
    .name        = "chipmunk_aggregated_hots_sig_wire",
    .version     = 1,
    .struct_size = sizeof(chipmunk_aggregated_hots_sig_wire_t),
    .field_count = sizeof(s_aggregated_hots_fields) / sizeof(s_aggregated_hots_fields[0]),
    .fields      = s_aggregated_hots_fields,
    .magic       = CHIPMUNK_MULTI_SIG_SCHEMA_MAGIC,
};

/* ---------------------------------------------------------------------- *
 *  Per-signer Merkle path (wire mirror)                                  *
 *                                                                        *
 *  Wire layout per proof:                                                *
 *      8B           index                                                *
 *      ARRAY_DYN    nodes  (4B count + count * 2 * poly)                 *
 * ---------------------------------------------------------------------- */

static const dap_serialize_field_t s_path_wire_fields[] = {
    DAP_SERIALIZE_FIELD_SIMPLE(chipmunk_path_wire_t, index,  DAP_SERIALIZE_TYPE_UINT64),
    DAP_SERIALIZE_FIELD_DYNAMIC_ARRAY(chipmunk_path_wire_t, nodes, path_length,
                                      &chipmunk_path_node_schema),
};

const dap_serialize_schema_t chipmunk_path_wire_schema = {
    .name        = "chipmunk_path_wire",
    .version     = 1,
    .struct_size = sizeof(chipmunk_path_wire_t),
    .field_count = sizeof(s_path_wire_fields) / sizeof(s_path_wire_fields[0]),
    .fields      = s_path_wire_fields,
    .magic       = CHIPMUNK_MULTI_SIG_SCHEMA_MAGIC,
};

/* ---------------------------------------------------------------------- *
 *  Top-level multi-signature (wire mirror)                               *
 * ---------------------------------------------------------------------- */

/* Helper field initialisers for the rho_seeds array (uint8_t[32] elements
 * carried as raw bytes — no nested schema, just a per-element size). */
static const dap_serialize_field_t s_multi_sig_fields[] = {
    {
        .name   = "message_hash",
        .type   = DAP_SERIALIZE_TYPE_BYTES_FIXED,
        .flags  = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_multi_signature_wire_t, message_hash),
        .size   = 32,
    },
    {
        .name   = "hvc_hasher_seed",
        .type   = DAP_SERIALIZE_TYPE_BYTES_FIXED,
        .flags  = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_multi_signature_wire_t, hvc_hasher_seed),
        .size   = 32,
    },
    DAP_SERIALIZE_FIELD_NESTED(chipmunk_multi_signature_wire_t,
                               aggregated_hots,
                               &chipmunk_aggregated_hots_sig_wire_schema),
    DAP_SERIALIZE_FIELD_NESTED(chipmunk_multi_signature_wire_t,
                               tree_root,
                               &chipmunk_hvc_poly_schema),

    DAP_SERIALIZE_FIELD_DYNAMIC_ARRAY(chipmunk_multi_signature_wire_t,
                                      public_key_roots, signer_count,
                                      &chipmunk_hvc_poly_schema),
    DAP_SERIALIZE_FIELD_DYNAMIC_ARRAY(chipmunk_multi_signature_wire_t,
                                      hots_pks, signer_count,
                                      &chipmunk_hots_pk_schema),
    /* rho_seeds is a count-prefixed array of fixed 32-byte blobs.  We
     * encode each element as raw bytes (size = 32, no nested schema)
     * which mirrors the v1 codec's behaviour byte-for-byte. */
    {
        .name         = "rho_seeds",
        .type         = DAP_SERIALIZE_TYPE_ARRAY_DYNAMIC,
        .flags        = DAP_SERIALIZE_FLAG_NONE,
        .offset       = offsetof(chipmunk_multi_signature_wire_t, rho_seeds),
        .size         = 32, /* per-element byte count */
        .count_offset = offsetof(chipmunk_multi_signature_wire_t, signer_count),
    },
    /* leaf_indices: ARRAY_DYNAMIC of uint32_t (with endian-aware element_type). */
    {
        .name         = "leaf_indices",
        .type         = DAP_SERIALIZE_TYPE_ARRAY_DYNAMIC,
        .flags        = DAP_SERIALIZE_FLAG_NONE,
        .offset       = offsetof(chipmunk_multi_signature_wire_t, leaf_indices),
        .size         = sizeof(uint32_t),
        .count_offset = offsetof(chipmunk_multi_signature_wire_t, signer_count),
        .element_type = DAP_SERIALIZE_TYPE_UINT32,
    },
    DAP_SERIALIZE_FIELD_DYNAMIC_ARRAY(chipmunk_multi_signature_wire_t,
                                      proofs, signer_count,
                                      &chipmunk_path_wire_schema),
};

const dap_serialize_schema_t chipmunk_multi_signature_wire_schema = {
    .name        = "chipmunk_multi_signature_wire",
    .version     = 1,
    .struct_size = sizeof(chipmunk_multi_signature_wire_t),
    .field_count = sizeof(s_multi_sig_fields) / sizeof(s_multi_sig_fields[0]),
    .fields      = s_multi_sig_fields,
    .magic       = CHIPMUNK_MULTI_SIG_SCHEMA_MAGIC,
};

/* ---------------------------------------------------------------------- *
 *  Wire ↔ runtime adapters                                               *
 * ---------------------------------------------------------------------- */

int chipmunk_multi_signature_to_wire(
        const chipmunk_multi_signature_t *a_runtime,
        chipmunk_multi_signature_wire_t  *a_out_wire)
{
    if (!a_runtime || !a_out_wire) {
        return -1;
    }
    if (a_runtime->signer_count == 0) {
        return -2;
    }
    if (a_runtime->signer_count > UINT32_MAX) {
        return -3;
    }

    memset(a_out_wire, 0, sizeof(*a_out_wire));

    /* Copy fixed-size by-value fields. */
    memcpy(a_out_wire->message_hash,    a_runtime->message_hash,    32);
    memcpy(a_out_wire->hvc_hasher_seed, a_runtime->hvc_hasher_seed, 32);
    memcpy(&a_out_wire->tree_root,      &a_runtime->tree_root,      sizeof(chipmunk_hvc_poly_t));

    /* Reorder aggregated_hots into the wire layout (is_randomized first). */
    a_out_wire->aggregated_hots.is_randomized = a_runtime->aggregated_hots.is_randomized ? 1 : 0;
    a_out_wire->aggregated_hots.reserved[0] = 0;
    a_out_wire->aggregated_hots.reserved[1] = 0;
    a_out_wire->aggregated_hots.reserved[2] = 0;
    memcpy(a_out_wire->aggregated_hots.sigma,
           a_runtime->aggregated_hots.sigma,
           sizeof(chipmunk_poly_t) * (size_t)CHIPMUNK_GAMMA);

    /* Share pointer arrays (read-only on the encode path). */
    a_out_wire->public_key_roots = a_runtime->public_key_roots;
    a_out_wire->hots_pks         = a_runtime->hots_pks;
    a_out_wire->rho_seeds        = a_runtime->rho_seeds;
    a_out_wire->leaf_indices     = a_runtime->leaf_indices;
    a_out_wire->signer_count     = (uint32_t)a_runtime->signer_count;

    /* Allocate the proof mirror — we have to demote per-signer counters
     * from size_t to uint32_t.  All other proof data (the nodes pointer)
     * is shared by reference. */
    a_out_wire->proofs = DAP_NEW_Z_COUNT(chipmunk_path_wire_t, a_runtime->signer_count);
    if (!a_out_wire->proofs) {
        return -4;
    }
    for (size_t i = 0; i < a_runtime->signer_count; ++i) {
        const chipmunk_path_t *l_src = &a_runtime->proofs[i];
        if (l_src->path_length > UINT32_MAX) {
            DAP_DEL_Z(a_out_wire->proofs);
            return -5;
        }
        a_out_wire->proofs[i].nodes       = l_src->nodes;
        a_out_wire->proofs[i].path_length = (uint32_t)l_src->path_length;
        a_out_wire->proofs[i].index       = (uint64_t)l_src->index;
    }
    return 0;
}

void chipmunk_multi_signature_wire_release(
        chipmunk_multi_signature_wire_t *a_wire)
{
    if (!a_wire) {
        return;
    }
    /* Only the proofs array was newly allocated — every other pointer
     * still belongs to the runtime owner and must NOT be freed here. */
    if (a_wire->proofs) {
        DAP_DEL_Z(a_wire->proofs);
    }
    memset(a_wire, 0, sizeof(*a_wire));
}

int chipmunk_multi_signature_from_wire(
        chipmunk_multi_signature_wire_t  *a_wire,
        chipmunk_multi_signature_t       *a_out_runtime)
{
    if (!a_wire || !a_out_runtime) {
        return -1;
    }
    if (a_wire->signer_count == 0) {
        return -2;
    }
    /* Cross-signer invariant retained from the v1 codec: every proof
     * must share the same path length.  This is a chipmunk protocol
     * requirement that the schema engine has no concept of, so we
     * assert it manually after the schema-driven decode. */
    if (!a_wire->proofs || a_wire->proofs[0].path_length == 0) {
        return -3;
    }
    const uint32_t l_common = a_wire->proofs[0].path_length;
    for (uint32_t i = 1; i < a_wire->signer_count; ++i) {
        if (a_wire->proofs[i].path_length != l_common) {
            return -4;
        }
        if (!a_wire->proofs[i].nodes) {
            return -5;
        }
    }

    memset(a_out_runtime, 0, sizeof(*a_out_runtime));

    /* Copy fixed by-value fields. */
    memcpy(a_out_runtime->message_hash,    a_wire->message_hash,    32);
    memcpy(a_out_runtime->hvc_hasher_seed, a_wire->hvc_hasher_seed, 32);
    memcpy(&a_out_runtime->tree_root,      &a_wire->tree_root,      sizeof(chipmunk_hvc_poly_t));

    /* Restore the runtime layout for aggregated_hots. */
    a_out_runtime->aggregated_hots.is_randomized = (a_wire->aggregated_hots.is_randomized != 0);
    memcpy(a_out_runtime->aggregated_hots.sigma,
           a_wire->aggregated_hots.sigma,
           sizeof(chipmunk_poly_t) * (size_t)CHIPMUNK_GAMMA);

    a_out_runtime->signer_count     = (size_t)a_wire->signer_count;

    /* Transfer ownership of the deserialised pointer arrays.
     * The schema engine allocated them with calloc/DAP_NEW_Z; the runtime
     * struct will own them from now on, so the codec must release the
     * wire mirror without freeing those buffers (handled by the caller). */
    a_out_runtime->public_key_roots = a_wire->public_key_roots;
    a_out_runtime->hots_pks         = a_wire->hots_pks;
    a_out_runtime->rho_seeds        = a_wire->rho_seeds;
    a_out_runtime->leaf_indices     = a_wire->leaf_indices;

    /* Allocate the proofs array in runtime layout (size_t fields).
     * The wire->proofs[i].nodes pointers are inherited verbatim. */
    a_out_runtime->proofs = DAP_NEW_Z_COUNT(chipmunk_path_t, a_wire->signer_count);
    if (!a_out_runtime->proofs) {
        a_out_runtime->public_key_roots = NULL;
        a_out_runtime->hots_pks         = NULL;
        a_out_runtime->rho_seeds        = NULL;
        a_out_runtime->leaf_indices     = NULL;
        a_out_runtime->signer_count     = 0;
        memset(&a_out_runtime->aggregated_hots, 0, sizeof(a_out_runtime->aggregated_hots));
        return -6;
    }
    for (uint32_t i = 0; i < a_wire->signer_count; ++i) {
        a_out_runtime->proofs[i].nodes       = a_wire->proofs[i].nodes;
        a_out_runtime->proofs[i].path_length = (size_t)a_wire->proofs[i].path_length;
        a_out_runtime->proofs[i].index       = (size_t)a_wire->proofs[i].index;
    }

    /* Wire arrays now belong to the runtime struct — release the wire
     * mirror's bookkeeping but keep the heap blocks alive. */
    DAP_DEL_Z(a_wire->proofs);
    memset(a_wire, 0, sizeof(*a_wire));
    return 0;
}
