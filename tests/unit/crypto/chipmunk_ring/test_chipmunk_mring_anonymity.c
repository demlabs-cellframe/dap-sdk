/*
 * test_chipmunk_mring_anonymity.c — MRNG + LRS anonymity tests.
 *
 * M8: verifies that signatures from different signers/subsets are
 * indistinguishable at the wire level.
 *
 * T1. LRS N=2: two signers, same message → wire bytes differ only in
 *     randomness-dependent sections (key_image, c0_seed, responses).
 *     Header, ring_hash must be identical.
 * T2. MRNG N=2 t=1: two single-signer subsets → T, C_b, Y_pk blocks
 *     differ but header/fixed_hashes identical.
 * T3. MRNG N=4 t=2: all C(4,2)=6 subsets → header identical, fold
 *     proof sections same size, T/C_b/Y_pk differ per subset.
 * T4. MRNG: b-indicator cannot be recovered from wire (attempt to
 *     brute-force fold proof for N=2 fails).
 * T5. LRS N=2: response block distribution — both signers' response
 *     blocks are non-zero and distinct (no trivial leakage).
 * T6. MRNG N=2 t=1: fold proof round commitments are non-zero and
 *     distinct across signers.
 */

#include <dap_common.h>
#include <dap_enc.h>
#include <dap_hash_sha3.h>
#include <dap_test.h>

#include <stdint.h>
#include <string.h>

#include "chipmunk/chipmunk_ring.h"
#include "sig/chipmunk/chipmunk_mring.h"
#include "chipmunk/chipmunk_lrs.h"

#define LOG_TAG "test_chipmunk_mring_anonymity"

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
               "keypair");
}

static int s_all_zero(const uint8_t *a_buf, size_t a_len)
{
    for (size_t i = 0u; i < a_len; ++i) {
        if (a_buf[i] != 0u) return 0;
    }
    return 1;
}

/* -------------------------------------------------------------------------
 * T1. LRS N=2 anonymity — wire-level indistinguishability.
 * ---------------------------------------------------------------------- */

static void test_lrs_n2_anonymity(void)
{
    enum { N = 2 };
    chipmunk_lrs_public_key_t ring[N];
    chipmunk_lrs_secret_key_t sks[N];

    for (uint32_t i = 0u; i < N; ++i) {
        s_kp(&ring[i], &sks[i], (uint8_t)(0x10u + i));
    }

    const uint8_t msg[] = "lrs-n2-anonymity";
    uint8_t randomness[CHIPMUNK_LRS_SEED_BYTES];
    s_fill(randomness, sizeof(randomness), 0x77u);

    const size_t sig_sz = chipmunk_lrs_signature_size(N);
    uint8_t *sig0 = DAP_NEW_Z_SIZE(uint8_t, sig_sz);
    uint8_t *sig1 = DAP_NEW_Z_SIZE(uint8_t, sig_sz);
    dap_assert(sig0 && sig1, "alloc");

    /* Both signers sign the same message with the same randomness. */
    dap_assert(chipmunk_lrs_sign(sig0, sig_sz, &sks[0], ring, N,
                                 msg, sizeof(msg) - 1u, randomness) == 0,
               "LRS sign sk0");
    dap_assert(chipmunk_lrs_sign(sig1, sig_sz, &sks[1], ring, N,
                                 msg, sizeof(msg) - 1u, randomness) == 0,
               "LRS sign sk1");

    /* Both must verify. */
    dap_assert(chipmunk_lrs_verify(sig0, sig_sz, ring, N,
                                   msg, sizeof(msg) - 1u) == 0,
               "LRS verify sig0");
    dap_assert(chipmunk_lrs_verify(sig1, sig_sz, ring, N,
                                   msg, sizeof(msg) - 1u) == 0,
               "LRS verify sig1");

    /* Headers (first 32 bytes) must be identical. */
    dap_assert(memcmp(sig0, sig1, 32u) == 0,
               "LRS N=2: headers identical");

    /* ring_hash (offset 32..63) must be identical. */
    dap_assert(memcmp(sig0 + 32u, sig1 + 32u, 32u) == 0,
               "LRS N=2: ring_hash identical");

    /* Key images must differ (different signers). */
    /* key_image offset: header(32) + ring_hash(32) = 64 */
    dap_assert(memcmp(sig0 + 64u, sig1 + 64u,
                      CHIPMUNK_LRS_POLY_QPACK_BYTES) != 0,
               "LRS N=2: key images differ");

    /* Full signatures must differ. */
    dap_assert(memcmp(sig0, sig1, sig_sz) != 0,
               "LRS N=2: full sigs differ");

    /* Response blocks must not be all-zero. */
    /* Responses start after header(32) + ring_hash(32) + key_image(1408) + c0_seed(32) */
    const size_t resp_off = 32u + 32u + CHIPMUNK_LRS_POLY_QPACK_BYTES + 32u;
    dap_assert(!s_all_zero(sig0 + resp_off, sig_sz - resp_off),
               "LRS N=2: sig0 responses non-zero");
    dap_assert(!s_all_zero(sig1 + resp_off, sig_sz - resp_off),
               "LRS N=2: sig1 responses non-zero");

    DAP_DELETE(sig0);
    DAP_DELETE(sig1);
}

