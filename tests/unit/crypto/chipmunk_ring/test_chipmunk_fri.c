/*
 * test_chipmunk_fri.c — Unit tests for FRI commit phase.
 *
 * Phase 9.6 of the FRI-DEEP polynomial commitment scheme.
 *
 * Test strategy:
 *   - Zero polynomial: all rounds produce zeros
 *   - Constant polynomial: all rounds produce same constant
 *   - Specific polynomial: Python reference vectors for all 7 round codewords
 *   - Folding relation verification at sample indices
 *   - Merkle cap consistency: verify caps match the round codewords
 *   - Final evaluations match expected values
 *   - chipmunk_fri_verify_fold() correctness
 *   - Invalid arguments
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <dap_common.h>
#include <dap_test.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <chipmunk_field.h>
#include <chipmunk_fri_ntt.h>
#include <chipmunk_rs.h>
#include <chipmunk_merkle_pcs.h>
#include <chipmunk_poseidon.h>
#include <chipmunk_fri.h>

#define LOG_TAG "test_chipmunk_fri"

/* Round sizes: 2048, 1024, 512, 256, 128, 64, 32 */
static const uint32_t s_round_sizes[CHIPMUNK_FRI_ROUNDS] = {
    2048, 1024, 512, 256, 128, 64, 32
};

/* Fixed alphas for deterministic reference vectors. */
static const int32_t s_alphas[CHIPMUNK_FRI_ROUNDS] = {
    7, 13, 42, 100, 200, 300, 1000
};

/* ========================================================================
 * Test 1: Zero polynomial — all codewords are zero, all folding correct.
 * ======================================================================== */
static void test_fri_zero_poly(void)
{
    chipmunk_fri_prover_t prov;
    int rc = chipmunk_fri_prover_init(&prov);
    dap_assert(rc == 0, "zero poly init");

    int32_t poly[512];
    memset(poly, 0, sizeof(poly));

    rc = chipmunk_fri_commit(&prov, poly, s_alphas);
    dap_assert(rc == 0, "zero poly commit");

    /* All round codewords should be zero. */
    for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
        uint32_t len;
        const int32_t *data = chipmunk_fri_prover_round_data(&prov, r, &len);
        dap_assert(data != NULL, "zero poly round data");
        dap_assert(len == s_round_sizes[r], "zero poly round len");
        for (uint32_t i = 0; i < len; ++i) {
            dap_assert(data[i] == 0, "zero poly round eval");
        }
    }

    /* Final evals all zero. */
    const int32_t *final = chipmunk_fri_prover_final_evals(&prov);
    dap_assert(final != NULL, "zero poly final");
    for (unsigned i = 0; i < CHIPMUNK_FRI_FINAL_SIZE; ++i) {
        dap_assert(final[i] == 0, "zero poly final eval");
    }

    chipmunk_fri_prover_free(&prov);
}

/* ========================================================================
 * Test 2: Constant polynomial f(x) = 42.
 * Folding preserves constants: [(1+a)*42 + (1-a)*42] / 2 = 42.
 * ======================================================================== */
static void test_fri_constant_poly(void)
{
    chipmunk_fri_prover_t prov;
    int rc = chipmunk_fri_prover_init(&prov);
    dap_assert(rc == 0, "const poly init");

    int32_t poly[512];
    memset(poly, 0, sizeof(poly));
    poly[0] = 42;

    rc = chipmunk_fri_commit(&prov, poly, s_alphas);
    dap_assert(rc == 0, "const poly commit");

    /* All round codewords should be 42. */
    for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
        uint32_t len;
        const int32_t *data = chipmunk_fri_prover_round_data(&prov, r, &len);
        for (uint32_t i = 0; i < len; ++i) {
            dap_assert(data[i] == 42, "const poly round eval");
        }
    }

    /* Final evals all 42. */
    const int32_t *final = chipmunk_fri_prover_final_evals(&prov);
    for (unsigned i = 0; i < CHIPMUNK_FRI_FINAL_SIZE; ++i) {
        dap_assert(final[i] == 42, "const poly final eval");
    }

    chipmunk_fri_prover_free(&prov);
}

