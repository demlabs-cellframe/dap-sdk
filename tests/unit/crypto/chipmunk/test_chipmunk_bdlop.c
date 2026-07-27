/*
 * test_chipmunk_bdlop.c — Unit tests for BDLOP commitment + opening proof.
 *
 * Tests:
 *   1. Commitment correctness (commit → open → verify)
 *   2. Opening proof validity (honest prover always passes)
 *   3. Tampered commitment (should fail)
 *   4. Tampered proof (should fail)
 *   5. Range proof prove/verify
 *   6. Serialization round-trip
 *   7. Proof size measurement
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2025 Cellframe Project
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dap_common.h"
#include "dap_memwipe.h"
#include "chipmunk/chipmunk.h"
#include "chipmunk/chipmunk_poly.h"
#include "chipmunk/chipmunk_pedersen.h"
#include "chipmunk/chipmunk_bdlop.h"
#include "chipmunk/chipmunk_range_proof_bdlop.h"

#define LOG_TAG "chipmunk_bdlop_test"

/* =======================================================================
 *  Test helpers
 * ======================================================================= */

static int g_tests_run = 0;
static int g_tests_pass = 0;
static int g_tests_fail = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        g_tests_fail++; \
        return -1; \
    } \
} while(0)

#define TEST_BEGIN(name) \
    printf("\n=== %s ===\n", name); \
    g_tests_run++

#define TEST_END() \
    printf("  PASS\n"); \
    g_tests_pass++

static void print_proof_size(void)
{
    size_t bdlop_proof = chipmunk_bdlop_proof_serialized_size();
    size_t range_proof = chipmunk_range_proof_bdlop_serialized_size();
    printf("  Proof sizes:\n");
    printf("    BDLOP opening proof:  %zu bytes (%.1f KB)\n",
           bdlop_proof, bdlop_proof / 1024.0);
    printf("    Range proof (full):   %zu bytes (%.1f KB)\n",
           range_proof, range_proof / 1024.0);
    printf("    sizeof(bdlop_proof_t):  %zu bytes (%.1f KB)\n",
           sizeof(chipmunk_bdlop_proof_t), sizeof(chipmunk_bdlop_proof_t) / 1024.0);
}

/* =======================================================================
 *  Test 1: Commitment correctness
 * ======================================================================= */

static int test_bdlop_commit_correctness(void)
{
    TEST_BEGIN("BDLOP commitment correctness");

    /* Initialize parameters */
    chipmunk_pedersen_params_t l_params;
    uint8_t l_seed[32];
    memset(l_seed, 0x42, 32);

    int l_rc = chipmunk_pedersen_init(&l_params, l_seed);
    TEST_ASSERT(l_rc == 0, "pedersen_init failed");

    /* Create a message polynomial (ternary, representing bits) */
    chipmunk_poly_t l_msg;
    memset(&l_msg, 0, sizeof(l_msg));
    for (int i = 0; i < 64; ++i)
        l_msg.coeffs[i] = (i * 7 + 3) % 2;  /* pseudo-random bits */

    /* Derive blinding */
    chipmunk_poly_t l_r[CHIPMUNK_BDLOP_L];
    l_rc = chipmunk_pedersen_derive_blinding(l_r, l_seed);
    TEST_ASSERT(l_rc == 0, "derive_blinding failed");

    /* Create commitment */
    chipmunk_bdlop_commit_t l_commit;
    l_rc = chipmunk_bdlop_commit_poly(&l_commit, &l_params, &l_msg, l_r);
    TEST_ASSERT(l_rc == 0, "bdlop_commit_poly failed");

    /* Verify commitment is not all-zero (sanity) */
    bool l_nonzero = false;
    for (int i = 0; i < CHIPMUNK_BDLOP_K && !l_nonzero; ++i)
        for (int k = 0; k < CHIPMUNK_N && !l_nonzero; ++k)
            if (l_commit.C[i].coeffs[k] != 0)
                l_nonzero = true;
    TEST_ASSERT(l_nonzero, "commitment is all zero");

    /* Cleanup */
    dap_memwipe(&l_msg, sizeof(l_msg));
    dap_memwipe(l_r, sizeof(l_r));
    dap_memwipe(&l_commit, sizeof(l_commit));

    TEST_END();
    return 0;
}

