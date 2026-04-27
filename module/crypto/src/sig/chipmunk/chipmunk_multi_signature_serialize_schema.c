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
 *  Per-signer record schema                                              *
 *                                                                        *
 *  Wire layout (matches v1 codec byte-for-byte):                         *
 *      [N*4]   pk_root.coeffs                                            *
 *      [N*4]   hots_pk.v0.coeffs                                         *
 *      [N*4]   hots_pk.v1.coeffs                                         *
 *      [32]    rho_seed                                                  *
 *      [4]     leaf_index            (uint32 LE)                         *
 *      [8]     index                 (uint64 LE)                         *
 *      [4]     path_length           (uint32 LE)                         *
 *      [path_length * (2*N*4)] nodes (chipmunk_path_node_t each)         *
 * ---------------------------------------------------------------------- */

static const dap_serialize_field_t s_signer_record_fields[] = {
    DAP_SERIALIZE_FIELD_NESTED(chipmunk_signer_record_wire_t, pk_root, &chipmunk_hvc_poly_schema),
    DAP_SERIALIZE_FIELD_NESTED(chipmunk_signer_record_wire_t, hots_pk, &chipmunk_hots_pk_schema),
    {
        .name   = "rho_seed",
        .type   = DAP_SERIALIZE_TYPE_BYTES_FIXED,
        .flags  = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_signer_record_wire_t, rho_seed),
        .size   = 32,
    },
    DAP_SERIALIZE_FIELD_SIMPLE(chipmunk_signer_record_wire_t, leaf_index,  DAP_SERIALIZE_TYPE_UINT32),
    DAP_SERIALIZE_FIELD_SIMPLE(chipmunk_signer_record_wire_t, index,       DAP_SERIALIZE_TYPE_UINT64),
    DAP_SERIALIZE_FIELD_SIMPLE(chipmunk_signer_record_wire_t, path_length, DAP_SERIALIZE_TYPE_UINT32),
    /* nodes[]: the path_length field above is the on-wire count, so the
     * array MUST NOT emit its own 4-byte prefix (otherwise the wire layout
     * would diverge from the legacy v1 codec, which wrote path_length
     * exactly once before the nodes block).  NO_COUNT_PREFIX makes the
     * schema read/write count from `path_length` directly. */
    {
        .name          = "nodes",
        .type          = DAP_SERIALIZE_TYPE_ARRAY_DYNAMIC,
        .flags         = DAP_SERIALIZE_FLAG_NO_COUNT_PREFIX,
        .offset        = offsetof(chipmunk_signer_record_wire_t, nodes),
        .count_offset  = offsetof(chipmunk_signer_record_wire_t, path_length),
        .nested_schema = &chipmunk_path_node_schema,
    },
};

const dap_serialize_schema_t chipmunk_signer_record_wire_schema = {
    .name        = "chipmunk_signer_record_wire",
    .version     = 1,
    .struct_size = sizeof(chipmunk_signer_record_wire_t),
    .field_count = sizeof(s_signer_record_fields) / sizeof(s_signer_record_fields[0]),
    .fields      = s_signer_record_fields,
    .magic       = CHIPMUNK_MULTI_SIG_SCHEMA_MAGIC,
};

