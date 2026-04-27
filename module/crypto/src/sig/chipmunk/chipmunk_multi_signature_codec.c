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

#include "chipmunk_multi_signature_codec.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "chipmunk.h"
#include "chipmunk_hots.h"
#include "chipmunk_tree.h"
#include "chipmunk_multi_signature_serialize_schema.h"
#include "dap_common.h"
#include "dap_serialize.h"

#define LOG_TAG "chipmunk_multi_sig_codec"

/* ---------------------------------------------------------------------- *
 *  Low-level LE helpers (header framing only)                             *
 *                                                                         *
 *  The body itself is produced by dap_serialize via the wire-mirror       *
 *  schemas.  These helpers cover only the 24-byte CHMA header that        *
 *  prefixes every multi-signature blob — that framing is intentionally    *
 *  hand-coded so we can structurally triage the buffer (magic, version,   *
 *  total length) without invoking the schema engine.                      *
 * ---------------------------------------------------------------------- */

static inline void s_write_u16(uint8_t *a_dst, uint16_t a_value)
{
    a_dst[0] = (uint8_t)(a_value & 0xFF);
    a_dst[1] = (uint8_t)((a_value >> 8) & 0xFF);
}

static inline void s_write_u32(uint8_t *a_dst, uint32_t a_value)
{
    a_dst[0] = (uint8_t)(a_value & 0xFF);
    a_dst[1] = (uint8_t)((a_value >> 8) & 0xFF);
    a_dst[2] = (uint8_t)((a_value >> 16) & 0xFF);
    a_dst[3] = (uint8_t)((a_value >> 24) & 0xFF);
}

static inline void s_write_u64(uint8_t *a_dst, uint64_t a_value)
{
    for (unsigned i = 0; i < 8; ++i) {
        a_dst[i] = (uint8_t)((a_value >> (8u * i)) & 0xFF);
    }
}

static inline uint16_t s_read_u16(const uint8_t *a_src)
{
    return (uint16_t)a_src[0] | ((uint16_t)a_src[1] << 8);
}

static inline uint32_t s_read_u32(const uint8_t *a_src)
{
    return  (uint32_t)a_src[0]
         | ((uint32_t)a_src[1] << 8)
         | ((uint32_t)a_src[2] << 16)
         | ((uint32_t)a_src[3] << 24);
}

static inline uint64_t s_read_u64(const uint8_t *a_src)
{
    uint64_t l_v = 0;
    for (unsigned i = 0; i < 8; ++i) {
        l_v |= (uint64_t)a_src[i] << (8u * i);
    }
    return l_v;
}

/* ---------------------------------------------------------------------- *
 *  Deep-free                                                              *
 * ---------------------------------------------------------------------- */

void chipmunk_multi_signature_deep_free(chipmunk_multi_signature_t *a_multi_sig)
{
    if (!a_multi_sig) {
        return;
    }

    if (a_multi_sig->proofs) {
        for (size_t i = 0; i < a_multi_sig->signer_count; ++i) {
            if (a_multi_sig->proofs[i].nodes) {
                free(a_multi_sig->proofs[i].nodes);
                a_multi_sig->proofs[i].nodes = NULL;
            }
            a_multi_sig->proofs[i].path_length = 0;
            a_multi_sig->proofs[i].index       = 0;
        }
        free(a_multi_sig->proofs);
        a_multi_sig->proofs = NULL;
    }

    if (a_multi_sig->public_key_roots) {
        free(a_multi_sig->public_key_roots);
        a_multi_sig->public_key_roots = NULL;
    }
    if (a_multi_sig->hots_pks) {
        free(a_multi_sig->hots_pks);
        a_multi_sig->hots_pks = NULL;
    }
    if (a_multi_sig->rho_seeds) {
        free(a_multi_sig->rho_seeds);
        a_multi_sig->rho_seeds = NULL;
    }
    if (a_multi_sig->leaf_indices) {
        free(a_multi_sig->leaf_indices);
        a_multi_sig->leaf_indices = NULL;
    }

    a_multi_sig->signer_count = 0;
    memset(&a_multi_sig->tree_root, 0, sizeof(a_multi_sig->tree_root));
    memset(&a_multi_sig->aggregated_hots, 0, sizeof(a_multi_sig->aggregated_hots));
    memset(a_multi_sig->message_hash, 0, sizeof(a_multi_sig->message_hash));
    memset(a_multi_sig->hvc_hasher_seed, 0, sizeof(a_multi_sig->hvc_hasher_seed));
}