/* ========================================================================
 * Test 3: Specific polynomial f(x) = 5 + 3x + 7x^2 + 2x^5.
 * Python reference vectors for round codewords.
 * ======================================================================== */
static void test_fri_specific_poly(void)
{
    chipmunk_fri_prover_t prov;
    int rc = chipmunk_fri_prover_init(&prov);
    dap_assert(rc == 0, "specific poly init");

    int32_t poly[512];
    memset(poly, 0, sizeof(poly));
    poly[0] = 5; poly[1] = 3; poly[2] = 7; poly[5] = 2;

    rc = chipmunk_fri_commit(&prov, poly, s_alphas);
    dap_assert(rc == 0, "specific poly commit");

    /* Round 0 codeword: RS-encoded, should match RS encode output. */
    uint32_t len;
    const int32_t *r0 = chipmunk_fri_prover_round_data(&prov, 0, &len);
    dap_assert(len == 2048, "round 0 len");

    /* Python reference: evals at coset domain */
    const int32_t r0_refs[] = {563, 2015349, 2208781, 3167830, 1728949};
    const uint32_t r0_idx[] = {0, 1, 42, 1024, 2047};
    for (int t = 0; t < 5; ++t) {
        dap_assert(r0[r0_idx[t]] == r0_refs[t], "round 0 eval ref");
    }

    /* Round 1 first values (corrected with domain-point folding). */
    const int32_t r1_refs[] = {1223, 1801736, 1284364, 2847494, 1371217};
    const int32_t *r1 = chipmunk_fri_prover_round_data(&prov, 1, &len);
    dap_assert(len == 1024, "round 1 len");

    for (int t = 0; t < 5; ++t) {
        dap_assert(r1[t] == r1_refs[t], "round 1 eval ref");
    }

    /* Round 2 first values (corrected with domain-point folding). */
    const int32_t r2_refs[] = {1433, 896752, 2466934, 1607429, 1410469};
    const int32_t *r2 = chipmunk_fri_prover_round_data(&prov, 2, &len);
    dap_assert(len == 512, "round 2 len");

    for (int t = 0; t < 5; ++t) {
        dap_assert(r2[t] == r2_refs[t], "round 2 eval ref");
    }

    /* Rounds 3-6 first values (domain-point folding).
     * For this small poly, all rounds converge to the same constant. */
    const int32_t *r3 = chipmunk_fri_prover_round_data(&prov, 3, &len);
    const int32_t *r4 = chipmunk_fri_prover_round_data(&prov, 4, &len);
    const int32_t *r5 = chipmunk_fri_prover_round_data(&prov, 5, &len);
    const int32_t *r6 = chipmunk_fri_prover_round_data(&prov, 6, &len);
    dap_assert(len == 32, "round 6 len");
    dap_assert(r3[0] == 16175, "round 3 first");
    dap_assert(r4[0] == 16175, "round 4 first");
    dap_assert(r5[0] == 16175, "round 5 first");
    dap_assert(r6[0] == 16175, "round 6 first");

    /* Final evals. */
    const int32_t *final_evals = chipmunk_fri_prover_final_evals(&prov);
    for (int t = 0; t < 16; ++t) {
        dap_assert(final_evals[t] == 16175, "final eval");
    }

    chipmunk_fri_prover_free(&prov);
}

/* ========================================================================
 * Test 4: Folding relation verification at every round, every sample index.
 * ======================================================================== */
static void test_fri_fold_relations(void)
{
    chipmunk_fri_prover_t prov;
    int rc = chipmunk_fri_prover_init(&prov);
    dap_assert(rc == 0, "fold init");

    int32_t poly[512];
    for (int i = 0; i < 512; ++i)
        poly[i] = (i * 7919 + 1) % CHIPMUNK_Q;

    rc = chipmunk_fri_commit(&prov, poly, s_alphas);
    dap_assert(rc == 0, "fold commit");

    /* Check folding at sample indices for each round. */
    for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
        uint32_t n_r;
        const int32_t *h_r = chipmunk_fri_prover_round_data(&prov, r, &n_r);
        dap_assert(h_r != NULL, "fold round data");

        /* Check first 10 indices. */
        for (uint32_t l = 0; l < 10 && l < n_r / 2; ++l) {
            bool ok = chipmunk_fri_verify_fold(h_r, h_r + n_r, n_r,
                                                s_alphas[r], r, l);
            dap_assert(ok, "fold relation at sample");
        }
    }

    chipmunk_fri_prover_free(&prov);
}

