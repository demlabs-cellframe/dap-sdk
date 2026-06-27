/*
 * test_chipmunk_mring_statement_eval.c — MRNG M3.2 unified statement.
 *
 * CR-11.G Phase 7.7 / task_ac273cea.  Validates the unified inner-product
 * statement (G2 v2.1 §3, "Claim 1 v2.1"):
 *
 *     ⟨b̃, P̃(c)⟩ = ρ(c)   in R_q   for an HONEST witness
 *
 * where
 *     b̃[i]   = b_i,  b̃[N+i] = b_i(b_i − 1) = 0,
 *     P̃[i](c)= c + c³·pk_i,   P̃[N+i](c) = c²,
 *     ρ(c)   = c·t + c³·Y_pk,
 *     pk_i   = relation_eval(A_pk, x_i),
 *     Y_pk   = relation_eval(A_pk, Σ b_i x_i).
 *
 * Tested properties:
 *   T1. Augmented-dim formula matches 2·N for in-range N and refuses
 *       out-of-range inputs.
 *   T2. augment_witness places b in the lower half and zeros the
 *       upper half (REL-1 binary square = 0).
 *   T3. aggregate_X is linear in the subset selection.
 *   T4. Honest CLAIM 1 holds for several independent sparse-ternary
 *       challenges c (=> the identity is satisfied symbolically, not
 *       just for one lucky transcript).
 *   T5. SOUNDNESS sanity: replacing Y_pk with the wrong subset sum
 *       breaks the identity (with overwhelming probability — the
 *       formal bound is ≤ 3·2⁻⁹⁸⁰ per round, see G2 v2 §A3).
 *   T6. SOUNDNESS sanity: replacing a single b_i in b̃ AFTER augmentation
 *       (simulating REL-1 abuse) breaks the identity.
 */

#include <dap_common.h>
#include <dap_test.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "chipmunk/chipmunk_mring_statement.h"
#include "chipmunk/chipmunk_poly.h"
#include "chipmunk/chipmunk_lrs.h"

#define LOG_TAG "test_chipmunk_mring_eval"

#define N_RING 4u
#define T_THRESH 2u

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

static bool s_polys_equal(const chipmunk_poly_t *a, const chipmunk_poly_t *b)
{
    for (size_t i = 0u; i < CHIPMUNK_N; ++i) {
        if (a->coeffs[i] != b->coeffs[i]) {
            return false;
        }
    }
    return true;
}

static void s_derive_x_for_member(chipmunk_poly_t a_x[CHIPMUNK_LRS_K],
                                  uint32_t a_member_idx)
{
    /* Per-member witness seed: derived deterministically from the index. */
    uint8_t x_seed[CHIPMUNK_LRS_SEED_BYTES];
    for (size_t i = 0u; i < CHIPMUNK_LRS_SEED_BYTES; ++i) {
        x_seed[i] = (uint8_t)(((uint32_t)i * 31u + a_member_idx * 7u) & 0xFFu);
    }
    const int rc = chipmunk_lrs_derive_witness(a_x, x_seed);
    dap_assert(rc == 0, "test fixture: chipmunk_lrs_derive_witness must succeed");
}

static void s_sample_sparse_ternary_c(chipmunk_poly_t *a_c, uint8_t a_salt)
{
    uint8_t seed[CHIPMUNK_LRS_CHALLENGE_SEED_BYTES];
    for (size_t i = 0u; i < CHIPMUNK_LRS_CHALLENGE_SEED_BYTES; ++i) {
        seed[i] = (uint8_t)(0xA5u ^ (uint8_t)i ^ a_salt);
    }
    const int rc =
        chipmunk_lrs_h_to_sparse_ternary(a_c,
                                         "mring-test-c",
                                         CHIPMUNK_LRS_PARAMS_C0,
                                         seed);
    dap_assert(rc == 0, "test fixture: sparse-ternary sampler must succeed");
}

/* Run one round of Claim 1 with a transcript-derived challenge salt.
 * On success returns true.  Optional `a_break_y_pk` tampers Y_pk by
 * adding a small known perturbation; Optional `a_break_b_bit` flips one
 * coefficient in b̃ to simulate REL-1 abuse. */
