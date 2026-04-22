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
#include "dap_common.h"

#define LOG_TAG "chipmunk_multi_sig_codec"

/* ---------------------------------------------------------------------- *
 *  Low-level LE helpers                                                  *
 *                                                                        *
 *  The codec is byte-level canonical on purpose — we never rely on       *
 *  host byte order or struct layout so the wire representation is        *
 *  identical on every platform the DAP SDK ships to.                     *
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
 *  Polynomial encoders / decoders                                        *
 *                                                                        *
 *  We intentionally pass int32 coefficients through the wire as raw      *
 *  two's-complement LE bytes.  Range validation and modular reduction    *
 *  are cryptographic concerns handled by chipmunk_verify_multi_signature *
 *  — enforcing them here would either duplicate that logic or reject     *
 *  legitimately-produced signatures whose intermediate coefficients sit  *
 *  temporarily in non-canonical form.                                    *
 * ---------------------------------------------------------------------- */

static void s_write_poly_coeffs(uint8_t *a_dst, const int32_t *a_coeffs)
{
    for (int i = 0; i < CHIPMUNK_N; ++i) {
        s_write_u32(a_dst + (size_t)i * 4u, (uint32_t)a_coeffs[i]);
    }
}

static void s_read_poly_coeffs(int32_t *a_dst, const uint8_t *a_src)
{
    for (int i = 0; i < CHIPMUNK_N; ++i) {
        a_dst[i] = (int32_t)s_read_u32(a_src + (size_t)i * 4u);
    }
}

#define CHIPMUNK_CODEC_POLY_BYTES          ((size_t)CHIPMUNK_N * 4u)
#define CHIPMUNK_CODEC_HOTS_SIGMA_BYTES    (CHIPMUNK_CODEC_POLY_BYTES * (size_t)CHIPMUNK_GAMMA)
#define CHIPMUNK_CODEC_HOTS_PK_BYTES       (CHIPMUNK_CODEC_POLY_BYTES * 2u)
#define CHIPMUNK_CODEC_RHO_SEED_BYTES      32u
#define CHIPMUNK_CODEC_PATH_NODE_BYTES     (CHIPMUNK_CODEC_POLY_BYTES * 2u)
#define CHIPMUNK_CODEC_FIXED_BODY_BYTES    ( /* message_hash     */ 32u         \
                                           + /* hvc_hasher_seed  */ 32u         \
                                           + /* is_randomized+rsvd */ 4u        \
                                           + /* tree_root        */ CHIPMUNK_CODEC_POLY_BYTES \
                                           + /* aggregated_hots  */ CHIPMUNK_CODEC_HOTS_SIGMA_BYTES)

#define CHIPMUNK_CODEC_PER_SIGNER_HEADER   ( /* public_key_roots */ CHIPMUNK_CODEC_POLY_BYTES \
                                           + /* hots_pks.v0/v1   */ CHIPMUNK_CODEC_HOTS_PK_BYTES \
                                           + /* rho_seeds        */ CHIPMUNK_CODEC_RHO_SEED_BYTES \
                                           + /* leaf_indices     */ 4u            \
                                           + /* proof.index      */ 8u            \
                                           + /* proof.path_len   */ 4u)