/* ========================================================================
 * Test 5: Merkle cap consistency — caps should match re-built Merkle trees.
 * ======================================================================== */
static void test_fri_merkle_caps(void)
{
    chipmunk_fri_prover_t prov;
    int rc = chipmunk_fri_prover_init(&prov);
    dap_assert(rc == 0, "merkle caps init");

    int32_t poly[512];
    for (int i = 0; i < 512; ++i)
        poly[i] = (i * 31337 + 42) % CHIPMUNK_Q;

    rc = chipmunk_fri_commit(&prov, poly, s_alphas);
    dap_assert(rc == 0, "merkle caps commit");

    /* Rebuild Merkle tree for each round and compare caps. */
    int32_t *scratch = calloc(2u * CHIPMUNK_FRI_INIT_SIZE, sizeof(int32_t));
    dap_assert(scratch != NULL, "scratch alloc");

    for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
        uint32_t n;
        const int32_t *data = chipmunk_fri_prover_round_data(&prov, r, &n);
        dap_assert(data != NULL, "merkle round data");

        const chipmunk_fri_cap_t *cap = chipmunk_fri_prover_cap(&prov, r);
        dap_assert(cap != NULL, "merkle cap");

        /* Compute expected cap size. */
        uint32_t cap_size = (n >= 32u) ? 16u : n;

        int32_t rebuilt_cap[16];
        rc = chipmunk_merkle_build(data, n, rebuilt_cap, cap_size, scratch);
        dap_assert(rc == 0, "merkle rebuild");

        for (uint32_t c = 0; c < cap_size; ++c) {
            dap_assert(cap->nodes[c] == rebuilt_cap[c], "cap node match");
        }
    }

    free(scratch);
    chipmunk_fri_prover_free(&prov);
}

/* ========================================================================
 * Test 6: Verify fold for deliberately wrong alpha (should fail).
 * ======================================================================== */
static inline int32_t s_test_fqmul(int32_t a, int32_t b)
{
    int64_t t = (int64_t)a * (int64_t)b;
    int32_t r = (int32_t)(t % (int64_t)CHIPMUNK_Q);
    if (r < 0) r += (int32_t)CHIPMUNK_Q;
    return r;
}

static void test_fri_fold_wrong_alpha(void)
{
    /* Use n_r=4, round=6 (domain has 32 points, indices 0,1 map to g·ω^0, g·ω^32). */
    int32_t h_r[4] = {10, 20, 30, 40};
    int32_t h_r1[2] = {0, 0};
    uint32_t round = 6;

    int32_t inv2 = chipmunk_field_inv_q(2, (uint64_t)CHIPMUNK_Q);
    int32_t a = 7;
    int32_t inv_g = chipmunk_field_inv_q((int32_t)CHIPMUNK_RS_COSET_G, (uint64_t)CHIPMUNK_Q);
    int32_t omega_inv = chipmunk_field_omega_2048_inv();

    /* Compute h_r1 using correct formula with domain points. */
    for (uint32_t l = 0; l < 2; ++l) {
        /* inv(x) = inv(g) · (inv(ω))^(l · 2^round) */
        int32_t inv_omega_exp = chipmunk_field_pow_q(omega_inv, l * (1u << round), (uint64_t)CHIPMUNK_Q);
        int32_t inv_x = s_test_fqmul(inv_g, inv_omega_exp);

        int32_t even = (int32_t)(((int64_t)h_r[l] + (int64_t)h_r[l + 2])
                                 * (int64_t)inv2 % (int64_t)CHIPMUNK_Q);
        int64_t diff = ((int64_t)h_r[l] - (int64_t)h_r[l + 2]) % (int64_t)CHIPMUNK_Q;
        if (diff < 0) diff += (int64_t)CHIPMUNK_Q;
        int32_t odd = s_test_fqmul(
            (int32_t)(diff * (int64_t)inv2 % (int64_t)CHIPMUNK_Q), inv_x);
        h_r1[l] = even + s_test_fqmul(a, odd);
        if (h_r1[l] >= (int32_t)CHIPMUNK_Q) h_r1[l] -= (int32_t)CHIPMUNK_Q;
    }

    /* Verify with correct alpha should pass. */
    bool ok = chipmunk_fri_verify_fold(h_r, h_r1, 4, 7, round, 0);
    dap_assert(ok, "correct alpha fold");

    /* Verify with wrong alpha should fail. */
    ok = chipmunk_fri_verify_fold(h_r, h_r1, 4, 8, round, 0);
    dap_assert(!ok, "wrong alpha fold rejected");
}

