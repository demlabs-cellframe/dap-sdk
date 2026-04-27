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

    /* Decode back into a fresh wire mirror.  The schema engine allocates
     * the dynamic arrays and the per-signer node buffers. */
    dap_deserialize_result_t l_des = dap_deserialize_from_buffer_raw(
            &chipmunk_multi_signature_wire_schema, l_buf, l_size,
            &l_decoded, NULL);
    TEST_ASSERT(l_des.error_code == DAP_SERIALIZE_ERROR_SUCCESS, "from_buffer_raw");
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
    log_it(L_INFO, "All chipmunk multi-signature schema round-trip tests passed!");
    return 0;
}
