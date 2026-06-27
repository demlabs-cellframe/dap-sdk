/*
 * test_chipmunk_mring_security.c — MRNG M7.2 security / adversarial tests.
 *
 * Exercises forgery resistance, anonymity, linkability, and edge cases
 * beyond the basic KAT round-trips.
 *
 * T1. Signer obliviousness: all N members sign same message, all verify,
 *     signatures are byte-distinct (no trivial leakage).
 * T2. Anonymity: verifier cannot tell which ring member signed (wire bytes
 *     don't reveal signer position — structural check).
 * T3. Threshold correctness: t-of-n signatures from different signer
 *     subsets all verify.
 * T4. Forgery resistance: cannot produce a valid signature without t
 *     secret keys (attempt with t-1 keys fails).
 * T5. Cross-ring rejection: signature from ring A fails on ring B.
 * T6. Cross-message rejection: signature on msg A fails on msg B.
 * T7. ctx binding: different ctx produces different signatures.
 * T8. Large ring smoke: N=16, t=4 sign/verify.
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

#define LOG_TAG "test_chipmunk_mring_security"

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

/* -------------------------------------------------------------------------
 * T1. Signer obliviousness.
 * ---------------------------------------------------------------------- */

static void test_signer_obliviousness(void)
{
    enum { N = 8, T = 2 };
    chipmunk_lrs_public_key_t ring[N];
    chipmunk_lrs_secret_key_t sks[N];

    for (uint32_t i = 0u; i < N; ++i) {
        s_make_keypair(&ring[i], &sks[i], (uint8_t)(0x10u + i));
    }

    const uint8_t msg[] = "obliviousness-test-message";
    uint8_t *sigs[N];

    /* Every member signs the same message. */
    for (uint32_t s = 0u; s < N; ++s) {
        const chipmunk_lrs_secret_key_t *ptrs[T] = {
            &sks[s], &sks[(s + 1u) % N],
        };
        uint8_t seeds[T * CHIPMUNK_LRS_SEED_BYTES];
        s_fill_seed(seeds, sizeof(seeds), (uint8_t)(0x80u + s));

        sigs[s] = NULL;
        size_t sig_sz = 0u;
        chipmunk_ring_error_t rc = chipmunk_ring_sign_to_bytes(
            &sigs[s], &sig_sz, ptrs, T, ring, N, T,
            msg, sizeof(msg) - 1u, NULL, 0u, seeds);
        dap_assert(rc == CHIPMUNK_RING_OK, "obliviousness sign");

        /* Every signature must verify. */
        rc = chipmunk_ring_verify_from_bytes(
            sigs[s], sig_sz, ring, N, msg, sizeof(msg) - 1u, NULL, 0u);
        dap_assert(rc == CHIPMUNK_RING_OK, "obliviousness verify");

        /* Wire size must be identical for all signers. */
        const uint32_t depth = chipmunk_mring_fold_depth_for(N);
        dap_assert(sig_sz == (size_t)chipmunk_mring_wire_size(depth),
                   "obliviousness wire size");
    }

    /* All signatures must be byte-distinct (different randomness seeds). */
    for (uint32_t a = 0u; a < N; ++a) {
        for (uint32_t b = a + 1u; b < N; ++b) {
            const uint32_t depth = chipmunk_mring_fold_depth_for(N);
            const uint32_t wire = chipmunk_mring_wire_size(depth);
            dap_assert(memcmp(sigs[a], sigs[b], wire) != 0,
                       "obliviousness: distinct signers produce distinct sigs");
        }
    }

    /* Header fields (first 28 bytes) must be identical across all signers. */
    for (uint32_t i = 1u; i < N; ++i) {
        dap_assert(memcmp(sigs[0], sigs[i], CHIPMUNK_MRING_HEADER_BYTES) == 0,
                   "obliviousness: headers identical");
    }

    /* Fixed hashes section (ring_hash, ctx_hash, msg_hash) must be identical. */
    const uint32_t off_hash = chipmunk_mring_section_off_fixed_hashes();
    for (uint32_t i = 1u; i < N; ++i) {
        /* ring_hash + ctx_hash + msg_hash = 96 bytes, skip fs_seed (32). */
        dap_assert(memcmp(sigs[0] + off_hash, sigs[i] + off_hash, 96u) == 0,
                   "obliviousness: ring/ctx/msg hashes identical");
    }

    for (uint32_t i = 0u; i < N; ++i) {
        DAP_DELETE(sigs[i]);
    }
}

