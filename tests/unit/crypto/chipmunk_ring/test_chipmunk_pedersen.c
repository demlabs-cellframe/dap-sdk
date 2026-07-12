/*
 * test_chipmunk_pedersen.c — Lattice-based Pedersen commitment tests.
 *
 * Tests: init, commit/verify, additive homomorphism, different values.
 * All params heap-allocated to avoid stack overflow (73KB struct).
 */

#include <dap_common.h>
#include <dap_test.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "sig/chipmunk/chipmunk_pedersen.h"

#define LOG_TAG "test_chipmunk_pedersen"

static void s_u64_to_amount_bytes(uint64_t a_val, uint8_t a_out[CHIPMUNK_PEDERSEN_VALUE_BYTES])
{
    memset(a_out, 0, CHIPMUNK_PEDERSEN_VALUE_BYTES);
    memcpy(a_out, &a_val, sizeof(a_val));
}

static void test_init(void)
{
    chipmunk_pedersen_params_t *l_params = DAP_NEW_Z(chipmunk_pedersen_params_t);
    dap_assert(l_params != NULL, "params alloc OK");

    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x42 + i;

    int l_rc = chipmunk_pedersen_init(l_params, l_seed);
    dap_assert(l_rc == 0, "Pedersen init OK");
    dap_assert(l_params->initialized, "params initialized");
    DAP_DELETE(l_params);
}

static void test_commit_verify(void)
{
    chipmunk_pedersen_params_t *l_params = DAP_NEW_Z(chipmunk_pedersen_params_t);
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x42 + i;
    chipmunk_pedersen_init(l_params, l_seed);

    chipmunk_pedersen_commit_t l_commit;
    uint8_t l_rand[32];
    for (int i = 0; i < 32; ++i) l_rand[i] = 0xAA + i;

    int64_t l_value = 1000000;
    uint8_t l_value_bytes[CHIPMUNK_PEDERSEN_VALUE_BYTES];
    s_u64_to_amount_bytes((uint64_t)l_value, l_value_bytes);
    int l_rc = chipmunk_pedersen_commit(&l_commit, l_params, l_value_bytes, l_rand);
    dap_assert(l_rc == 0, "commit OK");

    int l_nonzero = 0;
    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K; ++i) {
        for (uint32_t j = 0; j < CHIPMUNK_N; ++j) {
            if (l_commit.C[i].coeffs[j] != 0) { l_nonzero = 1; break; }
        }
        if (l_nonzero) break;
    }
    dap_assert(l_nonzero, "commitment non-zero");
    DAP_DELETE(l_params);
}

static void test_different_values(void)
{
    chipmunk_pedersen_params_t *l_params = DAP_NEW_Z(chipmunk_pedersen_params_t);
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x42 + i;
    chipmunk_pedersen_init(l_params, l_seed);

    chipmunk_pedersen_commit_t l_c1, l_c2;
    uint8_t l_r1[32], l_r2[32];
    for (int i = 0; i < 32; ++i) { l_r1[i] = 0xAA; l_r2[i] = 0xBB; }

    uint8_t l_v100[CHIPMUNK_PEDERSEN_VALUE_BYTES], l_v200[CHIPMUNK_PEDERSEN_VALUE_BYTES];
    s_u64_to_amount_bytes(100, l_v100);
    s_u64_to_amount_bytes(200, l_v200);
    chipmunk_pedersen_commit(&l_c1, l_params, l_v100, l_r1);
    chipmunk_pedersen_commit(&l_c2, l_params, l_v200, l_r2);

    int l_diff = 0;
    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K && !l_diff; ++i) {
        for (uint32_t j = 0; j < CHIPMUNK_N && !l_diff; ++j) {
            if (l_c1.C[i].coeffs[j] != l_c2.C[i].coeffs[j]) l_diff = 1;
        }
    }
    dap_assert(l_diff, "different values → different commitments");
    DAP_DELETE(l_params);
}

static void test_same_value_same_randomness(void)
{
    chipmunk_pedersen_params_t *l_params = DAP_NEW_Z(chipmunk_pedersen_params_t);
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x42 + i;
    chipmunk_pedersen_init(l_params, l_seed);

    chipmunk_pedersen_commit_t l_c1, l_c2;
    uint8_t l_rand[32];
    for (int i = 0; i < 32; ++i) l_rand[i] = 0xAA;

    uint8_t l_v100[CHIPMUNK_PEDERSEN_VALUE_BYTES];
    s_u64_to_amount_bytes(100, l_v100);
    chipmunk_pedersen_commit(&l_c1, l_params, l_v100, l_rand);
    chipmunk_pedersen_commit(&l_c2, l_params, l_v100, l_rand);

    int l_same = 1;
    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K && l_same; ++i) {
        for (uint32_t j = 0; j < CHIPMUNK_N && l_same; ++j) {
            if (l_c1.C[i].coeffs[j] != l_c2.C[i].coeffs[j]) l_same = 0;
        }
    }
    dap_assert(l_same, "same value + same randomness → same commitment");
    DAP_DELETE(l_params);
}

