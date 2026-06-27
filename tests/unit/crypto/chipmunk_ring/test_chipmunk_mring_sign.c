/*
 * test_chipmunk_mring_sign.c — MRNG M6 end-to-end sign / verify.
 *
 * T1. 2-of-4 honest sign → verify PASS.
 * T2. Tampered signature byte → verify FAIL.
 * T3. Wrong message → verify FAIL.
 * T4. Wire size matches chipmunk_mring_wire_size for N=4.
 */

#include <dap_common.h>
#include <dap_test.h>

#include <stdint.h>
#include <string.h>

#include "chipmunk/chipmunk_ring.h"
#include "sig/chipmunk/chipmunk_mring.h"
#include "chipmunk/chipmunk_lrs.h"

#define LOG_TAG "test_chipmunk_mring_sign"

#define N_RING 4u
#define T_THRESH 2u

static void s_fill_seed(uint8_t *a_out, size_t a_len, uint8_t a_salt)
{
    for (size_t i = 0u; i < a_len; ++i) {
        a_out[i] = (uint8_t)(0x42u ^ (uint8_t)i ^ a_salt);
    }
}

static void s_make_keypair(chipmunk_lrs_public_key_t *a_pk,
                           chipmunk_lrs_secret_key_t *a_sk,
                           uint8_t a_salt)
{
    uint8_t sk_seed[CHIPMUNK_LRS_SEED_BYTES];
    s_fill_seed(sk_seed, sizeof(sk_seed), a_salt);
    dap_assert(chipmunk_lrs_keypair_from_seeds(a_pk, a_sk, sk_seed) == 0,
               "keypair");
}

static void test_sign_verify_roundtrip(void)
{
    chipmunk_lrs_public_key_t ring[N_RING];
    chipmunk_lrs_secret_key_t sks[T_THRESH];

    for (uint32_t i = 0u; i < N_RING; ++i) {
        chipmunk_lrs_secret_key_t tmp;
        s_make_keypair(&ring[i], &tmp, (uint8_t)(0x10u + i));
    }
    s_make_keypair(&ring[0], &sks[0], 0xA1u);
    s_make_keypair(&ring[2], &sks[1], 0xA2u);

    const chipmunk_lrs_secret_key_t *signer_ptrs[T_THRESH] = {
        &sks[0], &sks[1],
    };

    uint8_t seeds[T_THRESH * CHIPMUNK_LRS_SEED_BYTES];
    s_fill_seed(seeds, sizeof(seeds), 0x99u);

    const uint8_t msg[] = "mring-m6-sign-verify";
    uint8_t *sig = NULL;
    size_t sig_sz = 0u;

    chipmunk_ring_error_t rc = chipmunk_ring_sign_to_bytes(
        &sig, &sig_sz, signer_ptrs, T_THRESH, ring, N_RING, T_THRESH,
        msg, sizeof(msg) - 1u, NULL, 0u, seeds);
    dap_assert(rc == CHIPMUNK_RING_OK, "sign must succeed");
    dap_assert(sig != NULL && sig_sz > 0u, "signature allocated");

    const uint32_t l_depth = chipmunk_mring_fold_depth_for(N_RING);
    dap_assert(sig_sz == (size_t)chipmunk_mring_wire_size(l_depth),
               "wire size pinned for N=4");

    rc = chipmunk_ring_verify_from_bytes(
        sig, sig_sz, ring, N_RING, msg, sizeof(msg) - 1u, NULL, 0u);
    dap_assert(rc == CHIPMUNK_RING_OK, "verify honest signature");

    sig[chipmunk_mring_section_off_T()] ^= 1u;
    rc = chipmunk_ring_verify_from_bytes(
        sig, sig_sz, ring, N_RING, msg, sizeof(msg) - 1u, NULL, 0u);
    dap_assert(rc != CHIPMUNK_RING_OK, "tampered sig must fail");

    sig[chipmunk_mring_section_off_T()] ^= 1u;
    const uint8_t bad_msg[] = "wrong-message";
    rc = chipmunk_ring_verify_from_bytes(
        sig, sig_sz, ring, N_RING, bad_msg, sizeof(bad_msg) - 1u,
        NULL, 0u);
    dap_assert(rc != CHIPMUNK_RING_OK, "wrong message must fail");

    DAP_DELETE(sig);
}

/* N=8, t=4 regression test. */
#define N8_RING 8u
#define T8_THRESH 4u

static void test_sign_verify_n8(void)
{
    chipmunk_lrs_public_key_t ring[N8_RING];
    chipmunk_lrs_secret_key_t sks[T8_THRESH];

    for (uint32_t i = 0u; i < N8_RING; ++i) {
        chipmunk_lrs_secret_key_t tmp;
        s_make_keypair(&ring[i], &tmp, (uint8_t)(0x20u + i));
    }
    for (uint32_t i = 0u; i < T8_THRESH; ++i) {
        s_make_keypair(&ring[i], &sks[i], (uint8_t)(0xB0u + i));
    }

    const chipmunk_lrs_secret_key_t *signer_ptrs[T8_THRESH];
    for (uint32_t i = 0u; i < T8_THRESH; ++i) {
        signer_ptrs[i] = &sks[i];
    }

    uint8_t seeds[T8_THRESH * CHIPMUNK_LRS_SEED_BYTES];
    s_fill_seed(seeds, sizeof(seeds), 0x77u);

    const uint8_t msg[] = "mring-n8-regression";
    uint8_t *sig = NULL;
    size_t sig_sz = 0u;

    chipmunk_ring_error_t rc = chipmunk_ring_sign_to_bytes(
        &sig, &sig_sz, signer_ptrs, T8_THRESH, ring, N8_RING, T8_THRESH,
        msg, sizeof(msg) - 1u, NULL, 0u, seeds);
    dap_assert(rc == CHIPMUNK_RING_OK, "N=8 sign must succeed");

    rc = chipmunk_ring_verify_from_bytes(
        sig, sig_sz, ring, N8_RING, msg, sizeof(msg) - 1u, NULL, 0u);
    dap_assert(rc == CHIPMUNK_RING_OK, "N=8 verify must succeed");

    DAP_DELETE(sig);
}

int main(void)
{
    dap_set_appname("test_chipmunk_mring_sign");
    dap_common_init("test_chipmunk_mring_sign", NULL);

    test_sign_verify_roundtrip();
    test_sign_verify_n8();

    log_it(L_INFO, "=== ALL MRNG M6 sign/verify tests PASSED ===");
    return 0;
}