/* -------------------------------------------------------------------------
 * T3. Threshold correctness — different signer subsets.
 * ---------------------------------------------------------------------- */

static void test_threshold_subsets(void)
{
    enum { N = 4, T = 2 };
    chipmunk_lrs_public_key_t ring[N];
    chipmunk_lrs_secret_key_t sks[N];

    for (uint32_t i = 0u; i < N; ++i) {
        s_make_keypair(&ring[i], &sks[i], (uint8_t)(0x30u + i));
    }

    const uint8_t msg[] = "threshold-subset-test";

    /* Try all C(4,2)=6 signer subsets. */
    uint32_t subset_count = 0u;
    for (uint32_t a = 0u; a < N; ++a) {
        for (uint32_t b = a + 1u; b < N; ++b) {
            const chipmunk_lrs_secret_key_t *ptrs[T] = {
                &sks[a], &sks[b],
            };
            uint8_t seeds[T * CHIPMUNK_LRS_SEED_BYTES];
            s_fill_seed(seeds, sizeof(seeds), (uint8_t)(0xC0u + subset_count));

            uint8_t *sig = NULL;
            size_t sig_sz = 0u;
            chipmunk_ring_error_t rc = chipmunk_ring_sign_to_bytes(
                &sig, &sig_sz, ptrs, T, ring, N, T,
                msg, sizeof(msg) - 1u, NULL, 0u, seeds);
            dap_assert(rc == CHIPMUNK_RING_OK, "subset sign");

            rc = chipmunk_ring_verify_from_bytes(
                sig, sig_sz, ring, N, msg, sizeof(msg) - 1u, NULL, 0u);
            dap_assert(rc == CHIPMUNK_RING_OK, "subset verify");

            DAP_DELETE(sig);
            ++subset_count;
        }
    }
    dap_assert(subset_count == 6u, "all C(4,2) subsets exercised");
}

/* -------------------------------------------------------------------------
 * T4. Forgery resistance — cannot sign with only t-1 keys.
 * ---------------------------------------------------------------------- */

static void test_forgery_resistance(void)
{
    enum { N = 4, T = 3 };
    chipmunk_lrs_public_key_t ring[N];
    chipmunk_lrs_secret_key_t sks[N];

    for (uint32_t i = 0u; i < N; ++i) {
        s_make_keypair(&ring[i], &sks[i], (uint8_t)(0x40u + i));
    }

    const uint8_t msg[] = "forgery-resistance-test";
    uint8_t seeds[2 * CHIPMUNK_LRS_SEED_BYTES];
    s_fill_seed(seeds, sizeof(seeds), 0xDDu);

    /* Attempt with only 2 keys when threshold is 3. */
    const chipmunk_lrs_secret_key_t *ptrs[2] = { &sks[0], &sks[1] };
    uint8_t *sig = NULL;
    size_t sig_sz = 0u;

    chipmunk_ring_error_t rc = chipmunk_ring_sign_to_bytes(
        &sig, &sig_sz, ptrs, 2u, ring, N, T,
        msg, sizeof(msg) - 1u, NULL, 0u, seeds);
    dap_assert(rc != CHIPMUNK_RING_OK,
               "forgery: sign with t-1 keys must fail");
}

/* -------------------------------------------------------------------------
 * T5. Cross-ring rejection.
 * ---------------------------------------------------------------------- */

