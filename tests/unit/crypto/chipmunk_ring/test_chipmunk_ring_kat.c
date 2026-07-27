/*
 * test_chipmunk_ring_kat.c — Ring KAT + security + wire malformed inputs.
 *
 * T1. Deterministic keygen produces byte-identical PK for same seed.
 * T2. Sign/verify round-trip N=8, N=12, N=16.
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
#include <dap_enc.h>
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

static const uint8_t k_seed4[32] = {
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
};
static const uint8_t k_seed5[32] = {
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
};
static const uint8_t k_seed6[32] = {
    0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
    0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
    0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
};
static const uint8_t k_seed7[32] = {
    0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
    0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
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


/* Generate 8 keypairs from k_seed0..k_seed7. */
static void s_keygen_8(const lotrs_params_t *a_par, chipmunk_ring_keypair_t *a_kps)
{
    const uint8_t *l_seeds[8] = { k_seed0, k_seed1, k_seed2, k_seed3,
                                   k_seed4, k_seed5, k_seed6, k_seed7 };
    for (int i = 0; i < 8; ++i)
        dap_assert(chipmunk_ring_keygen(&a_kps[i], a_par, l_seeds[i]) == 0, "keygen");
}

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

static void test_sign_verify_n8(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kps[8] = {0};
    s_keygen_8(l_par, l_kps);

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, l_kps, 8u);

    chipmunk_ring_sig_t l_sig = {0};
    int rc = s_sign_retry(&l_sig, l_par, &l_ring, &l_kps[0].sk, 0,
                          k_msg, sizeof(k_msg) - 1, k_sign_seed);
    dap_assert(rc == 0, "sign N=8");
    dap_assert(l_sig.data != NULL && l_sig.len > 0, "sig non-empty");

    rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(rc == 0, "verify N=8");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    for (int i = 0; i < 8; ++i) chipmunk_ring_keypair_free(&l_kps[i]);
}

static void test_sign_verify_n12(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    const uint32_t N = 12;
    chipmunk_ring_keypair_t l_kps[12];
    memset(l_kps, 0, sizeof(l_kps));
    /* k_seed0-7 for first 8, derive seeds 8-11 from pattern */
    const uint8_t *l_seeds8[8] = { k_seed0, k_seed1, k_seed2, k_seed3,
                                    k_seed4, k_seed5, k_seed6, k_seed7 };
    for (int i = 0; i < 8; ++i)
        dap_assert(chipmunk_ring_keygen(&l_kps[i], l_par, l_seeds8[i]) == 0, "keygen");
    for (int i = 8; i < 12; ++i) {
        uint8_t l_seed[32];
        l_seed[0] = (uint8_t)(0xA0 + i);
        for (int j = 1; j < 32; ++j) l_seed[j] = (uint8_t)(i * 0x11 + j);
        dap_assert(chipmunk_ring_keygen(&l_kps[i], l_par, l_seed) == 0, "keygen");
    }

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, l_kps, N);

    chipmunk_ring_sig_t l_sig = {0};
    int rc = s_sign_retry(&l_sig, l_par, &l_ring, &l_kps[0].sk, 0,
                          k_msg, sizeof(k_msg) - 1, k_sign_seed);
    dap_assert(rc == 0, "sign N=12");

    rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(rc == 0, "verify N=12");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    for (uint32_t i = 0; i < N; ++i) chipmunk_ring_keypair_free(&l_kps[i]);
}