/* ---------------------------------------------------------------------- *
 *  Pre-flight: cross-signer invariant validation                          *
 *                                                                         *
 *  The schema engine has no concept of cross-element invariants, so we    *
 *  enforce the chipmunk-protocol-level requirement (every proof has the   *
 *  same path length) before invoking the serialiser.                      *
 * ---------------------------------------------------------------------- */

static int s_preflight(const chipmunk_multi_signature_t *a_multi_sig,
                       uint32_t *a_out_path_length)
{
    if (!a_multi_sig) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_NULL;
    }
    if (a_multi_sig->signer_count == 0
        || a_multi_sig->signer_count > (size_t)CHIPMUNK_MULTI_SIG_MAX_SIGNERS) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_SIGNER_COUNT;
    }
    if (!a_multi_sig->public_key_roots || !a_multi_sig->hots_pks
        || !a_multi_sig->rho_seeds      || !a_multi_sig->proofs
        || !a_multi_sig->leaf_indices) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_NULL;
    }

    const uint32_t l_common = (uint32_t)a_multi_sig->proofs[0].path_length;
    if (l_common == 0 || l_common >= (uint32_t)CHIPMUNK_TREE_HEIGHT_MAX) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_PATH_LENGTH;
    }
    for (size_t i = 0; i < a_multi_sig->signer_count; ++i) {
        if ((uint32_t)a_multi_sig->proofs[i].path_length != l_common) {
            return CHIPMUNK_MULTI_SIG_CODEC_ERR_PATH_LENGTH_MISMATCH;
        }
        if (!a_multi_sig->proofs[i].nodes) {
            return CHIPMUNK_MULTI_SIG_CODEC_ERR_NULL;
        }
    }

    if (a_out_path_length) {
        *a_out_path_length = l_common;
    }
    return CHIPMUNK_MULTI_SIG_CODEC_OK;
}

/* ---------------------------------------------------------------------- *
 *  Size calculation — schema driven                                       *
 *                                                                         *
 *  Walks the wire-mirror schema once over a transient mirror of the      *
 *  runtime struct.  No deep copy of polynomial data; the mirror only     *
 *  allocates a signer_count-sized adapter array.                         *
 * ---------------------------------------------------------------------- */

static int s_compute_size(const chipmunk_multi_signature_t *a_multi_sig,
                          size_t *a_out_size,
                          uint32_t *a_out_path_length)
{
    uint32_t l_path_length = 0;
    int l_rc = s_preflight(a_multi_sig, &l_path_length);
    if (l_rc != CHIPMUNK_MULTI_SIG_CODEC_OK) {
        return l_rc;
    }

    chipmunk_multi_signature_wire_t l_wire = {0};
    if (chipmunk_multi_signature_to_wire(a_multi_sig, &l_wire) != 0) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_NULL;
    }

    const size_t l_body = dap_serialize_calc_size_raw(
            &chipmunk_multi_signature_wire_schema, NULL, &l_wire, NULL);
    chipmunk_multi_signature_wire_release(&l_wire);

    if (l_body == 0) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_BUFFER_TOO_SMALL;
    }
    /* Header is fixed-size, never overflows by construction. */
    const size_t l_total = (size_t)CHIPMUNK_MULTI_SIG_HEADER_SIZE + l_body;
    if (l_total < l_body) {  /* defensive against a malformed schema */
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_BUFFER_TOO_SMALL;
    }

    if (a_out_size) {
        *a_out_size = l_total;
    }
    if (a_out_path_length) {
        *a_out_path_length = l_path_length;
    }
    return CHIPMUNK_MULTI_SIG_CODEC_OK;
}

int chipmunk_multi_signature_serialized_size(
        const chipmunk_multi_signature_t *a_multi_sig,
        size_t *a_out_size)
{
    return s_compute_size(a_multi_sig, a_out_size, NULL);
}

/* ---------------------------------------------------------------------- *
 *  CHMA header writer / reader                                            *
 * ---------------------------------------------------------------------- */

static void s_write_header(uint8_t *a_dst,
                            uint32_t a_signer_count,
                            uint32_t a_path_length,
                            uint64_t a_total_bytes)
{
    a_dst[0] = CHIPMUNK_MULTI_SIG_MAGIC0;
    a_dst[1] = CHIPMUNK_MULTI_SIG_MAGIC1;
    a_dst[2] = CHIPMUNK_MULTI_SIG_MAGIC2;
    a_dst[3] = CHIPMUNK_MULTI_SIG_MAGIC3;
    s_write_u16(a_dst + 4,  CHIPMUNK_MULTI_SIG_VERSION);
    s_write_u16(a_dst + 6,  0);
    s_write_u32(a_dst + 8,  a_signer_count);
    s_write_u32(a_dst + 12, a_path_length);
    s_write_u64(a_dst + 16, a_total_bytes);
}

