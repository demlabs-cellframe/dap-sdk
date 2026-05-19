/*
 * test_chipmunk_lrs_kat.c — CR-11.D canonical C0/RB2 primitive KATs.
 *
 * These tests pin the deterministic base layer for the new Chipmunk LRS
 * implementation.  They intentionally do not exercise the old Acorn-era
 * chipmunk_ring_signature_t object and do not accept previous experimental
 * wire formats.
 */

#include <dap_common.h>
#include <dap_hash_sha3.h>
#include <dap_test.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chipmunk/chipmunk_lrs.h"

#define LOG_TAG "test_chipmunk_lrs_kat"

static const uint8_t k_x_seed[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};

static const uint8_t k_pk_seed[32] = {
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
};

static const uint8_t k_challenge_seed[32] = {
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
    0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
    0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
};

static const uint8_t k_expected_A_pk0_sha3[32] = {
    0xe8, 0xc5, 0x3a, 0xb6, 0x16, 0x1d, 0x56, 0x13,
    0xed, 0xdd, 0x32, 0xda, 0xee, 0x8e, 0x6b, 0xde,
    0x99, 0x9a, 0x89, 0x58, 0x6e, 0x55, 0xc7, 0x49,
    0xe2, 0x77, 0x44, 0xaf, 0x7e, 0x20, 0x72, 0x7a,
};
static const uint8_t k_expected_x0_sha3[32] = {
    0x11, 0x88, 0xc6, 0xd2, 0x90, 0x4b, 0x34, 0xce,
    0x2d, 0x6f, 0xf1, 0xbb, 0xd2, 0x47, 0xf2, 0xe9,
    0x18, 0xc4, 0xaa, 0xac, 0xbf, 0xe5, 0xfa, 0xaa,
    0xec, 0x16, 0x1b, 0xc6, 0x97, 0x14, 0xa0, 0xd4,
};
static const uint8_t k_expected_challenge_sha3[32] = {
    0x0d, 0xc0, 0xc8, 0x83, 0xf3, 0x78, 0xd3, 0x36,
    0x3c, 0xbd, 0xa0, 0x24, 0x16, 0xe3, 0xdb, 0x8f,
    0x6d, 0xeb, 0x8d, 0x73, 0x7b, 0xa0, 0x45, 0x34,
    0xe9, 0xe0, 0x91, 0x90, 0x52, 0x56, 0x3c, 0xd0,
};
static const uint8_t k_expected_clpk_sha3[32] = {
    0x7e, 0x26, 0x30, 0xe4, 0x3f, 0x5c, 0x9d, 0xe5,
    0xd7, 0xa2, 0xcb, 0xca, 0xa9, 0xb5, 0xdf, 0xc8,
    0x47, 0xe9, 0xf6, 0xf7, 0x4b, 0x08, 0xea, 0x07,
    0xcb, 0xa2, 0xcd, 0x81, 0xf5, 0x9f, 0xe8, 0x37,
};
static const uint8_t k_expected_clsk_sha3[32] = {
    0x9d, 0x89, 0x9a, 0xc4, 0x0d, 0x40, 0x63, 0x74,
    0xfd, 0x26, 0x8c, 0xc5, 0x27, 0xc2, 0x65, 0xac,
    0x42, 0x5a, 0xbf, 0x00, 0x93, 0x93, 0x0e, 0x3e,
    0x2d, 0x6c, 0xf4, 0x41, 0xd7, 0x55, 0xbb, 0x41,
};
static const uint8_t k_expected_key_image_sha3[32] = {
    0x6f, 0xd2, 0xcc, 0x06, 0xa1, 0x5e, 0xec, 0x75,
    0xc0, 0xa3, 0x13, 0x6b, 0xb5, 0xb2, 0x65, 0x1b,
    0x1f, 0xd4, 0x68, 0x89, 0xb2, 0x00, 0x95, 0xb8,
    0xfd, 0x67, 0xb4, 0xbc, 0xb3, 0x0f, 0x4c, 0x5a,
};
static const uint8_t k_expected_pk_hash_sha3[32] = {
    0x62, 0x3d, 0x0c, 0xa1, 0xfa, 0x9b, 0x53, 0xb6,
    0x98, 0xbd, 0x68, 0xc1, 0x94, 0xbe, 0x3e, 0x5f,
    0x51, 0x75, 0x07, 0x0d, 0xa7, 0xbb, 0x20, 0x42,
    0x95, 0x3c, 0xca, 0x94, 0xb7, 0x77, 0x60, 0xd5,
};
static const uint8_t k_expected_ring_hash_sha3[32] = {
    0x61, 0x38, 0xb2, 0xc0, 0xc9, 0x32, 0x65, 0xff,
    0xb2, 0xad, 0xa8, 0x57, 0xd2, 0xf7, 0x5f, 0x78,
    0x84, 0xea, 0x87, 0xf0, 0x2d, 0x7a, 0x77, 0x79,
    0x7c, 0x95, 0x94, 0x93, 0xb9, 0x06, 0xa5, 0xd0,
};

