/*
 * test_chipmunk_pedersen.c — Lattice-based Pedersen commitment tests.
 *
 * Tests: init, commit/verify, additive homomorphism, different values,
 * conservation property (Phase 2: homomorphic base-256 encoding).
 * All params heap-allocated to avoid stack overflow (73KB struct).
 */

#include <dap_common.h>
#include <dap_enc.h>
#include <dap_test.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "sig/chipmunk/chipmunk_pedersen.h"
#include "sig/chipmunk/chipmunk_poly.h"

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

/* Phase 6: Verify the Z-linear (scalar) conservation property.
 * With scalar encoding, encode(v1) + encode(v2) = encode(v1+v2) in R_q
 * because all coefficients are (v mod Q). This is Z-linear.
 *
 * Tests: C(v1) + C(v2) + ... == C(v1 + v2 + ...) with combined randomness.
 * This is what the ledger conservation check relies on:
 *   C_input == Σ C_output_i
 */
static void test_conservation_property(void)
{
    chipmunk_pedersen_params_t *l_params = DAP_NEW_Z(chipmunk_pedersen_params_t);
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x42 + i;
    chipmunk_pedersen_init(l_params, l_seed);

    /* Values: input = 300, outputs = 100 + 200 */
    uint8_t l_v300[CHIPMUNK_PEDERSEN_VALUE_BYTES];
    uint8_t l_v100[CHIPMUNK_PEDERSEN_VALUE_BYTES];
    uint8_t l_v200[CHIPMUNK_PEDERSEN_VALUE_BYTES];
    s_u64_to_amount_bytes(300, l_v300);
    s_u64_to_amount_bytes(100, l_v100);
    s_u64_to_amount_bytes(200, l_v200);

    /* Commit to input and outputs with SAME randomness.
     * Conservation: C(v1+v2) == C(v1) + C(v2) when randomness cancels:
     *   A*r1 + encode(v1) + A*r2 + encode(v2) = A*(r1+r2) + encode(v1+v2)
     * But with different randomness, we need r_combined = r1 + r2.
     * We test with shared randomness for simplicity:
     *   C(input) = A*r + encode(300)
     *   C(out1) + C(out2) = A*r + encode(100) + A*r + encode(200) = A*2r + encode(300)
     * That doesn't match directly. Instead, use opening-based verification:
     *   1. Compute C_input = commit(300, r)
     *   2. Compute C_out_sum = commit(100, r1) + commit(200, r2)
     *   3. Compute C_expected = commit(300, r1+r2)
     *   4. Verify C_out_sum == C_expected
     */
    uint8_t l_r[32], l_r1[32], l_r2[32];
    for (int i = 0; i < 32; ++i) { l_r[i] = 0xAA; l_r1[i] = 0x11; l_r2[i] = 0x22; }

    chipmunk_pedersen_commit_t l_c_input;
    chipmunk_pedersen_commit(&l_c_input, l_params, l_v300, l_r);

    chipmunk_pedersen_commit_t l_c_out1, l_c_out2;
    chipmunk_pedersen_commit(&l_c_out1, l_params, l_v100, l_r1);
    chipmunk_pedersen_commit(&l_c_out2, l_params, l_v200, l_r2);

    chipmunk_pedersen_commit_t l_c_out_sum;
    chipmunk_pedersen_add(&l_c_out_sum, &l_c_out1, &l_c_out2);

    /* Derive combined randomness r_combined = r1 + r2 */
    chipmunk_poly_t l_rr1[CHIPMUNK_LRS_K], l_rr2[CHIPMUNK_LRS_K], l_rr_combined[CHIPMUNK_LRS_K];
    chipmunk_pedersen_derive_blinding(l_rr1, l_r1);
    chipmunk_pedersen_derive_blinding(l_rr2, l_r2);
    for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
        chipmunk_poly_add(&l_rr_combined[j], &l_rr1[j], &l_rr2[j]);
    }

    /* Commit to same total value with combined randomness */
    chipmunk_pedersen_commit_t l_c_expected;
    int l_rc = chipmunk_pedersen_commit_explicit(&l_c_expected, l_params, l_v300, l_rr_combined);
    dap_assert(l_rc == 0, "expected commit OK");

    /* Verify: C(out1) + C(out2) == C(300, r1+r2) */
    int l_match = 1;
    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K && l_match; ++i) {
        for (uint32_t j = 0; j < CHIPMUNK_N && l_match; ++j) {
            if (l_c_out_sum.C[i].coeffs[j] != l_c_expected.C[i].coeffs[j]) l_match = 0;
        }
    }
    dap_assert(l_match, "CONSERVATION: C(100,r1) + C(200,r2) == C(300,r1+r2)");

    /* Also verify with carry-heavy values:
     * 128 + 128 = 256 (byte 0 wraps to 0, byte 1 = 1) */
    uint8_t l_v128[CHIPMUNK_PEDERSEN_VALUE_BYTES];
    uint8_t l_v256[CHIPMUNK_PEDERSEN_VALUE_BYTES];
    s_u64_to_amount_bytes(128, l_v128);
    s_u64_to_amount_bytes(256, l_v256);

    chipmunk_pedersen_commit_t l_c128a, l_c128b;
    chipmunk_pedersen_commit(&l_c128a, l_params, l_v128, l_r1);
    chipmunk_pedersen_commit(&l_c128b, l_params, l_v128, l_r2);

    chipmunk_pedersen_commit_t l_c128_sum;
    chipmunk_pedersen_add(&l_c128_sum, &l_c128a, &l_c128b);

    chipmunk_pedersen_commit_t l_c256_expected;
    l_rc = chipmunk_pedersen_commit_explicit(&l_c256_expected, l_params, l_v256, l_rr_combined);
    dap_assert(l_rc == 0, "carry commit OK");

    int l_carry_match = 1;
    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K && l_carry_match; ++i) {
        for (uint32_t j = 0; j < CHIPMUNK_N && l_carry_match; ++j) {
            if (l_c128_sum.C[i].coeffs[j] != l_c256_expected.C[i].coeffs[j]) l_carry_match = 0;
        }
    }
    dap_assert(l_carry_match, "CONSERVATION with carry: C(128,r1)+C(128,r2)==C(256,r1+r2)");

    /* Test with max digit values: 255+1 = 256 (digit 0 wraps to 0, carry to digit 1) */
    uint8_t l_v255[CHIPMUNK_PEDERSEN_VALUE_BYTES];
    s_u64_to_amount_bytes(255, l_v255);

    chipmunk_pedersen_commit_t l_c255;
    chipmunk_pedersen_commit(&l_c255, l_params, l_v255, l_r1);
    uint8_t l_v1[CHIPMUNK_PEDERSEN_VALUE_BYTES];
    s_u64_to_amount_bytes(1, l_v1);
    chipmunk_pedersen_commit_t l_c1;
    chipmunk_pedersen_commit(&l_c1, l_params, l_v1, l_r2);

    chipmunk_pedersen_commit_t l_c255_1_sum;
    chipmunk_pedersen_add(&l_c255_1_sum, &l_c255, &l_c1);

    chipmunk_pedersen_commit_t l_c256_expected2;
    l_rc = chipmunk_pedersen_commit_explicit(&l_c256_expected2, l_params, l_v256, l_rr_combined);
    dap_assert(l_rc == 0, "max-digit commit OK");

    int l_max_match = 1;
    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K && l_max_match; ++i) {
        for (uint32_t j = 0; j < CHIPMUNK_N && l_max_match; ++j) {
            if (l_c255_1_sum.C[i].coeffs[j] != l_c256_expected2.C[i].coeffs[j]) l_max_match = 0;
        }
    }
    dap_assert(l_max_match, "CONSERVATION max-digit: C(255,r1)+C(1,r2)==C(256,r1+r2)");

    /* Test with large values near Q boundary (scalar encoding supports [0, Q-1]).
     * Q = 3168257. Test: 2000000 + 1000000 = 3000000 < Q ✓ */
    uint8_t l_v2m[CHIPMUNK_PEDERSEN_VALUE_BYTES];
    uint8_t l_v1m[CHIPMUNK_PEDERSEN_VALUE_BYTES];
    uint8_t l_v3m[CHIPMUNK_PEDERSEN_VALUE_BYTES];
    s_u64_to_amount_bytes(2000000ULL, l_v2m);
    s_u64_to_amount_bytes(1000000ULL, l_v1m);
    s_u64_to_amount_bytes(3000000ULL, l_v3m);

    chipmunk_pedersen_commit_t l_c2m, l_c1m;
    chipmunk_pedersen_commit(&l_c2m, l_params, l_v2m, l_r1);
    chipmunk_pedersen_commit(&l_c1m, l_params, l_v1m, l_r2);

    chipmunk_pedersen_commit_t l_c3m_sum;
    chipmunk_pedersen_add(&l_c3m_sum, &l_c2m, &l_c1m);

    chipmunk_pedersen_commit_t l_c3m_expected;
    l_rc = chipmunk_pedersen_commit_explicit(&l_c3m_expected, l_params, l_v3m, l_rr_combined);
    dap_assert(l_rc == 0, "near-Q commit OK");

    int l_large_match = 1;
    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K && l_large_match; ++i) {
        for (uint32_t j = 0; j < CHIPMUNK_N && l_large_match; ++j) {
            if (l_c3m_sum.C[i].coeffs[j] != l_c3m_expected.C[i].coeffs[j]) l_large_match = 0;
        }
    }
    dap_assert(l_large_match, "CONSERVATION near-Q: C(2000000,r1)+C(1000000,r2)==C(3000000,r1+r2)");

    /* Test that all coefficients of C(v) equal (v mod Q) — scalar encoding property */
    {
        chipmunk_pedersen_commit_t l_c_test;
        uint8_t l_v42[CHIPMUNK_PEDERSEN_VALUE_BYTES];
        s_u64_to_amount_bytes(42, l_v42);
        /* Use zero randomness to isolate the encoding */
        chipmunk_poly_t l_zero_r[CHIPMUNK_LRS_K];
        memset(l_zero_r, 0, sizeof(l_zero_r));
        l_rc = chipmunk_pedersen_commit_explicit(&l_c_test, l_params, l_v42, l_zero_r);
        dap_assert(l_rc == 0, "zero-randomness commit OK");

        /* Only first polynomial (i=0) has the encoding added; others are pure A*r = 0 */
        int l_all_42 = 1;
        for (uint32_t j = 0; j < CHIPMUNK_N && l_all_42; ++j) {
            if (l_c_test.C[0].coeffs[j] != 42) l_all_42 = 0;
        }
        dap_assert(l_all_42, "scalar encoding: all coeffs of C[0] = 42 when r=0");
    }

    DAP_DELETE(l_params);
}

int main(void)
{
    dap_set_appname("test_chipmunk_pedersen");
    dap_common_init("test_chipmunk_pedersen", NULL);
    /* Initialise crypto subsystem (SIMD dispatch, chipmunk, etc.) */
    dap_enc_init();

    test_init();
    test_commit_verify();
    test_different_values();
    test_same_value_same_randomness();
    test_additive_homomorphism();
    test_serialize_deserialize();
    test_conservation_property();  /* Phase 6: Z-linear scalar conservation */

    log_it(L_INFO, "=== ALL Chipmunk Pedersen tests PASSED ===");
    dap_common_deinit();
    return 0;
}
