/*
 * test_chipmunk_mring_kat.c — MRNG canonical KAT (Known-Answer Tests).
 *
 * M7.1: pins deterministic sign/verify round-trips on canonical wire
 * vectors.  Set CHIPMUNK_MRING_KAT_DUMP=1 to regenerate expected hashes.
 *
 * T1. Deterministic sign produces byte-identical wire for same inputs.
 * T2. Pinned SHA3-256 of canonical N=4, t=2 signature.
 * T3. Pinned SHA3-256 of canonical N=2, t=1 signature.
 * T4. Verify round-trip + negative gates (tamper, wrong msg, wrong ring).
 * T5. Header wire layout pinning.
 * T6. Section-offset consistency.
 */

#include <dap_common.h>
#include <dap_hash_sha3.h>
#include <dap_memwipe.h>
#include <dap_test.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chipmunk/chipmunk_ring.h"
#include "sig/chipmunk/chipmunk_mring.h"
#include "chipmunk/chipmunk_lrs.h"

#define LOG_TAG "test_chipmunk_mring_kat"

/* Canonical test seeds — same pattern as LRS KAT. */
static const uint8_t k_ring_seed[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};

static const uint8_t k_signer_salt_0 = 0xA1u;
static const uint8_t k_signer_salt_1 = 0xA2u;
static const uint8_t k_signer_salt_2 = 0xA3u;
static const uint8_t k_signer_salt_3 = 0xA4u;

static const uint8_t k_randomness_seed_fill = 0x99u;

static const uint8_t k_msg_n4[] = "mring-kat-canonical-N4-t2";
static const uint8_t k_msg_n2[] = "mring-kat-canonical-N2-t1";

/* Expected SHA3-256 digests — initially zero, populated by DUMP mode. */
static const uint8_t k_expected_sig_n4_sha3[32] = {
    0x4a, 0xf1, 0x6e, 0x92, 0x1a, 0x70, 0xcc, 0xfa,
    0xe8, 0x75, 0xe0, 0x90, 0xcf, 0xca, 0xfe, 0xf3,
    0xac, 0x21, 0xf0, 0x96, 0x07, 0xc6, 0x95, 0x8d,
    0x26, 0xdd, 0xea, 0x38, 0xa6, 0x92, 0x80, 0x48,
};
static const uint8_t k_expected_sig_n2_sha3[32] = {
    0x76, 0xda, 0x0b, 0x63, 0x5a, 0x2f, 0x27, 0x6d,
    0x22, 0xc1, 0x4c, 0xcc, 0x3e, 0x59, 0x89, 0x90,
    0x1c, 0x64, 0xab, 0x24, 0x76, 0x34, 0x77, 0x71,
    0xd8, 0x23, 0xce, 0xcd, 0x81, 0x46, 0x80, 0x99,
};
static const uint8_t k_expected_header_n4_sha3[32] = {
    0x7b, 0xdb, 0x34, 0x03, 0xb6, 0x99, 0x7a, 0x24,
    0x7b, 0xc2, 0x94, 0xb2, 0x7e, 0x4b, 0x35, 0x32,
    0xec, 0x51, 0x4e, 0xf8, 0x85, 0x76, 0x3f, 0x3b,
    0x70, 0x95, 0xcb, 0x76, 0xcb, 0x84, 0x20, 0x36,
};

/* -------------------------------------------------------------------------
 * Helpers.
 * ---------------------------------------------------------------------- */

