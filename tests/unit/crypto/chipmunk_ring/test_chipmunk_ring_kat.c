/*
 * test_chipmunk_ring_kat.c — Ring KAT + security + wire malformed inputs.
 *
 * T1. Deterministic keygen produces byte-identical PK for same seed.
 * T2. Sign/verify round-trip N=1, N=2, N=4.
 * T3. Wrong message fails.
 * T4. Wrong ring (swapped PK) fails.
 * T5. Tampered T block fails.
 * T6. Tampered c block fails.
 * T7. Tampered z block fails.
 * T8. Truncated signature fails.
 * T9. Bad magic fails.
 * T10. Bad version fails.
 * T11. Bad N in header fails.
 * T12. Wire size formula check.
 */

#include <dap_common.h>
#include <dap_hash_sha3.h>
#include <dap_test.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "sig/chipmunk/chipmunk_ring.h"
#include "sig/lotrs/lotrs_params.h"

#define LOG_TAG "test_chipmunk_ring_kat"

static const uint8_t k_seed0[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};
static const uint8_t k_seed1[32] = {
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
};
static const uint8_t k_seed2[32] = {
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
    0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
    0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
};
static const uint8_t k_seed3[32] = {
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
    0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
};

static const uint8_t k_msg[] = "ring-kat-canonical-message";
static const uint8_t k_sign_seed[32] = {
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
    0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
    0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99,
};

/* --- Helpers --- */

static void s_build_ring(const lotrs_params_t *a_par,
                         chipmunk_ring_table_t *a_ring,
                         const chipmunk_ring_keypair_t *a_kps,
                         uint32_t a_N)
{
    a_ring->N = a_N;
    a_ring->pks = DAP_NEW_Z_COUNT(chipmunk_ring_pk_t, a_N);
    for (uint32_t i = 0u; i < a_N; ++i) {
        a_ring->pks[i].a_hat = lotrs_polyvec_alloc(a_par, a_par->k);
        for (uint32_t j = 0u; j < a_par->k; ++j) {
            lotrs_poly_copy(a_ring->pks[i].a_hat.polys[j],
                            a_kps[i].pk.a_hat.polys[j], a_par);
        }
    }
}

static int s_sign_retry(chipmunk_ring_sig_t *a_sig,
                        const lotrs_params_t *a_par,
                        const chipmunk_ring_table_t *a_ring,
                        const chipmunk_ring_sk_t *a_sk,
                        uint32_t a_signer_idx,
                        const uint8_t *a_msg, size_t a_msg_len,
                        const uint8_t a_seed[32])
{
    uint8_t l_seed[32];
    memcpy(l_seed, a_seed, 32u);
    for (int i = 0; i < 16; ++i) {
        int rc = chipmunk_ring_sign(a_sig, a_par, a_ring, a_sk, a_signer_idx,
                                    a_msg, a_msg_len, l_seed);
        if (rc == 0) return 0;
        if (rc != -EAGAIN) return rc;
        l_seed[0] ^= (uint8_t)(i + 1);
    }
    return -EAGAIN;
}

/* --- Tests --- */

static void test_deterministic_keygen(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kp1 = {0}, l_kp2 = {0};

    int rc1 = chipmunk_ring_keygen(&l_kp1, l_par, k_seed0);
    int rc2 = chipmunk_ring_keygen(&l_kp2, l_par, k_seed0);
    dap_assert(rc1 == 0, "keygen1 OK");
    dap_assert(rc2 == 0, "keygen2 OK");

    for (uint32_t i = 0u; i < l_par->k; ++i) {
        for (uint32_t j = 0u; j < l_par->d; ++j) {
            dap_assert(l_kp1.pk.a_hat.polys[i]->coeffs[j] ==
                       l_kp2.pk.a_hat.polys[i]->coeffs[j],
                       "deterministic PK");
        }
    }

    chipmunk_ring_keypair_free(&l_kp1);
    chipmunk_ring_keypair_free(&l_kp2);
}

static void test_sign_verify_n1(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kp = {0};
    dap_assert(chipmunk_ring_keygen(&l_kp, l_par, k_seed0) == 0, "keygen");

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, &l_kp, 1u);

    chipmunk_ring_sig_t l_sig = {0};
    int rc = s_sign_retry(&l_sig, l_par, &l_ring, &l_kp.sk, 0,
                          k_msg, sizeof(k_msg) - 1, k_sign_seed);
    dap_assert(rc == 0, "sign N=1");
    dap_assert(l_sig.data != NULL && l_sig.len > 0, "sig non-empty");

    rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(rc == 0, "verify N=1");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    chipmunk_ring_keypair_free(&l_kp);
}