/* =======================================================================
 *  Test 2: Opening proof — honest prover
 * ======================================================================= */

static int test_bdlop_opening_honest(void)
{
    TEST_BEGIN("BDLOP opening proof — honest prover");

    chipmunk_pedersen_params_t l_params;
    uint8_t l_seed[32];
    memset(l_seed, 0x42, 32);

    int l_rc = chipmunk_pedersen_init(&l_params, l_seed);
    TEST_ASSERT(l_rc == 0, "pedersen_init failed");

    /* Create a bit-polynomial message */
    chipmunk_poly_t l_msg;
    memset(&l_msg, 0, sizeof(l_msg));
    /* Encode value 0xDEADBEEF as bits */
    uint64_t l_val = 0xDEADBEEF;
    for (int i = 0; i < 64; ++i)
        l_msg.coeffs[i] = (l_val >> i) & 1;

    /* Derive blinding */
    chipmunk_poly_t l_r[CHIPMUNK_BDLOP_L];
    l_rc = chipmunk_pedersen_derive_blinding(l_r, l_seed);
    TEST_ASSERT(l_rc == 0, "derive_blinding failed");

    /* Create commitment */
    chipmunk_bdlop_commit_t l_commit;
    l_rc = chipmunk_bdlop_commit_poly(&l_commit, &l_params, &l_msg, l_r);
    TEST_ASSERT(l_rc == 0, "bdlop_commit_poly failed");

    /* Create opening proof */
    chipmunk_bdlop_proof_t l_proof;
    uint8_t l_prove_seed[32];
    memset(l_prove_seed, 0x99, 32);

    printf("  Proving...\n");
    clock_t l_start = clock();
    l_rc = chipmunk_bdlop_opening_prove(&l_proof, &l_params, &l_commit,
                                         &l_msg, l_r, 1, l_prove_seed);
    clock_t l_end = clock();
    double l_prove_ms = (double)(l_end - l_start) / CLOCKS_PER_SEC * 1000.0;
    printf("  Prove time: %.1f ms\n", l_prove_ms);
    TEST_ASSERT(l_rc == 0, "bdlop_opening_prove failed");

    /* Verify proof */
    printf("  Verifying...\n");
    l_start = clock();
    int l_valid = chipmunk_bdlop_opening_verify(&l_proof, &l_params, &l_commit);
    l_end = clock();
    double l_verify_ms = (double)(l_end - l_start) / CLOCKS_PER_SEC * 1000.0;
    printf("  Verify time: %.1f ms\n", l_verify_ms);
    TEST_ASSERT(l_valid == 1, "honest proof verification failed");

    /* Cleanup */
    chipmunk_bdlop_proof_wipe(&l_proof);
    dap_memwipe(&l_msg, sizeof(l_msg));
    dap_memwipe(l_r, sizeof(l_r));
    dap_memwipe(&l_commit, sizeof(l_commit));

    TEST_END();
    return 0;
}

/* =======================================================================
 *  Test 3: Tampered commitment (should fail)
 * ======================================================================= */

static int test_bdlop_tampered_commitment(void)
{
    TEST_BEGIN("BDLOP tampered commitment (should fail)");

    chipmunk_pedersen_params_t l_params;
    uint8_t l_seed[32];
    memset(l_seed, 0x42, 32);

    int l_rc = chipmunk_pedersen_init(&l_params, l_seed);
    TEST_ASSERT(l_rc == 0, "pedersen_init failed");

    chipmunk_poly_t l_msg;
    memset(&l_msg, 0, sizeof(l_msg));
    for (int i = 0; i < 64; ++i)
        l_msg.coeffs[i] = (0xCAFEBABE >> i) & 1;

    chipmunk_poly_t l_r[CHIPMUNK_BDLOP_L];
    chipmunk_pedersen_derive_blinding(l_r, l_seed);

    chipmunk_bdlop_commit_t l_commit;
    chipmunk_bdlop_commit_poly(&l_commit, &l_params, &l_msg, l_r);

    /* Create proof for the original commitment */
    chipmunk_bdlop_proof_t l_proof;
    uint8_t l_prove_seed[32];
    memset(l_prove_seed, 0x88, 32);
    l_rc = chipmunk_bdlop_opening_prove(&l_proof, &l_params, &l_commit,
                                         &l_msg, l_r, 1, l_prove_seed);
    TEST_ASSERT(l_rc == 0, "prove failed");

    /* Tamper: modify commitment C[0][0] */
    l_commit.C[0].coeffs[0] = (l_commit.C[0].coeffs[0] + 1) % CHIPMUNK_Q;

    /* Verify should fail */
    int l_valid = chipmunk_bdlop_opening_verify(&l_proof, &l_params, &l_commit);
    printf("  Tampered commitment verify result: %d (expected 0)\n", l_valid);
    TEST_ASSERT(l_valid == 0, "tampered commitment should be rejected");

    chipmunk_bdlop_proof_wipe(&l_proof);
    dap_memwipe(&l_msg, sizeof(l_msg));
    dap_memwipe(l_r, sizeof(l_r));

    TEST_END();
    return 0;
}