/* ========================================================================
 * Test 7: Final evaluations are in [0, q).
 * ======================================================================== */
static void test_fri_final_range(void)
{
    chipmunk_fri_prover_t prov;
    int rc = chipmunk_fri_prover_init(&prov);
    dap_assert(rc == 0, "range init");

    int32_t poly[512];
    for (int i = 0; i < 512; ++i)
        poly[i] = (i * 104729 + 65537) % CHIPMUNK_Q;

    rc = chipmunk_fri_commit(&prov, poly, s_alphas);
    dap_assert(rc == 0, "range commit");

    const int32_t *final = chipmunk_fri_prover_final_evals(&prov);
    for (unsigned i = 0; i < CHIPMUNK_FRI_FINAL_SIZE; ++i) {
        dap_assert(final[i] >= 0 && final[i] < CHIPMUNK_Q, "final in [0,q)");
    }

    chipmunk_fri_prover_free(&prov);
}

/* ========================================================================
 * Test 8: Determinism — same poly + alphas produce same output.
 * ======================================================================== */
static void test_fri_determinism(void)
{
    chipmunk_fri_prover_t prov1, prov2;
    int rc;

    rc = chipmunk_fri_prover_init(&prov1);
    dap_assert(rc == 0, "det init 1");
    rc = chipmunk_fri_prover_init(&prov2);
    dap_assert(rc == 0, "det init 2");

    int32_t poly[512];
    for (int i = 0; i < 512; ++i)
        poly[i] = (i * 99991 + 12345) % CHIPMUNK_Q;

    rc = chipmunk_fri_commit(&prov1, poly, s_alphas);
    dap_assert(rc == 0, "det commit 1");
    rc = chipmunk_fri_commit(&prov2, poly, s_alphas);
    dap_assert(rc == 0, "det commit 2");

    /* Compare all round codewords. */
    for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
        uint32_t len1, len2;
        const int32_t *d1 = chipmunk_fri_prover_round_data(&prov1, r, &len1);
        const int32_t *d2 = chipmunk_fri_prover_round_data(&prov2, r, &len2);
        dap_assert(len1 == len2, "det round len");
        for (uint32_t i = 0; i < len1; ++i) {
            dap_assert(d1[i] == d2[i], "det round data");
        }
    }

    /* Compare caps. */
    for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
        const chipmunk_fri_cap_t *c1 = chipmunk_fri_prover_cap(&prov1, r);
        const chipmunk_fri_cap_t *c2 = chipmunk_fri_prover_cap(&prov2, r);
        for (unsigned i = 0; i < CHIPMUNK_FRI_CAP_SIZE; ++i) {
            dap_assert(c1->nodes[i] == c2->nodes[i], "det cap");
        }
    }

    /* Compare final evals. */
    const int32_t *f1 = chipmunk_fri_prover_final_evals(&prov1);
    const int32_t *f2 = chipmunk_fri_prover_final_evals(&prov2);
    for (unsigned i = 0; i < CHIPMUNK_FRI_FINAL_SIZE; ++i) {
        dap_assert(f1[i] == f2[i], "det final");
    }

    chipmunk_fri_prover_free(&prov1);
    chipmunk_fri_prover_free(&prov2);
}

/* ========================================================================
 * Test 9: Different alphas produce different results.
 * ======================================================================== */