static void test_additive_homomorphism(void)
{
    chipmunk_pedersen_params_t *l_params = DAP_NEW_Z(chipmunk_pedersen_params_t);
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x42 + i;
    chipmunk_pedersen_init(l_params, l_seed);

    chipmunk_pedersen_commit_t l_c1, l_c2, l_sum;
    uint8_t l_r1[32], l_r2[32];
    for (int i = 0; i < 32; ++i) { l_r1[i] = 0xAA; l_r2[i] = 0xBB; }

    uint8_t l_v100[CHIPMUNK_PEDERSEN_VALUE_BYTES], l_v200[CHIPMUNK_PEDERSEN_VALUE_BYTES];
    s_u64_to_amount_bytes(100, l_v100);
    s_u64_to_amount_bytes(200, l_v200);
    chipmunk_pedersen_commit(&l_c1, l_params, l_v100, l_r1);
    chipmunk_pedersen_commit(&l_c2, l_params, l_v200, l_r2);
    chipmunk_pedersen_add(&l_sum, &l_c1, &l_c2);

    int l_nonzero = 0;
    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K && !l_nonzero; ++i) {
        for (uint32_t j = 0; j < CHIPMUNK_N && !l_nonzero; ++j) {
            if (l_sum.C[i].coeffs[j] != 0) l_nonzero = 1;
        }
    }
    dap_assert(l_nonzero, "additive homomorphism: sum non-zero");

    int l_diff_from_c1 = 0;
    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K && !l_diff_from_c1; ++i) {
        for (uint32_t j = 0; j < CHIPMUNK_N && !l_diff_from_c1; ++j) {
            if (l_sum.C[i].coeffs[j] != l_c1.C[i].coeffs[j]) l_diff_from_c1 = 1;
        }
    }
    dap_assert(l_diff_from_c1, "sum differs from C(100)");
    DAP_DELETE(l_params);
}

static void test_serialize_deserialize(void)
{
    chipmunk_pedersen_params_t *l_params = DAP_NEW_Z(chipmunk_pedersen_params_t);
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x42 + i;
    chipmunk_pedersen_init(l_params, l_seed);

    chipmunk_pedersen_commit_t l_commit;
    uint8_t l_rand[32];
    for (int i = 0; i < 32; ++i) l_rand[i] = 0xAA;
    uint8_t l_v42[CHIPMUNK_PEDERSEN_VALUE_BYTES];
    s_u64_to_amount_bytes(42, l_v42);
    chipmunk_pedersen_commit(&l_commit, l_params, l_v42, l_rand);

    /* Serialize — buffer must be K * N * 4 bytes */
    size_t l_ser_size = CHIPMUNK_PEDERSEN_K * CHIPMUNK_N * sizeof(int32_t);
    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, l_ser_size);
    dap_assert(l_buf != NULL, "serialize buffer alloc OK");
    int l_rc = chipmunk_pedersen_commit_serialize(l_buf, l_ser_size, &l_commit);
    dap_assert(l_rc == 0, "serialize OK");

    chipmunk_pedersen_commit_t l_commit2;
    l_rc = chipmunk_pedersen_commit_deserialize(&l_commit2, l_buf, l_ser_size);
    dap_assert(l_rc == 0, "deserialize OK");

    int l_match = 1;
    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K && l_match; ++i) {
        for (uint32_t j = 0; j < CHIPMUNK_N && l_match; ++j) {
            if (l_commit.C[i].coeffs[j] != l_commit2.C[i].coeffs[j]) l_match = 0;
        }
    }
    dap_assert(l_match, "serialize/deserialize round-trip");
    DAP_DELETE(l_buf);
    DAP_DELETE(l_params);
}

int main(void)
{
    dap_set_appname("test_chipmunk_pedersen");
    dap_common_init("test_chipmunk_pedersen", NULL);

    test_init();
    test_commit_verify();
    test_different_values();
    test_same_value_same_randomness();
    test_additive_homomorphism();
    test_serialize_deserialize();

    log_it(L_INFO, "=== ALL Chipmunk Pedersen tests PASSED ===");
    dap_common_deinit();
    return 0;
}
