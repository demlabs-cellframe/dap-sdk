/*
 * test_chipmunk_mring_fold.c — MRNG M4 halving fold over R_q^{(e)}.
 *
 * CR-11.G Phase 7.7 / task_ac273cea.  Validates in-memory fold prove/verify
 * (MRNG_M4_FOLD.md, G3.1 §4):
 *
 *   T1. padded_dim / fold_depth consistency with chipmunk_mring_fold_depth_for.
 *   T2. Honest prove → verify PASS for N=4, t=2, multiple fs_seeds.
 *   T3. Tampered L_0 → verify FAIL.
 *   T4. Tampered b* → verify FAIL.
 */

#include <dap_common.h>
#include <dap_test.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "chipmunk/chipmunk_mring_fold.h"
#include "sig/chipmunk/chipmunk_mring.h"
#include "chipmunk/chipmunk_mring_statement.h"
#include "chipmunk/chipmunk_lrs.h"

#define LOG_TAG "test_chipmunk_mring_fold"

#define N_RING 4u
#define T_THRESH 2u

static void s_derive_x_for_member(chipmunk_poly_t a_x[CHIPMUNK_LRS_K],
                                  uint32_t a_member_idx)
{
    uint8_t x_seed[CHIPMUNK_LRS_SEED_BYTES];
    for (size_t i = 0u; i < CHIPMUNK_LRS_SEED_BYTES; ++i) {
        x_seed[i] = (uint8_t)(((uint32_t)i * 31u + a_member_idx * 7u) & 0xFFu);
    }
    dap_assert(chipmunk_lrs_derive_witness(a_x, x_seed) == 0,
               "derive_witness");
}

static void s_build_fixture(chipmunk_poly_t *a_pks,
                            chipmunk_poly_t *a_x_flat,
                            uint8_t *a_b_indicator)
{
    chipmunk_poly_t A_pk[CHIPMUNK_LRS_K];
    dap_assert(chipmunk_lrs_derive_A_pk(A_pk, CHIPMUNK_LRS_PARAMS_C0) == 0,
               "derive A_pk");

    a_b_indicator[0] = 1u;
    a_b_indicator[1] = 1u;
    a_b_indicator[2] = 0u;
    a_b_indicator[3] = 0u;

    for (uint32_t i = 0u; i < N_RING; ++i) {
        chipmunk_poly_t x_i[CHIPMUNK_LRS_K];
        s_derive_x_for_member(x_i, i);
        for (uint32_t j = 0u; j < CHIPMUNK_LRS_K; ++j) {
            a_x_flat[i * CHIPMUNK_LRS_K + j] = x_i[j];
        }
        dap_assert(chipmunk_lrs_relation_eval(&a_pks[i], A_pk, x_i) == 0,
                   "pk_i");
    }
}

static void s_sample_c(chipmunk_poly_t *a_c, uint8_t a_salt)
{
    uint8_t seed[CHIPMUNK_LRS_CHALLENGE_SEED_BYTES];
    for (size_t i = 0u; i < CHIPMUNK_LRS_CHALLENGE_SEED_BYTES; ++i) {
        seed[i] = (uint8_t)(0x5Cu ^ (uint8_t)i ^ a_salt);
    }
    dap_assert(chipmunk_lrs_h_to_sparse_ternary(a_c,
                                                "mring-fold-test-c",
                                                CHIPMUNK_LRS_PARAMS_C0,
                                                seed) == 0,
               "sample c");
}

static void test_fold_dim_formulas(void)
{
    const uint32_t l_depth = chipmunk_mring_fold_depth_for(N_RING);
    dap_assert(l_depth == 3u, "N=4 fold_depth=3");
    dap_assert(chipmunk_mring_fold_padded_dim(N_RING) == 8u,
               "N=4 padded_dim=8");
    dap_assert(chipmunk_mring_fold_padded_dim(N_RING)
               == (1u << l_depth), "pad = 2^depth");
}

static void test_honest_fold_roundtrip(uint8_t a_fs_salt)
{
    chipmunk_poly_t pks[N_RING];
    chipmunk_poly_t x_flat[N_RING * CHIPMUNK_LRS_K];
    uint8_t b_ind[N_RING];
    s_build_fixture(pks, x_flat, b_ind);

    chipmunk_poly_t c;
    s_sample_c(&c, a_fs_salt);

    chipmunk_poly_t A_pk[CHIPMUNK_LRS_K];
    dap_assert(chipmunk_lrs_derive_A_pk(A_pk, CHIPMUNK_LRS_PARAMS_C0) == 0,
               "A_pk");

    chipmunk_poly_t X[CHIPMUNK_LRS_K];
    dap_assert(chipmunk_mring_aggregate_X(X, b_ind, x_flat, N_RING) == 0,
               "aggregate X");

    chipmunk_poly_t Y_pk;
    dap_assert(chipmunk_lrs_relation_eval(&Y_pk, A_pk, X) == 0, "Y_pk");

    uint8_t fs_seed[32];
    for (size_t i = 0u; i < sizeof(fs_seed); ++i) {
        fs_seed[i] = (uint8_t)(0xF0u ^ (uint8_t)i ^ a_fs_salt);
    }

    const uint32_t l_depth = chipmunk_mring_fold_depth_for(N_RING);
    chipmunk_mring_fold_proof_t proof;
    dap_assert(chipmunk_mring_fold_proof_alloc(&proof, l_depth) == 0,
               "proof alloc");

    const int rc_prove = chipmunk_mring_fold_prove(&proof, b_ind, N_RING,
                                                   pks, &c, T_THRESH, &Y_pk,
                                                   fs_seed);
    dap_assert(rc_prove == 0, "fold_prove must succeed");

    const int rc_verify = chipmunk_mring_fold_verify(&proof, N_RING,
                                                    pks, &c, T_THRESH, &Y_pk,
                                                    fs_seed);
    dap_assert(rc_verify == 0, "fold_verify must accept honest proof");

    chipmunk_mring_fold_proof_free(&proof);
}