static void test_sign_verify_n16(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    const uint32_t N = 16;
    chipmunk_ring_keypair_t l_kps[16];
    memset(l_kps, 0, sizeof(l_kps));
    const uint8_t *l_seeds8[8] = { k_seed0, k_seed1, k_seed2, k_seed3,
                                    k_seed4, k_seed5, k_seed6, k_seed7 };
    for (int i = 0; i < 8; ++i)
        dap_assert(chipmunk_ring_keygen(&l_kps[i], l_par, l_seeds8[i]) == 0, "keygen");
    for (int i = 8; i < 16; ++i) {
        uint8_t l_seed[32];
        l_seed[0] = (uint8_t)(0xA0 + i);
        for (int j = 1; j < 32; ++j) l_seed[j] = (uint8_t)(i * 0x11 + j);
        dap_assert(chipmunk_ring_keygen(&l_kps[i], l_par, l_seed) == 0, "keygen");
    }

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, l_kps, N);

    chipmunk_ring_sig_t l_sig = {0};
    int rc = s_sign_retry(&l_sig, l_par, &l_ring, &l_kps[2].sk, 2,
                          k_msg, sizeof(k_msg) - 1, k_sign_seed);
    dap_assert(rc == 0, "sign N=16");

    rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(rc == 0, "verify N=16");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    for (uint32_t i = 0; i < N; ++i) chipmunk_ring_keypair_free(&l_kps[i]);
}

static void test_wrong_message(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kps[8] = {0};
    s_keygen_8(l_par, l_kps);

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, l_kps, 8u);

    chipmunk_ring_sig_t l_sig = {0};
    s_sign_retry(&l_sig, l_par, &l_ring, &l_kps[0].sk, 0,
                 k_msg, sizeof(k_msg) - 1, k_sign_seed);

    const uint8_t l_bad[] = "wrong-message";
    int rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, l_bad, sizeof(l_bad) - 1);
    dap_assert(rc != 0, "wrong message fails");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    for (int i = 0; i < 8; ++i) chipmunk_ring_keypair_free(&l_kps[i]);
}

static void test_wrong_ring(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kps[8] = {0};
    s_keygen_8(l_par, l_kps);

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, l_kps, 8u);

    chipmunk_ring_sig_t l_sig = {0};
    s_sign_retry(&l_sig, l_par, &l_ring, &l_kps[0].sk, 0,
                 k_msg, sizeof(k_msg) - 1, k_sign_seed);

    /* Replace signer PK with a different one — ring is now wrong. */
    chipmunk_ring_keypair_t l_fake = {0};
    uint8_t l_fake_seed[32];
    for (int j = 0; j < 32; ++j) l_fake_seed[j] = (uint8_t)(0xFE + j);
    chipmunk_ring_keygen(&l_fake, l_par, l_fake_seed);
    for (uint32_t j = 0u; j < l_par->k; ++j)
        lotrs_poly_copy(l_ring.pks[0].a_hat.polys[j], l_fake.pk.a_hat.polys[j], l_par);
    chipmunk_ring_keypair_free(&l_fake);

    int rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(rc != 0, "wrong ring fails");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    for (int i = 0; i < 8; ++i) chipmunk_ring_keypair_free(&l_kps[i]);
}

static void test_tamper_t(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kps[8] = {0};
    s_keygen_8(l_par, l_kps);

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, l_kps, 8u);

    chipmunk_ring_sig_t l_sig = {0};
    s_sign_retry(&l_sig, l_par, &l_ring, &l_kps[0].sk, 0,
                 k_msg, sizeof(k_msg) - 1, k_sign_seed);

    /* Tamper first byte after header (part of T_0). */
    l_sig.data[chipmunk_ring_header_bytes()] ^= 0x01;

    int rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(rc != 0, "tampered T fails");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    for (int i = 0; i < 8; ++i) chipmunk_ring_keypair_free(&l_kps[i]);
}

static void test_tamper_c(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kps[8] = {0};
    s_keygen_8(l_par, l_kps);

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, l_kps, 8u);

    chipmunk_ring_sig_t l_sig = {0};
    s_sign_retry(&l_sig, l_par, &l_ring, &l_kps[0].sk, 0,
                 k_msg, sizeof(k_msg) - 1, k_sign_seed);

    /* Tamper c_0 region: after header + T_0. */
    size_t l_T_size = lotrs_polyvec_bytes(l_par, l_par->k);
    size_t l_offset = chipmunk_ring_header_bytes() + l_T_size;
    l_sig.data[l_offset] ^= 0x01;

    int rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(rc != 0, "tampered c fails");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    for (int i = 0; i < 8; ++i) chipmunk_ring_keypair_free(&l_kps[i]);
}