/* ---------------------------------------------------------------------- *
 *  Deep-free                                                             *
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
 *  Size calculation + structural preflight                               *
 *                                                                        *
 *  We also use s_compute_size for the serialise path's preflight, so the *
 *  producer and the verifier reject the same set of malformed inputs.    *
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

static int s_compute_size(const chipmunk_multi_signature_t *a_multi_sig,
                          size_t *a_out_size,
                          uint32_t *a_out_path_length)
{
    uint32_t l_path_length = 0;
    int l_rc = s_preflight(a_multi_sig, &l_path_length);
    if (l_rc != CHIPMUNK_MULTI_SIG_CODEC_OK) {
        return l_rc;
    }

    const size_t l_per_signer_body = CHIPMUNK_CODEC_PER_SIGNER_HEADER
                                   + (size_t)l_path_length * CHIPMUNK_CODEC_PATH_NODE_BYTES;

    const size_t l_total = (size_t)CHIPMUNK_MULTI_SIG_HEADER_SIZE
                         + CHIPMUNK_CODEC_FIXED_BODY_BYTES
                         + a_multi_sig->signer_count * l_per_signer_body;

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
 *  Serialise                                                             *
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

    uint8_t *l_p = a_buffer;

    /* Header */
    l_p[0] = CHIPMUNK_MULTI_SIG_MAGIC0;
    l_p[1] = CHIPMUNK_MULTI_SIG_MAGIC1;
    l_p[2] = CHIPMUNK_MULTI_SIG_MAGIC2;
    l_p[3] = CHIPMUNK_MULTI_SIG_MAGIC3;
    l_p += 4;
    s_write_u16(l_p, CHIPMUNK_MULTI_SIG_VERSION_V1); l_p += 2;
    s_write_u16(l_p, 0);                             l_p += 2;
    s_write_u32(l_p, (uint32_t)a_multi_sig->signer_count); l_p += 4;
    s_write_u32(l_p, l_path_length);                 l_p += 4;
    s_write_u64(l_p, (uint64_t)l_total);             l_p += 8;

    /* Fixed body */
    memcpy(l_p, a_multi_sig->message_hash, 32);    l_p += 32;
    memcpy(l_p, a_multi_sig->hvc_hasher_seed, 32); l_p += 32;
    *l_p++ = a_multi_sig->aggregated_hots.is_randomized ? 1 : 0;
    l_p[0] = 0; l_p[1] = 0; l_p[2] = 0;            l_p += 3;  /* reserved */

    s_write_poly_coeffs(l_p, a_multi_sig->tree_root.coeffs);
    l_p += CHIPMUNK_CODEC_POLY_BYTES;

    for (int k = 0; k < CHIPMUNK_GAMMA; ++k) {
        s_write_poly_coeffs(l_p, a_multi_sig->aggregated_hots.sigma[k].coeffs);
        l_p += CHIPMUNK_CODEC_POLY_BYTES;
    }

    /* Per-signer records */
    for (size_t i = 0; i < a_multi_sig->signer_count; ++i) {
        s_write_poly_coeffs(l_p, a_multi_sig->public_key_roots[i].coeffs);
        l_p += CHIPMUNK_CODEC_POLY_BYTES;

        s_write_poly_coeffs(l_p, a_multi_sig->hots_pks[i].v0.coeffs);
        l_p += CHIPMUNK_CODEC_POLY_BYTES;
        s_write_poly_coeffs(l_p, a_multi_sig->hots_pks[i].v1.coeffs);
        l_p += CHIPMUNK_CODEC_POLY_BYTES;

        memcpy(l_p, a_multi_sig->rho_seeds[i], CHIPMUNK_CODEC_RHO_SEED_BYTES);
        l_p += CHIPMUNK_CODEC_RHO_SEED_BYTES;

        s_write_u32(l_p, a_multi_sig->leaf_indices[i]);
        l_p += 4;

        s_write_u64(l_p, (uint64_t)a_multi_sig->proofs[i].index);
        l_p += 8;
        s_write_u32(l_p, (uint32_t)a_multi_sig->proofs[i].path_length);
        l_p += 4;

        for (size_t j = 0; j < a_multi_sig->proofs[i].path_length; ++j) {
            s_write_poly_coeffs(l_p, a_multi_sig->proofs[i].nodes[j].left.coeffs);
            l_p += CHIPMUNK_CODEC_POLY_BYTES;
            s_write_poly_coeffs(l_p, a_multi_sig->proofs[i].nodes[j].right.coeffs);
            l_p += CHIPMUNK_CODEC_POLY_BYTES;
        }
    }

    const size_t l_actual = (size_t)(l_p - a_buffer);
    if (l_actual != l_total) {
        /* This is a programming error — the size calculation above must */
        /* match the serialise walk byte-for-byte.  Fail loudly so a     */
        /* mismatched layout surfaces in tests instead of producing a    */
        /* silently truncated blob.                                      */
        log_it(L_ERROR,
               "chipmunk_multi_signature_serialize: internal size "
               "mismatch (walked %zu, computed %zu) — blob rejected",
               l_actual, l_total);
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_SIZE_MISMATCH;
    }
    if (a_bytes_written) {
        *a_bytes_written = l_actual;
    }
    return CHIPMUNK_MULTI_SIG_CODEC_OK;
}

