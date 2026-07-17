/*
 * test_chipmunk_mring_transcript.c — MRNG G4 byte-exact FS transcript.
 *
 * T1. ring_hash canonical order + duplicate rejection.
 * T2. ctx_hash / msg_hash domain separation.
 * T3. fs_seed binds T and C_b (flip one byte → different digest).
 * T4. transcript_sample_c deterministic in fs_seed.
 * T5. fold_round_fs matches pre-G4 fold path (via prove/verify still PASS).
 * T6. bind_fs + c_star + same-witness z_x reconstruction identity.
 */

#include <dap_common.h>
#include <dap_enc.h>
#include <dap_test.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "chipmunk/chipmunk_mring_transcript.h"
#include "chipmunk/chipmunk_mring_fold.h"
#include "sig/chipmunk/chipmunk_mring.h"
#include "chipmunk/chipmunk_mring_statement.h"
#include "chipmunk/chipmunk_mring_params.h"
#include "chipmunk/chipmunk_lrs.h"

#define LOG_TAG "test_chipmunk_mring_transcript"

#define N_RING 4u
#define T_THRESH 2u

static void s_fill_seed(uint8_t *a_out, size_t a_len, uint8_t a_salt)
{
    for (size_t i = 0u; i < a_len; ++i) {
        a_out[i] = (uint8_t)(0x71u ^ (uint8_t)i ^ a_salt);
    }
}

static void s_make_ring(chipmunk_lrs_public_key_t *a_ring, uint32_t a_n)
{
    for (uint32_t i = 0u; i < a_n; ++i) {
        uint8_t sk_seed[CHIPMUNK_LRS_SEED_BYTES];
        s_fill_seed(sk_seed, sizeof(sk_seed), (uint8_t)(0x10u + i));
        chipmunk_lrs_secret_key_t sk;
        dap_assert(chipmunk_lrs_keypair_from_seeds(&a_ring[i], &sk,
                                                   sk_seed) == 0,
                   "keypair");
    }
}

static void test_ring_hash_canonical(void)
{
    chipmunk_lrs_public_key_t ring[N_RING];
    s_make_ring(ring, N_RING);

    uint8_t h_ab[32], h_ba[32];
    chipmunk_lrs_public_key_t perm[N_RING];
    perm[0] = ring[2];
    perm[1] = ring[0];
    perm[2] = ring[3];
    perm[3] = ring[1];

    dap_assert(chipmunk_mring_hash_ring(h_ab, ring, N_RING) == 0, "hash ab");
    dap_assert(chipmunk_mring_hash_ring(h_ba, perm, N_RING) == 0, "hash ba");
    dap_assert(memcmp(h_ab, h_ba, 32u) == 0, "order invariant");

    chipmunk_lrs_public_key_t dup[N_RING];
    memcpy(dup, ring, sizeof(ring));
    dup[3] = ring[0];
    dap_assert(chipmunk_mring_hash_ring(h_ab, dup, N_RING) == -EEXIST,
               "duplicate pk rejected");
}

static void test_ctx_msg_hash(void)
{
    uint8_t h1[32], h2[32];
    const uint8_t ctx_a[] = { 0x01u, 0x02u };
    const uint8_t ctx_b[] = { 0x01u, 0x03u };

    dap_assert(chipmunk_mring_hash_ctx(h1, CHIPMUNK_MRING_PARAMS_ID,
                                       ctx_a, sizeof(ctx_a)) == 0,
               "ctx a");
    dap_assert(chipmunk_mring_hash_ctx(h2, CHIPMUNK_MRING_PARAMS_ID,
                                       ctx_b, sizeof(ctx_b)) == 0,
               "ctx b");
    dap_assert(memcmp(h1, h2, 32u) != 0, "ctx differs");

    const uint8_t msg[] = "mring-g4-test";
    dap_assert(chipmunk_mring_hash_msg(h1, CHIPMUNK_MRING_PARAMS_ID,
                                       msg, sizeof(msg) - 1u) == 0,
               "msg hash");
}