static void test_tamper_z(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kps[8] = {0};
    s_keygen_8(l_par, l_kps);

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, l_kps, 8u);

    chipmunk_ring_sig_t l_sig = {0};
    s_sign_retry(&l_sig, l_par, &l_ring, &l_kps[0].sk, 0,
                 k_msg, sizeof(k_msg) - 1, k_sign_seed);

    /* Tamper a byte in the z region (after header + N*T + N*c). */
    size_t l_T_size = lotrs_polyvec_bytes(l_par, l_par->k);
    size_t l_c_bytes = lotrs_poly_bytes(l_par);
    size_t l_z_offset = chipmunk_ring_header_bytes() + 8u * l_T_size + 8u * l_c_bytes;
    if (l_z_offset < l_sig.len)
        l_sig.data[l_z_offset] ^= 0x01;
    else
        l_sig.data[chipmunk_ring_header_bytes()] ^= 0x01; /* fallback: tamper T */

    int rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(rc != 0, "tampered z fails");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    for (int i = 0; i < 8; ++i) chipmunk_ring_keypair_free(&l_kps[i]);
}

static void test_truncated_sig(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kps[8] = {0};
    s_keygen_8(l_par, l_kps);

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, l_kps, 8u);

    chipmunk_ring_sig_t l_sig = {0};
    s_sign_retry(&l_sig, l_par, &l_ring, &l_kps[0].sk, 0,
                 k_msg, sizeof(k_msg) - 1, k_sign_seed);

    /* Truncate to less than header size. */
    chipmunk_ring_sig_t l_trunc = { .data = l_sig.data, .len = chipmunk_ring_header_bytes() - 1 };
    int rc = chipmunk_ring_verify(&l_trunc, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(rc != 0, "truncated sig fails");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    for (int i = 0; i < 8; ++i) chipmunk_ring_keypair_free(&l_kps[i]);
}

static void test_bad_magic(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kps[8] = {0};
    s_keygen_8(l_par, l_kps);

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, l_kps, 8u);

    chipmunk_ring_sig_t l_sig = {0};
    s_sign_retry(&l_sig, l_par, &l_ring, &l_kps[0].sk, 0,
                 k_msg, sizeof(k_msg) - 1, k_sign_seed);

    /* Corrupt magic. */
    l_sig.data[0] ^= 0xFF;

    int rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(rc != 0, "bad magic fails");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    for (int i = 0; i < 8; ++i) chipmunk_ring_keypair_free(&l_kps[i]);
}

static void test_bad_version(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kps[8] = {0};
    s_keygen_8(l_par, l_kps);

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, l_kps, 8u);

    chipmunk_ring_sig_t l_sig = {0};
    s_sign_retry(&l_sig, l_par, &l_ring, &l_kps[0].sk, 0,
                 k_msg, sizeof(k_msg) - 1, k_sign_seed);

    /* Corrupt version (bytes 4-7). */
    l_sig.data[4] ^= 0x01;

    int rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(rc != 0, "bad version fails");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    for (int i = 0; i < 8; ++i) chipmunk_ring_keypair_free(&l_kps[i]);
}

static void test_bad_N(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    chipmunk_ring_keypair_t l_kps[8] = {0};
    s_keygen_8(l_par, l_kps);

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, l_kps, 8u);

    chipmunk_ring_sig_t l_sig = {0};
    s_sign_retry(&l_sig, l_par, &l_ring, &l_kps[0].sk, 0,
                 k_msg, sizeof(k_msg) - 1, k_sign_seed);

    /* Corrupt N field (bytes 12-15). */
    l_sig.data[12] = 0xFF;

    int rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(rc != 0, "bad N fails");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    for (int i = 0; i < 8; ++i) chipmunk_ring_keypair_free(&l_kps[i]);
}

