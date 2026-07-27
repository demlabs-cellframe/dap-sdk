/*
 * test_chipmunk_fri_verify.c — Unit tests for FRI full verification phase.
 *
 * Tests the chipmunk_fri_verify() function: transcript reconstruction,
 * grinding verification, challenge derivation, query index matching,
 * Merkle path verification, and folding relation checks.
 */

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <dap_common.h>
#include <dap_test.h>

#include "chipmunk.h"
#include "chipmunk_field.h"
#include "chipmunk_poly.h"
#include "chipmunk_fri.h"
#include "chipmunk_fri_transcript.h"

#define LOG_TAG "test_chipmunk_fri_verify"

/* Domain separator for all tests in this file. */
static const uint8_t FRI_VERIFY_DOMAIN[16] = {
    'F','R','I','-','V','E','R','I','F','Y','-','T','E','S','T','0'
};

/* Helper: create a simple test polynomial. */
static void s_make_poly(int32_t poly[CHIPMUNK_N], unsigned degree)
{
    memset(poly, 0, CHIPMUNK_N * sizeof(int32_t));
    /* Set coefficients 0..degree to small nonzero values. */
    for (unsigned i = 0; i <= degree && i < CHIPMUNK_N; ++i)
        poly[i] = (int32_t)((i + 1) * 7 % CHIPMUNK_Q);
}

/* Helper: run full prover pipeline and produce a proof. */
static int s_prove(chipmunk_fri_proof_t *proof,
                   const int32_t poly[CHIPMUNK_N],
                   uint32_t indices[CHIPMUNK_FRI_NUM_QUERIES])
{
    chipmunk_fri_prover_t prov;
    int rc = chipmunk_fri_prover_init(&prov);
    if (rc != 0) return rc;

    /* Use fixed alphas for deterministic testing. */
    int32_t alphas[CHIPMUNK_FRI_ROUNDS] = {
        42, 137, 999, 555, 1234, 777, 314159 % CHIPMUNK_Q
    };

    rc = chipmunk_fri_commit(&prov, poly, alphas);
    if (rc != 0) {
        chipmunk_fri_prover_free(&prov);
        return rc;
    }

    /* Fixed query indices. */
    uint32_t fixed_indices[CHIPMUNK_FRI_NUM_QUERIES] = {
        42, 1001, 567, 1893, 234, 1567, 89, 1780
    };
    memcpy(indices, fixed_indices, sizeof(fixed_indices));

    rc = chipmunk_fri_query(&prov, CHIPMUNK_FRI_NUM_QUERIES,
                            fixed_indices, proof->queries);
    if (rc != 0) {
        chipmunk_fri_prover_free(&prov);
        return rc;
    }

    /* Copy commit proof. */
    memcpy(&proof->commit, &prov.proof, sizeof(chipmunk_fri_commit_proof_t));

    chipmunk_fri_prover_free(&prov);
    return 0;
}

/* Fixed alphas for all transcript-based tests. Both prover and verifier
 * use these. In production (Phase 9.11), alphas come from the DEEP transcript. */
static const int32_t TEST_ALPHAS[CHIPMUNK_FRI_ROUNDS] = {
    42, 137, 999, 555, 1234, 777, 314159 % CHIPMUNK_Q
};

/* Helper: build a proof with fixed alphas and transcript-derived query indices.
 * Returns the grinding nonce. */
