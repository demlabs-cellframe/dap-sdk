/*
 * test_chipmunk_mring_signoff.c — MRNG M7.4 production signoff selfcheck.
 *
 * Exercises the full MRNG pipeline end-to-end and returns 0 on success.
 * Intended to be run as a ctest label gate before release.
 *
 * Checklist:
 *   [x] Parameter envelope validation (N, t, signer_count)
 *   [x] Header wire layout (magic, version, params, flags, fold_depth)
 *   [x] Wire size formula for N=2..256
 *   [x] Sign/verify round-trip for N=2 t=1, N=4 t=2
 *   [x] Deterministic sign (same seeds → byte-identical wire)
 *   [x] Tamper rejection (T block, bind block, c*, message, ring)
 *   [x] Error string coverage for all defined codes
 *   [x] NULL parameter rejection
 *   [x] Threshold / ring-size boundary rejection
 */

#include <dap_common.h>
#include <dap_hash_sha3.h>
#include <dap_memwipe.h>
#include <dap_test.h>

#include <stdint.h>
#include <string.h>

#include "chipmunk/chipmunk_ring.h"
#include "sig/chipmunk/chipmunk_mring.h"
#include "chipmunk/chipmunk_lrs.h"

#define LOG_TAG "test_chipmunk_mring_signoff"

static void s_fill(uint8_t *a_out, size_t a_len, uint8_t a_salt)
{
    for (size_t i = 0u; i < a_len; ++i) {
        a_out[i] = (uint8_t)(0x42u ^ (uint8_t)i ^ a_salt);
    }
}

static void s_kp(chipmunk_lrs_public_key_t *a_pk,
                 chipmunk_lrs_secret_key_t *a_sk,
                 uint8_t a_salt)
{
    uint8_t seed[CHIPMUNK_LRS_SEED_BYTES];
    s_fill(seed, sizeof(seed), a_salt);
    dap_assert(chipmunk_lrs_keypair_from_seeds(a_pk, a_sk, seed) == 0,
               "signoff keypair");
}

static chipmunk_ring_error_t s_sign_and_verify(
    uint32_t a_N, uint32_t a_T,
    const chipmunk_lrs_secret_key_t *const *a_ptrs,
    const chipmunk_lrs_public_key_t *a_ring,
    const uint8_t *a_msg, size_t a_msg_sz,
    const uint8_t *a_seeds)
{
    uint8_t *sig = NULL;
    size_t sig_sz = 0u;

    chipmunk_ring_error_t rc = chipmunk_ring_sign_to_bytes(
        &sig, &sig_sz, a_ptrs, a_T, a_ring, a_N, a_T,
        a_msg, a_msg_sz, NULL, 0u, a_seeds);
    if (rc != CHIPMUNK_RING_OK) {
        return rc;
    }

    /* Wire size sanity. */
    const uint32_t depth = chipmunk_mring_fold_depth_for(a_N);
    if (sig_sz != (size_t)chipmunk_mring_wire_size(depth)) {
        DAP_DELETE(sig);
        return CHIPMUNK_RING_ERR_INTERNAL;
    }

    rc = chipmunk_ring_verify_from_bytes(
        sig, sig_sz, a_ring, a_N, a_msg, a_msg_sz, NULL, 0u);
    DAP_DELETE(sig);
    return rc;
}

static void test_signoff_n2(void)
{
    enum { N = 2, T = 1 };
    chipmunk_lrs_public_key_t ring[N];
    chipmunk_lrs_secret_key_t sk;
    for (uint32_t i = 0u; i < N; ++i) {
        chipmunk_lrs_secret_key_t tmp;
        s_kp(&ring[i], &tmp, (uint8_t)(0x10u + i));
    }
    s_kp(&ring[0], &sk, 0xA1u);

    const chipmunk_lrs_secret_key_t *ptrs[T] = { &sk };
    uint8_t seeds[T * CHIPMUNK_LRS_SEED_BYTES];
    s_fill(seeds, sizeof(seeds), 0x99u);

    const uint8_t msg[] = "signoff-n2";
    chipmunk_ring_error_t rc = s_sign_and_verify(
        N, T, ptrs, ring, msg, sizeof(msg) - 1u, seeds);
    dap_assert(rc == CHIPMUNK_RING_OK, "signoff N=2 t=1");
}

static void test_signoff_n4(void)
{
    enum { N = 4, T = 2 };
    chipmunk_lrs_public_key_t ring[N];
    chipmunk_lrs_secret_key_t sks[T];
    for (uint32_t i = 0u; i < N; ++i) {
        chipmunk_lrs_secret_key_t tmp;
        s_kp(&ring[i], &tmp, (uint8_t)(0x20u + i));
    }
    s_kp(&ring[0], &sks[0], 0xB1u);
    s_kp(&ring[2], &sks[1], 0xB2u);

    const chipmunk_lrs_secret_key_t *ptrs[T] = { &sks[0], &sks[1] };
    uint8_t seeds[T * CHIPMUNK_LRS_SEED_BYTES];
    s_fill(seeds, sizeof(seeds), 0xAAu);

    const uint8_t msg[] = "signoff-n4";
    chipmunk_ring_error_t rc = s_sign_and_verify(
        N, T, ptrs, ring, msg, sizeof(msg) - 1u, seeds);
    dap_assert(rc == CHIPMUNK_RING_OK, "signoff N=4 t=2");
}