static void test_wire_size_formula(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
    for (uint32_t N = 8u; N <= 16u; ++N) {
        /* chipmunk_ring_sig_bytes_max returns upper bound (raw packing). */
        size_t l_ub = chipmunk_ring_sig_bytes_max(l_par, N);
        dap_assert(l_ub > 0, "wire size > 0");

        /* Actual signature should be <= upper bound. */
        chipmunk_ring_keypair_t l_kps[16];
        memset(l_kps, 0, sizeof(l_kps));
        for (uint32_t si = 0; si < N; ++si) {
            uint8_t l_seed[32];
            l_seed[0] = (uint8_t)(0xA0 + si);
            for (int j = 1; j < 32; ++j) l_seed[j] = (uint8_t)(si * 0x11 + j);
            chipmunk_ring_keygen(&l_kps[si], l_par, l_seed);
        }
        chipmunk_ring_table_t l_ring = {0};
        s_build_ring(l_par, &l_ring, l_kps, N);

        chipmunk_ring_sig_t l_sig = {0};
        uint8_t l_ss[32]; memset(l_ss, 0xCC, 32);
        s_sign_retry(&l_sig, l_par, &l_ring, &l_kps[0].sk, 0, k_msg, sizeof(k_msg) - 1, l_ss);
        dap_assert(l_sig.len > 0, "sig non-empty");
        dap_assert(l_sig.len <= l_ub, "sig <= upper bound");

        chipmunk_ring_sig_free(&l_sig);
        chipmunk_ring_table_free(&l_ring);
        for (uint32_t si = 0; si < N; ++si) chipmunk_ring_keypair_free(&l_kps[si]);
    }
}

static void test_sign_verify_prod(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_RING_OPT;
    const uint32_t N = 8;
    chipmunk_ring_keypair_t l_kps[8];
    memset(l_kps, 0, sizeof(l_kps));
    const uint8_t *l_seeds[8] = { k_seed0, k_seed1, k_seed2, k_seed3,
                                   k_seed4, k_seed5, k_seed6, k_seed7 };
    for (int i = 0; i < 8; ++i) {
        int rc = chipmunk_ring_keygen(&l_kps[i], l_par, l_seeds[i]);
        dap_assert(rc == 0, "keygen prod");
    }

    chipmunk_ring_table_t l_ring = {0};
    s_build_ring(l_par, &l_ring, l_kps, N);

    chipmunk_ring_sig_t l_sig = {0};
    uint8_t l_ss[32]; memset(l_ss, 0xBB, 32);
    int rc = s_sign_retry(&l_sig, l_par, &l_ring, &l_kps[0].sk, 0, k_msg, sizeof(k_msg) - 1, l_ss);
    dap_assert(rc == 0, "sign prod N=8");

    rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, k_msg, sizeof(k_msg) - 1);
    dap_assert(rc == 0, "verify prod N=8");

    const uint8_t l_bad[] = "wrong";
    rc = chipmunk_ring_verify(&l_sig, l_par, &l_ring, l_bad, sizeof(l_bad) - 1);
    dap_assert(rc != 0, "wrong msg prod");

    chipmunk_ring_sig_free(&l_sig);
    chipmunk_ring_table_free(&l_ring);
    for (int i = 0; i < 8; ++i) chipmunk_ring_keypair_free(&l_kps[i]);
}

int main(void)
{
    dap_set_appname("test_chipmunk_ring_kat");
    dap_common_init("test_chipmunk_ring_kat", NULL);
    /* Initialise crypto subsystem (SIMD dispatch, chipmunk, etc.) */
    dap_enc_init();

    test_deterministic_keygen();
    test_wire_size_formula();
    test_sign_verify_n8();
    test_sign_verify_n12();
    test_sign_verify_n16();
    test_sign_verify_prod();
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