static bool s_dump_mode(void)
{
    const char *e = getenv("CHIPMUNK_MRING_KAT_DUMP");
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
 * T5. Header wire layout pinning.
 * ---------------------------------------------------------------------- */

static bool s_test_header_layout(void)
{
    chipmunk_mring_header_t hdr = {
        .magic      = CHIPMUNK_MRING_MAGIC,
        .version    = CHIPMUNK_MRING_VERSION,
        .params_id  = CHIPMUNK_MRING_PARAMS_ID,
        .n_ring     = 4u,
        .threshold  = 2u,
        .fold_depth = chipmunk_mring_fold_depth_for(4u),
        .flags      = CHIPMUNK_MRING_FLAGS_DEFAULT,
    };
    /* header_read validates buf_size >= wire_size, so allocate full buffer. */
    const uint32_t wire = chipmunk_mring_wire_size(hdr.fold_depth);
    uint8_t *buf = DAP_NEW_Z_SIZE(uint8_t, wire);
    dap_assert(buf != NULL, "header test buf alloc");

    chipmunk_mring_header_write(buf, &hdr);

    /* Magic at offset 0: 'MRNG' LE = 4d 52 4e 47. */
    dap_assert(buf[0] == 0x4d && buf[1] == 0x52 &&
               buf[2] == 0x4e && buf[3] == 0x47,
               "header magic 'MRNG' LE");

    /* Version at offset 4: 1 LE = 01 00 00 00. */
    dap_assert(buf[4] == 0x01 && buf[5] == 0x00 &&
               buf[6] == 0x00 && buf[7] == 0x00,
               "header version 1 LE");

    /* Params at offset 8: 'MRV1' LE = 52 52 56 31. */
    dap_assert(buf[8] == 0x52 && buf[9] == 0x52 &&
               buf[10] == 0x56 && buf[11] == 0x31,
               "header params 'MRV1' LE");

    /* N=4 at offset 12. */
    dap_assert(buf[12] == 0x04 && buf[13] == 0x00 &&
               buf[14] == 0x00 && buf[15] == 0x00,
               "header N=4 LE");

    /* t=2 at offset 16. */
    dap_assert(buf[16] == 0x02 && buf[17] == 0x00 &&
               buf[18] == 0x00 && buf[19] == 0x00,
               "header t=2 LE");

    /* fold_depth=3 at offset 20. */
    dap_assert(buf[20] == 0x03 && buf[21] == 0x00 &&
               buf[22] == 0x00 && buf[23] == 0x00,
               "header fold_depth=3 LE");

    /* flags at offset 24: default linkable = 1. */
    dap_assert(buf[24] == 0x01 && buf[25] == 0x00 &&
               buf[26] == 0x00 && buf[27] == 0x00,
               "header flags=LINKABLE LE");

    /* Pinned SHA3-256 of full header bytes. */
    uint8_t h[32];
    s_sha3_256(buf, wire, h);
    bool ok = s_check_or_dump("k_expected_header_n4_sha3", h,
                              k_expected_header_n4_sha3);

    /* Roundtrip: read back and compare. */
    chipmunk_mring_header_t hdr2;
    chipmunk_ring_error_t rc = chipmunk_mring_header_read(&hdr2, buf, wire);
    dap_assert(rc == CHIPMUNK_RING_OK, "header_read roundtrip OK");
    dap_assert(hdr2.magic == hdr.magic, "roundtrip magic");
    dap_assert(hdr2.version == hdr.version, "roundtrip version");
    dap_assert(hdr2.params_id == hdr.params_id, "roundtrip params_id");
    dap_assert(hdr2.n_ring == hdr.n_ring, "roundtrip n_ring");
    dap_assert(hdr2.threshold == hdr.threshold, "roundtrip threshold");
    dap_assert(hdr2.fold_depth == hdr.fold_depth, "roundtrip fold_depth");
    dap_assert(hdr2.flags == hdr.flags, "roundtrip flags");

    /* Negative: bad magic. */
    buf[0] ^= 0xff;
    rc = chipmunk_mring_header_read(&hdr2, buf, wire);
    dap_assert(rc == CHIPMUNK_RING_ERR_MAGIC_MISMATCH, "bad magic rejected");
    buf[0] ^= 0xff;

    /* Negative: non-zero reserved flags. */
    buf[25] = 0x01;
    rc = chipmunk_mring_header_read(&hdr2, buf, wire);
    dap_assert(rc == CHIPMUNK_RING_ERR_PARAMS_MISMATCH, "reserved flags rejected");
    buf[25] = 0x00;

    DAP_DELETE(buf);
    return ok;
}

/* -------------------------------------------------------------------------
 * T6. Section-offset consistency.
 * ---------------------------------------------------------------------- */

static bool s_test_section_offsets(void)
{
    /* fold_depth_for boundary checks. */
    dap_assert(chipmunk_mring_fold_depth_for(0u) == 0u, "fold_depth N=0 invalid");
    dap_assert(chipmunk_mring_fold_depth_for(1u) == 0u, "fold_depth N=1 invalid");
    dap_assert(chipmunk_mring_fold_depth_for(2u) == 2u, "fold_depth N=2");
    dap_assert(chipmunk_mring_fold_depth_for(4u) == 3u, "fold_depth N=4");
    dap_assert(chipmunk_mring_fold_depth_for(16u) == 5u, "fold_depth N=16");
    dap_assert(chipmunk_mring_fold_depth_for(64u) == 7u, "fold_depth N=64");
    dap_assert(chipmunk_mring_fold_depth_for(256u) == 9u, "fold_depth N=256");
    dap_assert(chipmunk_mring_fold_depth_for(257u) == 0u, "fold_depth N=257 invalid");

    /* Offsets must be strictly increasing for any valid fold_depth. */
    for (uint32_t d = 2u; d <= 9u; ++d) {
        uint32_t off_T    = chipmunk_mring_section_off_T();
        uint32_t off_cb   = chipmunk_mring_section_off_cb();
        uint32_t off_ypk  = chipmunk_mring_section_off_ypk();
        uint32_t off_fos  = chipmunk_mring_section_off_fold_opening_seed();
        uint32_t off_fold = chipmunk_mring_section_off_fold();
        uint32_t off_fin  = chipmunk_mring_section_off_final(d);
        uint32_t off_lm   = chipmunk_mring_section_off_leaf_mask(d);
        uint32_t off_bind = chipmunk_mring_section_off_bind(d);
        uint32_t wire     = chipmunk_mring_wire_size(d);

        dap_assert(off_T < off_cb, "T < cb");
        dap_assert(off_cb < off_ypk, "cb < ypk");
        dap_assert(off_ypk < off_fos, "ypk < fold_opening_seed");
        dap_assert(off_fos < off_fold, "fold_opening_seed < fold");
        dap_assert(off_fold < off_fin, "fold < final");
        dap_assert(off_fin < off_lm, "final < leaf_mask");
        dap_assert(off_lm < off_bind, "leaf_mask < bind");
        dap_assert(off_bind + CHIPMUNK_MRING_BIND_BYTES == wire,
                   "bind + BIND_BYTES == wire_size");
    }

    /* Wire size formula cross-check for N=4, depth=3. */
    const uint32_t d4 = chipmunk_mring_fold_depth_for(4u);
    const uint32_t w4 = chipmunk_mring_wire_size(d4);
    dap_assert(w4 == 33532u + d4 * 16896u,
               "wire_size formula for N=4");

    return true;
}

/* -------------------------------------------------------------------------
 * T1+T2. Deterministic N=4, t=2 sign + pinned SHA3.
 * ---------------------------------------------------------------------- */

static bool s_test_sign_n4_deterministic(void)
{
    enum { N = 4, T = 2 };
    chipmunk_lrs_public_key_t ring[N];
    chipmunk_lrs_secret_key_t sks[T];

    for (uint32_t i = 0u; i < N; ++i) {
        chipmunk_lrs_secret_key_t tmp;
        s_make_keypair(&ring[i], &tmp, (uint8_t)(0x10u + i));
    }
    s_make_keypair(&ring[0], &sks[0], k_signer_salt_0);
    s_make_keypair(&ring[2], &sks[1], k_signer_salt_1);

    const chipmunk_lrs_secret_key_t *signer_ptrs[T] = {
        &sks[0], &sks[1],
    };

    uint8_t seeds[T * CHIPMUNK_LRS_SEED_BYTES];
    s_fill_seed(seeds, sizeof(seeds), k_randomness_seed_fill);

    uint8_t *sig1 = NULL, *sig2 = NULL;
    size_t sig1_sz = 0u, sig2_sz = 0u;

    chipmunk_ring_error_t rc1 = chipmunk_ring_sign_to_bytes(
        &sig1, &sig1_sz, signer_ptrs, T, ring, N, T,
        k_msg_n4, sizeof(k_msg_n4) - 1u, NULL, 0u, seeds);
    dap_assert(rc1 == CHIPMUNK_RING_OK, "N=4 sign 1 OK");
    dap_assert(sig1 != NULL && sig1_sz > 0u, "N=4 sig1 allocated");

    chipmunk_ring_error_t rc2 = chipmunk_ring_sign_to_bytes(
        &sig2, &sig2_sz, signer_ptrs, T, ring, N, T,
        k_msg_n4, sizeof(k_msg_n4) - 1u, NULL, 0u, seeds);
    dap_assert(rc2 == CHIPMUNK_RING_OK, "N=4 sign 2 OK");

    /* T1: deterministic — byte-identical. */
    dap_assert(sig1_sz == sig2_sz, "N=4 sig sizes equal");
    dap_assert(memcmp(sig1, sig2, sig1_sz) == 0,
               "N=4 deterministic sign byte-identical");
    DAP_DELETE(sig2);

    /* Wire size pinned. */
    const uint32_t l_depth = chipmunk_mring_fold_depth_for(N);
    dap_assert(sig1_sz == (size_t)chipmunk_mring_wire_size(l_depth),
               "N=4 wire size pinned");

    /* T2: pinned SHA3-256 of canonical signature. */
    uint8_t h[32];
    s_sha3_256(sig1, sig1_sz, h);
    bool ok = s_check_or_dump("k_expected_sig_n4_sha3", h,
                              k_expected_sig_n4_sha3);

    /* T4: verify round-trip. */
    rc1 = chipmunk_ring_verify_from_bytes(
        sig1, sig1_sz, ring, N, k_msg_n4, sizeof(k_msg_n4) - 1u, NULL, 0u);
    dap_assert(rc1 == CHIPMUNK_RING_OK, "N=4 verify honest");

    /* Negative: tamper T block. */
    const uint32_t off_T = chipmunk_mring_section_off_T();
    sig1[off_T] ^= 1u;
    rc1 = chipmunk_ring_verify_from_bytes(
        sig1, sig1_sz, ring, N, k_msg_n4, sizeof(k_msg_n4) - 1u, NULL, 0u);
    dap_assert(rc1 != CHIPMUNK_RING_OK, "N=4 tampered T fails");
    sig1[off_T] ^= 1u;

    /* Negative: wrong message. */
    const uint8_t bad_msg[] = "wrong-message";
    rc1 = chipmunk_ring_verify_from_bytes(
        sig1, sig1_sz, ring, N, bad_msg, sizeof(bad_msg) - 1u, NULL, 0u);
    dap_assert(rc1 != CHIPMUNK_RING_OK, "N=4 wrong message fails");

    /* Negative: tamper bind block (z_x). */
    const uint32_t off_bind = chipmunk_mring_section_off_bind(l_depth);
    sig1[off_bind] ^= 0x01u;
    rc1 = chipmunk_ring_verify_from_bytes(
        sig1, sig1_sz, ring, N, k_msg_n4, sizeof(k_msg_n4) - 1u, NULL, 0u);
    dap_assert(rc1 != CHIPMUNK_RING_OK, "N=4 tampered bind fails");
    sig1[off_bind] ^= 0x01u;

    /* Negative: tamper c* in bind block. */
    const uint32_t off_cstar = off_bind +
        CHIPMUNK_MRING_K_PK * CHIPMUNK_MRING_POLY_ZPACK;
    sig1[off_cstar] ^= 0x01u;
    rc1 = chipmunk_ring_verify_from_bytes(
        sig1, sig1_sz, ring, N, k_msg_n4, sizeof(k_msg_n4) - 1u, NULL, 0u);
    dap_assert(rc1 != CHIPMUNK_RING_OK, "N=4 tampered c* fails");
    sig1[off_cstar] ^= 0x01u;

    /* Untampered must still verify. */
    rc1 = chipmunk_ring_verify_from_bytes(
        sig1, sig1_sz, ring, N, k_msg_n4, sizeof(k_msg_n4) - 1u, NULL, 0u);
    dap_assert(rc1 == CHIPMUNK_RING_OK, "N=4 untampered recovers");

    DAP_DELETE(sig1);
    return ok;
}

/* -------------------------------------------------------------------------
 * T3. N=2, t=1 sign + pinned SHA3.
 * ---------------------------------------------------------------------- */

static bool s_test_sign_n2_deterministic(void)
{
    enum { N = 2, T = 1 };
    chipmunk_lrs_public_key_t ring[N];
    chipmunk_lrs_secret_key_t sk;

    for (uint32_t i = 0u; i < N; ++i) {
        chipmunk_lrs_secret_key_t tmp;
        s_make_keypair(&ring[i], &tmp, (uint8_t)(0x20u + i));
    }
    s_make_keypair(&ring[0], &sk, k_signer_salt_2);

    const chipmunk_lrs_secret_key_t *signer_ptrs[T] = { &sk };

    uint8_t seeds[T * CHIPMUNK_LRS_SEED_BYTES];
    s_fill_seed(seeds, sizeof(seeds), 0xBBu);

    uint8_t *sig = NULL;
    size_t sig_sz = 0u;

    chipmunk_ring_error_t rc = chipmunk_ring_sign_to_bytes(
        &sig, &sig_sz, signer_ptrs, T, ring, N, T,
        k_msg_n2, sizeof(k_msg_n2) - 1u, NULL, 0u, seeds);
    dap_assert(rc == CHIPMUNK_RING_OK, "N=2 sign OK");
    dap_assert(sig != NULL && sig_sz > 0u, "N=2 sig allocated");

    const uint32_t l_depth = chipmunk_mring_fold_depth_for(N);
    dap_assert(sig_sz == (size_t)chipmunk_mring_wire_size(l_depth),
               "N=2 wire size pinned");

    uint8_t h[32];
    s_sha3_256(sig, sig_sz, h);
    bool ok = s_check_or_dump("k_expected_sig_n2_sha3", h,
                              k_expected_sig_n2_sha3);

    rc = chipmunk_ring_verify_from_bytes(
        sig, sig_sz, ring, N, k_msg_n2, sizeof(k_msg_n2) - 1u, NULL, 0u);
    dap_assert(rc == CHIPMUNK_RING_OK, "N=2 verify honest");

    DAP_DELETE(sig);
    return ok;
}

/* -------------------------------------------------------------------------
 * T4 extra: wrong ring (substituted member) must fail.
 * ---------------------------------------------------------------------- */

static bool s_test_wrong_ring_rejection(void)
{
    enum { N = 4, T = 2 };
    chipmunk_lrs_public_key_t ring[N];
    chipmunk_lrs_secret_key_t sks[T];

    for (uint32_t i = 0u; i < N; ++i) {
        chipmunk_lrs_secret_key_t tmp;
        s_make_keypair(&ring[i], &tmp, (uint8_t)(0x10u + i));
    }
    s_make_keypair(&ring[0], &sks[0], k_signer_salt_0);
    s_make_keypair(&ring[2], &sks[1], k_signer_salt_1);

    const chipmunk_lrs_secret_key_t *signer_ptrs[T] = {
        &sks[0], &sks[1],
    };

    uint8_t seeds[T * CHIPMUNK_LRS_SEED_BYTES];
    s_fill_seed(seeds, sizeof(seeds), k_randomness_seed_fill);

    uint8_t *sig = NULL;
    size_t sig_sz = 0u;

    chipmunk_ring_error_t rc = chipmunk_ring_sign_to_bytes(
        &sig, &sig_sz, signer_ptrs, T, ring, N, T,
        k_msg_n4, sizeof(k_msg_n4) - 1u, NULL, 0u, seeds);
    dap_assert(rc == CHIPMUNK_RING_OK, "wrong_ring sign OK");

    /* Substitute one ring member. */
    chipmunk_lrs_public_key_t bad_ring[N];
    memcpy(bad_ring, ring, sizeof(ring));
    chipmunk_lrs_secret_key_t tmp_sk;
    s_make_keypair(&bad_ring[1], &tmp_sk, 0xFFu);

    rc = chipmunk_ring_verify_from_bytes(
        sig, sig_sz, bad_ring, N, k_msg_n4, sizeof(k_msg_n4) - 1u, NULL, 0u);
    dap_assert(rc != CHIPMUNK_RING_OK, "wrong ring member fails");

    /* Wrong ring size. */
    rc = chipmunk_ring_verify_from_bytes(
        sig, sig_sz, ring, N - 1u, k_msg_n4, sizeof(k_msg_n4) - 1u, NULL, 0u);
    dap_assert(rc != CHIPMUNK_RING_OK, "wrong ring size fails");

    DAP_DELETE(sig);
    return true;
}

/* -------------------------------------------------------------------------
 * T4 extra: wrong threshold must fail at sign time.
 * ---------------------------------------------------------------------- */

static bool s_test_threshold_gates(void)
{
    enum { N = 4 };
    chipmunk_lrs_public_key_t ring[N];
    chipmunk_lrs_secret_key_t sk;

    for (uint32_t i = 0u; i < N; ++i) {
        chipmunk_lrs_secret_key_t tmp;
        s_make_keypair(&ring[i], &tmp, (uint8_t)(0x10u + i));
    }
    s_make_keypair(&ring[0], &sk, k_signer_salt_0);

    const chipmunk_lrs_secret_key_t *signer_ptrs[1] = { &sk };
    uint8_t seeds[1 * CHIPMUNK_LRS_SEED_BYTES];
    s_fill_seed(seeds, sizeof(seeds), 0xCCu);

    uint8_t *sig = NULL;
    size_t sig_sz = 0u;

    /* signer_count != threshold → must fail. */
    chipmunk_ring_error_t rc = chipmunk_ring_sign_to_bytes(
        &sig, &sig_sz, signer_ptrs, 1u, ring, N, 2u,
        k_msg_n4, sizeof(k_msg_n4) - 1u, NULL, 0u, seeds);
    dap_assert(rc != CHIPMUNK_RING_OK, "signer_count != threshold fails");

    /* threshold > ring_size → must fail. */
    rc = chipmunk_ring_sign_to_bytes(
        &sig, &sig_sz, signer_ptrs, 1u, ring, N, N + 1u,
        k_msg_n4, sizeof(k_msg_n4) - 1u, NULL, 0u, seeds);
    dap_assert(rc != CHIPMUNK_RING_OK, "threshold > ring_size fails");

    /* ring_size < N_MIN → must fail. */
    rc = chipmunk_ring_sign_to_bytes(
        &sig, &sig_sz, signer_ptrs, 1u, ring, 1u, 1u,
        k_msg_n4, sizeof(k_msg_n4) - 1u, NULL, 0u, seeds);
    dap_assert(rc != CHIPMUNK_RING_OK, "ring_size < N_MIN fails");

    return true;
}

/* -------------------------------------------------------------------------
 * T4 extra: NULL parameter rejection.
 * ---------------------------------------------------------------------- */

static bool s_test_null_param_gates(void)
{
    chipmunk_ring_error_t rc;

    rc = chipmunk_ring_sign_to_bytes(
        NULL, NULL, NULL, 0u, NULL, 0u, 0u, NULL, 0u, NULL, 0u, NULL);
    dap_assert(rc == CHIPMUNK_RING_ERR_NULL_PARAM, "sign all-NULL");

    rc = chipmunk_ring_verify_from_bytes(
        NULL, 0u, NULL, 0u, NULL, 0u, NULL, 0u);
    dap_assert(rc == CHIPMUNK_RING_ERR_NULL_PARAM, "verify all-NULL");

    return true;
}

/* -------------------------------------------------------------------------
 * Error string coverage.
 * ---------------------------------------------------------------------- */

static bool s_test_strerror_coverage(void)
{
    /* Every defined error code must return a non-NULL, non-empty string. */
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
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
        const char *s = chipmunk_ring_strerror(codes[i]);
        dap_assert(s != NULL && s[0] != '\0', "strerror non-empty");
    }
    /* Unknown code must not return NULL. */
    const char *s_unknown = chipmunk_ring_strerror((chipmunk_ring_error_t)-9999);
    dap_assert(s_unknown != NULL, "strerror unknown code non-NULL");

    return true;
}