static void test_fs_seed_binding(void)
{
    uint8_t ring_h[32], ctx_h[32], msg_h[32];
    s_fill_seed(ring_h, 32u, 0x01u);
    s_fill_seed(ctx_h, 32u, 0x02u);
    s_fill_seed(msg_h, 32u, 0x03u);

    uint8_t T_q[CHIPMUNK_MRING_POLY_QPACK];
    uint8_t C_q[CHIPMUNK_MRING_POLY_QPACK];
    memset(T_q, 0xAA, sizeof(T_q));
    memset(C_q, 0xBB, sizeof(C_q));

    uint8_t fs1[32], fs2[32];
    dap_assert(chipmunk_mring_fs_seed(fs1, ring_h, ctx_h, msg_h, T_q, C_q) == 0,
               "fs seed");
    C_q[0] ^= 1u;
    dap_assert(chipmunk_mring_fs_seed(fs2, ring_h, ctx_h, msg_h, T_q, C_q) == 0,
               "fs seed tampered");
    dap_assert(memcmp(fs1, fs2, 32u) != 0, "C_b flip changes fs_seed");
}

static void test_sample_c_deterministic(void)
{
    uint8_t fs[32];
    s_fill_seed(fs, sizeof(fs), 0x44u);

    chipmunk_poly_t c1, c2;
    dap_assert(chipmunk_mring_transcript_sample_c(&c1, fs) == 0, "c1");
    dap_assert(chipmunk_mring_transcript_sample_c(&c2, fs) == 0, "c2");
    dap_assert(memcmp(&c1, &c2, sizeof(c1)) == 0, "c deterministic");
}

static chipmunk_mring_fold_proof_t *s_proof_new(uint32_t a_depth)
{
    chipmunk_mring_fold_proof_t *l_proof =
        DAP_NEW_Z(chipmunk_mring_fold_proof_t);
    dap_assert(l_proof != NULL, "alloc");
    dap_assert(chipmunk_mring_fold_proof_alloc(l_proof, a_depth) == 0,
               "proof alloc");
    return l_proof;
}

static void s_proof_delete(chipmunk_mring_fold_proof_t *a_proof)
{
    if (!a_proof) {
        return;
    }
    chipmunk_mring_fold_proof_free(a_proof);
    DAP_DELETE(a_proof);
}