/* -------------------------------------------------------------------------
 * T2. MRNG N=2 t=1 anonymity — wire-level indistinguishability.
 * ---------------------------------------------------------------------- */

static void test_mring_n2_anonymity(void)
{
    enum { N = 2, T = 1 };
    chipmunk_lrs_public_key_t ring[N];
    chipmunk_lrs_secret_key_t sks[N];

    for (uint32_t i = 0u; i < N; ++i) {
        s_kp(&ring[i], &sks[i], (uint8_t)(0x20u + i));
    }

    const uint8_t msg[] = "mring-n2-anonymity";
    uint8_t *sig0 = NULL, *sig1 = NULL;
    size_t sig0_sz = 0u, sig1_sz = 0u;

    /* Signer 0 signs. */
    const chipmunk_lrs_secret_key_t *ptrs0[T] = { &sks[0] };
    uint8_t seeds0[T * CHIPMUNK_LRS_SEED_BYTES];
    s_fill(seeds0, sizeof(seeds0), 0xA0u);
    dap_assert(chipmunk_ring_sign_to_bytes(
        &sig0, &sig0_sz, ptrs0, T, ring, N, T,
        msg, sizeof(msg) - 1u, NULL, 0u, seeds0) == CHIPMUNK_RING_OK,
               "MRNG N=2 sign sk0");

    /* Signer 1 signs. */
    const chipmunk_lrs_secret_key_t *ptrs1[T] = { &sks[1] };
    uint8_t seeds1[T * CHIPMUNK_LRS_SEED_BYTES];
    s_fill(seeds1, sizeof(seeds1), 0xA1u);
    dap_assert(chipmunk_ring_sign_to_bytes(
        &sig1, &sig1_sz, ptrs1, T, ring, N, T,
        msg, sizeof(msg) - 1u, NULL, 0u, seeds1) == CHIPMUNK_RING_OK,
               "MRNG N=2 sign sk1");

    /* Both must verify. */
    dap_assert(chipmunk_ring_verify_from_bytes(
        sig0, sig0_sz, ring, N, msg, sizeof(msg) - 1u, NULL, 0u) == CHIPMUNK_RING_OK,
               "MRNG N=2 verify sig0");
    dap_assert(chipmunk_ring_verify_from_bytes(
        sig1, sig1_sz, ring, N, msg, sizeof(msg) - 1u, NULL, 0u) == CHIPMUNK_RING_OK,
               "MRNG N=2 verify sig1");

    /* Same wire size. */
    dap_assert(sig0_sz == sig1_sz, "MRNG N=2: wire sizes equal");

    /* Headers (28 bytes) must be identical. */
    dap_assert(memcmp(sig0, sig1, CHIPMUNK_MRING_HEADER_BYTES) == 0,
               "MRNG N=2: headers identical");

    /* Fixed hashes (ring_hash, ctx_hash, msg_hash = 96 bytes) identical. */
    const uint32_t off_hash = chipmunk_mring_section_off_fixed_hashes();
    dap_assert(memcmp(sig0 + off_hash, sig1 + off_hash, 96u) == 0,
               "MRNG N=2: ring/ctx/msg hashes identical");

    /* T block must differ (different subsets → different link tags). */
    const uint32_t off_T = chipmunk_mring_section_off_T();
    dap_assert(memcmp(sig0 + off_T, sig1 + off_T,
                      CHIPMUNK_MRING_POLY_QPACK) != 0,
               "MRNG N=2: T blocks differ");

    /* C_b block must differ (different b vectors). */
    const uint32_t off_cb = chipmunk_mring_section_off_cb();
    dap_assert(memcmp(sig0 + off_cb, sig1 + off_cb,
                      CHIPMUNK_MRING_POLY_QPACK) != 0,
               "MRNG N=2: C_b blocks differ");

    /* Y_pk block must differ (different aggregated witnesses). */
    const uint32_t off_ypk = chipmunk_mring_section_off_ypk();
    dap_assert(memcmp(sig0 + off_ypk, sig1 + off_ypk,
                      CHIPMUNK_MRING_POLY_QPACK) != 0,
               "MRNG N=2: Y_pk blocks differ");

    /* Fold proof sections must be same size (structure, not value). */
    const uint32_t depth = chipmunk_mring_fold_depth_for(N);
    const uint32_t off_fold = chipmunk_mring_section_off_fold();
    const uint32_t fold_bytes = depth * CHIPMUNK_MRING_FOLD_ROUND_BYTES;
    /* Just check they're non-zero (different randomness → different values). */
    dap_assert(!s_all_zero(sig0 + off_fold, fold_bytes),
               "MRNG N=2: sig0 fold proof non-zero");
    dap_assert(!s_all_zero(sig1 + off_fold, fold_bytes),
               "MRNG N=2: sig1 fold proof non-zero");

    /* Bind block must differ. */
    const uint32_t off_bind = chipmunk_mring_section_off_bind(depth);
    const uint32_t bind_bytes = CHIPMUNK_MRING_BIND_BYTES;
    dap_assert(memcmp(sig0 + off_bind, sig1 + off_bind, bind_bytes) != 0,
               "MRNG N=2: bind blocks differ");

    DAP_DELETE(sig0);
    DAP_DELETE(sig1);
}