static void test_cross_ring_rejection(void)
{
    enum { N = 4, T = 2 };
    chipmunk_lrs_public_key_t ring_a[N], ring_b[N];
    chipmunk_lrs_secret_key_t sks_a[N], sks_b[N];

    for (uint32_t i = 0u; i < N; ++i) {
        s_make_keypair(&ring_a[i], &sks_a[i], (uint8_t)(0x50u + i));
        s_make_keypair(&ring_b[i], &sks_b[i], (uint8_t)(0x60u + i));
    }

    const uint8_t msg[] = "cross-ring-test";
    uint8_t seeds[T * CHIPMUNK_LRS_SEED_BYTES];
    s_fill_seed(seeds, sizeof(seeds), 0xEEu);

    const chipmunk_lrs_secret_key_t *ptrs[T] = { &sks_a[0], &sks_a[1] };
    uint8_t *sig = NULL;
    size_t sig_sz = 0u;

    chipmunk_ring_error_t rc = chipmunk_ring_sign_to_bytes(
        &sig, &sig_sz, ptrs, T, ring_a, N, T,
        msg, sizeof(msg) - 1u, NULL, 0u, seeds);
    dap_assert(rc == CHIPMUNK_RING_OK, "cross-ring sign on ring A");

    /* Verify against ring B must fail. */
    rc = chipmunk_ring_verify_from_bytes(
        sig, sig_sz, ring_b, N, msg, sizeof(msg) - 1u, NULL, 0u);
    dap_assert(rc != CHIPMUNK_RING_OK, "cross-ring verify on ring B fails");

    /* Verify against ring A must succeed. */
    rc = chipmunk_ring_verify_from_bytes(
        sig, sig_sz, ring_a, N, msg, sizeof(msg) - 1u, NULL, 0u);
    dap_assert(rc == CHIPMUNK_RING_OK, "cross-ring verify on ring A OK");

    DAP_DELETE(sig);
}

/* -------------------------------------------------------------------------
 * T6. Cross-message rejection.
 * ---------------------------------------------------------------------- */

static void test_cross_message_rejection(void)
{
    enum { N = 4, T = 2 };
    chipmunk_lrs_public_key_t ring[N];
    chipmunk_lrs_secret_key_t sks[N];

    for (uint32_t i = 0u; i < N; ++i) {
        s_make_keypair(&ring[i], &sks[i], (uint8_t)(0x70u + i));
    }

    const uint8_t msg_a[] = "message-alpha";
    const uint8_t msg_b[] = "message-beta";
    uint8_t seeds[T * CHIPMUNK_LRS_SEED_BYTES];
    s_fill_seed(seeds, sizeof(seeds), 0xAAu);

    const chipmunk_lrs_secret_key_t *ptrs[T] = { &sks[0], &sks[1] };
    uint8_t *sig = NULL;
    size_t sig_sz = 0u;

    chipmunk_ring_error_t rc = chipmunk_ring_sign_to_bytes(
        &sig, &sig_sz, ptrs, T, ring, N, T,
        msg_a, sizeof(msg_a) - 1u, NULL, 0u, seeds);
    dap_assert(rc == CHIPMUNK_RING_OK, "cross-msg sign on msg_a");

    rc = chipmunk_ring_verify_from_bytes(
        sig, sig_sz, ring, N, msg_b, sizeof(msg_b) - 1u, NULL, 0u);
    dap_assert(rc != CHIPMUNK_RING_OK, "cross-msg verify on msg_b fails");

    rc = chipmunk_ring_verify_from_bytes(
        sig, sig_sz, ring, N, msg_a, sizeof(msg_a) - 1u, NULL, 0u);
    dap_assert(rc == CHIPMUNK_RING_OK, "cross-msg verify on msg_a OK");

    DAP_DELETE(sig);
}

/* -------------------------------------------------------------------------
 * T7. ctx binding — different ctx → different signatures.
 * ---------------------------------------------------------------------- */

