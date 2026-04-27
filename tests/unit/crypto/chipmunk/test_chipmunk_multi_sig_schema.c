/*
 * Smoke / round-trip test for the schema-driven wire layer introduced in
 * chipmunk_multi_signature_serialize_schema.{h,c}.
 *
 * The full codec migration lives in the next commit — for now we just
 * verify that the schemas correctly describe every wire element by:
 *   1. building a synthetic chipmunk_multi_signature_t with known data,
 *   2. converting it to its wire mirror,
 *   3. running dap_serialize_to_buffer_raw + from_buffer_raw,
 *   4. converting the decoded mirror back to runtime form,
 *   5. asserting bitwise equality with the original.
 *
 * This catches schema mistakes (offset / size / count_offset / element_type
 * typos) before we hook the schema into the production codec.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "dap_common.h"
#include "dap_serialize.h"
#include "dap_test.h"

#include "chipmunk/chipmunk.h"
#include "chipmunk/chipmunk_tree.h"
#include "chipmunk/chipmunk_aggregation.h"
#include "chipmunk/chipmunk_multi_signature_codec.h"
#include "chipmunk/chipmunk_multi_signature_serialize_schema.h"

#define LOG_TAG "test_chipmunk_multi_sig_schema"

#define TEST_ASSERT(cond, msg)                                              \
    do {                                                                    \
        if (!(cond)) {                                                      \
            log_it(L_ERROR, "[FAIL] %s:%d: %s  (expr: %s)",                 \
                   __FILE__, __LINE__, (msg), #cond);                       \
            return false;                                                   \
        }                                                                   \
    } while (0)

/* ---------------------------------------------------------------------- *
 *  Helpers                                                               *
 * ---------------------------------------------------------------------- */

static void s_fill_poly(chipmunk_poly_t *a_poly, int32_t a_seed)
{
    for (int i = 0; i < CHIPMUNK_N; ++i) {
        a_poly->coeffs[i] = a_seed * 1000 + i - (CHIPMUNK_N / 2);
    }
}

static void s_fill_hvc_poly(chipmunk_hvc_poly_t *a_poly, int32_t a_seed)
{
    for (int i = 0; i < CHIPMUNK_N; ++i) {
        a_poly->coeffs[i] = a_seed * 7919 + i;
    }
}

static int s_alloc_runtime(chipmunk_multi_signature_t *a_out,
                           size_t a_signer_count,
                           size_t a_path_length)
{
    memset(a_out, 0, sizeof(*a_out));
    a_out->signer_count = a_signer_count;

    a_out->public_key_roots = DAP_NEW_Z_COUNT(chipmunk_hvc_poly_t,        a_signer_count);
    a_out->hots_pks         = DAP_NEW_Z_COUNT(chipmunk_hots_public_key_t, a_signer_count);
    a_out->rho_seeds        = DAP_NEW_Z_SIZE(uint8_t (*)[32],
                                              a_signer_count * 32);
    a_out->leaf_indices     = DAP_NEW_Z_COUNT(uint32_t,                   a_signer_count);
    a_out->proofs           = DAP_NEW_Z_COUNT(chipmunk_path_t,            a_signer_count);
    if (!a_out->public_key_roots || !a_out->hots_pks
        || !a_out->rho_seeds || !a_out->leaf_indices || !a_out->proofs) {
        return -1;
    }

    for (size_t i = 0; i < a_signer_count; ++i) {
        s_fill_hvc_poly(&a_out->public_key_roots[i], (int32_t)(i + 11));
        s_fill_poly(&a_out->hots_pks[i].v0, (int32_t)(i + 21));
        s_fill_poly(&a_out->hots_pks[i].v1, (int32_t)(i + 31));
        for (int b = 0; b < 32; ++b) {
            a_out->rho_seeds[i][b] = (uint8_t)((i * 37 + b) & 0xFF);
        }
        a_out->leaf_indices[i] = (uint32_t)(0xDEAD0000u + i);

        a_out->proofs[i].path_length = a_path_length;
        a_out->proofs[i].index       = i + 1;
        a_out->proofs[i].nodes       = DAP_NEW_Z_COUNT(chipmunk_path_node_t, a_path_length);
        if (!a_out->proofs[i].nodes) {
            return -2;
        }
        for (size_t j = 0; j < a_path_length; ++j) {
            s_fill_hvc_poly(&a_out->proofs[i].nodes[j].left,  (int32_t)(i * 100 + j * 2 + 1));
            s_fill_hvc_poly(&a_out->proofs[i].nodes[j].right, (int32_t)(i * 100 + j * 2 + 2));
        }
    }

    a_out->aggregated_hots.is_randomized = true;
    for (int g = 0; g < CHIPMUNK_GAMMA; ++g) {
        s_fill_poly(&a_out->aggregated_hots.sigma[g], (int32_t)(g + 41));
    }
    s_fill_hvc_poly(&a_out->tree_root, 51);
    for (int b = 0; b < 32; ++b) {
        a_out->message_hash[b]    = (uint8_t)(0xA0 + b);
        a_out->hvc_hasher_seed[b] = (uint8_t)(0xB0 + b);
    }
    return 0;
}