static int s_parse_header(const uint8_t *a_buffer,
                           size_t a_buffer_size,
                           uint32_t *a_out_signer_count,
                           uint32_t *a_out_path_length,
                           uint64_t *a_out_payload_len)
{
    if (a_buffer_size < (size_t)CHIPMUNK_MULTI_SIG_HEADER_SIZE) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_BUFFER_TOO_SMALL;
    }
    const uint8_t *l_p = a_buffer;

    if (l_p[0] != CHIPMUNK_MULTI_SIG_MAGIC0
        || l_p[1] != CHIPMUNK_MULTI_SIG_MAGIC1
        || l_p[2] != CHIPMUNK_MULTI_SIG_MAGIC2
        || l_p[3] != CHIPMUNK_MULTI_SIG_MAGIC3) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_MAGIC;
    }
    const uint16_t l_version  = s_read_u16(l_p + 4);
    const uint16_t l_reserved = s_read_u16(l_p + 6);
    if (l_version != CHIPMUNK_MULTI_SIG_VERSION) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_VERSION;
    }
    if (l_reserved != 0) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_RESERVED;
    }

    const uint32_t l_signer_count = s_read_u32(l_p + 8);
    const uint32_t l_path_length  = s_read_u32(l_p + 12);
    const uint64_t l_payload_len  = s_read_u64(l_p + 16);

    if (l_signer_count == 0
        || l_signer_count > (uint32_t)CHIPMUNK_MULTI_SIG_MAX_SIGNERS) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_SIGNER_COUNT;
    }
    if (l_path_length == 0 || l_path_length >= (uint32_t)CHIPMUNK_TREE_HEIGHT_MAX) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_PATH_LENGTH;
    }
    if ((size_t)l_payload_len != a_buffer_size) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_SIZE_MISMATCH;
    }

    if (a_out_signer_count) *a_out_signer_count = l_signer_count;
    if (a_out_path_length)  *a_out_path_length  = l_path_length;
    if (a_out_payload_len)  *a_out_payload_len  = l_payload_len;
    return CHIPMUNK_MULTI_SIG_CODEC_OK;
}

/* ---------------------------------------------------------------------- *
 *  Public serialise                                                       *
 *                                                                         *
 *  Writes the 24-byte CHMA header, then dispatches the body to            *
 *  dap_serialize via the wire-mirror schema.  No deep copy of poly data; *
 *  the wire mirror lives on the stack and only owns the signer_count-     *
 *  sized adapter array, which we release after the schema walk.          *
 * ---------------------------------------------------------------------- */

int chipmunk_multi_signature_serialize(
        const chipmunk_multi_signature_t *a_multi_sig,
        uint8_t *a_buffer,
        size_t a_buffer_size,
        size_t *a_bytes_written)
{
    if (!a_buffer) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_NULL;
    }

    size_t l_total = 0;
    uint32_t l_path_length = 0;
    int l_rc = s_compute_size(a_multi_sig, &l_total, &l_path_length);
    if (l_rc != CHIPMUNK_MULTI_SIG_CODEC_OK) {
        return l_rc;
    }
    if (a_buffer_size < l_total) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_BUFFER_TOO_SMALL;
    }

    s_write_header(a_buffer,
                   (uint32_t)a_multi_sig->signer_count,
                   l_path_length,
                   (uint64_t)l_total);

    chipmunk_multi_signature_wire_t l_wire = {0};
    if (chipmunk_multi_signature_to_wire(a_multi_sig, &l_wire) != 0) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_NULL;
    }

    dap_serialize_result_t l_ser = dap_serialize_to_buffer_raw(
            &chipmunk_multi_signature_wire_schema,
            &l_wire,
            a_buffer + CHIPMUNK_MULTI_SIG_HEADER_SIZE,
            a_buffer_size - CHIPMUNK_MULTI_SIG_HEADER_SIZE,
            NULL);
    chipmunk_multi_signature_wire_release(&l_wire);

    if (l_ser.error_code != DAP_SERIALIZE_ERROR_SUCCESS) {
        log_it(L_ERROR,
               "chipmunk_multi_signature_serialize: schema writer failed "
               "(error=%d, field=%s, message=%s)",
               l_ser.error_code,
               l_ser.failed_field ? l_ser.failed_field : "?",
               l_ser.error_message ? l_ser.error_message : "?");
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_SIZE_MISMATCH;
    }

    const size_t l_actual = (size_t)CHIPMUNK_MULTI_SIG_HEADER_SIZE + l_ser.bytes_written;
    if (l_actual != l_total) {
        log_it(L_ERROR,
               "chipmunk_multi_signature_serialize: schema-walk / size-calc "
               "disagreement (walked %zu vs computed %zu) — blob rejected",
               l_actual, l_total);
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_SIZE_MISMATCH;
    }
    if (a_bytes_written) {
        *a_bytes_written = l_actual;
    }
    return CHIPMUNK_MULTI_SIG_CODEC_OK;
}