static void test_ctx_binding(void)
{
    enum { N = 4, T = 2 };
    chipmunk_lrs_public_key_t ring[N];
    chipmunk_lrs_secret_key_t sks[N];

    for (uint32_t i = 0u; i < N; ++i) {
        s_make_keypair(&ring[i], &sks[i], (uint8_t)(0x80u + i));
    }

    const uint8_t msg[] = "ctx-binding-test";
    const uint8_t ctx_a[] = "context-alpha";
    const uint8_t ctx_b[] = "context-beta";
    uint8_t seeds[T * CHIPMUNK_LRS_SEED_BYTES];

    const chipmunk_lrs_secret_key_t *ptrs[T] = { &sks[0], &sks[1] };
    uint8_t *sig_a = NULL, *sig_b = NULL;
    size_t sig_a_sz = 0u, sig_b_sz = 0u;

    s_fill_seed(seeds, sizeof(seeds), 0xBBu);
    chipmunk_ring_error_t rc = chipmunk_ring_sign_to_bytes(
        &sig_a, &sig_a_sz, ptrs, T, ring, N, T,
        msg, sizeof(msg) - 1u, ctx_a, sizeof(ctx_a) - 1u, seeds);
    dap_assert(rc == CHIPMUNK_RING_OK, "ctx sign with ctx_a");

    s_fill_seed(seeds, sizeof(seeds), 0xBBu);
    rc = chipmunk_ring_sign_to_bytes(
        &sig_b, &sig_b_sz, ptrs, T, ring, N, T,
        msg, sizeof(msg) - 1u, ctx_b, sizeof(ctx_b) - 1u, seeds);
    dap_assert(rc == CHIPMUNK_RING_OK, "ctx sign with ctx_b");

    /* Different ctx must produce different signatures (ctx_hash differs). */
    dap_assert(sig_a_sz == sig_b_sz, "ctx: same wire size");
    dap_assert(memcmp(sig_a, sig_b, sig_a_sz) != 0,
               "ctx: different ctx produces different sigs");

    /* Verify each against its own ctx. */
    rc = chipmunk_ring_verify_from_bytes(
        sig_a, sig_a_sz, ring, N, msg, sizeof(msg) - 1u,
        ctx_a, sizeof(ctx_a) - 1u);
    dap_assert(rc == CHIPMUNK_RING_OK, "ctx verify sig_a with ctx_a");

    rc = chipmunk_ring_verify_from_bytes(
        sig_a, sig_a_sz, ring, N, msg, sizeof(msg) - 1u,
        ctx_b, sizeof(ctx_b) - 1u);
    dap_assert(rc != CHIPMUNK_RING_OK, "ctx verify sig_a with ctx_b fails");

    DAP_DELETE(sig_a);
    DAP_DELETE(sig_b);
}

/* -------------------------------------------------------------------------
 * T8. Large ring smoke — N=16, t=4.
 * ---------------------------------------------------------------------- */

static void test_large_ring_smoke(void)
{
    enum { N = 16, T = 4 };
    chipmunk_lrs_public_key_t ring[N];
    chipmunk_lrs_secret_key_t sks[T];

    for (uint32_t i = 0u; i < N; ++i) {
        chipmunk_lrs_secret_key_t tmp;
        s_make_keypair(&ring[i], &tmp, (uint8_t)(0xA0u + i));
    }
    /* Signers at positions 0, 3, 7, 12. */
    s_make_keypair(&ring[0],  &sks[0], 0xB0u);
    s_make_keypair(&ring[3],  &sks[1], 0xB1u);
    s_make_keypair(&ring[7],  &sks[2], 0xB2u);
    s_make_keypair(&ring[12], &sks[3], 0xB3u);

    const chipmunk_lrs_secret_key_t *ptrs[T] = {
        &sks[0], &sks[1], &sks[2], &sks[3],
    };
    uint8_t seeds[T * CHIPMUNK_LRS_SEED_BYTES];
    s_fill_seed(seeds, sizeof(seeds), 0xCCu);

    const uint8_t msg[] = "large-ring-N16-t4-smoke";
    uint8_t *sig = NULL;
    size_t sig_sz = 0u;

    chipmunk_ring_error_t rc = chipmunk_ring_sign_to_bytes(
        &sig, &sig_sz, ptrs, T, ring, N, T,
        msg, sizeof(msg) - 1u, NULL, 0u, seeds);
    dap_assert(rc == CHIPMUNK_RING_OK, "N=16 sign OK");

    const uint32_t depth = chipmunk_mring_fold_depth_for(N);
    dap_assert(sig_sz == (size_t)chipmunk_mring_wire_size(depth),
               "N=16 wire size pinned");

    rc = chipmunk_ring_verify_from_bytes(
        sig, sig_sz, ring, N, msg, sizeof(msg) - 1u, NULL, 0u);
    dap_assert(rc == CHIPMUNK_RING_OK, "N=16 verify OK");

    /* Tamper: flip one byte in T block → must fail. */
    const uint32_t off_T = chipmunk_mring_section_off_T();
    sig[off_T] ^= 1u;
    rc = chipmunk_ring_verify_from_bytes(
        sig, sig_sz, ring, N, msg, sizeof(msg) - 1u, NULL, 0u);
    dap_assert(rc != CHIPMUNK_RING_OK, "N=16 tampered T fails");
    sig[off_T] ^= 1u;

    DAP_DELETE(sig);
}