static bool s_check_claim1_once(const chipmunk_poly_t *a_pks,
                                const chipmunk_poly_t *a_x_flat,
                                const uint8_t *a_b_indicator,
                                uint8_t a_salt,
                                bool a_expect_pass,
                                bool a_break_y_pk,
                                bool a_break_b_bit_index_or_zero)
{
    /* Compute A_pk and the aggregated witness X = Σ b_i x_i. */
    chipmunk_poly_t A_pk[CHIPMUNK_LRS_K];
    dap_assert(chipmunk_lrs_derive_A_pk(A_pk, CHIPMUNK_LRS_PARAMS_C0) == 0,
               "derive A_pk");

    chipmunk_poly_t X[CHIPMUNK_LRS_K];
    dap_assert(chipmunk_mring_aggregate_X(X, a_b_indicator, a_x_flat, N_RING) == 0,
               "aggregate_X");

    /* Y_pk = relation_eval(A_pk, X). */
    chipmunk_poly_t Y_pk;
    dap_assert(chipmunk_lrs_relation_eval(&Y_pk, A_pk, X) == 0,
               "Y_pk = relation_eval(A_pk, X)");

    if (a_break_y_pk) {
        /* Add a small perturbation: a single non-zero coefficient bump.
         * Even a 1-coefficient delta yields a non-zero D(c) of degree 3,
         * which evaluates to non-zero on a random c with probability
         * ≥ 1 − 3·2⁻⁹⁸⁰ (G2 v2 §A3). */
        Y_pk.coeffs[0] = (Y_pk.coeffs[0] + 1) % CHIPMUNK_Q;
    }

    /* Augmented witness. */
    chipmunk_mring_polyvec_t b_tilde;
    dap_assert(chipmunk_mring_polyvec_alloc(
                   &b_tilde, chipmunk_mring_augmented_dim(N_RING)) == 0,
               "polyvec_alloc(b̃)");
    dap_assert(chipmunk_mring_augment_witness(&b_tilde, a_b_indicator, N_RING) == 0,
               "augment_witness");

    if (a_break_b_bit_index_or_zero) {
        /* Flip coeffs[0] of b̃[N+0] from 0 to 1.  This breaks REL-1's
         * b_i(b_i − 1) = 0 in slot N+0 even though b_i ∈ {0,1}, so the
         * c² coefficient becomes 1 and the inner product picks up an
         * extra c² term not matched by ρ. */
        b_tilde.slots[N_RING].coeffs[0] = 1;
    }

    /* Public vector P̃(c) and target ρ(c). */
    chipmunk_poly_t c;
    s_sample_sparse_ternary_c(&c, a_salt);

    chipmunk_mring_polyvec_t P_tilde;
    dap_assert(chipmunk_mring_polyvec_alloc(
                   &P_tilde, chipmunk_mring_augmented_dim(N_RING)) == 0,
               "polyvec_alloc(P̃)");
    dap_assert(chipmunk_mring_eval_public_P(&P_tilde, &c, a_pks, N_RING) == 0,
               "eval_public_P");

    chipmunk_poly_t rho;
    dap_assert(chipmunk_mring_eval_public_rho(&rho, &c, T_THRESH, &Y_pk) == 0,
               "eval_public_rho");

    chipmunk_poly_t lhs;
    dap_assert(chipmunk_mring_inner_product(&lhs, &b_tilde, &P_tilde) == 0,
               "inner_product");

    const bool passed = s_polys_equal(&lhs, &rho);

    chipmunk_mring_polyvec_free(&P_tilde);
    chipmunk_mring_polyvec_free(&b_tilde);

    if (a_expect_pass) {
        if (!passed) {
            log_it(L_ERROR,
                   "Claim 1: identity FAILED for salt=%u (honest witness, expected PASS)",
                   (unsigned)a_salt);
        }
        return passed;
    }
    /* Tampered: we EXPECT the identity to fail. */
    if (passed) {
        log_it(L_ERROR,
               "Soundness: identity unexpectedly PASSED for salt=%u under tampering",
               (unsigned)a_salt);
    }
    return !passed;
}

/* -------------------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------------- */

static bool s_test_augmented_dim(void)
{
    dap_assert(chipmunk_mring_augmented_dim(CHIPMUNK_MRING_N_MIN) ==
                   2u * CHIPMUNK_MRING_N_MIN,
               "augmented_dim(N_MIN) = 2·N_MIN");
    dap_assert(chipmunk_mring_augmented_dim(CHIPMUNK_MRING_N_MAX) ==
                   2u * CHIPMUNK_MRING_N_MAX,
               "augmented_dim(N_MAX) = 2·N_MAX");
    dap_assert(chipmunk_mring_augmented_dim(1u) == 0u,
               "augmented_dim refuses N < N_MIN");
    dap_assert(chipmunk_mring_augmented_dim(CHIPMUNK_MRING_N_MAX + 1u) == 0u,
               "augmented_dim refuses N > N_MAX");
    return true;
}