static void test_bind_joint_roundtrip(void)
{
    chipmunk_poly_t pks[N_RING];
    uint8_t b_ind[N_RING] = { 1u, 1u, 0u, 0u };
    chipmunk_poly_t A_pk[CHIPMUNK_LRS_K];
    dap_assert(chipmunk_lrs_derive_A_pk(A_pk, CHIPMUNK_LRS_PARAMS_C0) == 0,
               "A_pk");

    chipmunk_poly_t X[CHIPMUNK_LRS_K];
    chipmunk_poly_t x_flat[N_RING * CHIPMUNK_LRS_K];
    for (uint32_t i = 0u; i < N_RING; ++i) {
        uint8_t x_seed[CHIPMUNK_LRS_SEED_BYTES];
        s_fill_seed(x_seed, sizeof(x_seed), (uint8_t)(0x20u + i));
        dap_assert(chipmunk_lrs_derive_witness(&x_flat[i * CHIPMUNK_LRS_K],
                                               x_seed) == 0,
                   "x_i");
        dap_assert(chipmunk_lrs_relation_eval(
                       &pks[i], A_pk, &x_flat[i * CHIPMUNK_LRS_K]) == 0,
                   "pk_i");
    }
    dap_assert(chipmunk_mring_aggregate_X(X, b_ind, x_flat, N_RING) == 0,
               "aggregate X");

    chipmunk_poly_t Y_pk;
    dap_assert(chipmunk_lrs_relation_eval(&Y_pk, A_pk, X) == 0, "Y_pk");

    uint8_t fs_seed[32], ring_hash[32], opening_seed[32];
    s_fill_seed(ring_hash, sizeof(ring_hash), 0x55u);
    s_fill_seed(fs_seed, sizeof(fs_seed), 0x66u);
    s_fill_seed(opening_seed, sizeof(opening_seed), 0x77u);

    chipmunk_poly_t c;
    dap_assert(chipmunk_mring_transcript_sample_c(&c, fs_seed) == 0, "c");

    uint8_t ctx_h[32];
    s_fill_seed(ctx_h, sizeof(ctx_h), 0x99u);
    chipmunk_poly_t A_T[CHIPMUNK_LRS_K];
    dap_assert(chipmunk_mring_derive_A_T(A_T, ring_hash, ctx_h) == 0, "A_T");

    chipmunk_poly_t T_tag;
    dap_assert(chipmunk_lrs_relation_eval(&T_tag, A_T, X) == 0, "T");

    const uint32_t l_depth = chipmunk_mring_fold_depth_for(N_RING);
    chipmunk_mring_fold_proof_t *l_proof = s_proof_new(l_depth);
    dap_assert(chipmunk_mring_fold_prove(l_proof, b_ind, N_RING, pks, &c,
                                         T_THRESH, &Y_pk, ring_hash, fs_seed,
                                         opening_seed) == 0,
               "fold prove");

    chipmunk_poly_t rho_x[CHIPMUNK_LRS_K];
    chipmunk_poly_t z_x[CHIPMUNK_LRS_K];
    chipmunk_poly_t c_star;
    uint8_t mask_seed[32];
    s_fill_seed(mask_seed, sizeof(mask_seed), 0x88u);

    int rc_prove = -EAGAIN;
    for (uint32_t att = 0u;
         att < CHIPMUNK_MRING_MAX_ATTEMPTS && rc_prove == -EAGAIN;
         ++att) {
        dap_assert(chipmunk_mring_bind_mask_sample(rho_x, mask_seed, att) == 0,
                   "mask sample");

        chipmunk_poly_t M_pk, M_T;
        dap_assert(chipmunk_lrs_relation_eval(&M_pk, A_pk, rho_x) == 0,
                   "M_pk");
        dap_assert(chipmunk_lrs_relation_eval(&M_T, A_T, rho_x) == 0,
                   "M_T");

        uint8_t bind_fs[32];
        dap_assert(chipmunk_mring_transcript_bind_fs(
                       bind_fs, fs_seed, &c, &M_pk, &M_T, l_proof,
                       l_depth) == 0,
                   "bind fs");
        dap_assert(chipmunk_mring_transcript_sample_c_star(&c_star, bind_fs)
                   == 0,
                   "c*");
        rc_prove = chipmunk_mring_bind_prove_z_x(z_x, rho_x, &c_star, X);
    }
    dap_assert(rc_prove == 0, "z_x prove converged");

    chipmunk_poly_t M_pk_v, M_T_v;
    dap_assert(chipmunk_mring_bind_verify_reconstruct(
                   &M_pk_v, &M_T_v, A_pk, A_T, z_x, &c_star, &Y_pk, &T_tag)
               == 0,
               "bind verify");

    dap_assert(chipmunk_mring_fold_verify(l_proof, N_RING, pks, &c, T_THRESH,
                                          &Y_pk, ring_hash, fs_seed) == 0,
               "fold verify after bind path");

    s_proof_delete(l_proof);
}

int main(void)
{
    dap_set_appname("test_chipmunk_mring_transcript");
    dap_common_init("test_chipmunk_mring_transcript", NULL);
    /* Initialise crypto subsystem (SIMD dispatch, chipmunk, etc.) */
    dap_enc_init();

    test_ring_hash_canonical();
    test_ctx_msg_hash();
    test_fs_seed_binding();
    test_sample_c_deterministic();
    test_bind_joint_roundtrip();

    log_it(L_INFO, "=== ALL MRNG G4 transcript tests PASSED ===");
    return 0;
}