/* ---------------------------------------------------------------------- *
 *  Top-level multi-signature schema                                      *
 *                                                                        *
 *  Wire layout (after the 24-byte CHMA header written by the codec):     *
 *      [32]                         message_hash                          *
 *      [32]                         hvc_hasher_seed                       *
 *      [1]                          is_randomized                         *
 *      [3]                          reserved                              *
 *      [N*4]                        tree_root.coeffs                      *
 *      [GAMMA*N*4]                  sigma[GAMMA].coeffs                   *
 *      [signer_count * record_size] signers[]   (NO inner count prefix —  *
 *                                   signer_count comes from the header)  *
 * ---------------------------------------------------------------------- */

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
    DAP_SERIALIZE_FIELD_SIMPLE(chipmunk_multi_signature_wire_t, is_randomized, DAP_SERIALIZE_TYPE_UINT8),
    {
        .name   = "reserved",
        .type   = DAP_SERIALIZE_TYPE_BYTES_FIXED,
        .flags  = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(chipmunk_multi_signature_wire_t, reserved),
        .size   = 3,
    },
    DAP_SERIALIZE_FIELD_NESTED(chipmunk_multi_signature_wire_t, tree_root, &chipmunk_hvc_poly_schema),
    DAP_SERIALIZE_FIELD_FIXED_ARRAY_NESTED(chipmunk_multi_signature_wire_t,
                                           sigma, CHIPMUNK_GAMMA, &chipmunk_poly_schema),
    /* signers: ARRAY_DYNAMIC with NO_COUNT_PREFIX — count lives in
     * signer_count, populated by the codec from the CHMA header before
     * deserialise; serialised count is taken from the wire mirror. */
    {
        .name          = "signers",
        .type          = DAP_SERIALIZE_TYPE_ARRAY_DYNAMIC,
        .flags         = DAP_SERIALIZE_FLAG_NO_COUNT_PREFIX,
        .offset        = offsetof(chipmunk_multi_signature_wire_t, signers),
        .count_offset  = offsetof(chipmunk_multi_signature_wire_t, signer_count),
        .nested_schema = &chipmunk_signer_record_wire_schema,
    },
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
    if (!a_runtime->public_key_roots || !a_runtime->hots_pks
        || !a_runtime->rho_seeds      || !a_runtime->leaf_indices
        || !a_runtime->proofs) {
        return -4;
    }

    memset(a_out_wire, 0, sizeof(*a_out_wire));

    /* Copy fixed by-value fields. */
    memcpy(a_out_wire->message_hash,    a_runtime->message_hash,    32);
    memcpy(a_out_wire->hvc_hasher_seed, a_runtime->hvc_hasher_seed, 32);
    memcpy(&a_out_wire->tree_root,      &a_runtime->tree_root,      sizeof(chipmunk_hvc_poly_t));
    a_out_wire->is_randomized = a_runtime->aggregated_hots.is_randomized ? 1 : 0;
    /* reserved already zeroed by memset above. */
    memcpy(a_out_wire->sigma, a_runtime->aggregated_hots.sigma,
           sizeof(chipmunk_poly_t) * (size_t)CHIPMUNK_GAMMA);

    a_out_wire->signer_count = (uint32_t)a_runtime->signer_count;
    a_out_wire->signers = DAP_NEW_Z_COUNT(chipmunk_signer_record_wire_t, a_runtime->signer_count);
    if (!a_out_wire->signers) {
        return -5;
    }
    for (size_t i = 0; i < a_runtime->signer_count; ++i) {
        chipmunk_signer_record_wire_t *l_dst = &a_out_wire->signers[i];
        memcpy(&l_dst->pk_root, &a_runtime->public_key_roots[i], sizeof(chipmunk_hvc_poly_t));
        memcpy(&l_dst->hots_pk, &a_runtime->hots_pks[i],         sizeof(chipmunk_hots_pk_t));
        memcpy(l_dst->rho_seed, a_runtime->rho_seeds[i], 32);
        l_dst->leaf_index  = a_runtime->leaf_indices[i];

        const chipmunk_path_t *l_src_proof = &a_runtime->proofs[i];
        if (l_src_proof->path_length > UINT32_MAX) {
            DAP_DEL_Z(a_out_wire->signers);
            return -6;
        }
        l_dst->index       = (uint64_t)l_src_proof->index;
        l_dst->path_length = (uint32_t)l_src_proof->path_length;
        l_dst->nodes       = l_src_proof->nodes;  /* shared on encode */
    }
    return 0;
}