/* -------------------------------------------------------------------------
 * Empty message + empty ctx sign/verify.
 * ---------------------------------------------------------------------- */

static bool s_test_empty_msg_ctx(void)
{
    enum { N = 4, T = 2 };
    chipmunk_lrs_public_key_t ring[N];
    chipmunk_lrs_secret_key_t sks[T];

    for (uint32_t i = 0u; i < N; ++i) {
        chipmunk_lrs_secret_key_t tmp;
        s_make_keypair(&ring[i], &tmp, (uint8_t)(0x30u + i));
    }
    s_make_keypair(&ring[0], &sks[0], 0xD1u);
    s_make_keypair(&ring[2], &sks[1], 0xD2u);

    const chipmunk_lrs_secret_key_t *ptrs[T] = { &sks[0], &sks[1] };
    uint8_t seeds[T * CHIPMUNK_LRS_SEED_BYTES];
    s_fill_seed(seeds, sizeof(seeds), 0xDDu);

    /* Empty message, NULL ctx. */
    uint8_t *sig = NULL;
    size_t sig_sz = 0u;
    chipmunk_ring_error_t rc = chipmunk_ring_sign_to_bytes(
        &sig, &sig_sz, ptrs, T, ring, N, T,
        NULL, 0u, NULL, 0u, seeds);
    dap_assert(rc == CHIPMUNK_RING_OK, "empty msg sign OK");

    rc = chipmunk_ring_verify_from_bytes(
        sig, sig_sz, ring, N, NULL, 0u, NULL, 0u);
    dap_assert(rc == CHIPMUNK_RING_OK, "empty msg verify OK");

    /* Non-empty message must fail against empty-msg signature. */
    const uint8_t nonempty[] = "not-empty";
    rc = chipmunk_ring_verify_from_bytes(
        sig, sig_sz, ring, N, nonempty, sizeof(nonempty) - 1u, NULL, 0u);
    dap_assert(rc != CHIPMUNK_RING_OK, "non-empty msg against empty sig fails");

    DAP_DELETE(sig);

    /* Empty message, non-empty ctx. */
    const uint8_t ctx[] = "some-context";
    sig = NULL;
    rc = chipmunk_ring_sign_to_bytes(
        &sig, &sig_sz, ptrs, T, ring, N, T,
        NULL, 0u, ctx, sizeof(ctx) - 1u, seeds);
    dap_assert(rc == CHIPMUNK_RING_OK, "empty msg + ctx sign OK");

    rc = chipmunk_ring_verify_from_bytes(
        sig, sig_sz, ring, N, NULL, 0u, ctx, sizeof(ctx) - 1u);
    dap_assert(rc == CHIPMUNK_RING_OK, "empty msg + ctx verify OK");

    DAP_DELETE(sig);
    return true;
}