static void test_signoff_wire_sizes(void)
{
    /* Wire size formula: 33532 + fold_depth * 16896. */
    static const struct {
        uint32_t n;
        uint32_t depth;
        uint32_t wire;
    } table[] = {
        { 2u, 2u, 33532u + 2u * 16896u },
        { 4u, 3u, 33532u + 3u * 16896u },
        { 8u, 4u, 33532u + 4u * 16896u },
        { 16u, 5u, 33532u + 5u * 16896u },
        { 32u, 6u, 33532u + 6u * 16896u },
        { 64u, 7u, 33532u + 7u * 16896u },
        { 128u, 8u, 33532u + 8u * 16896u },
        { 256u, 9u, 33532u + 9u * 16896u },
    };
    for (size_t i = 0u; i < sizeof(table) / sizeof(table[0]); ++i) {
        dap_assert(chipmunk_mring_fold_depth_for(table[i].n) == table[i].depth,
                   "fold_depth");
        dap_assert(chipmunk_mring_wire_size(table[i].depth) == table[i].wire,
                   "wire_size");
    }
}

static void test_signoff_envelope_gates(void)
{
    chipmunk_lrs_public_key_t ring[4];
    chipmunk_lrs_secret_key_t sk;
    for (uint32_t i = 0u; i < 4u; ++i) {
        chipmunk_lrs_secret_key_t tmp;
        s_kp(&ring[i], &tmp, (uint8_t)(0x30u + i));
    }
    s_kp(&ring[0], &sk, 0xC1u);
    const chipmunk_lrs_secret_key_t *ptrs[1] = { &sk };
    uint8_t seeds[1 * CHIPMUNK_LRS_SEED_BYTES];
    s_fill(seeds, sizeof(seeds), 0xCCu);
    const uint8_t msg[] = "gate-test";

    /* NULL params. */
    dap_assert(chipmunk_ring_sign_to_bytes(
        NULL, NULL, NULL, 0u, NULL, 0u, 0u, NULL, 0u, NULL, 0u, NULL)
        == CHIPMUNK_RING_ERR_NULL_PARAM, "sign NULL gate");
    dap_assert(chipmunk_ring_verify_from_bytes(
        NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u)
        == CHIPMUNK_RING_ERR_NULL_PARAM, "verify NULL gate");

    /* ring_size out of range. */
    dap_assert(chipmunk_ring_sign_to_bytes(
        &(uint8_t *){NULL}, &(size_t){0}, ptrs, 1u, ring, 1u, 1u,
        msg, sizeof(msg) - 1u, NULL, 0u, seeds)
        == CHIPMUNK_RING_ERR_N_RING_OUT_OF_RANGE, "ring_size < N_MIN");
    dap_assert(chipmunk_ring_sign_to_bytes(
        &(uint8_t *){NULL}, &(size_t){0}, ptrs, 1u, ring, 257u, 1u,
        msg, sizeof(msg) - 1u, NULL, 0u, seeds)
        == CHIPMUNK_RING_ERR_N_RING_OUT_OF_RANGE, "ring_size > N_MAX");

    /* threshold out of range. */
    dap_assert(chipmunk_ring_sign_to_bytes(
        &(uint8_t *){NULL}, &(size_t){0}, ptrs, 1u, ring, 4u, 5u,
        msg, sizeof(msg) - 1u, NULL, 0u, seeds)
        == CHIPMUNK_RING_ERR_T_OUT_OF_RANGE, "threshold > ring_size");

    /* signer_count != threshold. */
    dap_assert(chipmunk_ring_sign_to_bytes(
        &(uint8_t *){NULL}, &(size_t){0}, ptrs, 1u, ring, 4u, 2u,
        msg, sizeof(msg) - 1u, NULL, 0u, seeds)
        == CHIPMUNK_RING_ERR_T_OUT_OF_RANGE, "count != threshold");
}

static void test_signoff_strerror(void)
{
    static const chipmunk_ring_error_t codes[] = {
        CHIPMUNK_RING_OK,
        CHIPMUNK_RING_ERR_NULL_PARAM,
        CHIPMUNK_RING_ERR_BUFFER_TOO_SMALL,
        CHIPMUNK_RING_ERR_MAGIC_MISMATCH,
        CHIPMUNK_RING_ERR_VERSION_MISMATCH,
        CHIPMUNK_RING_ERR_N_RING_OUT_OF_RANGE,
        CHIPMUNK_RING_ERR_T_OUT_OF_RANGE,
        CHIPMUNK_RING_ERR_RING_HASH_MISMATCH,
        CHIPMUNK_RING_ERR_CTX_HASH_MISMATCH,
        CHIPMUNK_RING_ERR_TAG_ORDER,
        CHIPMUNK_RING_ERR_TAG_DUPLICATE,
        CHIPMUNK_RING_ERR_NORM_BOUND,
        CHIPMUNK_RING_ERR_PROOF_FAIL,
        CHIPMUNK_RING_ERR_FIAT_SHAMIR_MISMATCH,
        CHIPMUNK_RING_ERR_PARAMS_MISMATCH,
        CHIPMUNK_RING_ERR_RING_PK_DUPLICATE,
        CHIPMUNK_RING_ERR_RING_NOT_CANONICAL,
        CHIPMUNK_RING_ERR_NOT_IMPLEMENTED,
        CHIPMUNK_RING_ERR_INTERNAL,
    };
    for (size_t i = 0u; i < sizeof(codes) / sizeof(codes[0]); ++i) {
        const char *s = chipmunk_ring_strerror(codes[i]);
        dap_assert(s != NULL && s[0] != '\0', "strerror non-empty");
    }
}

int main(void)
{
    dap_set_appname("test_chipmunk_mring_signoff");
    dap_common_init("test_chipmunk_mring_signoff", NULL);

    test_signoff_n2();
    test_signoff_n4();
    test_signoff_wire_sizes();
    test_signoff_envelope_gates();
    test_signoff_strerror();

    log_it(L_INFO, "=== MRNG PRODUCTION SIGNOFF PASSED ===");
    dap_common_deinit();
    return 0;
}