static bool s_dump_mode(void)
{
    const char *e = getenv("CHIPMUNK_LRS_KAT_DUMP");
    return e && *e && *e != '0';
}

static bool s_all_zero(const uint8_t *a_buf, size_t a_size)
{
    for (size_t i = 0; i < a_size; ++i) {
        if (a_buf[i] != 0) {
            return false;
        }
    }
    return true;
}

static void s_print_vector(const char *a_name, const uint8_t *a_buf, size_t a_size)
{
    fprintf(stderr, "static const uint8_t %s[%zu] = {", a_name, a_size);
    for (size_t i = 0; i < a_size; ++i) {
        if (i % 8u == 0u) {
            fprintf(stderr, "\n   ");
        }
        fprintf(stderr, " 0x%02x,", a_buf[i]);
    }
    fprintf(stderr, "\n};\n");
}

static void s_sha3_256(const void *a_in, size_t a_in_size, uint8_t a_out[32])
{
    dap_hash_sha3_256_t h;
    dap_assert(dap_hash_sha3_256(a_in, a_in_size, &h), "sha3-256 ok");
    memcpy(a_out, &h, 32);
}

static void s_poly_hash(const chipmunk_poly_t *a_poly, uint8_t a_out[32])
{
    uint8_t *buf = DAP_NEW_Z_SIZE(uint8_t, CHIPMUNK_N * 4u);
    dap_assert(buf != NULL, "poly hash buffer alloc");
    for (size_t i = 0; i < CHIPMUNK_N; ++i) {
        uint32_t v = (uint32_t)a_poly->coeffs[i];
        buf[4u * i + 0u] = (uint8_t)v;
        buf[4u * i + 1u] = (uint8_t)(v >> 8);
        buf[4u * i + 2u] = (uint8_t)(v >> 16);
        buf[4u * i + 3u] = (uint8_t)(v >> 24);
    }
    s_sha3_256(buf, CHIPMUNK_N * 4u, a_out);
    DAP_DELETE(buf);
}

static bool s_check_or_dump(const char *a_name,
                            const uint8_t a_actual[32],
                            const uint8_t a_expected[32])
{
    if (s_dump_mode() || s_all_zero(a_expected, 32u)) {
        s_print_vector(a_name, a_actual, 32u);
        return false;
    }
    if (memcmp(a_actual, a_expected, 32u) != 0) {
        log_it(L_ERROR, "KAT mismatch: %s", a_name);
        s_print_vector("actual", a_actual, 32u);
        s_print_vector("expected", a_expected, 32u);
        return false;
    }
    return true;
}