static void test_sign_verify_n2(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kps[2] = {0};
    dap_assert(chipmunk_ring_keygen(&l_kps[0], l_par, k_seed0) == 0, "keygen0");
    dap_assert(chipmunk_ring_keygen(&l_kps[1], l_par, k_seed1) == 0, "keygen1");

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, l_kps, 2u);

    chipmunk_ring_sig_t l_sig = {0};
    int rc = s_sign_retry(&l_sig, l_par, &l_ring, &l_kps[0].sk, 0,
                          k_msg, sizeof(k_msg) - 1, k_sign_seed);
    dap_assert(rc == 0, "sign N=2");

    rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(rc == 0, "verify N=2");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    chipmunk_ring_keypair_free(&l_kps[0]);
    chipmunk_ring_keypair_free(&l_kps[1]);
}

static void test_sign_verify_n4(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kps[4] = {0};
    const uint8_t *l_seeds[4] = { k_seed0, k_seed1, k_seed2, k_seed3 };
    for (int i = 0; i < 4; ++i)
        dap_assert(chipmunk_ring_keygen(&l_kps[i], l_par, l_seeds[i]) == 0, "keygen");

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, l_kps, 4u);

    chipmunk_ring_sig_t l_sig = {0};
    int rc = s_sign_retry(&l_sig, l_par, &l_ring, &l_kps[2].sk, 2,
                          k_msg, sizeof(k_msg) - 1, k_sign_seed);
    dap_assert(rc == 0, "sign N=4");

    rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(rc == 0, "verify N=4");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    for (int i = 0; i < 4; ++i) chipmunk_ring_keypair_free(&l_kps[i]);
}

static void test_wrong_message(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kp = {0};
    chipmunk_ring_keygen(&l_kp, l_par, k_seed0);

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, &l_kp, 1u);

    chipmunk_ring_sig_t l_sig = {0};
    s_sign_retry(&l_sig, l_par, &l_ring, &l_kp.sk, 0,
                 k_msg, sizeof(k_msg) - 1, k_sign_seed);

    const uint8_t l_bad[] = "wrong-message";
    int rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, l_bad, sizeof(l_bad) - 1);
    dap_assert(rc != 0, "wrong message fails");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    chipmunk_ring_keypair_free(&l_kp);
}

static void test_wrong_ring(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kps[2] = {0};
    chipmunk_ring_keygen(&l_kps[0], l_par, k_seed0);
    chipmunk_ring_keygen(&l_kps[1], l_par, k_seed1);

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, l_kps, 2u);

    chipmunk_ring_sig_t l_sig = {0};
    s_sign_retry(&l_sig, l_par, &l_ring, &l_kps[0].sk, 0,
                 k_msg, sizeof(k_msg) - 1, k_sign_seed);

    /* Swap PKs — ring is now wrong. */
    lotrs_polyvec_t l_tmp = l_ring.pks[0].a_hat;
    l_ring.pks[0].a_hat = l_ring.pks[1].a_hat;
    l_ring.pks[1].a_hat = l_tmp;

    int rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(rc != 0, "wrong ring fails");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    chipmunk_ring_keypair_free(&l_kps[0]);
    chipmunk_ring_keypair_free(&l_kps[1]);
}

static void test_tamper_t(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kp = {0};
    chipmunk_ring_keygen(&l_kp, l_par, k_seed0);

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, &l_kp, 1u);

    chipmunk_ring_sig_t l_sig = {0};
    s_sign_retry(&l_sig, l_par, &l_ring, &l_kp.sk, 0,
                 k_msg, sizeof(k_msg) - 1, k_sign_seed);

    /* Tamper first byte after header (part of T_0). */
    l_sig.data[CHIPMUNK_RING_HEADER_BYTES] ^= 0x01;

    int rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(rc != 0, "tampered T fails");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    chipmunk_ring_keypair_free(&l_kp);
}

static void test_tamper_c(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kp = {0};
    chipmunk_ring_keygen(&l_kp, l_par, k_seed0);

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, &l_kp, 1u);

    chipmunk_ring_sig_t l_sig = {0};
    s_sign_retry(&l_sig, l_par, &l_ring, &l_kp.sk, 0,
                 k_msg, sizeof(k_msg) - 1, k_sign_seed);

    /* Tamper c_0 region: after header + T_0. */
    size_t l_T_size = lotrs_polyvec_bytes(l_par, l_par->k);
    size_t l_offset = CHIPMUNK_RING_HEADER_BYTES + l_T_size;
    l_sig.data[l_offset] ^= 0x01;

    int rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(rc != 0, "tampered c fails");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    chipmunk_ring_keypair_free(&l_kp);
}

static void test_tamper_z(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kp = {0};
    chipmunk_ring_keygen(&l_kp, l_par, k_seed0);

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, &l_kp, 1u);

    chipmunk_ring_sig_t l_sig = {0};
    s_sign_retry(&l_sig, l_par, &l_ring, &l_kp.sk, 0,
                 k_msg, sizeof(k_msg) - 1, k_sign_seed);

    /* Tamper last byte of signature (part of z). */
    l_sig.data[l_sig.len - 1] ^= 0x01;

    int rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(rc != 0, "tampered z fails");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    chipmunk_ring_keypair_free(&l_kp);
}