static int s_prove_with_transcript(chipmunk_fri_proof_t *proof,
                                   const int32_t poly[CHIPMUNK_N],
                                   const uint8_t domain[16],
                                   uint32_t *out_nonce)
{
    chipmunk_fri_prover_t prov;
    int rc = chipmunk_fri_prover_init(&prov);
    if (rc != 0) return rc;

    rc = chipmunk_fri_commit(&prov, poly, TEST_ALPHAS);
    if (rc != 0) {
        chipmunk_fri_prover_free(&prov);
        return rc;
    }

    /* Build prover-side transcript to derive query indices + get nonce. */
    chipmunk_fri_transcript_t tr;
    rc = chipmunk_fri_transcript_init(&tr, domain);
    if (rc != 0) {
        chipmunk_fri_prover_free(&prov);
        return rc;
    }

    /* Absorb caps. */
    for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
        uint32_t n = prov.round_sizes[r];
        uint32_t cap_size = n >= 32u ? 16u : n;
        rc = chipmunk_fri_transcript_absorb_cap(&tr, prov.proof.caps[r].nodes, cap_size);
        if (rc != 0) {
            chipmunk_fri_prover_free(&prov);
            return rc;
        }
    }

    /* Absorb final evals. */
    for (unsigned i = 0; i < CHIPMUNK_FRI_FINAL_SIZE; ++i) {
        rc = chipmunk_fri_transcript_absorb_fq(&tr, prov.proof.final_evals[i]);
        if (rc != 0) {
            chipmunk_fri_prover_free(&prov);
            return rc;
        }
    }

    /* Absorb alphas (same as verifier will). */
    for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
        rc = chipmunk_fri_transcript_absorb_fq(&tr, TEST_ALPHAS[r]);
        if (rc != 0) {
            chipmunk_fri_prover_free(&prov);
            return rc;
        }
    }

    /* Finalize → grinding. */
    rc = chipmunk_fri_transcript_finalize(&tr);
    if (rc != 0) {
        chipmunk_fri_prover_free(&prov);
        return rc;
    }

    if (out_nonce)
        *out_nonce = tr.grinding_nonce;

    /* Derive query indices from transcript. */
    uint32_t indices[CHIPMUNK_FRI_NUM_QUERIES];
    rc = chipmunk_fri_derive_query_indices(&tr, CHIPMUNK_FRI_NUM_QUERIES,
                                            CHIPMUNK_FRI_INIT_SIZE, indices);
    if (rc != 0) {
        chipmunk_fri_prover_free(&prov);
        return rc;
    }

    rc = chipmunk_fri_query(&prov, CHIPMUNK_FRI_NUM_QUERIES, indices,
                            proof->queries);
    if (rc != 0) {
        chipmunk_fri_prover_free(&prov);
        return rc;
    }

    memcpy(&proof->commit, &prov.proof, sizeof(chipmunk_fri_commit_proof_t));
    chipmunk_fri_prover_free(&prov);
    return 0;
}

/* ========================================================================
 * Test 1: Derive query indices.
 * ======================================================================== */
static void test_verify_derive_indices(void)
{
    chipmunk_fri_transcript_t tr;
    chipmunk_fri_transcript_init(&tr, FRI_VERIFY_DOMAIN);

    uint8_t data[64];
    for (unsigned i = 0; i < sizeof(data); ++i)
        data[i] = (uint8_t)i;
    chipmunk_fri_transcript_absorb(&tr, data, sizeof(data));

    int rc = chipmunk_fri_transcript_finalize(&tr);
    dap_assert(rc == 0, "finalize ok");

    /* Derive 8 query indices. */
    uint32_t indices[8];
    rc = chipmunk_fri_derive_query_indices(&tr, 8, 2048, indices);
    dap_assert(rc == 0, "derive indices ok");

    /* All should be < 2048. */
    for (unsigned i = 0; i < 8; ++i)
        dap_assert(indices[i] < 2048, "index < 2048");

    /* NULL args. */
    rc = chipmunk_fri_derive_query_indices(NULL, 1, 2048, indices);
    dap_assert(rc < 0, "null tr rejected");
    rc = chipmunk_fri_derive_query_indices(&tr, 1, 2048, NULL);
    dap_assert(rc < 0, "null out rejected");
}

/* ========================================================================
 * Test 2: Query index determinism.
 * ======================================================================== */