/* -------------------------------------------------------------------------
 * Tamper fs_seed and fold_opening_seed on wire.
 * ---------------------------------------------------------------------- */

static bool s_test_wire_tamper_seeds(void)
{
    enum { N = 4, T = 2 };
    chipmunk_lrs_public_key_t ring[N];
    chipmunk_lrs_secret_key_t sks[T];

    for (uint32_t i = 0u; i < N; ++i) {
        chipmunk_lrs_secret_key_t tmp;
        s_make_keypair(&ring[i], &tmp, (uint8_t)(0x40u + i));
    }
    s_make_keypair(&ring[0], &sks[0], 0xE1u);
    s_make_keypair(&ring[2], &sks[1], 0xE2u);

    const chipmunk_lrs_secret_key_t *ptrs[T] = { &sks[0], &sks[1] };
    uint8_t seeds[T * CHIPMUNK_LRS_SEED_BYTES];
    s_fill_seed(seeds, sizeof(seeds), 0xEEu);

    const uint8_t msg[] = "tamper-seed-test";
    uint8_t *sig = NULL;
    size_t sig_sz = 0u;

    chipmunk_ring_error_t rc = chipmunk_ring_sign_to_bytes(
        &sig, &sig_sz, ptrs, T, ring, N, T,
        msg, sizeof(msg) - 1u, NULL, 0u, seeds);
    dap_assert(rc == CHIPMUNK_RING_OK, "tamper-seed sign OK");

    /* Tamper fs_seed (offset 96..127 in fixed hashes section). */
    const uint32_t off_hash = chipmunk_mring_section_off_fixed_hashes();
    sig[off_hash + 96u] ^= 0x01u;
    rc = chipmunk_ring_verify_from_bytes(
        sig, sig_sz, ring, N, msg, sizeof(msg) - 1u, NULL, 0u);
    dap_assert(rc != CHIPMUNK_RING_OK, "tampered fs_seed fails");
    sig[off_hash + 96u] ^= 0x01u;

    /* Tamper fold_opening_seed (section_off_fold_opening_seed). */
    const uint32_t off_fos = chipmunk_mring_section_off_fold_opening_seed();
    sig[off_fos] ^= 0x01u;
    rc = chipmunk_ring_verify_from_bytes(
        sig, sig_sz, ring, N, msg, sizeof(msg) - 1u, NULL, 0u);
    dap_assert(rc != CHIPMUNK_RING_OK, "tampered fold_opening_seed fails");
    sig[off_fos] ^= 0x01u;

    /* Untampered must still verify. */
    rc = chipmunk_ring_verify_from_bytes(
        sig, sig_sz, ring, N, msg, sizeof(msg) - 1u, NULL, 0u);
    dap_assert(rc == CHIPMUNK_RING_OK, "untampered recovers");

    DAP_DELETE(sig);
    return true;
}

/* -------------------------------------------------------------------------
 * main.
 * ---------------------------------------------------------------------- */

int main(void)
{
    dap_set_appname("test_chipmunk_mring_kat");
    dap_common_init("test_chipmunk_mring_kat", NULL);

    int rc = 0;
    if (!s_test_header_layout())          rc = 1;
    if (!s_test_section_offsets())        rc = 1;
    if (!s_test_sign_n4_deterministic())  rc = 1;
    if (!s_test_sign_n2_deterministic())  rc = 1;
    if (!s_test_wrong_ring_rejection())   rc = 1;
    if (!s_test_threshold_gates())        rc = 1;
    if (!s_test_null_param_gates())       rc = 1;
    if (!s_test_strerror_coverage())      rc = 1;
    if (!s_test_empty_msg_ctx())          rc = 1;
    if (!s_test_wire_tamper_seeds())      rc = 1;

    if (s_dump_mode()) {
        log_it(L_WARNING, "CHIPMUNK_MRING_KAT_DUMP active: dump is not a pass");
        rc = 1;
    } else if (rc == 0) {
        log_it(L_INFO, "=== ALL MRNG M7.1 KAT tests PASSED ===");
    }

    dap_common_deinit();
    return rc;
}
