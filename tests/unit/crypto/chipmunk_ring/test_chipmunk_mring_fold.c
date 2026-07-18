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
 *   T5. Wire write → read roundtrip preserves verify acceptance (M4.1).
 *   T6. chipmunk_mring_wire_size matches pinned formula for N=4.
 *   T7. ext qpack → qunpack roundtrip on fold C_L commitment.
 *   T8. fold_opening_seed derivation is deterministic.
 *   T9. vcom commit → open roundtrip on one Y-component of C_L.
 *  T10. leaf_mask 49-bit pack → unpack roundtrip.
 *  T11. tampered leaf_mask → verify FAIL.
 */

#include <dap_common.h>
#include <dap_enc.h>
#include <dap_test.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "chipmunk/chipmunk_mring_fold.h"
#include "chipmunk/chipmunk_mring_params.h"
#include "sig/chipmunk/chipmunk_mring.h"
#include "chipmunk/chipmunk_mring_statement.h"
#include "chipmunk/chipmunk_lrs.h"

#define LOG_TAG "test_chipmunk_mring_fold"

#define N_RING 4u
#define T_THRESH 2u

static chipmunk_mring_fold_proof_t *s_proof_new(uint32_t a_depth)
{
    chipmunk_mring_fold_proof_t *l_proof =
        DAP_NEW_Z(chipmunk_mring_fold_proof_t);
    dap_assert(l_proof != NULL, "proof heap alloc");
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

static void s_fill_ring_hash(uint8_t a_out[32], uint8_t a_byte)
{
    for (size_t i = 0u; i < 32u; ++i) {
        a_out[i] = (uint8_t)(a_byte ^ (uint8_t)i);
    }
}

static void s_fill_opening_seed(uint8_t a_out[32], uint8_t a_salt)
{
    for (size_t i = 0u; i < 32u; ++i) {
        a_out[i] = (uint8_t)(0x2Du ^ (uint8_t)i ^ a_salt);
    }
}

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
        dap_assert(chipmunk_lrs_relation_eval(&a_pks[i], A_pk, x_i, (uint64_t)CHIPMUNK_Q) == 0,
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
    dap_assert(chipmunk_mring_aggregate_X(X, b_ind, x_flat, N_RING, (uint64_t)CHIPMUNK_Q) == 0,
               "aggregate X");

    chipmunk_poly_t Y_pk;
    dap_assert(chipmunk_lrs_relation_eval(&Y_pk, A_pk, X, (uint64_t)CHIPMUNK_Q) == 0, "Y_pk");

    uint8_t fs_seed[32];
    uint8_t ring_hash[32];
    uint8_t opening_seed[32];
    for (size_t i = 0u; i < sizeof(fs_seed); ++i) {
        fs_seed[i] = (uint8_t)(0xF0u ^ (uint8_t)i ^ a_fs_salt);
    }
    s_fill_ring_hash(ring_hash, (uint8_t)(0xA0u ^ a_fs_salt));
    s_fill_opening_seed(opening_seed, a_fs_salt);

    const uint32_t l_depth = chipmunk_mring_fold_depth_for(N_RING);
    chipmunk_mring_fold_proof_t *l_proof = s_proof_new(l_depth);

    const int rc_prove = chipmunk_mring_fold_prove(l_proof, b_ind, N_RING,
                                                   pks, &c, T_THRESH, &Y_pk,
                                                   ring_hash, fs_seed,
                                                   opening_seed, (uint64_t)CHIPMUNK_Q);
    dap_assert(rc_prove == 0, "fold_prove must succeed");

    const int rc_verify = chipmunk_mring_fold_verify(l_proof, N_RING,
                                                    pks, &c, T_THRESH, &Y_pk,
                                                    ring_hash, fs_seed, (uint64_t)CHIPMUNK_Q);
    dap_assert(rc_verify == 0, "fold_verify must accept honest proof");

    s_proof_delete(l_proof);
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
    dap_assert(chipmunk_mring_aggregate_X(X, b_ind, x_flat, N_RING, (uint64_t)CHIPMUNK_Q) == 0,
               "X");
    chipmunk_poly_t Y_pk;
    dap_assert(chipmunk_lrs_relation_eval(&Y_pk, A_pk, X, (uint64_t)CHIPMUNK_Q) == 0, "Y_pk");

    uint8_t fs_seed[32];
    uint8_t ring_hash[32];
    uint8_t opening_seed[32];
    memset(fs_seed, 0xAB, sizeof(fs_seed));
    s_fill_ring_hash(ring_hash, 0x11u);
    s_fill_opening_seed(opening_seed, 0x11u);

    const uint32_t l_depth = chipmunk_mring_fold_depth_for(N_RING);
    chipmunk_mring_fold_proof_t *l_proof = s_proof_new(l_depth);
    dap_assert(chipmunk_mring_fold_prove(l_proof, b_ind, N_RING,
                                         pks, &c, T_THRESH, &Y_pk,
                                         ring_hash, fs_seed,
                                         opening_seed, (uint64_t)CHIPMUNK_Q) == 0,
               "prove");

    l_proof->rounds[0].C_L.c[0].coeffs[0] =
        (l_proof->rounds[0].C_L.c[0].coeffs[0] + 1u) % CHIPMUNK_Q;

    const int rc = chipmunk_mring_fold_verify(l_proof, N_RING,
                                              pks, &c, T_THRESH, &Y_pk,
                                              ring_hash, fs_seed, (uint64_t)CHIPMUNK_Q);
    dap_assert(rc == -EBADMSG, "tampered L must fail verify");

    s_proof_delete(l_proof);
}

static void s_prove_fixture(chipmunk_mring_fold_proof_t *a_proof,
                            chipmunk_poly_t *a_pks,
                            chipmunk_poly_t *a_c,
                            chipmunk_poly_t *a_Y_pk,
                            uint8_t *a_ring_hash,
                            uint8_t *a_fs_seed,
                            uint8_t *a_opening_seed,
                            uint8_t a_salt)
{
    chipmunk_poly_t x_flat[N_RING * CHIPMUNK_LRS_K];
    uint8_t b_ind[N_RING];
    s_build_fixture(a_pks, x_flat, b_ind);
    s_sample_c(a_c, a_salt);

    chipmunk_poly_t A_pk[CHIPMUNK_LRS_K];
    dap_assert(chipmunk_lrs_derive_A_pk(A_pk, CHIPMUNK_LRS_PARAMS_C0) == 0,
               "A_pk");
    chipmunk_poly_t X[CHIPMUNK_LRS_K];
    dap_assert(chipmunk_mring_aggregate_X(X, b_ind, x_flat, N_RING, (uint64_t)CHIPMUNK_Q) == 0,
               "X");
    dap_assert(chipmunk_lrs_relation_eval(a_Y_pk, A_pk, X, (uint64_t)CHIPMUNK_Q) == 0, "Y_pk");

    for (size_t i = 0u; i < sizeof(*a_fs_seed); ++i) {
        a_fs_seed[i] = (uint8_t)(0xA7u ^ (uint8_t)i ^ a_salt);
    }
    s_fill_ring_hash(a_ring_hash, (uint8_t)(0xB1u ^ a_salt));
    s_fill_opening_seed(a_opening_seed, a_salt);

    const uint32_t l_depth = chipmunk_mring_fold_depth_for(N_RING);
    dap_assert(chipmunk_mring_fold_prove(a_proof, b_ind, N_RING,
                                         a_pks, a_c, T_THRESH, a_Y_pk,
                                         a_ring_hash, a_fs_seed,
                                         a_opening_seed, (uint64_t)CHIPMUNK_Q) == 0,
               "prove");
}

static void test_wire_roundtrip_verify(void)
{
    chipmunk_poly_t pks[N_RING];
    chipmunk_poly_t c, Y_pk;
    uint8_t ring_hash[32];
    uint8_t fs_seed[32];
    uint8_t opening_seed[32];

    const uint32_t l_depth = chipmunk_mring_fold_depth_for(N_RING);
    chipmunk_mring_fold_proof_t *l_proof = s_proof_new(l_depth);
    s_prove_fixture(l_proof, pks, &c, &Y_pk, ring_hash, fs_seed,
                    opening_seed, 0x33u);

    const uint32_t l_wire = chipmunk_mring_wire_size(l_depth);
    uint8_t *l_buf = DAP_NEW_Z_COUNT(uint8_t, l_wire);
    dap_assert(l_buf != NULL, "wire buffer alloc");

    dap_assert(chipmunk_mring_fold_write(l_buf, l_wire, l_depth, l_proof) == 0,
               "fold_write");

    chipmunk_mring_fold_proof_t *l_parsed = s_proof_new(l_depth);
    dap_assert(chipmunk_mring_fold_read(l_parsed, l_depth, l_buf, l_wire, (uint64_t)CHIPMUNK_Q) == 0,
               "fold_read");

    dap_assert(chipmunk_mring_fold_verify(l_parsed, N_RING, pks, &c, T_THRESH,
                                          &Y_pk, ring_hash, fs_seed, (uint64_t)CHIPMUNK_Q) == 0,
               "verify after wire roundtrip");

    s_proof_delete(l_parsed);
    s_proof_delete(l_proof);
    DAP_DELETE(l_buf);
}

static void test_wire_size_formula(void)
{
    const uint32_t l_depth = chipmunk_mring_fold_depth_for(N_RING);
    const uint32_t l_wire = chipmunk_mring_wire_size(l_depth);
    const uint32_t l_expected = 33532u + l_depth * 16896u;
    dap_assert(l_wire == l_expected,
               "wire_size matches M4.3 formula for N=4");
}

static void test_ext_qpack_roundtrip(void)
{
    chipmunk_poly_t pks[N_RING];
    chipmunk_poly_t c, Y_pk;
    uint8_t ring_hash[32];
    uint8_t fs_seed[32];
    uint8_t opening_seed[32];

    const uint32_t l_depth = chipmunk_mring_fold_depth_for(N_RING);
    chipmunk_mring_fold_proof_t *l_proof = s_proof_new(l_depth);
    s_prove_fixture(l_proof, pks, &c, &Y_pk, ring_hash, fs_seed,
                    opening_seed, 0x44u);

    uint8_t l_packed[CHIPMUNK_FQ6_EXT_QPACK_BYTES];
    chipmunk_fq6_ext_t l_restored;
    dap_assert(chipmunk_fq6_ext_qpack(l_packed, sizeof(l_packed),
                                        &l_proof->rounds[0].C_L) == 0,
               "ext_qpack");
    dap_assert(chipmunk_fq6_ext_qunpack(&l_restored, l_packed,
                                          sizeof(l_packed), (uint64_t)CHIPMUNK_Q) == 0,
               "ext_qunpack");

    for (uint32_t j = 0u; j < (uint32_t)CHIPMUNK_FQ6_EXT_DEG; ++j) {
        for (size_t k = 0u; k < CHIPMUNK_N; ++k) {
            dap_assert(l_proof->rounds[0].C_L.c[j].coeffs[k]
                       == l_restored.c[j].coeffs[k],
                       "qpack roundtrip coeff match");
        }
    }

    s_proof_delete(l_proof);
}

static void test_opening_derivation_deterministic(void)
{
    uint8_t seed[32];
    s_fill_opening_seed(seed, 0x55u);

    chipmunk_poly_t r1[CHIPMUNK_MRING_K_PK];
    chipmunk_poly_t r2[CHIPMUNK_MRING_K_PK];
    dap_assert(chipmunk_mring_fold_derive_opening(r1, seed, 1u, 0u, 2u) == 0,
               "derive opening");
    dap_assert(chipmunk_mring_fold_derive_opening(r2, seed, 1u, 0u, 2u) == 0,
               "derive opening again");
    for (uint32_t k = 0u; k < CHIPMUNK_MRING_K_PK; ++k) {
        for (size_t i = 0u; i < CHIPMUNK_N; ++i) {
            dap_assert(r1[k].coeffs[i] == r2[k].coeffs[i],
                       "opening derivation deterministic");
        }
    }
}

static void test_vcom_commit_open_roundtrip(void)
{
    uint8_t ring_hash[32];
    uint8_t opening_seed[32];
    s_fill_ring_hash(ring_hash, 0x77u);
    s_fill_opening_seed(opening_seed, 0x88u);

    chipmunk_mring_vcom_gens_t gens;
    dap_assert(chipmunk_mring_derive_vcom_generators(&gens, ring_hash) == 0,
               "vcom gens");

    chipmunk_poly_t l_r[CHIPMUNK_MRING_K_PK];
    dap_assert(chipmunk_mring_fold_derive_opening(l_r, opening_seed,
                                                  0u, 1u, 0u) == 0,
               "derive r");

    chipmunk_poly_t l_msg;
    memset(&l_msg, 0, sizeof(l_msg));
    l_msg.coeffs[3] = 5;
    l_msg.coeffs[7] = -2;

    chipmunk_poly_t l_C;
    dap_assert(chipmunk_mring_vcom_commit(&l_C, &gens, &l_msg, l_r, (uint64_t)CHIPMUNK_Q) == 0,
               "vcom commit");

    chipmunk_poly_t l_opened;
    dap_assert(chipmunk_mring_vcom_open(&l_opened, &l_C, &gens, l_r, (uint64_t)CHIPMUNK_Q) == 0,
               "vcom open");

    for (size_t i = 0u; i < CHIPMUNK_N; ++i) {
        int32_t l_a = l_msg.coeffs[i];
        int32_t l_b = l_opened.coeffs[i];
        if (l_a < 0) {
            l_a += (int32_t)CHIPMUNK_Q;
        }
        if (l_b < 0) {
            l_b += (int32_t)CHIPMUNK_Q;
        }
        dap_assert(l_a == l_b, "vcom open recovers message mod q");
    }
}

static void test_leaf_mask_pack_roundtrip(void)
{
    int64_t l_coeffs[CHIPMUNK_MRING_N];
    uint8_t seed[32];
    s_fill_opening_seed(seed, 0x66u);

    const int64_t l_bound = chipmunk_mring_leaf_bound_for_depth(3u);
    dap_assert(chipmunk_mring_leaf_mask_sample(l_coeffs, seed, l_bound) == 0,
               "leaf sample");

    uint8_t l_packed[CHIPMUNK_MRING_LEAF_MASK_BYTES];
    int64_t l_restored[CHIPMUNK_MRING_N];
    dap_assert(chipmunk_mring_leaf_mask_pack(
                   l_packed, sizeof(l_packed), l_coeffs,
                   CHIPMUNK_MRING_LEAF_BOUND_MAX) == 0,
               "leaf pack");
    dap_assert(chipmunk_mring_leaf_mask_unpack(
                   l_restored, l_packed, sizeof(l_packed),
                   CHIPMUNK_MRING_LEAF_BOUND_MAX) == 0,
               "leaf unpack");

    for (size_t i = 0u; i < CHIPMUNK_MRING_N; ++i) {
        dap_assert(l_coeffs[i] == l_restored[i], "leaf pack roundtrip");
    }
}

static void test_tampered_leaf_mask_rejected(void)
{
    chipmunk_poly_t pks[N_RING];
    chipmunk_poly_t c, Y_pk;
    uint8_t ring_hash[32];
    uint8_t fs_seed[32];
    uint8_t opening_seed[32];

    const uint32_t l_depth = chipmunk_mring_fold_depth_for(N_RING);
    chipmunk_mring_fold_proof_t *l_proof = s_proof_new(l_depth);
    s_prove_fixture(l_proof, pks, &c, &Y_pk, ring_hash, fs_seed,
                    opening_seed, 0x99u);

    l_proof->leaf_mask[0] += 1;

    const int rc = chipmunk_mring_fold_verify(l_proof, N_RING,
                                              pks, &c, T_THRESH, &Y_pk,
                                              ring_hash, fs_seed, (uint64_t)CHIPMUNK_Q);
    dap_assert(rc == -EBADMSG, "tampered leaf_mask must fail verify");

    s_proof_delete(l_proof);
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
    dap_assert(chipmunk_mring_aggregate_X(X, b_ind, x_flat, N_RING, (uint64_t)CHIPMUNK_Q) == 0,
               "X");
    chipmunk_poly_t Y_pk;
    dap_assert(chipmunk_lrs_relation_eval(&Y_pk, A_pk, X, (uint64_t)CHIPMUNK_Q) == 0, "Y_pk");

    uint8_t fs_seed[32];
    uint8_t ring_hash[32];
    uint8_t opening_seed[32];
    memset(fs_seed, 0xCD, sizeof(fs_seed));
    s_fill_ring_hash(ring_hash, 0x22u);
    s_fill_opening_seed(opening_seed, 0x22u);

    const uint32_t l_depth = chipmunk_mring_fold_depth_for(N_RING);
    chipmunk_mring_fold_proof_t *l_proof = s_proof_new(l_depth);
    dap_assert(chipmunk_mring_fold_prove(l_proof, b_ind, N_RING,
                                         pks, &c, T_THRESH, &Y_pk,
                                         ring_hash, fs_seed,
                                         opening_seed, (uint64_t)CHIPMUNK_Q) == 0,
               "prove");

    l_proof->b_star.c[0].coeffs[1] =
        (l_proof->b_star.c[0].coeffs[1] + 1u) % CHIPMUNK_Q;

    const int rc = chipmunk_mring_fold_verify(l_proof, N_RING,
                                              pks, &c, T_THRESH, &Y_pk,
                                              ring_hash, fs_seed, (uint64_t)CHIPMUNK_Q);
    dap_assert(rc == -EBADMSG, "tampered b* must fail verify");

    s_proof_delete(l_proof);
}

int main(void)
{
    dap_set_appname("test_chipmunk_mring_fold");
    dap_common_init(dap_get_appname(), NULL);
    /* Initialise crypto subsystem (SIMD dispatch, chipmunk, etc.) */
    dap_enc_init();

    log_it(L_INFO, "=== MRNG M4 fold tests ===");

    test_fold_dim_formulas();
    test_honest_fold_roundtrip(0u);
    test_honest_fold_roundtrip(1u);
    test_honest_fold_roundtrip(42u);
    test_tampered_L_rejected();
    test_tampered_bstar_rejected();
    test_wire_roundtrip_verify();
    test_wire_size_formula();
    test_ext_qpack_roundtrip();
    test_opening_derivation_deterministic();
    test_vcom_commit_open_roundtrip();
    test_leaf_mask_pack_roundtrip();
    test_tampered_leaf_mask_rejected();

    log_it(L_INFO, "=== ALL MRNG M4 fold tests PASSED ===");
    return 0;
}