static void test_verify_indices_determinism(void)
{
    chipmunk_fri_transcript_t tr1, tr2;
    chipmunk_fri_transcript_init(&tr1, FRI_VERIFY_DOMAIN);
    chipmunk_fri_transcript_init(&tr2, FRI_VERIFY_DOMAIN);

    uint8_t data[32];
    memset(data, 0xAA, sizeof(data));
    chipmunk_fri_transcript_absorb(&tr1, data, sizeof(data));
    chipmunk_fri_transcript_absorb(&tr2, data, sizeof(data));
    chipmunk_fri_transcript_finalize(&tr1);
    chipmunk_fri_transcript_finalize(&tr2);

    uint32_t idx1[8], idx2[8];
    chipmunk_fri_derive_query_indices(&tr1, 8, 2048, idx1);
    chipmunk_fri_derive_query_indices(&tr2, 8, 2048, idx2);

    for (unsigned i = 0; i < 8; ++i)
        dap_assert(idx1[i] == idx2[i], "index determinism");
}

/* ========================================================================
 * Test 3: Verify with fixed alphas (no transcript — direct verify_query).
 * ======================================================================== */
static void test_verify_fixed_alphas(void)
{
    int32_t poly[CHIPMUNK_N];
    s_make_poly(poly, 10);

    chipmunk_fri_proof_t proof;
    uint32_t indices[CHIPMUNK_FRI_NUM_QUERIES];
    int rc = s_prove(&proof, poly, indices);
    dap_assert(rc == 0, "prove ok");

    int32_t alphas[CHIPMUNK_FRI_ROUNDS] = {
        42, 137, 999, 555, 1234, 777, 314159 % CHIPMUNK_Q
    };

    /* Verify each query individually. */
    for (unsigned q = 0; q < CHIPMUNK_FRI_NUM_QUERIES; ++q) {
        bool ok = chipmunk_fri_verify_query(&proof, q, alphas);
        dap_assert(ok, "verify_query pass");
    }
}

/* ========================================================================
 * Test 4: Full verify via chipmunk_fri_verify with transcript.
 * ======================================================================== */
static void test_verify_full(void)
{
    int32_t poly[CHIPMUNK_N];
    s_make_poly(poly, 5);

    chipmunk_fri_proof_t proof;
    uint32_t nonce;
    int rc = s_prove_with_transcript(&proof, poly, FRI_VERIFY_DOMAIN, &nonce);
    dap_assert(rc == 0, "prove with transcript ok");

    /* Full verify. */
    chipmunk_fri_verify_result_t result;
    bool ok = chipmunk_fri_verify(&proof, FRI_VERIFY_DOMAIN, TEST_ALPHAS, &result);
    dap_assert(ok, "full verify pass");
    dap_assert(result.valid, "result.valid");
    dap_assert(result.grinding_nonce == nonce, "grinding nonce matches");
}

/* ========================================================================
 * Test 5: Tampered cap → verify fails.
 * ======================================================================== */
static void test_verify_tampered_cap(void)
{
    int32_t poly[CHIPMUNK_N];
    s_make_poly(poly, 3);

    chipmunk_fri_proof_t proof;
    uint32_t nonce;
    int rc = s_prove_with_transcript(&proof, poly, FRI_VERIFY_DOMAIN, &nonce);
    dap_assert(rc == 0, "prove ok");

    /* Tamper with cap in round 2. */
    proof.commit.caps[2].nodes[0] ^= 1;

    chipmunk_fri_verify_result_t result;
    bool ok = chipmunk_fri_verify(&proof, FRI_VERIFY_DOMAIN, TEST_ALPHAS, &result);
    dap_assert(!ok, "tampered cap rejected");
    dap_assert(!result.valid, "result not valid");
}

/* ========================================================================
 * Test 6: Tampered leaf value → verify fails.
 * ======================================================================== */
static void test_verify_tampered_leaf(void)
{
    int32_t poly[CHIPMUNK_N];
    s_make_poly(poly, 7);

    chipmunk_fri_proof_t proof;
    uint32_t nonce;
    int rc = s_prove_with_transcript(&proof, poly, FRI_VERIFY_DOMAIN, &nonce);
    dap_assert(rc == 0, "prove ok");

    /* Tamper with a leaf value in query 0, round 1. */
    proof.queries[0].leaf_values[1] ^= 42;

    chipmunk_fri_verify_result_t result;
    bool ok = chipmunk_fri_verify(&proof, FRI_VERIFY_DOMAIN, TEST_ALPHAS, &result);
    dap_assert(!ok, "tampered leaf rejected");
}