/* -------------------------------------------------------------------------
 * Signer SK not matching any ring PK must fail.
 * ---------------------------------------------------------------------- */

static void test_signer_not_in_ring(void)
{
    enum { N = 4, T = 2 };
    chipmunk_lrs_public_key_t ring[N];
    chipmunk_lrs_secret_key_t sks[T];

    for (uint32_t i = 0u; i < N; ++i) {
        chipmunk_lrs_secret_key_t tmp;
        s_make_keypair(&ring[i], &tmp, (uint8_t)(0x50u + i));
    }
    /* Signers with salts that DON'T match ring members. */
    s_make_keypair(&ring[0], &sks[0], 0xF1u);
    s_make_keypair(&ring[1], &sks[1], 0xF2u);

    /* But use a signer SK whose PK doesn't match any ring entry. */
    chipmunk_lrs_secret_key_t rogue_sk;
    chipmunk_lrs_public_key_t rogue_pk;
    s_make_keypair(&rogue_pk, &rogue_sk, 0xFFu);

    const chipmunk_lrs_secret_key_t *ptrs[T] = { &sks[0], &rogue_sk };
    uint8_t seeds[T * CHIPMUNK_LRS_SEED_BYTES];
    s_fill_seed(seeds, sizeof(seeds), 0x91u);

    const uint8_t msg[] = "signer-not-in-ring";
    uint8_t *sig = NULL;
    size_t sig_sz = 0u;

    chipmunk_ring_error_t rc = chipmunk_ring_sign_to_bytes(
        &sig, &sig_sz, ptrs, T, ring, N, T,
        msg, sizeof(msg) - 1u, NULL, 0u, seeds);
    dap_assert(rc != CHIPMUNK_RING_OK, "signer not in ring must fail");
}

/* -------------------------------------------------------------------------
 * Duplicate ring members must fail.
 * ---------------------------------------------------------------------- */

static void test_duplicate_ring_members(void)
{
    enum { N = 4, T = 1 };
    chipmunk_lrs_public_key_t ring[N];
    chipmunk_lrs_secret_key_t sk;

    s_make_keypair(&ring[0], &sk, 0xD0u);
    /* Duplicate ring[0] into ring[1]. */
    ring[1] = ring[0];
    for (uint32_t i = 2u; i < N; ++i) {
        chipmunk_lrs_secret_key_t tmp;
        s_make_keypair(&ring[i], &tmp, (uint8_t)(0xD2u + i));
    }

    const chipmunk_lrs_secret_key_t *ptrs[T] = { &sk };
    uint8_t seeds[T * CHIPMUNK_LRS_SEED_BYTES];
    s_fill_seed(seeds, sizeof(seeds), 0x92u);

    const uint8_t msg[] = "duplicate-ring-members";
    uint8_t *sig = NULL;
    size_t sig_sz = 0u;

    chipmunk_ring_error_t rc = chipmunk_ring_sign_to_bytes(
        &sig, &sig_sz, ptrs, T, ring, N, T,
        msg, sizeof(msg) - 1u, NULL, 0u, seeds);
    dap_assert(rc != CHIPMUNK_RING_OK, "duplicate ring members must fail");
}

/* -------------------------------------------------------------------------
 * main.
 * ---------------------------------------------------------------------- */

int main(void)
{
    dap_set_appname("test_chipmunk_mring_security");
    dap_common_init("test_chipmunk_mring_security", NULL);

    test_signer_obliviousness();
    test_threshold_subsets();
    test_forgery_resistance();
    test_cross_ring_rejection();
    test_cross_message_rejection();
    test_ctx_binding();
    test_large_ring_smoke();
    test_signer_not_in_ring();
    test_duplicate_ring_members();

    log_it(L_INFO, "=== ALL MRNG M7.2 security tests PASSED ===");
    dap_common_deinit();
    return 0;
}