static bool s_test_augment_witness_layout(void)
{
    const uint8_t b[N_RING] = { 1, 0, 1, 0 };
    chipmunk_mring_polyvec_t b_tilde;
    dap_assert(chipmunk_mring_polyvec_alloc(
                   &b_tilde, chipmunk_mring_augmented_dim(N_RING)) == 0,
               "polyvec_alloc(N=4)");
    dap_assert(chipmunk_mring_augment_witness(&b_tilde, b, N_RING) == 0,
               "augment_witness honest");

    /* Lower half: degree-0 polynomials with coeffs[0] = b_i, rest zero. */
    for (uint32_t i = 0u; i < N_RING; ++i) {
        dap_assert(b_tilde.slots[i].coeffs[0] == (int32_t)b[i],
                   "lower-half slot stores b_i in coeffs[0]");
        for (size_t k = 1u; k < CHIPMUNK_N; ++k) {
            dap_assert(b_tilde.slots[i].coeffs[k] == 0,
                       "lower-half slot keeps higher coefficients zero");
        }
    }
    /* Upper half: identically zero. */
    for (uint32_t i = 0u; i < N_RING; ++i) {
        for (size_t k = 0u; k < CHIPMUNK_N; ++k) {
            dap_assert(b_tilde.slots[N_RING + i].coeffs[k] == 0,
                       "upper-half slot identically zero for honest b");
        }
    }
    chipmunk_mring_polyvec_free(&b_tilde);
    return true;
}

static bool s_test_aggregate_X_linearity(void)
{
    /* Build x_flat with two independent ring members. */
    const uint32_t N = 2u;
    chipmunk_poly_t x_flat[2 * CHIPMUNK_LRS_K];
    s_derive_x_for_member(&x_flat[0u * CHIPMUNK_LRS_K], 0u);
    s_derive_x_for_member(&x_flat[1u * CHIPMUNK_LRS_K], 1u);

    chipmunk_poly_t X_b00[CHIPMUNK_LRS_K], X_b10[CHIPMUNK_LRS_K],
                    X_b01[CHIPMUNK_LRS_K], X_b11[CHIPMUNK_LRS_K], X_sum[CHIPMUNK_LRS_K];

    const uint8_t b00[2] = { 0, 0 }, b10[2] = { 1, 0 }, b01[2] = { 0, 1 }, b11[2] = { 1, 1 };

    dap_assert(chipmunk_mring_aggregate_X(X_b00, b00, x_flat, N) == 0, "X(0,0)");
    dap_assert(chipmunk_mring_aggregate_X(X_b10, b10, x_flat, N) == 0, "X(1,0)");
    dap_assert(chipmunk_mring_aggregate_X(X_b01, b01, x_flat, N) == 0, "X(0,1)");
    dap_assert(chipmunk_mring_aggregate_X(X_b11, b11, x_flat, N) == 0, "X(1,1)");

    /* X(0,0) must be zero. */
    for (uint32_t j = 0u; j < CHIPMUNK_LRS_K; ++j) {
        for (size_t k = 0u; k < CHIPMUNK_N; ++k) {
            dap_assert(X_b00[j].coeffs[k] == 0, "X(empty subset) must be zero");
        }
    }
    /* X(1,1) must equal X(1,0) + X(0,1). */
    for (uint32_t j = 0u; j < CHIPMUNK_LRS_K; ++j) {
        dap_assert(chipmunk_poly_add(&X_sum[j], &X_b10[j], &X_b01[j]) == 0, "add");
        dap_assert(s_polys_equal(&X_sum[j], &X_b11[j]),
                   "aggregate_X linearity: X(1,1) = X(1,0) + X(0,1)");
    }
    return true;
}

static bool s_test_claim1_multiple_challenges(void)
{
    /* Derive x_i and pk_i for the whole ring. */
    chipmunk_poly_t x_flat[N_RING * CHIPMUNK_LRS_K];
    chipmunk_poly_t pks[N_RING];
    chipmunk_poly_t A_pk[CHIPMUNK_LRS_K];
    dap_assert(chipmunk_lrs_derive_A_pk(A_pk, CHIPMUNK_LRS_PARAMS_C0) == 0,
               "derive A_pk");
    for (uint32_t i = 0u; i < N_RING; ++i) {
        s_derive_x_for_member(&x_flat[i * CHIPMUNK_LRS_K], i);
        dap_assert(chipmunk_lrs_relation_eval(&pks[i], A_pk,
                                              &x_flat[i * CHIPMUNK_LRS_K]) == 0,
                   "pk_i = relation_eval(A_pk, x_i)");
    }
    const uint8_t b_indicator[N_RING] = { 1, 0, 1, 0 };  /* subset {0,2}, t=2 */

    /* Run Claim 1 across 5 independent sparse-ternary challenges. */
    bool ok = true;
    for (uint8_t salt = 0u; salt < 5u; ++salt) {
        const bool pass = s_check_claim1_once(pks, x_flat, b_indicator,
                                              salt,
                                              /*expect_pass=*/true,
                                              /*break_y_pk=*/false,
                                              /*break_b_bit=*/false);
        dap_assert(pass, "Claim 1 v2.1: ⟨b̃, P̃(c)⟩ = ρ(c) for honest witness");
        if (!pass) ok = false;
    }
    return ok;
}