void chipmunk_multi_signature_wire_release(
        chipmunk_multi_signature_wire_t *a_wire)
{
    if (!a_wire) {
        return;
    }
    /* Only the signers[] array was newly allocated; the proof node
     * pointers it carries still belong to the runtime owner and must
     * NOT be freed here. */
    if (a_wire->signers) {
        DAP_DEL_Z(a_wire->signers);
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
    if (!a_wire->signers) {
        return -3;
    }

    /* Cross-signer invariant retained from the v1 codec: every proof
     * must share the same path length.  The schema engine has no
     * concept of cross-element invariants, so we assert it here. */
    const uint32_t l_common = a_wire->signers[0].path_length;
    if (l_common == 0) {
        return -4;
    }
    for (uint32_t i = 0; i < a_wire->signer_count; ++i) {
        if (a_wire->signers[i].path_length != l_common) {
            return -5;
        }
        if (!a_wire->signers[i].nodes) {
            return -6;
        }
    }
    if (a_wire->is_randomized != 0 && a_wire->is_randomized != 1) {
        return -7;
    }
    if (a_wire->reserved[0] != 0 || a_wire->reserved[1] != 0 || a_wire->reserved[2] != 0) {
        return -8;
    }

    memset(a_out_runtime, 0, sizeof(*a_out_runtime));

    memcpy(a_out_runtime->message_hash,    a_wire->message_hash,    32);
    memcpy(a_out_runtime->hvc_hasher_seed, a_wire->hvc_hasher_seed, 32);
    memcpy(&a_out_runtime->tree_root,      &a_wire->tree_root,      sizeof(chipmunk_hvc_poly_t));
    a_out_runtime->aggregated_hots.is_randomized = (a_wire->is_randomized != 0);
    memcpy(a_out_runtime->aggregated_hots.sigma, a_wire->sigma,
           sizeof(chipmunk_poly_t) * (size_t)CHIPMUNK_GAMMA);

    a_out_runtime->signer_count = (size_t)a_wire->signer_count;

    /* Allocate parallel arrays in runtime layout and copy out of the
     * AoS wire layout.  The proof nodes pointer is *moved* (ownership
     * transferred), every other field is by-value. */
    a_out_runtime->public_key_roots = DAP_NEW_Z_COUNT(chipmunk_hvc_poly_t, a_wire->signer_count);
    a_out_runtime->hots_pks         = DAP_NEW_Z_COUNT(chipmunk_hots_pk_t,  a_wire->signer_count);
    a_out_runtime->rho_seeds        = DAP_NEW_Z_COUNT(uint8_t[32],         a_wire->signer_count);
    a_out_runtime->leaf_indices     = DAP_NEW_Z_COUNT(uint32_t,            a_wire->signer_count);
    a_out_runtime->proofs           = DAP_NEW_Z_COUNT(chipmunk_path_t,     a_wire->signer_count);
    if (!a_out_runtime->public_key_roots || !a_out_runtime->hots_pks
        || !a_out_runtime->rho_seeds || !a_out_runtime->leaf_indices
        || !a_out_runtime->proofs) {
        DAP_DEL_Z(a_out_runtime->public_key_roots);
        DAP_DEL_Z(a_out_runtime->hots_pks);
        DAP_DEL_Z(a_out_runtime->rho_seeds);
        DAP_DEL_Z(a_out_runtime->leaf_indices);
        DAP_DEL_Z(a_out_runtime->proofs);
        a_out_runtime->signer_count = 0;
        memset(&a_out_runtime->aggregated_hots, 0, sizeof(a_out_runtime->aggregated_hots));
        return -9;
    }
    for (uint32_t i = 0; i < a_wire->signer_count; ++i) {
        memcpy(&a_out_runtime->public_key_roots[i], &a_wire->signers[i].pk_root,
               sizeof(chipmunk_hvc_poly_t));
        memcpy(&a_out_runtime->hots_pks[i],         &a_wire->signers[i].hots_pk,
               sizeof(chipmunk_hots_pk_t));
        memcpy(a_out_runtime->rho_seeds[i],         a_wire->signers[i].rho_seed, 32);
        a_out_runtime->leaf_indices[i]    = a_wire->signers[i].leaf_index;
        a_out_runtime->proofs[i].index       = (size_t)a_wire->signers[i].index;
        a_out_runtime->proofs[i].path_length = (size_t)a_wire->signers[i].path_length;
        a_out_runtime->proofs[i].nodes       = a_wire->signers[i].nodes; /* moved */
        a_wire->signers[i].nodes = NULL;
    }

    /* Wire arrays now belong to the runtime struct — release the
     * adapter array but keep the moved heap blocks alive. */
    DAP_DEL_Z(a_wire->signers);
    memset(a_wire, 0, sizeof(*a_wire));
    return 0;
}