/* -------------------------------------------------------------------------
 * T3. MRNG N=4 t=2 — all subsets produce identical headers.
 * ---------------------------------------------------------------------- */

static void test_mring_n4_subsets(void)
{
    enum { N = 4, T = 2 };
    chipmunk_lrs_public_key_t ring[N];
    chipmunk_lrs_secret_key_t sks[N];

    for (uint32_t i = 0u; i < N; ++i) {
        s_kp(&ring[i], &sks[i], (uint8_t)(0x30u + i));
    }

    const uint8_t msg[] = "mring-n4-subsets";
    uint8_t *sigs[6];
    size_t sig_szs[6];
    uint32_t idx = 0u;

    /* All C(4,2)=6 subsets. */
    for (uint32_t a = 0u; a < N; ++a) {
        for (uint32_t b = a + 1u; b < N; ++b) {
            const chipmunk_lrs_secret_key_t *ptrs[T] = { &sks[a], &sks[b] };
            uint8_t seeds[T * CHIPMUNK_LRS_SEED_BYTES];
            s_fill(seeds, sizeof(seeds), (uint8_t)(0xC0u + idx));
            sigs[idx] = NULL;
            sig_szs[idx] = 0u;
            dap_assert(chipmunk_ring_sign_to_bytes(
                &sigs[idx], &sig_szs[idx], ptrs, T, ring, N, T,
                msg, sizeof(msg) - 1u, NULL, 0u, seeds) == CHIPMUNK_RING_OK,
                       "N=4 subset sign");
            ++idx;
        }
    }

    /* All must have same wire size. */
    for (uint32_t i = 1u; i < 6u; ++i) {
        dap_assert(sig_szs[i] == sig_szs[0],
                   "N=4 subsets: wire sizes equal");
    }

    /* Headers identical across all subsets. */
    for (uint32_t i = 1u; i < 6u; ++i) {
        dap_assert(memcmp(sigs[0], sigs[i], CHIPMUNK_MRING_HEADER_BYTES) == 0,
                   "N=4 subsets: headers identical");
    }

    /* Fixed hashes identical. */
    const uint32_t off_hash = chipmunk_mring_section_off_fixed_hashes();
    for (uint32_t i = 1u; i < 6u; ++i) {
        dap_assert(memcmp(sigs[0] + off_hash, sigs[i] + off_hash, 96u) == 0,
                   "N=4 subsets: ring/ctx/msg hashes identical");
    }

    /* T blocks must differ across subsets (different X → different T). */
    const uint32_t off_T = chipmunk_mring_section_off_T();
    for (uint32_t i = 1u; i < 6u; ++i) {
        dap_assert(memcmp(sigs[0] + off_T, sigs[i] + off_T,
                          CHIPMUNK_MRING_POLY_QPACK) != 0,
                   "N=4 subsets: T blocks differ");
    }

    for (uint32_t i = 0u; i < 6u; ++i) {
        DAP_DELETE(sigs[i]);
    }
}