static bool s_test_qpack_roundtrip_and_reject(void)
{
    chipmunk_poly_t p;
    for (size_t i = 0; i < CHIPMUNK_N; ++i) {
        p.coeffs[i] = (i % 3u == 0u) ? -(int32_t)(i % 997u)
                    : (int32_t)((i * 8191u) % CHIPMUNK_Q);
    }

    uint8_t packed[CHIPMUNK_LRS_POLY_QPACK_BYTES];
    chipmunk_poly_t q;
    dap_assert(chipmunk_lrs_poly_qpack(packed, &p) == 0, "qpack accepts centered/q coefficients");
    dap_assert(chipmunk_lrs_poly_qunpack(&q, packed) == 0, "qunpack accepts canonical qpack");
    bool roundtrip_ok = true;
    for (size_t i = 0; i < CHIPMUNK_N; ++i) {
        int32_t expected = p.coeffs[i] < 0 ? p.coeffs[i] + CHIPMUNK_Q : p.coeffs[i];
        roundtrip_ok &= q.coeffs[i] == expected;
    }
    dap_assert(roundtrip_ok, "qpack roundtrip coefficients");

    memset(packed, 0, sizeof(packed));
    packed[0] = (uint8_t)CHIPMUNK_Q;
    packed[1] = (uint8_t)(CHIPMUNK_Q >> 8);
    packed[2] = (uint8_t)(CHIPMUNK_Q >> 16);
    dap_assert(chipmunk_lrs_poly_qunpack(&q, packed) != 0,
               "qunpack rejects coeff == q");
    return true;
}

static bool s_test_deterministic_primitives(void)
{
    chipmunk_poly_t A_pk[CHIPMUNK_LRS_K];
    chipmunk_poly_t x[CHIPMUNK_LRS_K];
    chipmunk_poly_t challenge;
    uint8_t h[32];
    bool ok = true;

    dap_assert(chipmunk_lrs_derive_A_pk(A_pk, k_pk_seed) == 0, "derive A_pk");
    s_poly_hash(&A_pk[0], h);
    ok &= s_check_or_dump("k_expected_A_pk0_sha3", h, k_expected_A_pk0_sha3);

    dap_assert(chipmunk_lrs_derive_witness(x, k_x_seed) == 0, "derive witness");
    dap_assert(chipmunk_lrs_poly_chknorm_centered(&x[0], CHIPMUNK_LRS_WITNESS_BOUND) == 0,
               "witness within B_x");
    s_poly_hash(&x[0], h);
    ok &= s_check_or_dump("k_expected_x0_sha3", h, k_expected_x0_sha3);

    dap_assert(chipmunk_lrs_h_to_sparse_ternary(&challenge, "chipmunk-lrs-challenge",
                                                CHIPMUNK_LRS_PARAMS_C0,
                                                k_challenge_seed) == 0,
               "challenge expansion");
    uint32_t weight = 0;
    bool ternary = true;
    for (size_t i = 0; i < CHIPMUNK_N; ++i) {
        ternary &= challenge.coeffs[i] == -1 || challenge.coeffs[i] == 0 ||
                   challenge.coeffs[i] == 1;
        if (challenge.coeffs[i] != 0) {
            ++weight;
        }
    }
    dap_assert(ternary, "challenge ternary");
    dap_assert(weight == CHIPMUNK_LRS_CHALLENGE_WEIGHT, "challenge weight");
    s_poly_hash(&challenge, h);
    ok &= s_check_or_dump("k_expected_challenge_sha3", h, k_expected_challenge_sha3);
    return ok;
}