static void test_truncated_sig(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kp = {0};
    chipmunk_ring_keygen(&l_kp, l_par, k_seed0);

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, &l_kp, 1u);

    chipmunk_ring_sig_t l_sig = {0};
    s_sign_retry(&l_sig, l_par, &l_ring, &l_kp.sk, 0,
                 k_msg, sizeof(k_msg) - 1, k_sign_seed);

    /* Truncate to less than header size. */
    chipmunk_ring_sig_t l_trunc = { .data = l_sig.data, .len = CHIPMUNK_RING_HEADER_BYTES - 1 };
    int rc = chipmunk_ring_verify(&l_trunc, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(rc != 0, "truncated sig fails");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    chipmunk_ring_keypair_free(&l_kp);
}

static void test_bad_magic(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kp = {0};
    chipmunk_ring_keygen(&l_kp, l_par, k_seed0);

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, &l_kp, 1u);

    chipmunk_ring_sig_t l_sig = {0};
    s_sign_retry(&l_sig, l_par, &l_ring, &l_kp.sk, 0,
                 k_msg, sizeof(k_msg) - 1, k_sign_seed);

    /* Corrupt magic. */
    l_sig.data[0] ^= 0xFF;

    int rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(rc != 0, "bad magic fails");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    chipmunk_ring_keypair_free(&l_kp);
}

static void test_bad_version(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kp = {0};
    chipmunk_ring_keygen(&l_kp, l_par, k_seed0);

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, &l_kp, 1u);

    chipmunk_ring_sig_t l_sig = {0};
    s_sign_retry(&l_sig, l_par, &l_ring, &l_kp.sk, 0,
                 k_msg, sizeof(k_msg) - 1, k_sign_seed);

    /* Corrupt version (bytes 4-7). */
    l_sig.data[4] ^= 0x01;

    int rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(rc != 0, "bad version fails");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    chipmunk_ring_keypair_free(&l_kp);
}

static void test_bad_N(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kp = {0};
    chipmunk_ring_keygen(&l_kp, l_par, k_seed0);

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, &l_kp, 1u);

    chipmunk_ring_sig_t l_sig = {0};
    s_sign_retry(&l_sig, l_par, &l_ring, &l_kp.sk, 0,
                 k_msg, sizeof(k_msg) - 1, k_sign_seed);

    /* Corrupt N field (bytes 12-15). */
    l_sig.data[12] = 0xFF;

    int rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(rc != 0, "bad N fails");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    chipmunk_ring_keypair_free(&l_kp);
}

static void test_wire_size_formula(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    for (uint32_t N = 1u; N <= 4u; ++N) {
        /* chipmunk_ring_sig_bytes returns upper bound (raw packing). */
        size_t l_ub = chipmunk_ring_sig_bytes(l_par, N);
        dap_assert(l_ub > 0, "wire size > 0");

        /* Actual signature should be <= upper bound. */
        chipmunk_ring_keypair_t l_kp = {0};
        chipmunk_ring_keygen(&l_kp, l_par, k_seed0);
        chipmunk_ring_table_t l_ring = {0};
        l_ring.N = N;
        l_ring.pks = DAP_NEW_Z_COUNT(chipmunk_ring_pk_t, N);
        for (uint32_t i = 0u; i < N; ++i) {
            l_ring.pks[i].a_hat = lotrs_polyvec_alloc(l_par, l_par->k);
            for (uint32_t j = 0u; j < l_par->k; ++j)
                lotrs_poly_copy(l_ring.pks[i].a_hat.polys[j], l_kp.pk.a_hat.polys[j], l_par);
        }
        chipmunk_ring_sig_t l_sig = {0};
        uint8_t l_ss[32]; memset(l_ss, 0xCC, 32);
        s_sign_retry(&l_sig, l_par, &l_ring, &l_kp.sk, 0, k_msg, sizeof(k_msg) - 1, l_ss);
        dap_assert(l_sig.len > 0, "sig non-empty");
        dap_assert(l_sig.len <= l_ub, "sig <= upper bound");

        chipmunk_ring_sig_free(&l_sig);
        chipmunk_ring_table_free(&l_ring);
        chipmunk_ring_keypair_free(&l_kp);
    }
}

int main(void)
{
    dap_set_appname("test_chipmunk_ring_kat");
    dap_common_init("test_chipmunk_ring_kat", NULL);

    test_deterministic_keygen();
    test_wire_size_formula();
    test_sign_verify_n1();
    test_sign_verify_n2();
    test_sign_verify_n4();
    test_wrong_message();
    test_wrong_ring();
    test_tamper_t();
    test_tamper_c();
    test_tamper_z();
    test_truncated_sig();
    test_bad_magic();
    test_bad_version();
    test_bad_N();

    log_it(L_INFO, "=== ALL Chipmunk Ring KAT/security/wire tests PASSED ===");
    dap_common_deinit();
    return 0;
}