/* ---------------------------------------------------------------------- *
 *  Public deserialise                                                     *
 *                                                                         *
 *  After the 24-byte CHMA header is validated, we hand the body to the    *
 *  schema engine with the count fields (signer_count) pre-populated in    *
 *  the wire mirror — hence dap_serialize_from_buffer_raw_preserve() so    *
 *  the implicit memset() does not clobber the outer-framed counts.       *
 *  chipmunk_multi_signature_from_wire() then transfers ownership of all   *
 *  heap blocks into the runtime struct.                                  *
 * ---------------------------------------------------------------------- */

int chipmunk_multi_signature_deserialize(
        const uint8_t *a_buffer,
        size_t a_buffer_size,
        chipmunk_multi_signature_t *a_multi_sig)
{
    if (!a_buffer || !a_multi_sig) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_NULL;
    }
    memset(a_multi_sig, 0, sizeof(*a_multi_sig));

    uint32_t l_signer_count = 0;
    uint32_t l_path_length  = 0;
    uint64_t l_payload_len  = 0;
    int l_rc = s_parse_header(a_buffer, a_buffer_size,
                              &l_signer_count, &l_path_length, &l_payload_len);
    if (l_rc != CHIPMUNK_MULTI_SIG_CODEC_OK) {
        return l_rc;
    }

    /* Pre-populate the wire-mirror count slot so the NO_COUNT_PREFIX
     * `signers` field can find it during the schema walk.  The
     * _preserve variant skips the framework's implicit memset(0) that
     * would otherwise wipe this value before the array decode starts. */
    chipmunk_multi_signature_wire_t l_wire = {0};
    l_wire.signer_count = l_signer_count;

    dap_serialize_result_t l_des = dap_serialize_from_buffer_raw_preserve(
            &chipmunk_multi_signature_wire_schema,
            a_buffer + CHIPMUNK_MULTI_SIG_HEADER_SIZE,
            a_buffer_size - CHIPMUNK_MULTI_SIG_HEADER_SIZE,
            &l_wire,
            NULL);

    if (l_des.error_code != DAP_SERIALIZE_ERROR_SUCCESS) {
        log_it(L_ERROR,
               "chipmunk_multi_signature_deserialize: schema reader failed "
               "(error=%d, field=%s, message=%s)",
               l_des.error_code,
               l_des.failed_field ? l_des.failed_field : "?",
               l_des.error_message ? l_des.error_message : "?");
        chipmunk_multi_signature_wire_release(&l_wire);
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_SIZE_MISMATCH;
    }
    if (l_des.bytes_read != a_buffer_size - CHIPMUNK_MULTI_SIG_HEADER_SIZE) {
        chipmunk_multi_signature_wire_release(&l_wire);
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_SIZE_MISMATCH;
    }

    /* Cross-validate header counters against what the schema decoded. */
    if (l_wire.signer_count != l_signer_count) {
        chipmunk_multi_signature_wire_release(&l_wire);
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_SIGNER_COUNT;
    }
    if (!l_wire.signers) {
        chipmunk_multi_signature_wire_release(&l_wire);
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_NULL;
    }
    for (uint32_t i = 0; i < l_signer_count; ++i) {
        if (l_wire.signers[i].path_length != l_path_length) {
            chipmunk_multi_signature_wire_release(&l_wire);
            return CHIPMUNK_MULTI_SIG_CODEC_ERR_PATH_LENGTH_MISMATCH;
        }
    }
    if (l_wire.is_randomized != 0 && l_wire.is_randomized != 1) {
        chipmunk_multi_signature_wire_release(&l_wire);
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_FLAG;
    }
    if (l_wire.reserved[0] != 0 || l_wire.reserved[1] != 0 || l_wire.reserved[2] != 0) {
        chipmunk_multi_signature_wire_release(&l_wire);
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_RESERVED;
    }

    /* Transfer ownership: from_wire moves the schema-allocated heap
     * blocks (signers[i].nodes etc.) into the runtime struct and zeroes
     * the wire mirror.  No further release needed on success. */
    if (chipmunk_multi_signature_from_wire(&l_wire, a_multi_sig) != 0) {
        chipmunk_multi_signature_wire_release(&l_wire);
        chipmunk_multi_signature_deep_free(a_multi_sig);
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_ALLOC;
    }
    return CHIPMUNK_MULTI_SIG_CODEC_OK;
}