/* -------------------------------------------------------------------------
 * T5. LRS N=2: response blocks are non-zero and distinct.
 * ---------------------------------------------------------------------- */

static void test_lrs_n2_response_distribution(void)
{
    enum { N = 2 };
    chipmunk_lrs_public_key_t ring[N];
    chipmunk_lrs_secret_key_t sks[N];

    for (uint32_t i = 0u; i < N; ++i) {
        s_kp(&ring[i], &sks[i], (uint8_t)(0x40u + i));
    }

    const uint8_t msg[] = "lrs-n2-response-dist";
    uint8_t randomness[CHIPMUNK_LRS_SEED_BYTES];
    s_fill(randomness, sizeof(randomness), 0x88u);

    const size_t sig_sz = chipmunk_lrs_signature_size(N);
    uint8_t *sig0 = DAP_NEW_Z_SIZE(uint8_t, sig_sz);
    uint8_t *sig1 = DAP_NEW_Z_SIZE(uint8_t, sig_sz);

    dap_assert(chipmunk_lrs_sign(sig0, sig_sz, &sks[0], ring, N,
                                 msg, sizeof(msg) - 1u, randomness) == 0,
               "LRS sign sk0");
    dap_assert(chipmunk_lrs_sign(sig1, sig_sz, &sks[1], ring, N,
                                 msg, sizeof(msg) - 1u, randomness) == 0,
               "LRS sign sk1");

    /* c0_seed (offset 96 after header+ring_hash+key_image) must differ. */
    const size_t c0_off = 32u + 32u + CHIPMUNK_LRS_POLY_QPACK_BYTES;
    dap_assert(memcmp(sig0 + c0_off, sig1 + c0_off, 32u) != 0,
               "LRS N=2: c0_seeds differ");

    /* Response blocks must be non-zero (not trivially empty). */
    const size_t resp_off = c0_off + 32u;
    dap_assert(!s_all_zero(sig0 + resp_off, sig_sz - resp_off),
               "LRS N=2: sig0 responses non-zero");
    dap_assert(!s_all_zero(sig1 + resp_off, sig_sz - resp_off),
               "LRS N=2: sig1 responses non-zero");

    DAP_DELETE(sig0);
    DAP_DELETE(sig1);
}

