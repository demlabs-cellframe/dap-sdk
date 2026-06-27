/*
 * test_chipmunk_mring_wire.c — MRNG malformed wire input tests.
 *
 * Verifies that the verifier correctly rejects malformed inputs
 * without crashing or accepting invalid signatures.
 */

#include <dap_common.h>
#include <dap_test.h>

#include <stdint.h>
#include <string.h>

#include "chipmunk/chipmunk_ring.h"
#include "sig/chipmunk/chipmunk_mring.h"
#include "chipmunk/chipmunk_lrs.h"

#define LOG_TAG "test_chipmunk_mring_wire"

static void s_fill(uint8_t *a_out, size_t a_len, uint8_t a_salt)
{
    for (size_t i = 0u; i < a_len; ++i) a_out[i] = (uint8_t)(0x42u ^ (uint8_t)i ^ a_salt);
}

static void s_kp(chipmunk_lrs_public_key_t *a_pk,
                 chipmunk_lrs_secret_key_t *a_sk,
                 uint8_t a_salt)
{
    uint8_t l_seed[CHIPMUNK_LRS_SEED_BYTES];
    s_fill(l_seed, sizeof(l_seed), a_salt);
    dap_assert(chipmunk_lrs_keypair_from_seeds(a_pk, a_sk, l_seed) == 0, "keypair");
}

/* Generate a valid signature for reuse in tamper tests. */
static uint8_t *s_make_valid_sig(size_t *a_sz)
{
    enum { N = 4, T = 2 };
    chipmunk_lrs_public_key_t l_ring[N];
    chipmunk_lrs_secret_key_t l_sks[T];
    for (uint32_t i = 0u; i < N; ++i) {
        chipmunk_lrs_secret_key_t tmp;
        s_kp(&l_ring[i], &tmp, (uint8_t)(0x10u + i));
    }
    s_kp(&l_ring[0], &l_sks[0], 0xA1u);
    s_kp(&l_ring[2], &l_sks[1], 0xA2u);

    const chipmunk_lrs_secret_key_t *l_ptrs[T] = { &l_sks[0], &l_sks[1] };
    uint8_t l_seeds[T * CHIPMUNK_LRS_SEED_BYTES];
    s_fill(l_seeds, sizeof(l_seeds), 0x99u);

    uint8_t *l_sig = NULL;
    *a_sz = 0u;
    chipmunk_ring_error_t l_rc = chipmunk_ring_sign_to_bytes(
        &l_sig, a_sz, l_ptrs, T, l_ring, N, T,
        (const uint8_t *)"test", 4u, NULL, 0u, l_seeds);
    dap_assert(l_rc == CHIPMUNK_RING_OK, "valid sig generation");
    return l_sig;
}

/* Make a valid ring for verification. */
static void s_make_ring(chipmunk_lrs_public_key_t *a_ring, uint32_t a_N)
{
    for (uint32_t i = 0u; i < a_N; ++i) {
        chipmunk_lrs_secret_key_t tmp;
        s_kp(&a_ring[i], &tmp, (uint8_t)(0x10u + i));
    }
}

/* -------------------------------------------------------------------------
 * T1. Truncated wire — shorter than header.
 * ---------------------------------------------------------------------- */

static void test_truncated_wire(void)
{
    chipmunk_lrs_public_key_t l_ring[4];
    s_make_ring(l_ring, 4u);

    uint8_t l_buf[20]; /* Less than 28-byte header. */
    memset(l_buf, 0x42, sizeof(l_buf));

    chipmunk_ring_error_t l_rc = chipmunk_ring_verify_from_bytes(
        l_buf, sizeof(l_buf), l_ring, 4u,
        (const uint8_t *)"test", 4u, NULL, 0u);
    dap_assert(l_rc != CHIPMUNK_RING_OK, "truncated wire rejected");
}

/* -------------------------------------------------------------------------
 * T2. Bad magic.
 * ---------------------------------------------------------------------- */

static void test_bad_magic(void)
{
    chipmunk_lrs_public_key_t l_ring[4];
    s_make_ring(l_ring, 4u);

    size_t l_sz;
    uint8_t *l_sig = s_make_valid_sig(&l_sz);

    l_sig[0] ^= 0xFF; /* Corrupt magic byte. */
    chipmunk_ring_error_t l_rc = chipmunk_ring_verify_from_bytes(
        l_sig, l_sz, l_ring, 4u,
        (const uint8_t *)"test", 4u, NULL, 0u);
    dap_assert(l_rc == CHIPMUNK_RING_ERR_MAGIC_MISMATCH, "bad magic rejected");

    DAP_DELETE(l_sig);
}