static void s_free_runtime(chipmunk_multi_signature_t *a_runtime)
{
    if (!a_runtime) return;
    if (a_runtime->proofs) {
        for (size_t i = 0; i < a_runtime->signer_count; ++i) {
            DAP_DEL_Z(a_runtime->proofs[i].nodes);
        }
        DAP_DEL_Z(a_runtime->proofs);
    }
    DAP_DEL_Z(a_runtime->public_key_roots);
    DAP_DEL_Z(a_runtime->hots_pks);
    DAP_DEL_Z(a_runtime->rho_seeds);
    DAP_DEL_Z(a_runtime->leaf_indices);
    memset(a_runtime, 0, sizeof(*a_runtime));
}

static bool s_runtime_equal(const chipmunk_multi_signature_t *a, const chipmunk_multi_signature_t *b)
{
    if (a->signer_count != b->signer_count) return false;
    if (memcmp(a->message_hash,    b->message_hash,    32) != 0) return false;
    if (memcmp(a->hvc_hasher_seed, b->hvc_hasher_seed, 32) != 0) return false;
    if (memcmp(&a->tree_root,      &b->tree_root,      sizeof(chipmunk_hvc_poly_t)) != 0) return false;
    if (a->aggregated_hots.is_randomized != b->aggregated_hots.is_randomized) return false;
    if (memcmp(a->aggregated_hots.sigma, b->aggregated_hots.sigma,
               sizeof(chipmunk_poly_t) * (size_t)CHIPMUNK_GAMMA) != 0) return false;
    for (size_t i = 0; i < a->signer_count; ++i) {
        if (memcmp(&a->public_key_roots[i], &b->public_key_roots[i], sizeof(chipmunk_hvc_poly_t)) != 0) return false;
        if (memcmp(&a->hots_pks[i],         &b->hots_pks[i],         sizeof(chipmunk_hots_pk_t))   != 0) return false;
        if (memcmp(a->rho_seeds[i],         b->rho_seeds[i],         32) != 0) return false;
        if (a->leaf_indices[i] != b->leaf_indices[i]) return false;
        if (a->proofs[i].path_length != b->proofs[i].path_length)    return false;
        if (a->proofs[i].index       != b->proofs[i].index)          return false;
        for (size_t j = 0; j < a->proofs[i].path_length; ++j) {
            if (memcmp(&a->proofs[i].nodes[j], &b->proofs[i].nodes[j],
                       sizeof(chipmunk_path_node_t)) != 0) {
                return false;
            }
        }
    }
    return true;
}

/* ---------------------------------------------------------------------- *
 *  Round-trip test                                                       *
 * ---------------------------------------------------------------------- */