static void test_fri_different_alphas(void)
{
    chipmunk_fri_prover_t prov1, prov2;
    int rc;

    rc = chipmunk_fri_prover_init(&prov1);
    dap_assert(rc == 0, "diff alpha init 1");
    rc = chipmunk_fri_prover_init(&prov2);
    dap_assert(rc == 0, "diff alpha init 2");

    int32_t poly[512];
    for (int i = 0; i < 512; ++i)
        poly[i] = (i + 1) % CHIPMUNK_Q;

    /* First set of alphas. */
    rc = chipmunk_fri_commit(&prov1, poly, s_alphas);
    dap_assert(rc == 0, "diff alpha commit 1");

    /* Second set: shifted by 1. */
    int32_t alphas2[CHIPMUNK_FRI_ROUNDS] = {8, 14, 43, 101, 201, 301, 1001};
    rc = chipmunk_fri_commit(&prov2, poly, alphas2);
    dap_assert(rc == 0, "diff alpha commit 2");

    /* Round 0 should be identical (same RS encoding). */
    uint32_t len1, len2;
    const int32_t *d1_0 = chipmunk_fri_prover_round_data(&prov1, 0, &len1);
    const int32_t *d2_0 = chipmunk_fri_prover_round_data(&prov2, 0, &len2);
    for (uint32_t i = 0; i < len1; ++i) {
        dap_assert(d1_0[i] == d2_0[i], "same round 0");
    }

    /* Round 1 should differ (different alpha_0). */
    const int32_t *d1_1 = chipmunk_fri_prover_round_data(&prov1, 1, &len1);
    const int32_t *d2_1 = chipmunk_fri_prover_round_data(&prov2, 1, &len2);
    int same_count = 0;
    for (uint32_t i = 0; i < len1; ++i) {
        if (d1_1[i] == d2_1[i]) same_count++;
    }
    /* For a non-trivial poly, different alpha should produce different round 1. */
    dap_assert((uint32_t)same_count < len1, "different alphas -> different round 1");

    chipmunk_fri_prover_free(&prov1);
    chipmunk_fri_prover_free(&prov2);
}

/* ========================================================================
 * Test 10: Invalid arguments.
 * ======================================================================== */
static void test_fri_invalid_args(void)
{
    chipmunk_fri_prover_t prov;
    int rc;

    rc = chipmunk_fri_prover_init(NULL);
    dap_assert(rc < 0, "init NULL");

    rc = chipmunk_fri_prover_init(&prov);
    dap_assert(rc == 0, "init ok");

    int32_t poly[512];
    memset(poly, 0, sizeof(poly));

    rc = chipmunk_fri_commit(NULL, poly, s_alphas);
    dap_assert(rc < 0, "commit NULL prov");

    rc = chipmunk_fri_commit(&prov, NULL, s_alphas);
    dap_assert(rc < 0, "commit NULL poly");

    rc = chipmunk_fri_commit(&prov, poly, NULL);
    dap_assert(rc < 0, "commit NULL alphas");

    /* Round data before commit. */
    const int32_t *d = chipmunk_fri_prover_round_data(&prov, 0, NULL);
    dap_assert(d == NULL, "round data before commit");

    /* Round data with invalid round. */
    rc = chipmunk_fri_commit(&prov, poly, s_alphas);
    dap_assert(rc == 0, "commit ok for arg test");
    d = chipmunk_fri_prover_round_data(&prov, CHIPMUNK_FRI_ROUNDS, NULL);
    dap_assert(d == NULL, "round data invalid round");

    chipmunk_fri_prover_free(&prov);

    /* verify_fold with NULL. */
    bool ok = chipmunk_fri_verify_fold(NULL, NULL, 4, 1, 0, 0);
    dap_assert(!ok, "verify_fold NULL");

    ok = chipmunk_fri_verify_fold(poly, poly, 0, 1, 0, 0);
    dap_assert(!ok, "verify_fold n=0");
}

/* ========================================================================
 * Test 11: FRI query phase — 8 queries with auth paths.
 * ======================================================================== */