static bool s_test_soundness_tampered_y_pk(void)
{
    /* Same setup as above. */
    chipmunk_poly_t x_flat[N_RING * CHIPMUNK_LRS_K];
    chipmunk_poly_t pks[N_RING];
    chipmunk_poly_t A_pk[CHIPMUNK_LRS_K];
    dap_assert(chipmunk_lrs_derive_A_pk(A_pk, CHIPMUNK_LRS_PARAMS_C0) == 0,
               "derive A_pk");
    for (uint32_t i = 0u; i < N_RING; ++i) {
        s_derive_x_for_member(&x_flat[i * CHIPMUNK_LRS_K], i);
        dap_assert(chipmunk_lrs_relation_eval(&pks[i], A_pk,
                                              &x_flat[i * CHIPMUNK_LRS_K]) == 0,
                   "pk_i");
    }
    const uint8_t b_indicator[N_RING] = { 1, 0, 1, 0 };

    /* Run Claim 1 with Y_pk perturbed by a single coefficient.  By G2 v2
     * §A3 the identity must FAIL for every sampled c with probability
     * ≥ 1 − 3·2⁻⁹⁸⁰; we sample 5 independent challenges to verify. */
    bool ok = true;
    for (uint8_t salt = 0u; salt < 5u; ++salt) {
        const bool pass_expected = s_check_claim1_once(pks, x_flat, b_indicator,
                                                       salt,
                                                       /*expect_pass=*/false,
                                                       /*break_y_pk=*/true,
                                                       /*break_b_bit=*/false);
        dap_assert(pass_expected,
                   "Soundness: tampered Y_pk must break Claim 1");
        if (!pass_expected) ok = false;
    }
    return ok;
}

static bool s_test_soundness_tampered_b_square(void)
{
    chipmunk_poly_t x_flat[N_RING * CHIPMUNK_LRS_K];
    chipmunk_poly_t pks[N_RING];
    chipmunk_poly_t A_pk[CHIPMUNK_LRS_K];
    dap_assert(chipmunk_lrs_derive_A_pk(A_pk, CHIPMUNK_LRS_PARAMS_C0) == 0,
               "derive A_pk");
    for (uint32_t i = 0u; i < N_RING; ++i) {
        s_derive_x_for_member(&x_flat[i * CHIPMUNK_LRS_K], i);
        dap_assert(chipmunk_lrs_relation_eval(&pks[i], A_pk,
                                              &x_flat[i * CHIPMUNK_LRS_K]) == 0,
                   "pk_i");
    }
    const uint8_t b_indicator[N_RING] = { 1, 0, 1, 0 };

    bool ok = true;
    for (uint8_t salt = 0u; salt < 5u; ++salt) {
        const bool pass_expected = s_check_claim1_once(pks, x_flat, b_indicator,
                                                       salt,
                                                       /*expect_pass=*/false,
                                                       /*break_y_pk=*/false,
                                                       /*break_b_bit=*/true);
        dap_assert(pass_expected,
                   "Soundness: tampered b̃[N] (REL-1 abuse) must break Claim 1");
        if (!pass_expected) ok = false;
    }
    return ok;
}

int main(void)
{
    dap_set_appname("test_chipmunk_mring_statement_eval");
    dap_common_init("test_chipmunk_mring_statement_eval", NULL);

    int rc = 0;
    if (!s_test_augmented_dim())              rc = 1;
    if (!s_test_augment_witness_layout())     rc = 1;
    if (!s_test_aggregate_X_linearity())      rc = 1;
    if (!s_test_claim1_multiple_challenges()) rc = 1;
    if (!s_test_soundness_tampered_y_pk())    rc = 1;
    if (!s_test_soundness_tampered_b_square()) rc = 1;

    if (rc == 0) {
        log_it(L_INFO,
               "MRNG M3.2 unified-statement Claim 1 v2.1 tests PASSED "
               "(honest identity + 2 soundness tampers across 5 challenges each)");
    }
    dap_common_deinit();
    return rc;
}