static bool test_schema_roundtrip(size_t a_signer_count, size_t a_path_length)
{
    chipmunk_multi_signature_t l_orig = {0};
    chipmunk_multi_signature_t l_rt   = {0};
    chipmunk_multi_signature_wire_t l_wire = {0};
    chipmunk_multi_signature_wire_t l_decoded = {0};
    uint8_t *l_buf = NULL;
    bool l_ok = false;

    TEST_ASSERT(s_alloc_runtime(&l_orig, a_signer_count, a_path_length) == 0,
                "alloc runtime");

    /* Runtime → wire mirror (shallow). */
    TEST_ASSERT(chipmunk_multi_signature_to_wire(&l_orig, &l_wire) == 0,
                "to_wire");

    /* Compute size + serialise raw (no built-in 12-byte header). */
    const size_t l_size = dap_serialize_calc_size_raw(&chipmunk_multi_signature_wire_schema,
                                                      NULL, &l_wire, NULL);
    TEST_ASSERT(l_size > 0, "calc_size_raw");

    l_buf = DAP_NEW_Z_SIZE(uint8_t, l_size);
    TEST_ASSERT(l_buf != NULL, "alloc buffer");

    dap_serialize_result_t l_ser = dap_serialize_to_buffer_raw(
            &chipmunk_multi_signature_wire_schema, &l_wire,
            l_buf, l_size, NULL);
    TEST_ASSERT(l_ser.error_code == DAP_SERIALIZE_ERROR_SUCCESS, "to_buffer_raw");
    TEST_ASSERT(l_ser.bytes_written == l_size, "bytes_written matches");

    /* Decode back into a fresh wire mirror.  The top-level signers[]
     * array uses DAP_SERIALIZE_FLAG_NO_COUNT_PREFIX (the count is
     * carried out-of-band, by the codec's CHMA header in production),
     * so the test must pre-populate the count slot and use the
     * _preserve variant that skips the implicit memset(0). */
    l_decoded.signer_count = (uint32_t)a_signer_count;
    dap_deserialize_result_t l_des = dap_deserialize_from_buffer_raw_preserve(
            &chipmunk_multi_signature_wire_schema, l_buf, l_size,
            &l_decoded, NULL);
    TEST_ASSERT(l_des.error_code == DAP_SERIALIZE_ERROR_SUCCESS, "from_buffer_raw_preserve");
    TEST_ASSERT(l_des.bytes_read == l_size, "bytes_read matches");

    /* Wire mirror → runtime struct (transfers pointer ownership). */
    TEST_ASSERT(chipmunk_multi_signature_from_wire(&l_decoded, &l_rt) == 0,
                "from_wire");

    TEST_ASSERT(s_runtime_equal(&l_orig, &l_rt), "round-trip equality");

    l_ok = true;

    /* Cleanup. */
    chipmunk_multi_signature_wire_release(&l_wire);
    /* l_decoded was zeroed by from_wire on success. */
    s_free_runtime(&l_rt);
    s_free_runtime(&l_orig);
    DAP_DEL_Z(l_buf);
    return l_ok;
}

/* ---------------------------------------------------------------------- *
 *  Reference v1 hand-rolled writer                                        *
 *                                                                         *
 *  This routine is the *source of truth* for the legacy v1 byte layout    *
 *  that the schema-driven encoder must reproduce exactly.  It writes      *
 *  ONLY the body (no CHMA header) so it can be diffed against the         *
 *  schema's raw output byte-for-byte.                                     *
 * ---------------------------------------------------------------------- */

static inline void s_w32(uint8_t *a_dst, uint32_t a_v)
{
    a_dst[0] = (uint8_t)(a_v & 0xFF);
    a_dst[1] = (uint8_t)((a_v >> 8)  & 0xFF);
    a_dst[2] = (uint8_t)((a_v >> 16) & 0xFF);
    a_dst[3] = (uint8_t)((a_v >> 24) & 0xFF);
}
static inline void s_w64(uint8_t *a_dst, uint64_t a_v)
{
    for (unsigned i = 0; i < 8; ++i) {
        a_dst[i] = (uint8_t)((a_v >> (8u * i)) & 0xFF);
    }
}
static void s_write_poly(uint8_t *a_dst, const int32_t *a_coeffs)
{
    for (int i = 0; i < CHIPMUNK_N; ++i) {
        s_w32(a_dst + (size_t)i * 4u, (uint32_t)a_coeffs[i]);
    }
}

#define POLY_BYTES        ((size_t)CHIPMUNK_N * 4u)
#define HOTS_PK_BYTES     (POLY_BYTES * 2u)
#define HOTS_SIGMA_BYTES  (POLY_BYTES * (size_t)CHIPMUNK_GAMMA)
#define PATH_NODE_BYTES   (POLY_BYTES * 2u)
#define FIXED_BODY_BYTES  (32u + 32u + 4u + POLY_BYTES + HOTS_SIGMA_BYTES)
#define PER_SIGNER_HEADER (POLY_BYTES + HOTS_PK_BYTES + 32u + 4u + 8u + 4u)

static size_t s_legacy_v1_body_size(const chipmunk_multi_signature_t *a_ms)
{
    const size_t l_per = PER_SIGNER_HEADER
                       + a_ms->proofs[0].path_length * PATH_NODE_BYTES;
    return FIXED_BODY_BYTES + a_ms->signer_count * l_per;
}