static bool s_test_key_material(void)
{
    chipmunk_lrs_public_key_t pk;
    chipmunk_lrs_secret_key_t sk;
    uint8_t key_image[CHIPMUNK_LRS_POLY_QPACK_BYTES];
    uint8_t h[32];
    bool ok = true;

    dap_assert(chipmunk_lrs_keypair_from_seeds(&pk, &sk, k_x_seed, k_pk_seed) == 0,
               "keypair from seeds");
    dap_assert(pk.magic == CHIPMUNK_LRS_MAGIC_CLPK, "CLPK magic");
    dap_assert(sk.magic == CHIPMUNK_LRS_MAGIC_CLSK, "CLSK magic");
    dap_assert(pk.params_id == CHIPMUNK_LRS_PARAMS_C0, "CLPK C0 params");
    dap_assert(sk.params_id == CHIPMUNK_LRS_PARAMS_C0, "CLSK C0 params");
    dap_assert(chipmunk_lrs_secret_key_validate(&sk) == 0, "secret key validates");

    s_sha3_256(&pk, sizeof(pk), h);
    ok &= s_check_or_dump("k_expected_clpk_sha3", h, k_expected_clpk_sha3);

    s_sha3_256(&sk, sizeof(sk), h);
    ok &= s_check_or_dump("k_expected_clsk_sha3", h, k_expected_clsk_sha3);

    dap_assert(chipmunk_lrs_key_image(key_image, &sk) == 0, "key image");
    bool nonzero = false;
    for (size_t i = 0; i < sizeof(key_image); ++i) {
        nonzero |= key_image[i] != 0;
    }
    dap_assert(nonzero, "key image non-zero");
    s_sha3_256(key_image, sizeof(key_image), h);
    ok &= s_check_or_dump("k_expected_key_image_sha3", h, k_expected_key_image_sha3);

    sk.reserved0 = 1;
    dap_assert(chipmunk_lrs_secret_key_validate(&sk) != 0,
               "secret key rejects non-zero reserved");
    return ok;
}

static bool s_test_public_key_and_ring_hash(void)
{
    chipmunk_lrs_public_key_t pk1, pk2;
    chipmunk_lrs_secret_key_t sk1, sk2;
    chipmunk_lrs_public_key_t ring[2];
    uint8_t seed2[32];
    uint8_t pk_hash[32], ring_hash[32], h[32];
    bool ok = true;

    memcpy(seed2, k_x_seed, sizeof(seed2));
    seed2[0] ^= 0xa5;

    dap_assert(chipmunk_lrs_keypair_from_seeds(&pk1, &sk1, k_x_seed, k_pk_seed) == 0,
               "ring keypair 1");
    dap_assert(chipmunk_lrs_keypair_from_seeds(&pk2, &sk2, seed2, k_pk_seed) == 0,
               "ring keypair 2");
    dap_assert(chipmunk_lrs_public_key_validate(&pk1) == 0, "CLPK validates");
    dap_assert(chipmunk_lrs_public_key_hash(pk_hash, &pk1) == 0, "CLPK hash");
    s_sha3_256(pk_hash, sizeof(pk_hash), h);
    ok &= s_check_or_dump("k_expected_pk_hash_sha3", h, k_expected_pk_hash_sha3);

    /* Input order deliberately reversed; ring hash sorts canonically. */
    ring[0] = pk2;
    ring[1] = pk1;
    dap_assert(chipmunk_lrs_ring_hash(ring_hash, ring, 2) == 0, "ring hash");
    s_sha3_256(ring_hash, sizeof(ring_hash), h);
    ok &= s_check_or_dump("k_expected_ring_hash_sha3", h, k_expected_ring_hash_sha3);

    ring[0] = pk1;
    ring[1] = pk1;
    dap_assert(chipmunk_lrs_ring_hash(ring_hash, ring, 2) != 0,
               "ring hash rejects duplicate CLPK");

    pk1.reserved0 = 1;
    dap_assert(chipmunk_lrs_public_key_validate(&pk1) != 0,
               "CLPK rejects non-zero reserved");
    return ok;
}

int main(void)
{
    dap_set_appname("test_chipmunk_lrs_kat");
    dap_common_init("test_chipmunk_lrs_kat", NULL);

    int rc = 0;
    if (!s_test_qpack_roundtrip_and_reject()) rc = 1;
    if (!s_test_deterministic_primitives()) rc = 1;
    if (!s_test_key_material()) rc = 1;
    if (!s_test_public_key_and_ring_hash()) rc = 1;

    if (s_dump_mode()) {
        log_it(L_WARNING, "CHIPMUNK_LRS_KAT_DUMP active: dump is not a pass");
        rc = 1;
    } else if (rc == 0) {
        log_it(L_INFO, "Chipmunk LRS C0/RB2 primitive KATs PASSED");
    }

    dap_common_deinit();
    return rc;
}