static void test_fri_query(void)
{
    chipmunk_fri_prover_t prov;
    int rc = chipmunk_fri_prover_init(&prov);
    dap_assert(rc == 0, "query init");

    int32_t poly[512];
    for (int i = 0; i < 512; ++i)
        poly[i] = (i * 7919 + 1) % CHIPMUNK_Q;

    rc = chipmunk_fri_commit(&prov, poly, s_alphas);
    dap_assert(rc == 0, "query commit");

    /* Query 8 indices. */
    uint32_t indices[CHIPMUNK_FRI_NUM_QUERIES] = {0, 1, 42, 256, 511, 1024, 1500, 2047};
    chipmunk_fri_query_opening_t openings[CHIPMUNK_FRI_NUM_QUERIES];

    rc = chipmunk_fri_query(&prov, CHIPMUNK_FRI_NUM_QUERIES, indices, openings);
    dap_assert(rc == 0, "query query");

    /* Check that leaf values match round data. */
    for (unsigned qi = 0; qi < CHIPMUNK_FRI_NUM_QUERIES; ++qi) {
        dap_assert(openings[qi].idx == indices[qi], "query idx stored");

        for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
            uint32_t len;
            const int32_t *data = chipmunk_fri_prover_round_data(&prov, r, &len);
            dap_assert(data != NULL, "query round data");
            uint32_t leaf_idx = indices[qi] % len;
            dap_assert(openings[qi].leaf_values[r] == data[leaf_idx],
                       "query leaf value match");

            /* Sibling should be antipodal. */
            uint32_t sib_idx = leaf_idx ^ (len / 2u);
            dap_assert(openings[qi].sibling_values[r] == data[sib_idx],
                       "query sibling value match");
        }
    }

    /* Build a proof struct and verify queries. */
    chipmunk_fri_proof_t proof;
    memcpy(&proof.commit, &prov.proof, sizeof(chipmunk_fri_commit_proof_t));
    memcpy(proof.queries, openings, sizeof(openings));

    for (unsigned qi = 0; qi < CHIPMUNK_FRI_NUM_QUERIES; ++qi) {
        bool ok = chipmunk_fri_verify_query(&proof, qi, s_alphas);
        dap_assert(ok, "query verify");
    }

    chipmunk_fri_prover_free(&prov);
}

/* ========================================================================
 * Test 12: Query with zero polynomial.
 * ======================================================================== */
static void test_fri_query_zero_poly(void)
{
    chipmunk_fri_prover_t prov;
    int rc = chipmunk_fri_prover_init(&prov);
    dap_assert(rc == 0, "query zero init");

    int32_t poly[512];
    memset(poly, 0, sizeof(poly));

    rc = chipmunk_fri_commit(&prov, poly, s_alphas);
    dap_assert(rc == 0, "query zero commit");

    uint32_t indices[4] = {0, 100, 1024, 2047};
    chipmunk_fri_query_opening_t openings[4];

    rc = chipmunk_fri_query(&prov, 4, indices, openings);
    dap_assert(rc == 0, "query zero query");

    /* All leaf and sibling values should be zero. */
    for (unsigned qi = 0; qi < 4; ++qi) {
        for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
            dap_assert(openings[qi].leaf_values[r] == 0, "query zero leaf");
            dap_assert(openings[qi].sibling_values[r] == 0, "query zero sib");
        }
    }

    chipmunk_fri_proof_t proof;
    memcpy(&proof.commit, &prov.proof, sizeof(chipmunk_fri_commit_proof_t));
    memcpy(proof.queries, openings, sizeof(openings));

    for (unsigned qi = 0; qi < 4; ++qi) {
        bool ok = chipmunk_fri_verify_query(&proof, qi, s_alphas);
        dap_assert(ok, "query zero verify");
    }

    chipmunk_fri_prover_free(&prov);
}

/* ========================================================================
 * Test 13: Query verification rejects tampered leaf.
 * ======================================================================== */