/* ---------------------------------------------------------------------- *
 *  Deserialise                                                           *
 *                                                                        *
 *  On every failure path we deep-free the partially populated struct so  *
 *  the caller sees a canonical, empty output.                            *
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

    if (a_buffer_size < (size_t)CHIPMUNK_MULTI_SIG_HEADER_SIZE) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_BUFFER_TOO_SMALL;
    }

    const uint8_t *l_p   = a_buffer;
    const uint8_t *l_end = a_buffer + a_buffer_size;

    if (l_p[0] != CHIPMUNK_MULTI_SIG_MAGIC0
        || l_p[1] != CHIPMUNK_MULTI_SIG_MAGIC1
        || l_p[2] != CHIPMUNK_MULTI_SIG_MAGIC2
        || l_p[3] != CHIPMUNK_MULTI_SIG_MAGIC3) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_MAGIC;
    }
    l_p += 4;

    const uint16_t l_version = s_read_u16(l_p); l_p += 2;
    if (l_version != CHIPMUNK_MULTI_SIG_VERSION_V1) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_VERSION;
    }
    const uint16_t l_reserved_hdr = s_read_u16(l_p); l_p += 2;
    if (l_reserved_hdr != 0) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_RESERVED;
    }

    const uint32_t l_signer_count = s_read_u32(l_p); l_p += 4;
    const uint32_t l_path_length  = s_read_u32(l_p); l_p += 4;
    const uint64_t l_payload_len  = s_read_u64(l_p); l_p += 8;

    if (l_signer_count == 0 || l_signer_count > (uint32_t)CHIPMUNK_MULTI_SIG_MAX_SIGNERS) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_SIGNER_COUNT;
    }
    if (l_path_length == 0 || l_path_length >= (uint32_t)CHIPMUNK_TREE_HEIGHT_MAX) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_PATH_LENGTH;
    }

    const size_t l_per_signer_body = CHIPMUNK_CODEC_PER_SIGNER_HEADER
                                   + (size_t)l_path_length * CHIPMUNK_CODEC_PATH_NODE_BYTES;
    const size_t l_expected = (size_t)CHIPMUNK_MULTI_SIG_HEADER_SIZE
                            + CHIPMUNK_CODEC_FIXED_BODY_BYTES
                            + (size_t)l_signer_count * l_per_signer_body;
    if ((size_t)l_payload_len != a_buffer_size
        || l_expected != a_buffer_size) {
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_SIZE_MISMATCH;
    }

    /* Allocate parallel arrays up front so every failure funnel can */
    /* safely call chipmunk_multi_signature_deep_free().             */
    a_multi_sig->public_key_roots = calloc(l_signer_count, sizeof(chipmunk_hvc_poly_t));
    a_multi_sig->hots_pks         = calloc(l_signer_count, sizeof(chipmunk_hots_public_key_t));
    a_multi_sig->rho_seeds        = calloc(l_signer_count, CHIPMUNK_CODEC_RHO_SEED_BYTES);
    a_multi_sig->proofs           = calloc(l_signer_count, sizeof(chipmunk_path_t));
    a_multi_sig->leaf_indices     = calloc(l_signer_count, sizeof(uint32_t));
    if (!a_multi_sig->public_key_roots || !a_multi_sig->hots_pks
        || !a_multi_sig->rho_seeds      || !a_multi_sig->proofs
        || !a_multi_sig->leaf_indices) {
        chipmunk_multi_signature_deep_free(a_multi_sig);
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_ALLOC;
    }
    a_multi_sig->signer_count = l_signer_count;

    /* Fixed body */
    memcpy(a_multi_sig->message_hash, l_p, 32);    l_p += 32;
    memcpy(a_multi_sig->hvc_hasher_seed, l_p, 32); l_p += 32;

    const uint8_t l_flag = *l_p++;
    if (l_flag != 0 && l_flag != 1) {
        chipmunk_multi_signature_deep_free(a_multi_sig);
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_FLAG;
    }
    a_multi_sig->aggregated_hots.is_randomized = (l_flag == 1);

    /* reserved tail of the flag byte */
    if (l_p[0] != 0 || l_p[1] != 0 || l_p[2] != 0) {
        chipmunk_multi_signature_deep_free(a_multi_sig);
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_BAD_RESERVED;
    }
    l_p += 3;

    s_read_poly_coeffs(a_multi_sig->tree_root.coeffs, l_p);
    l_p += CHIPMUNK_CODEC_POLY_BYTES;

    for (int k = 0; k < CHIPMUNK_GAMMA; ++k) {
        s_read_poly_coeffs(a_multi_sig->aggregated_hots.sigma[k].coeffs, l_p);
        l_p += CHIPMUNK_CODEC_POLY_BYTES;
    }

    /* Per-signer records */
    for (uint32_t i = 0; i < l_signer_count; ++i) {
        s_read_poly_coeffs(a_multi_sig->public_key_roots[i].coeffs, l_p);
        l_p += CHIPMUNK_CODEC_POLY_BYTES;

        s_read_poly_coeffs(a_multi_sig->hots_pks[i].v0.coeffs, l_p);
        l_p += CHIPMUNK_CODEC_POLY_BYTES;
        s_read_poly_coeffs(a_multi_sig->hots_pks[i].v1.coeffs, l_p);
        l_p += CHIPMUNK_CODEC_POLY_BYTES;

        memcpy(a_multi_sig->rho_seeds[i], l_p, CHIPMUNK_CODEC_RHO_SEED_BYTES);
        l_p += CHIPMUNK_CODEC_RHO_SEED_BYTES;

        a_multi_sig->leaf_indices[i] = s_read_u32(l_p);
        l_p += 4;

        const uint64_t l_proof_index  = s_read_u64(l_p); l_p += 8;
        const uint32_t l_proof_length = s_read_u32(l_p); l_p += 4;
        if (l_proof_length != l_path_length) {
            chipmunk_multi_signature_deep_free(a_multi_sig);
            return CHIPMUNK_MULTI_SIG_CODEC_ERR_PATH_LENGTH_MISMATCH;
        }

        a_multi_sig->proofs[i].index       = (size_t)l_proof_index;
        a_multi_sig->proofs[i].path_length = (size_t)l_path_length;
        a_multi_sig->proofs[i].nodes       = calloc((size_t)l_path_length,
                                                    sizeof(chipmunk_path_node_t));
        if (!a_multi_sig->proofs[i].nodes) {
            chipmunk_multi_signature_deep_free(a_multi_sig);
            return CHIPMUNK_MULTI_SIG_CODEC_ERR_ALLOC;
        }

        for (uint32_t j = 0; j < l_path_length; ++j) {
            s_read_poly_coeffs(a_multi_sig->proofs[i].nodes[j].left.coeffs, l_p);
            l_p += CHIPMUNK_CODEC_POLY_BYTES;
            s_read_poly_coeffs(a_multi_sig->proofs[i].nodes[j].right.coeffs, l_p);
            l_p += CHIPMUNK_CODEC_POLY_BYTES;
        }
    }

    if (l_p != l_end) {
        /* Trailing bytes — reject: either the producer is buggy or the */
        /* blob has been tampered with.                                 */
        chipmunk_multi_signature_deep_free(a_multi_sig);
        return CHIPMUNK_MULTI_SIG_CODEC_ERR_SIZE_MISMATCH;
    }

    return CHIPMUNK_MULTI_SIG_CODEC_OK;
}