/* =======================================================================
 *  Test 4: Tampered proof (should fail)
 * ======================================================================= */

static int test_bdlop_tampered_proof(void)
{
    TEST_BEGIN("BDLOP tampered proof (should fail)");

    chipmunk_pedersen_params_t l_params;
    uint8_t l_seed[32];
    memset(l_seed, 0x42, 32);

    int l_rc = chipmunk_pedersen_init(&l_params, l_seed);
    TEST_ASSERT(l_rc == 0, "pedersen_init failed");

    chipmunk_poly_t l_msg;
    memset(&l_msg, 0, sizeof(l_msg));
    for (int i = 0; i < 64; ++i)
        l_msg.coeffs[i] = (0x12345678 >> i) & 1;

    chipmunk_poly_t l_r[CHIPMUNK_BDLOP_L];
    chipmunk_pedersen_derive_blinding(l_r, l_seed);

    chipmunk_bdlop_commit_t l_commit;
    chipmunk_bdlop_commit_poly(&l_commit, &l_params, &l_msg, l_r);

    chipmunk_bdlop_proof_t l_proof;
    uint8_t l_prove_seed[32];
    memset(l_prove_seed, 0x77, 32);
    l_rc = chipmunk_bdlop_opening_prove(&l_proof, &l_params, &l_commit,
                                         &l_msg, l_r, 1, l_prove_seed);
    TEST_ASSERT(l_rc == 0, "prove failed");

    /* Tamper: modify z_m[0][0] of round 0 (should break norm check or linear equation) */
    l_proof.rounds[0].z_m[0].coeffs[0] = (l_proof.rounds[0].z_m[0].coeffs[0] + 1) % CHIPMUNK_Q;

    int l_valid = chipmunk_bdlop_opening_verify(&l_proof, &l_params, &l_commit);
    printf("  Tampered proof verify result: %d (expected 0)\n", l_valid);
    TEST_ASSERT(l_valid == 0, "tampered proof should be rejected");

    chipmunk_bdlop_proof_wipe(&l_proof);
    dap_memwipe(&l_msg, sizeof(l_msg));
    dap_memwipe(l_r, sizeof(l_r));

    TEST_END();
    return 0;
}

/* =======================================================================
 *  Test 5: Range proof prove/verify
 * ======================================================================= */