static void s_legacy_v1_write_body(const chipmunk_multi_signature_t *a_ms,
                                    uint8_t *a_buf)
{
    uint8_t *l_p = a_buf;

    memcpy(l_p, a_ms->message_hash, 32);    l_p += 32;
    memcpy(l_p, a_ms->hvc_hasher_seed, 32); l_p += 32;
    *l_p++ = a_ms->aggregated_hots.is_randomized ? 1 : 0;
    l_p[0] = 0; l_p[1] = 0; l_p[2] = 0;     l_p += 3;

    s_write_poly(l_p, a_ms->tree_root.coeffs);
    l_p += POLY_BYTES;

    for (int k = 0; k < CHIPMUNK_GAMMA; ++k) {
        s_write_poly(l_p, a_ms->aggregated_hots.sigma[k].coeffs);
        l_p += POLY_BYTES;
    }

    for (size_t i = 0; i < a_ms->signer_count; ++i) {
        s_write_poly(l_p, a_ms->public_key_roots[i].coeffs);
        l_p += POLY_BYTES;

        s_write_poly(l_p, a_ms->hots_pks[i].v0.coeffs);
        l_p += POLY_BYTES;
        s_write_poly(l_p, a_ms->hots_pks[i].v1.coeffs);
        l_p += POLY_BYTES;

        memcpy(l_p, a_ms->rho_seeds[i], 32);
        l_p += 32;

        s_w32(l_p, a_ms->leaf_indices[i]);
        l_p += 4;

        s_w64(l_p, (uint64_t)a_ms->proofs[i].index);
        l_p += 8;
        s_w32(l_p, (uint32_t)a_ms->proofs[i].path_length);
        l_p += 4;

        for (size_t j = 0; j < a_ms->proofs[i].path_length; ++j) {
            s_write_poly(l_p, a_ms->proofs[i].nodes[j].left.coeffs);
            l_p += POLY_BYTES;
            s_write_poly(l_p, a_ms->proofs[i].nodes[j].right.coeffs);
            l_p += POLY_BYTES;
        }
    }
}

/* ---------------------------------------------------------------------- *
 *  Bit-exact equality test: schema body must equal legacy v1 body         *
 * ---------------------------------------------------------------------- */

static bool test_schema_matches_v1_layout(size_t a_signer_count, size_t a_path_length)
{
    chipmunk_multi_signature_t l_orig = {0};
    chipmunk_multi_signature_wire_t l_wire = {0};
    uint8_t *l_schema_body = NULL;
    uint8_t *l_legacy_body = NULL;
    bool l_ok = false;

    TEST_ASSERT(s_alloc_runtime(&l_orig, a_signer_count, a_path_length) == 0,
                "alloc runtime");

    /* Reference v1 body. */
    const size_t l_legacy_size = s_legacy_v1_body_size(&l_orig);
    l_legacy_body = DAP_NEW_Z_SIZE(uint8_t, l_legacy_size);
    TEST_ASSERT(l_legacy_body != NULL, "alloc legacy buf");
    s_legacy_v1_write_body(&l_orig, l_legacy_body);

    /* Schema-driven body. */
    TEST_ASSERT(chipmunk_multi_signature_to_wire(&l_orig, &l_wire) == 0,
                "to_wire");
    const size_t l_schema_size = dap_serialize_calc_size_raw(
            &chipmunk_multi_signature_wire_schema, NULL, &l_wire, NULL);
    TEST_ASSERT(l_schema_size == l_legacy_size,
                "schema size matches legacy v1 size");

    l_schema_body = DAP_NEW_Z_SIZE(uint8_t, l_schema_size);
    TEST_ASSERT(l_schema_body != NULL, "alloc schema buf");

    dap_serialize_result_t l_ser = dap_serialize_to_buffer_raw(
            &chipmunk_multi_signature_wire_schema, &l_wire,
            l_schema_body, l_schema_size, NULL);
    TEST_ASSERT(l_ser.error_code == DAP_SERIALIZE_ERROR_SUCCESS, "to_buffer_raw");
    TEST_ASSERT(l_ser.bytes_written == l_schema_size, "bytes_written matches");

    if (memcmp(l_legacy_body, l_schema_body, l_schema_size) != 0) {
        /* Pinpoint the first divergence to make schema bugs trivial to debug. */
        size_t l_diff = 0;
        while (l_diff < l_schema_size && l_legacy_body[l_diff] == l_schema_body[l_diff]) {
            ++l_diff;
        }
        log_it(L_ERROR,
               "Schema/v1 byte mismatch at offset %zu: legacy=0x%02x schema=0x%02x",
               l_diff, l_legacy_body[l_diff], l_schema_body[l_diff]);
        TEST_ASSERT(false, "schema body == legacy v1 body");
    }
    l_ok = true;

    chipmunk_multi_signature_wire_release(&l_wire);
    s_free_runtime(&l_orig);
    DAP_DEL_Z(l_schema_body);
    DAP_DEL_Z(l_legacy_body);
    return l_ok;
}

/* ---------------------------------------------------------------------- *
 *  Codec round-trip via the public API                                    *
 * ---------------------------------------------------------------------- */