/* ========================================================================
 * Test 7: Wrong domain separator → verify fails.
 * ======================================================================== */
static void test_verify_wrong_domain(void)
{
    int32_t poly[CHIPMUNK_N];
    s_make_poly(poly, 4);

    chipmunk_fri_proof_t proof;
    uint32_t nonce;
    int rc = s_prove_with_transcript(&proof, poly, FRI_VERIFY_DOMAIN, &nonce);
    dap_assert(rc == 0, "prove ok");

    uint8_t wrong_domain[16];
    memcpy(wrong_domain, "WRONG-DOMAIN-01", 16);

    chipmunk_fri_verify_result_t result;
    bool ok = chipmunk_fri_verify(&proof, wrong_domain, TEST_ALPHAS, &result);
    dap_assert(!ok, "wrong domain rejected");
}

/* ========================================================================
 * Test 8: NULL arguments.
 * ======================================================================== */
static void test_verify_null_args(void)
{
    chipmunk_fri_verify_result_t result;

    bool ok = chipmunk_fri_verify(NULL, FRI_VERIFY_DOMAIN, TEST_ALPHAS, &result);
    dap_assert(!ok, "null proof rejected");

    ok = chipmunk_fri_verify(&(chipmunk_fri_proof_t){0}, NULL, TEST_ALPHAS, &result);
    dap_assert(!ok, "null domain rejected");

    ok = chipmunk_fri_verify(&(chipmunk_fri_proof_t){0}, FRI_VERIFY_DOMAIN, NULL, &result);
    dap_assert(!ok, "null alphas rejected");

    /* NULL result is allowed — just returns true/false. */
}

/* ========================================================================
 * Test 9: Higher degree polynomial verify.
 * ======================================================================== */
static void test_verify_high_degree(void)
{
    int32_t poly[CHIPMUNK_N];
    s_make_poly(poly, 511); /* max degree for RS encoding */

    chipmunk_fri_proof_t proof;
    uint32_t nonce;
    int rc = s_prove_with_transcript(&proof, poly, FRI_VERIFY_DOMAIN, &nonce);
    dap_assert(rc == 0, "prove high degree ok");

    chipmunk_fri_verify_result_t result;
    bool ok = chipmunk_fri_verify(&proof, FRI_VERIFY_DOMAIN, TEST_ALPHAS, &result);
    dap_assert(ok, "high degree verify pass");
}

/* ========================================================================
 * Test 10: Tampered final evals → verify fails.
 * ======================================================================== */
static void test_verify_tampered_final(void)
{
    int32_t poly[CHIPMUNK_N];
    s_make_poly(poly, 2);

    chipmunk_fri_proof_t proof;
    uint32_t nonce;
    int rc = s_prove_with_transcript(&proof, poly, FRI_VERIFY_DOMAIN, &nonce);
    dap_assert(rc == 0, "prove ok");

    /* Tamper with a final evaluation. */
    proof.commit.final_evals[5] ^= 1;

    chipmunk_fri_verify_result_t result;
    bool ok = chipmunk_fri_verify(&proof, FRI_VERIFY_DOMAIN, TEST_ALPHAS, &result);
    /* Fails because transcript hash differs (different absorbed data). */
    dap_assert(!ok, "tampered final eval rejected");
}

/* ========================================================================
 * Main
 * ======================================================================== */
int main(void)
{
    dap_set_appname("test_chipmunk_fri_verify");
    if (0 != dap_common_init("test_chipmunk_fri_verify", NULL)) {
        fprintf(stderr, "dap_common_init failed\n");
        return 1;
    }

    log_it(L_INFO, "=== FRI verify tests ===");

    test_verify_derive_indices();
    test_verify_indices_determinism();
    test_verify_fixed_alphas();
    test_verify_full();
    test_verify_tampered_cap();
    test_verify_tampered_leaf();
    test_verify_wrong_domain();
    test_verify_null_args();
    test_verify_high_degree();
    test_verify_tampered_final();

    log_it(L_INFO, "All FRI verify tests passed");

    dap_common_deinit();
    return 0;
}