/* -------------------------------------------------------------------------
 * T3. Bad version.
 * ---------------------------------------------------------------------- */

static void test_bad_version(void)
{
    chipmunk_lrs_public_key_t l_ring[4];
    s_make_ring(l_ring, 4u);

    size_t l_sz;
    uint8_t *l_sig = s_make_valid_sig(&l_sz);

    l_sig[4] = 0xFF; /* Version byte. */
    chipmunk_ring_error_t l_rc = chipmunk_ring_verify_from_bytes(
        l_sig, l_sz, l_ring, 4u,
        (const uint8_t *)"test", 4u, NULL, 0u);
    dap_assert(l_rc == CHIPMUNK_RING_ERR_VERSION_MISMATCH, "bad version rejected");

    DAP_DELETE(l_sig);
}

/* -------------------------------------------------------------------------
 * T4. Bad params_id.
 * ---------------------------------------------------------------------- */

static void test_bad_params(void)
{
    chipmunk_lrs_public_key_t l_ring[4];
    s_make_ring(l_ring, 4u);

    size_t l_sz;
    uint8_t *l_sig = s_make_valid_sig(&l_sz);

    l_sig[8] ^= 0xFF; /* params_id byte. */
    chipmunk_ring_error_t l_rc = chipmunk_ring_verify_from_bytes(
        l_sig, l_sz, l_ring, 4u,
        (const uint8_t *)"test", 4u, NULL, 0u);
    dap_assert(l_rc == CHIPMUNK_RING_ERR_PARAMS_MISMATCH, "bad params rejected");

    DAP_DELETE(l_sig);
}

/* -------------------------------------------------------------------------
 * T5. Non-zero reserved flags.
 * ---------------------------------------------------------------------- */

static void test_bad_flags(void)
{
    chipmunk_lrs_public_key_t l_ring[4];
    s_make_ring(l_ring, 4u);

    size_t l_sz;
    uint8_t *l_sig = s_make_valid_sig(&l_sz);

    l_sig[25] = 0x02; /* Set reserved bit. */
    chipmunk_ring_error_t l_rc = chipmunk_ring_verify_from_bytes(
        l_sig, l_sz, l_ring, 4u,
        (const uint8_t *)"test", 4u, NULL, 0u);
    dap_assert(l_rc == CHIPMUNK_RING_ERR_PARAMS_MISMATCH, "reserved flags rejected");

    DAP_DELETE(l_sig);
}

/* -------------------------------------------------------------------------
 * T6. Wrong ring size in verify call.
 * ---------------------------------------------------------------------- */

static void test_wrong_ring_size(void)
{
    chipmunk_lrs_public_key_t l_ring[4];
    s_make_ring(l_ring, 4u);

    size_t l_sz;
    uint8_t *l_sig = s_make_valid_sig(&l_sz);

    chipmunk_ring_error_t l_rc = chipmunk_ring_verify_from_bytes(
        l_sig, l_sz, l_ring, 3u, /* Wrong N. */
        (const uint8_t *)"test", 4u, NULL, 0u);
    dap_assert(l_rc != CHIPMUNK_RING_OK, "wrong ring size rejected");

    DAP_DELETE(l_sig);
}

/* -------------------------------------------------------------------------
 * T7. NULL parameters.
 * ---------------------------------------------------------------------- */

static void test_null_params(void)
{
    chipmunk_ring_error_t l_rc;

    l_rc = chipmunk_ring_verify_from_bytes(
        NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u);
    dap_assert(l_rc == CHIPMUNK_RING_ERR_NULL_PARAM, "all NULL rejected");

    l_rc = chipmunk_ring_sign_to_bytes(
        NULL, NULL, NULL, 0u, NULL, 0u, 0u, NULL, 0u, NULL, 0u, NULL);
    dap_assert(l_rc == CHIPMUNK_RING_ERR_NULL_PARAM, "sign all NULL rejected");
}

/* -------------------------------------------------------------------------
 * T8. Tampered T block (should fail verify, not crash).
 * ---------------------------------------------------------------------- */