static bool test_codec_roundtrip(size_t a_signer_count, size_t a_path_length)
{
    chipmunk_multi_signature_t l_orig = {0};
    chipmunk_multi_signature_t l_rt   = {0};
    uint8_t *l_buf = NULL;
    bool l_ok = false;

    TEST_ASSERT(s_alloc_runtime(&l_orig, a_signer_count, a_path_length) == 0,
                "alloc runtime");

    size_t l_size = 0;
    TEST_ASSERT(chipmunk_multi_signature_serialized_size(&l_orig, &l_size)
                == CHIPMUNK_MULTI_SIG_CODEC_OK, "serialized_size");
    TEST_ASSERT(l_size > CHIPMUNK_MULTI_SIG_HEADER_SIZE,
                "size includes header + body");

    l_buf = DAP_NEW_Z_SIZE(uint8_t, l_size);
    TEST_ASSERT(l_buf != NULL, "alloc buf");

    size_t l_written = 0;
    TEST_ASSERT(chipmunk_multi_signature_serialize(&l_orig, l_buf, l_size, &l_written)
                == CHIPMUNK_MULTI_SIG_CODEC_OK, "serialize");
    TEST_ASSERT(l_written == l_size, "wrote full size");

    TEST_ASSERT(chipmunk_multi_signature_deserialize(l_buf, l_size, &l_rt)
                == CHIPMUNK_MULTI_SIG_CODEC_OK, "deserialize");
    TEST_ASSERT(s_runtime_equal(&l_orig, &l_rt), "codec round-trip equality");

    l_ok = true;
    chipmunk_multi_signature_deep_free(&l_rt);
    s_free_runtime(&l_orig);
    DAP_DEL_Z(l_buf);
    return l_ok;
}

/* ---------------------------------------------------------------------- *
 *  Main                                                                  *
 * ---------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    dap_log_level_set(L_DEBUG);

    log_it(L_INFO, "Testing chipmunk multi-signature schema round-trip (1 signer, path=4)...");
    if (!test_schema_roundtrip(1, 4)) {
        log_it(L_ERROR, "FAIL: 1 signer / path 4");
        return 1;
    }
    log_it(L_INFO, "Testing chipmunk multi-signature schema round-trip (3 signers, path=8)...");
    if (!test_schema_roundtrip(3, 8)) {
        log_it(L_ERROR, "FAIL: 3 signers / path 8");
        return 1;
    }
    log_it(L_INFO, "Testing chipmunk multi-signature schema round-trip (10 signers, path=15)...");
    if (!test_schema_roundtrip(10, 15)) {
        log_it(L_ERROR, "FAIL: 10 signers / path 15");
        return 1;
    }

    /* Bit-exact comparison vs. legacy v1 hand-rolled writer. */
    log_it(L_INFO, "Testing schema body bit-exact match vs legacy v1 layout (1/4)...");
    if (!test_schema_matches_v1_layout(1, 4)) {
        log_it(L_ERROR, "FAIL: schema vs legacy v1 (1/4)");
        return 1;
    }
    log_it(L_INFO, "Testing schema body bit-exact match vs legacy v1 layout (3/8)...");
    if (!test_schema_matches_v1_layout(3, 8)) {
        log_it(L_ERROR, "FAIL: schema vs legacy v1 (3/8)");
        return 1;
    }
    log_it(L_INFO, "Testing schema body bit-exact match vs legacy v1 layout (10/15)...");
    if (!test_schema_matches_v1_layout(10, 15)) {
        log_it(L_ERROR, "FAIL: schema vs legacy v1 (10/15)");
        return 1;
    }

    /* Public codec round-trip. */
    log_it(L_INFO, "Testing chipmunk_multi_signature codec round-trip (1/4)...");
    if (!test_codec_roundtrip(1, 4)) {
        log_it(L_ERROR, "FAIL: codec round-trip (1/4)");
        return 1;
    }
    log_it(L_INFO, "Testing chipmunk_multi_signature codec round-trip (3/8)...");
    if (!test_codec_roundtrip(3, 8)) {
        log_it(L_ERROR, "FAIL: codec round-trip (3/8)");
        return 1;
    }
    log_it(L_INFO, "Testing chipmunk_multi_signature codec round-trip (10/15)...");
    if (!test_codec_roundtrip(10, 15)) {
        log_it(L_ERROR, "FAIL: codec round-trip (10/15)");
        return 1;
    }

    log_it(L_INFO, "All chipmunk multi-signature schema/codec tests passed!");
    return 0;
}