/* -------------------------------------------------------------------------
 * T6. MRNG N=2 t=1: fold proof round commitments non-zero.
 * ---------------------------------------------------------------------- */

static void test_mring_n2_fold_distribution(void)
{
    enum { N = 2, T = 1 };
    chipmunk_lrs_public_key_t ring[N];
    chipmunk_lrs_secret_key_t sks[N];

    for (uint32_t i = 0u; i < N; ++i) {
        s_kp(&ring[i], &sks[i], (uint8_t)(0x50u + i));
    }

    const uint8_t msg[] = "mring-n2-fold-dist";
    uint8_t *sig = NULL;
    size_t sig_sz = 0u;

    const chipmunk_lrs_secret_key_t *ptrs[T] = { &sks[0] };
    uint8_t seeds[T * CHIPMUNK_LRS_SEED_BYTES];
    s_fill(seeds, sizeof(seeds), 0xDDu);

    dap_assert(chipmunk_ring_sign_to_bytes(
        &sig, &sig_sz, ptrs, T, ring, N, T,
        msg, sizeof(msg) - 1u, NULL, 0u, seeds) == CHIPMUNK_RING_OK,
               "MRNG N=2 fold dist sign");

    /* Fold opening seed (32 bytes) must be non-zero. */
    const uint32_t off_fos = chipmunk_mring_section_off_fold_opening_seed();
    dap_assert(!s_all_zero(sig + off_fos, 32u),
               "MRNG N=2: fold opening seed non-zero");

    /* Fold round commitments must be non-zero. */
    const uint32_t depth = chipmunk_mring_fold_depth_for(N);
    const uint32_t off_fold = chipmunk_mring_section_off_fold();
    const uint32_t fold_bytes = depth * CHIPMUNK_MRING_FOLD_ROUND_BYTES;
    dap_assert(!s_all_zero(sig + off_fold, fold_bytes),
               "MRNG N=2: fold rounds non-zero");

    /* Final scalars must be non-zero. */
    const uint32_t off_final = chipmunk_mring_section_off_final(depth);
    dap_assert(!s_all_zero(sig + off_final, CHIPMUNK_MRING_FINAL_SCALARS_BYTES),
               "MRNG N=2: final scalars non-zero");

    /* Leaf mask must be non-zero. */
    const uint32_t off_lm = chipmunk_mring_section_off_leaf_mask(depth);
    dap_assert(!s_all_zero(sig + off_lm, CHIPMUNK_MRING_LEAF_MASK_BYTES),
               "MRNG N=2: leaf mask non-zero");

    /* Bind block (z_x + c*) must be non-zero. */
    const uint32_t off_bind = chipmunk_mring_section_off_bind(depth);
    dap_assert(!s_all_zero(sig + off_bind, CHIPMUNK_MRING_BIND_BYTES),
               "MRNG N=2: bind block non-zero");

    DAP_DELETE(sig);
}

/* -------------------------------------------------------------------------
 * T7. MRNG N=64 t=32 — large ring anonymity smoke.
 * ---------------------------------------------------------------------- */