static int test_range_proof_bdlop(void)
{
    TEST_BEGIN("Range proof (BDLOP) — prove/verify");

    chipmunk_pedersen_params_t l_params;
    uint8_t l_seed[32];
    memset(l_seed, 0x42, 32);

    int l_rc = chipmunk_pedersen_init(&l_params, l_seed);
    TEST_ASSERT(l_rc == 0, "pedersen_init failed");

    /* Test with several values */
    uint64_t l_test_values[] = {
        0,
        1,
        42,
        1000000,
        0xDEADBEEFCAFEBABEULL,
        0xFFFFFFFFFFFFFFFFULL,  /* max 64-bit */
    };

    for (size_t t = 0; t < sizeof(l_test_values)/sizeof(l_test_values[0]); ++t) {
        uint8_t l_value[32];
        memset(l_value, 0, 32);
        memcpy(l_value, &l_test_values[t], sizeof(uint64_t));

        /* Derive unique seed per test */
        uint8_t l_rp_seed[32];
        memset(l_rp_seed, 0, 32);
        memcpy(l_rp_seed, &l_test_values[t], sizeof(uint64_t));
        l_rp_seed[31] = (uint8_t)t;

        chipmunk_range_proof_bdlop_t l_proof;
        printf("  Testing value %llu...\n", (unsigned long long)l_test_values[t]);
        l_rc = chipmunk_range_proof_bdlop_prove(&l_proof, &l_params, l_value, l_rp_seed);
        if (l_rc != 0) {
            printf("  FAIL: prove failed for value %llu (rc=%d)\n",
                   (unsigned long long)l_test_values[t], l_rc);
            g_tests_fail++;
            return -1;
        }

        int l_valid = chipmunk_range_proof_bdlop_verify(&l_proof, &l_params);
        if (l_valid != 1) {
            printf("  FAIL: verify failed for value %llu\n",
                   (unsigned long long)l_test_values[t]);
            g_tests_fail++;
            return -1;
        }
        printf("  value %llu: PASS\n", (unsigned long long)l_test_values[t]);

        chipmunk_range_proof_bdlop_wipe(&l_proof);
    }

    TEST_END();
    return 0;
}

/* =======================================================================
 *  Test 6: Serialization round-trip
 * ======================================================================= */

static int test_range_proof_serialize(void)
{
    TEST_BEGIN("Range proof serialization round-trip");

    chipmunk_pedersen_params_t l_params;
    uint8_t l_seed[32];
    memset(l_seed, 0x42, 32);

    int l_rc = chipmunk_pedersen_init(&l_params, l_seed);
    TEST_ASSERT(l_rc == 0, "pedersen_init failed");

    uint64_t l_val = 0xBEEF1234;
    uint8_t l_value[32];
    memset(l_value, 0, 32);
    memcpy(l_value, &l_val, sizeof(uint64_t));

    uint8_t l_rp_seed[32];
    memset(l_rp_seed, 0x55, 32);

    chipmunk_range_proof_bdlop_t l_proof;
    l_rc = chipmunk_range_proof_bdlop_prove(&l_proof, &l_params, l_value, l_rp_seed);
    TEST_ASSERT(l_rc == 0, "prove failed");

    /* Serialize */
    size_t l_ser_size = chipmunk_range_proof_bdlop_serialized_size();
    printf("  Serialized size: %zu bytes (%.1f KB)\n", l_ser_size, l_ser_size / 1024.0);

    uint8_t *l_buf = malloc(l_ser_size);
    TEST_ASSERT(l_buf != NULL, "malloc failed");

    l_rc = chipmunk_range_proof_bdlop_serialize(l_buf, l_ser_size, &l_proof);
    TEST_ASSERT(l_rc > 0, "serialize failed");

    /* Deserialize */
    chipmunk_range_proof_bdlop_t l_proof2;
    l_rc = chipmunk_range_proof_bdlop_deserialize(&l_proof2, l_buf, l_ser_size);
    TEST_ASSERT(l_rc == 0, "deserialize failed");

    /* Verify deserialized proof */
    int l_valid = chipmunk_range_proof_bdlop_verify(&l_proof2, &l_params);
    TEST_ASSERT(l_valid == 1, "deserialized proof verification failed");

    free(l_buf);
    chipmunk_range_proof_bdlop_wipe(&l_proof);
    chipmunk_range_proof_bdlop_wipe(&l_proof2);

    TEST_END();
    return 0;
}

/* =======================================================================
 *  Main
 * ======================================================================= */

int main(void)
{
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║   Chipmunk BDLOP + Range Proof — Unit Tests              ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n\n");

    print_proof_size();

    /* Run tests */
    test_bdlop_commit_correctness();
    test_bdlop_opening_honest();
    test_bdlop_tampered_commitment();
    test_bdlop_tampered_proof();
    test_range_proof_bdlop();
    test_range_proof_serialize();

    /* Summary */
    printf("\n═════════════════════════════════════════════════════════════\n");
    printf("  Results: %d/%d passed", g_tests_pass, g_tests_run);
    if (g_tests_fail > 0)
        printf(" (%d FAILED)", g_tests_fail);
    printf("\n");

    print_proof_size();

    return g_tests_fail > 0 ? 1 : 0;
}