static void test_tampered_L_rejected(void)
{
    chipmunk_poly_t pks[N_RING];
    chipmunk_poly_t x_flat[N_RING * CHIPMUNK_LRS_K];
    uint8_t b_ind[N_RING];
    s_build_fixture(pks, x_flat, b_ind);

    chipmunk_poly_t c;
    s_sample_c(&c, 0x11u);

    chipmunk_poly_t A_pk[CHIPMUNK_LRS_K];
    dap_assert(chipmunk_lrs_derive_A_pk(A_pk, CHIPMUNK_LRS_PARAMS_C0) == 0,
               "A_pk");
    chipmunk_poly_t X[CHIPMUNK_LRS_K];
    dap_assert(chipmunk_mring_aggregate_X(X, b_ind, x_flat, N_RING) == 0,
               "X");
    chipmunk_poly_t Y_pk;
    dap_assert(chipmunk_lrs_relation_eval(&Y_pk, A_pk, X) == 0, "Y_pk");

    uint8_t fs_seed[32];
    memset(fs_seed, 0xAB, sizeof(fs_seed));

    const uint32_t l_depth = chipmunk_mring_fold_depth_for(N_RING);
    chipmunk_mring_fold_proof_t proof;
    dap_assert(chipmunk_mring_fold_proof_alloc(&proof, l_depth) == 0,
               "alloc");
    dap_assert(chipmunk_mring_fold_prove(&proof, b_ind, N_RING,
                                         pks, &c, T_THRESH, &Y_pk,
                                         fs_seed) == 0,
               "prove");

    proof.rounds[0].L.c[0].coeffs[0] =
        (proof.rounds[0].L.c[0].coeffs[0] + 1u) % CHIPMUNK_Q;

    const int rc = chipmunk_mring_fold_verify(&proof, N_RING,
                                              pks, &c, T_THRESH, &Y_pk,
                                              fs_seed);
    dap_assert(rc == -EBADMSG, "tampered L must fail verify");

    chipmunk_mring_fold_proof_free(&proof);
}

static void test_tampered_bstar_rejected(void)
{
    chipmunk_poly_t pks[N_RING];
    chipmunk_poly_t x_flat[N_RING * CHIPMUNK_LRS_K];
    uint8_t b_ind[N_RING];
    s_build_fixture(pks, x_flat, b_ind);

    chipmunk_poly_t c;
    s_sample_c(&c, 0x22u);

    chipmunk_poly_t A_pk[CHIPMUNK_LRS_K];
    dap_assert(chipmunk_lrs_derive_A_pk(A_pk, CHIPMUNK_LRS_PARAMS_C0) == 0,
               "A_pk");
    chipmunk_poly_t X[CHIPMUNK_LRS_K];
    dap_assert(chipmunk_mring_aggregate_X(X, b_ind, x_flat, N_RING) == 0,
               "X");
    chipmunk_poly_t Y_pk;
    dap_assert(chipmunk_lrs_relation_eval(&Y_pk, A_pk, X) == 0, "Y_pk");

    uint8_t fs_seed[32];
    memset(fs_seed, 0xCD, sizeof(fs_seed));

    const uint32_t l_depth = chipmunk_mring_fold_depth_for(N_RING);
    chipmunk_mring_fold_proof_t proof;
    dap_assert(chipmunk_mring_fold_proof_alloc(&proof, l_depth) == 0,
               "alloc");
    dap_assert(chipmunk_mring_fold_prove(&proof, b_ind, N_RING,
                                         pks, &c, T_THRESH, &Y_pk,
                                         fs_seed) == 0,
               "prove");

    proof.b_star.c[0].coeffs[1] =
        (proof.b_star.c[0].coeffs[1] + 1u) % CHIPMUNK_Q;

    const int rc = chipmunk_mring_fold_verify(&proof, N_RING,
                                              pks, &c, T_THRESH, &Y_pk,
                                              fs_seed);
    dap_assert(rc == -EBADMSG, "tampered b* must fail verify");

    chipmunk_mring_fold_proof_free(&proof);
}

int main(void)
{
    log_it(L_INFO, "=== MRNG M4 fold tests ===");

    test_fold_dim_formulas();
    test_honest_fold_roundtrip(0u);
    test_honest_fold_roundtrip(1u);
    test_honest_fold_roundtrip(42u);
    test_tampered_L_rejected();
    test_tampered_bstar_rejected();

    log_it(L_INFO, "=== ALL MRNG M4 fold tests PASSED ===");
    return 0;
}