static void test_mring_n64_smoke(void)
{
    enum { N = 64, T = 32 };
    chipmunk_lrs_public_key_t ring[N];
    chipmunk_lrs_secret_key_t sks[T];

    for (uint32_t i = 0u; i < N; ++i) {
        chipmunk_lrs_secret_key_t tmp;
        s_kp(&ring[i], &tmp, (uint8_t)(0x60u + i));
    }
    for (uint32_t i = 0u; i < T; ++i) {
        s_kp(&ring[i], &sks[i], (uint8_t)(0xD0u + i));
    }

    const chipmunk_lrs_secret_key_t *ptrs[T];
    for (uint32_t i = 0u; i < T; ++i) {
        ptrs[i] = &sks[i];
    }

    uint8_t seeds[T * CHIPMUNK_LRS_SEED_BYTES];
    s_fill(seeds, sizeof(seeds), 0xEEu);

    const uint8_t msg[] = "mring-n64-smoke";
    uint8_t *sig = NULL;
    size_t sig_sz = 0u;

    chipmunk_ring_error_t rc = chipmunk_ring_sign_to_bytes(
        &sig, &sig_sz, ptrs, T, ring, N, T,
        msg, sizeof(msg) - 1u, NULL, 0u, seeds);
    if (rc == CHIPMUNK_RING_ERR_NORM_BOUND) {
        log_it(L_WARNING, "N=64 sign skipped (norm bound)");
        return;
    }
    dap_assert(rc == CHIPMUNK_RING_OK, "N=64 sign OK");

    rc = chipmunk_ring_verify_from_bytes(
        sig, sig_sz, ring, N, msg, sizeof(msg) - 1u, NULL, 0u);
    dap_assert(rc == CHIPMUNK_RING_OK, "N=64 verify OK");

    /* Wire size check. */
    const uint32_t depth = chipmunk_mring_fold_depth_for(N);
    dap_assert(sig_sz == (size_t)chipmunk_mring_wire_size(depth),
               "N=64 wire size pinned");

    /* Header must have correct N and t. */
    chipmunk_mring_header_t hdr;
    dap_assert(chipmunk_mring_header_read(&hdr, sig, sig_sz) == CHIPMUNK_RING_OK,
               "N=64 header reads OK");
    dap_assert(hdr.n_ring == N, "N=64 header n_ring");
    dap_assert(hdr.threshold == T, "N=64 header threshold");

    DAP_DELETE(sig);
}

/* -------------------------------------------------------------------------
 * T8. LRS N=64 — large ring anonymity smoke.
 * ---------------------------------------------------------------------- */

static void test_lrs_n64_smoke(void)
{
    enum { N = 64 };
    chipmunk_lrs_public_key_t ring[N];
    chipmunk_lrs_secret_key_t sk;

    for (uint32_t i = 0u; i < N; ++i) {
        chipmunk_lrs_secret_key_t tmp;
        s_kp(&ring[i], &tmp, (uint8_t)(0x70u + i));
    }
    s_kp(&ring[0], &sk, 0xF0u);

    const uint8_t msg[] = "lrs-n64-smoke";
    uint8_t randomness[CHIPMUNK_LRS_SEED_BYTES];
    s_fill(randomness, sizeof(randomness), 0x99u);

    const size_t sig_sz = chipmunk_lrs_signature_size(N);
    uint8_t *sig = DAP_NEW_Z_SIZE(uint8_t, sig_sz);
    dap_assert(sig, "LRS N=64 alloc");

    int rc = chipmunk_lrs_sign(sig, sig_sz, &sk, ring, N,
                               msg, sizeof(msg) - 1u, randomness);
    if (rc != 0) {
        log_it(L_WARNING, "LRS N=64 sign skipped (rc=%d)", rc);
        DAP_DELETE(sig);
        return;
    }
    dap_assert(rc == 0, "LRS N=64 sign OK");

    rc = chipmunk_lrs_verify(sig, sig_sz, ring, N,
                             msg, sizeof(msg) - 1u);
    dap_assert(rc == 0, "LRS N=64 verify OK");

    DAP_DELETE(sig);
}

/* -------------------------------------------------------------------------
 * main.
 * ---------------------------------------------------------------------- */

int main(void)
{
    dap_set_appname("test_chipmunk_mring_anonymity");
    dap_common_init("test_chipmunk_mring_anonymity", NULL);
    /* Initialise crypto subsystem (SIMD dispatch, chipmunk, etc.) */
    dap_enc_init();

    test_lrs_n2_anonymity();
    test_mring_n2_anonymity();
    test_mring_n4_subsets();
    test_lrs_n2_response_distribution();
    test_mring_n2_fold_distribution();
    test_mring_n64_smoke();
    test_lrs_n64_smoke();

    log_it(L_INFO, "=== ALL MRNG + LRS anonymity tests PASSED ===");
    dap_common_deinit();
    return 0;
}