static void test_fri_query_tampered(void)
{
    chipmunk_fri_prover_t prov;
    int rc = chipmunk_fri_prover_init(&prov);
    dap_assert(rc == 0, "tampered init");

    int32_t poly[512];
    for (int i = 0; i < 512; ++i)
        poly[i] = (i + 1) % CHIPMUNK_Q;

    rc = chipmunk_fri_commit(&prov, poly, s_alphas);
    dap_assert(rc == 0, "tampered commit");

    uint32_t indices[1] = {42};
    chipmunk_fri_query_opening_t openings[1];

    rc = chipmunk_fri_query(&prov, 1, indices, openings);
    dap_assert(rc == 0, "tampered query");

    chipmunk_fri_proof_t proof;
    memcpy(&proof.commit, &prov.proof, sizeof(chipmunk_fri_commit_proof_t));
    memcpy(proof.queries, openings, sizeof(openings));

    /* Verify original — should pass. */
    bool ok = chipmunk_fri_verify_query(&proof, 0, s_alphas);
    dap_assert(ok, "tampered original verify");

    /* Tamper with round 0 leaf value. */
    proof.queries[0].leaf_values[0] ^= 1;
    ok = chipmunk_fri_verify_query(&proof, 0, s_alphas);
    dap_assert(!ok, "tampered leaf rejected");

    /* Restore and tamper with sibling. */
    memcpy(proof.queries, openings, sizeof(openings));
    proof.queries[0].sibling_values[3] ^= 1;
    ok = chipmunk_fri_verify_query(&proof, 0, s_alphas);
    dap_assert(!ok, "tampered sibling rejected");

    chipmunk_fri_prover_free(&prov);
}

/* ========================================================================
 * Test 14: Query invalid arguments.
 * ======================================================================== */
static void test_fri_query_invalid_args(void)
{
    chipmunk_fri_prover_t prov;
    int rc = chipmunk_fri_prover_init(&prov);
    dap_assert(rc == 0, "q args init");

    int32_t poly[512];
    memset(poly, 0, sizeof(poly));
    rc = chipmunk_fri_commit(&prov, poly, s_alphas);
    dap_assert(rc == 0, "q args commit");

    uint32_t idx[1] = {0};
    chipmunk_fri_query_opening_t out[1];

    rc = chipmunk_fri_query(NULL, 1, idx, out);
    dap_assert(rc < 0, "q NULL prov");

    rc = chipmunk_fri_query(&prov, 0, idx, out);
    dap_assert(rc < 0, "q zero queries");

    rc = chipmunk_fri_query(&prov, 1, NULL, out);
    dap_assert(rc < 0, "q NULL indices");

    rc = chipmunk_fri_query(&prov, 1, idx, NULL);
    dap_assert(rc < 0, "q NULL out");

    /* Out-of-range index. */
    uint32_t bad_idx[1] = {2048};
    rc = chipmunk_fri_query(&prov, 1, bad_idx, out);
    dap_assert(rc < 0, "q out of range idx");

    /* Verify query with invalid index. */
    chipmunk_fri_proof_t proof;
    memcpy(&proof.commit, &prov.proof, sizeof(chipmunk_fri_commit_proof_t));
    bool ok = chipmunk_fri_verify_query(&proof, 0, s_alphas);
    /* query 0 was not opened, so values are zero — should fail merkle verify. */
    dap_assert(!ok, "verify unopened query");

    chipmunk_fri_prover_free(&prov);
}

/* ========================================================================
 * Main
 * ======================================================================== */
int main(void)
{
    dap_set_appname("test_chipmunk_fri");
    if (0 != dap_common_init("test_chipmunk_fri", NULL)) {
        fprintf(stderr, "dap_common_init failed\n");
        return 1;
    }

    int rc = chipmunk_field_init();
    dap_assert(rc == 0, "chipmunk_field_init");
    rc = chipmunk_fri_ntt_init();
    dap_assert(rc == 0, "chipmunk_fri_ntt_init");

    log_it(L_INFO, "=== FRI commit phase tests ===");

    test_fri_zero_poly();
    test_fri_constant_poly();
    test_fri_specific_poly();
    test_fri_fold_relations();
    test_fri_merkle_caps();
    test_fri_fold_wrong_alpha();
    test_fri_final_range();
    test_fri_determinism();
    test_fri_different_alphas();
    test_fri_invalid_args();

    log_it(L_INFO, "=== FRI query phase tests ===");

    test_fri_query();
    test_fri_query_zero_poly();
    test_fri_query_tampered();
    test_fri_query_invalid_args();

    log_it(L_INFO, "All FRI tests passed");

    dap_common_deinit();
    return 0;
}