static void test_tampered_T(void)
{
    chipmunk_lrs_public_key_t l_ring[4];
    s_make_ring(l_ring, 4u);

    size_t l_sz;
    uint8_t *l_sig = s_make_valid_sig(&l_sz);

    /* Tamper T block. */
    const uint32_t l_off_T = chipmunk_mring_section_off_T();
    l_sig[l_off_T] ^= 0x01u;

    chipmunk_ring_error_t l_rc = chipmunk_ring_verify_from_bytes(
        l_sig, l_sz, l_ring, 4u,
        (const uint8_t *)"test", 4u, NULL, 0u);
    dap_assert(l_rc != CHIPMUNK_RING_OK, "tampered T rejected");

    DAP_DELETE(l_sig);
}

/* -------------------------------------------------------------------------
 * T9. Tampered fs_seed (should fail verify, not crash).
 * ---------------------------------------------------------------------- */

static void test_tampered_fs_seed(void)
{
    chipmunk_lrs_public_key_t l_ring[4];
    s_make_ring(l_ring, 4u);

    size_t l_sz;
    uint8_t *l_sig = s_make_valid_sig(&l_sz);

    /* Tamper fs_seed (last 32 bytes of fixed hashes section). */
    const uint32_t l_off_hash = chipmunk_mring_section_off_fixed_hashes();
    l_sig[l_off_hash + 96u] ^= 0x01u;

    chipmunk_ring_error_t l_rc = chipmunk_ring_verify_from_bytes(
        l_sig, l_sz, l_ring, 4u,
        (const uint8_t *)"test", 4u, NULL, 0u);
    dap_assert(l_rc != CHIPMUNK_RING_OK, "tampered fs_seed rejected");

    DAP_DELETE(l_sig);
}

/* -------------------------------------------------------------------------
 * T10. Tampered leaf mask (should fail verify, not crash).
 * ---------------------------------------------------------------------- */

static void test_tampered_leaf_mask(void)
{
    chipmunk_lrs_public_key_t l_ring[4];
    s_make_ring(l_ring, 4u);

    size_t l_sz;
    uint8_t *l_sig = s_make_valid_sig(&l_sz);

    /* Tamper leaf mask. */
    const uint32_t l_depth = chipmunk_mring_fold_depth_for(4u);
    const uint32_t l_off_lm = chipmunk_mring_section_off_leaf_mask(l_depth);
    l_sig[l_off_lm] ^= 0x01u;

    chipmunk_ring_error_t l_rc = chipmunk_ring_verify_from_bytes(
        l_sig, l_sz, l_ring, 4u,
        (const uint8_t *)"test", 4u, NULL, 0u);
    dap_assert(l_rc != CHIPMUNK_RING_OK, "tampered leaf mask rejected");

    DAP_DELETE(l_sig);
}

/* -------------------------------------------------------------------------
 * T11. Wire size too small for claimed N.
 * ---------------------------------------------------------------------- */

static void test_wire_too_small(void)
{
    chipmunk_lrs_public_key_t l_ring[4];
    s_make_ring(l_ring, 4u);

    /* Create a header claiming N=256 but with only 100 bytes total. */
    uint8_t l_buf[100];
    memset(l_buf, 0, sizeof(l_buf));
    l_buf[0] = 0x4d; /* 'M' */
    l_buf[1] = 0x52; /* 'R' */
    l_buf[2] = 0x4e; /* 'N' */
    l_buf[3] = 0x47; /* 'G' */
    l_buf[4] = 0x01; /* version = 1 */
    l_buf[12] = 0x00; /* N = 256 (LE) */
    l_buf[13] = 0x01;

    chipmunk_ring_error_t l_rc = chipmunk_ring_verify_from_bytes(
        l_buf, sizeof(l_buf), l_ring, 4u,
        (const uint8_t *)"test", 4u, NULL, 0u);
    dap_assert(l_rc != CHIPMUNK_RING_OK, "wire too small rejected");
}

/* -------------------------------------------------------------------------
 * main.
 * ---------------------------------------------------------------------- */

int main(void)
{
    dap_set_appname("test_chipmunk_mring_wire");
    dap_common_init("test_chipmunk_mring_wire", NULL);

    test_truncated_wire();
    test_bad_magic();
    test_bad_version();
    test_bad_params();
    test_bad_flags();
    test_wrong_ring_size();
    test_null_params();
    test_tampered_T();
    test_tampered_fs_seed();
    test_tampered_leaf_mask();
    test_wire_too_small();

    log_it(L_INFO, "=== ALL MRNG malformed wire tests PASSED ===");
    dap_common_deinit();
    return 0;
}
