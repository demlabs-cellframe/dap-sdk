/*
 * test_chipmunk_ring_kat.c — CR-11.B Reproducible Known-Answer Tests
 *
 * Pins the byte-for-byte behaviour of every deterministic ChipmunkRing
 * primitive that has stabilised on the feature branch:
 *
 *   - chipmunk_ht_keypair_from_seed          (deterministic from 32-byte seed)
 *   - chipmunk_ht_sign                       (deterministic from sk + msg)
 *   - chipmunk_ht_pop_message_derive         (deterministic from pk)
 *   - chipmunk_ring_pop_create / _verify     (deterministic from sk)
 *   - chipmunk_ring_domain_hash_internal     (deterministic from inputs)
 *   - chipmunk_ring_threshold_deal/combine   (round-trip; deal is random)
 *   - chipmunk_ring_key_new_generate         (deterministic from 32-byte seed)
 *
 * Pinning policy:
 *   Expected hashes / transcripts below are produced once on the feature
 *   branch with a fixed input set and then locked.  Any silent change to
 *   hypertree layout, PoP wire envelope, domain-separation construction,
 *   or Shamir/ring keygen will flip a vector and break this test, which
 *   is the entire point: CR-11.B exists so that a follow-up patch cannot
 *   redefine the wire format without an explicit, reviewed re-pinning.
 *
 *   Re-pinning workflow:
 *     1. set CHIPMUNK_RING_KAT_DUMP=1 in the env and run the test;
 *        the test prints every expected vector as a C initialiser and
 *        exits non-zero so it cannot be mistaken for a passing run;
 *     2. copy the printed initialisers into the constants below;
 *     3. re-run without the env var; the test must now PASS.
 *
 * See SLC documents:
 *   - documentation_a57a7626f6cb30b2  (CR-11 master design)
 *   - CR-11.B design slice in the same master (this file's anchor)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <dap_common.h>
#include <dap_test.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dap_enc_key.h"
#include "dap_enc_chipmunk_ring.h"
#include "dap_enc_chipmunk_ring_params.h"
#include "dap_chipmunk_ring_threshold.h"
#include "dap_hash_sha3.h"

#include "chipmunk/chipmunk.h"
#include "chipmunk/chipmunk_hypertree.h"
#include "chipmunk/chipmunk_ring.h"

/* Internal helper deliberately re-exposed for KAT only. */
extern int chipmunk_ring_domain_hash_internal(const char *a_domain,
                                              const void *a_salt, size_t a_salt_size,
                                              const void *a_input, size_t a_input_size,
                                              void *a_output, size_t a_output_size,
                                              uint32_t a_iterations);

#define LOG_TAG "test_chipmunk_ring_kat"

/* ------------------------------------------------------------------ *
 *  Fixed test inputs                                                 *
 * ------------------------------------------------------------------ */

/* Canonical 32-byte seed: 0x00, 0x01, ..., 0x1F.  Used for every
 * keypair below so the entire suite is reproducible from one constant. */
static const uint8_t k_seed_iota[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};

/* Canonical 32-byte master seed for Shamir KAT (distinct from k_seed_iota
 * so a misuse of one in place of the other shows up immediately). */
static const uint8_t k_master_seed[32] = {
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
};

/* Canonical message for ht_sign KAT.  ASCII so a hex-diff is grep-able. */
static const char k_message[] =
    "CR-11.B reproducible KAT message: ChipmunkRing 2026-05-18";
#define K_MESSAGE_LEN  (sizeof(k_message) - 1u)

/* Canonical input for the domain-hash KAT.  Uses an arbitrary but stable
 * salt + input pair so the helper's TupleHash-style length prefixing is
 * exercised non-trivially. */
static const char k_dh_domain[] = "chipmunk-ring-kat/v2";
static const uint8_t k_dh_salt[16] = {
    0x73, 0x61, 0x6c, 0x74, 0x2d, 0x66, 0x6f, 0x72,
    0x2d, 0x63, 0x72, 0x31, 0x31, 0x62, 0x21, 0x21,  /* "salt-for-cr11b!!" */
};
static const uint8_t k_dh_input[24] = {
    0x69, 0x6e, 0x70, 0x75, 0x74, 0x2d, 0x66, 0x6f,
    0x72, 0x2d, 0x63, 0x72, 0x31, 0x31, 0x62, 0x2d,
    0x6b, 0x61, 0x74, 0x21, 0x21, 0x21, 0x21, 0x21,  /* "input-for-cr11b-kat!!!!!" */
};

/* ------------------------------------------------------------------ *
 *  Pinned expected vectors (CR-11.B publication anchor)              *
 * ------------------------------------------------------------------ *
 *
 *  All zeros = "not yet pinned".  In CHIPMUNK_RING_KAT_DUMP mode the
 *  test prints the actual vectors as ready-to-paste C initialisers and
 *  exits non-zero so the dump cannot masquerade as a passing run.
 */

static const uint8_t k_expected_ht_pk_sha3_256[32] = {
    0xe7, 0xcc, 0x26, 0x70, 0xdc, 0x21, 0xd8, 0xb0,
    0x1f, 0x64, 0xf5, 0x19, 0x80, 0x3c, 0x85, 0x3c,
    0xe4, 0xb5, 0x95, 0x9f, 0x2a, 0x51, 0xb9, 0x64,
    0x7c, 0xbe, 0xb0, 0x1f, 0x66, 0x3a, 0xfd, 0x42,
};

static const uint8_t k_expected_ht_sig_sha3_256[32] = {
    0xcd, 0xe8, 0xad, 0xa6, 0xb4, 0x50, 0xaa, 0x64,
    0x0c, 0x79, 0xc5, 0x7e, 0xa5, 0x9f, 0x5b, 0x5c,
    0x81, 0x62, 0x64, 0xa0, 0x09, 0xcc, 0x23, 0xff,
    0x31, 0x69, 0x63, 0xec, 0x02, 0x09, 0x00, 0xb2,
};

static const uint8_t k_expected_pop_msg[32] = {
    0xe0, 0xf7, 0x26, 0xc4, 0x71, 0x1b, 0x93, 0xc3,
    0xc6, 0x52, 0x27, 0x5c, 0x9e, 0xa2, 0x38, 0x82,
    0x1d, 0x56, 0xe3, 0x75, 0xad, 0xb4, 0x33, 0x36,
    0xd6, 0x62, 0x4f, 0x5b, 0x5d, 0xc7, 0x45, 0xd9,
};

static const uint8_t k_expected_pop_blob_sha3_256[32] = {
    0xb4, 0x0d, 0x3d, 0xdb, 0x22, 0xca, 0x0d, 0xcb,
    0xb7, 0xf5, 0xb3, 0x4c, 0x8f, 0xe6, 0x97, 0x51,
    0x99, 0x25, 0x56, 0xd6, 0xfe, 0x17, 0x61, 0xa0,
    0xdd, 0xb7, 0x52, 0x40, 0xab, 0x6c, 0x03, 0x83,
};

static const uint8_t k_expected_dh_output[32] = {
    0x5f, 0xeb, 0xfe, 0x38, 0x07, 0x46, 0x09, 0x5b,
    0xb9, 0xec, 0xef, 0xd3, 0x97, 0xe0, 0x17, 0xdb,
    0x35, 0xb8, 0x13, 0xae, 0x7a, 0x60, 0xad, 0xa7,
    0x83, 0x84, 0x29, 0xf0, 0x97, 0xd2, 0x24, 0x81,
};

static const uint8_t k_expected_ring_pk_sha3_256[32] = {
    0xe7, 0xcc, 0x26, 0x70, 0xdc, 0x21, 0xd8, 0xb0,
    0x1f, 0x64, 0xf5, 0x19, 0x80, 0x3c, 0x85, 0x3c,
    0xe4, 0xb5, 0x95, 0x9f, 0x2a, 0x51, 0xb9, 0x64,
    0x7c, 0xbe, 0xb0, 0x1f, 0x66, 0x3a, 0xfd, 0x42,
};

/* ------------------------------------------------------------------ *
 *  Helpers                                                           *
 * ------------------------------------------------------------------ */

static bool s_dump_mode(void)
{
    const char *e = getenv("CHIPMUNK_RING_KAT_DUMP");
    return e && *e && *e != '0';
}

static bool s_all_zero(const uint8_t *a_buf, size_t a_size)
{
    for (size_t i = 0; i < a_size; ++i) {
        if (a_buf[i] != 0) return false;
    }
    return true;
}

static void s_print_vector(const char *a_name, const uint8_t *a_buf, size_t a_size)
{
    fprintf(stderr, "/* CR-11.B KAT — %s (%zu bytes) */\n", a_name, a_size);
    fprintf(stderr, "static const uint8_t %s[%zu] = {", a_name, a_size);
    for (size_t i = 0; i < a_size; ++i) {
        if (i % 8u == 0u) fprintf(stderr, "\n   ");
        fprintf(stderr, " 0x%02x,", a_buf[i]);
    }
    fprintf(stderr, "\n};\n");
}

static bool s_check_or_dump(const char *a_label,
                            const uint8_t *a_actual, size_t a_size,
                            const uint8_t *a_expected)
{
    if (s_dump_mode() || s_all_zero(a_expected, a_size)) {
        s_print_vector(a_label, a_actual, a_size);
        return false;
    }
    if (memcmp(a_actual, a_expected, a_size) != 0) {
        log_it(L_ERROR, "KAT mismatch on '%s'", a_label);
        s_print_vector("actual", a_actual, a_size);
        s_print_vector("expected", a_expected, a_size);
        return false;
    }
    return true;
}

static void s_sha3_256(const void *a_in, size_t a_in_size, uint8_t a_out[32])
{
    dap_hash_sha3_256_t l_h;
    bool ok = dap_hash_sha3_256(a_in, a_in_size, &l_h);
    dap_assert(ok, "dap_hash_sha3_256 OK");
    memcpy(a_out, &l_h, 32);
}

/* ------------------------------------------------------------------ *
 *  Test 1 — Hypertree keypair-from-seed                              *
 * ------------------------------------------------------------------ */

static bool s_test_ht_keypair_kat(void)
{
    chipmunk_ht_public_key_t  l_pk;
    chipmunk_ht_private_key_t l_sk;
    memset(&l_pk, 0, sizeof(l_pk));
    memset(&l_sk, 0, sizeof(l_sk));

    int rc = chipmunk_ht_keypair_from_seed(k_seed_iota, &l_pk, &l_sk);
    dap_assert(rc == 0, "ht_keypair_from_seed OK");

    uint8_t *l_pk_bytes = DAP_NEW_Z_SIZE(uint8_t, CHIPMUNK_HT_PUBLIC_KEY_SIZE);
    dap_assert(l_pk_bytes != NULL, "pk bytes alloc");

    rc = chipmunk_ht_public_key_to_bytes(l_pk_bytes, &l_pk);
    dap_assert(rc == 0, "ht_public_key_to_bytes OK");

    uint8_t l_actual[32];
    s_sha3_256(l_pk_bytes, CHIPMUNK_HT_PUBLIC_KEY_SIZE, l_actual);

    bool ok = s_check_or_dump("k_expected_ht_pk_sha3_256",
                              l_actual, sizeof(l_actual),
                              k_expected_ht_pk_sha3_256);

    DAP_DELETE(l_pk_bytes);
    chipmunk_ht_private_key_clear(&l_sk);
    return ok;
}

/* ------------------------------------------------------------------ *
 *  Test 2 — Hypertree sign (deterministic HOTS)                      *
 * ------------------------------------------------------------------ */

static bool s_test_ht_sign_kat(void)
{
    chipmunk_ht_public_key_t  l_pk;
    chipmunk_ht_private_key_t l_sk;
    memset(&l_pk, 0, sizeof(l_pk));
    memset(&l_sk, 0, sizeof(l_sk));

    int rc = chipmunk_ht_keypair_from_seed(k_seed_iota, &l_pk, &l_sk);
    dap_assert(rc == 0, "ht_keypair_from_seed OK");

    chipmunk_ht_signature_t l_sig;
    memset(&l_sig, 0, sizeof(l_sig));

    rc = chipmunk_ht_sign(&l_sk, (const uint8_t *)k_message, K_MESSAGE_LEN, &l_sig);
    dap_assert(rc == 0, "ht_sign OK");
    dap_assert(l_sig.leaf_index == 0, "first signature lands at leaf 0");

    uint8_t *l_sig_bytes = DAP_NEW_Z_SIZE(uint8_t, CHIPMUNK_HT_SIGNATURE_SIZE);
    dap_assert(l_sig_bytes != NULL, "sig bytes alloc");

    rc = chipmunk_ht_signature_to_bytes(l_sig_bytes, &l_sig);
    dap_assert(rc == 0, "ht_signature_to_bytes OK");

    uint8_t l_actual[32];
    s_sha3_256(l_sig_bytes, CHIPMUNK_HT_SIGNATURE_SIZE, l_actual);

    bool ok = s_check_or_dump("k_expected_ht_sig_sha3_256",
                              l_actual, sizeof(l_actual),
                              k_expected_ht_sig_sha3_256);

    /* Cross-check: signature verifies under the matching pk. */
    rc = chipmunk_ht_verify(&l_pk, (const uint8_t *)k_message, K_MESSAGE_LEN, &l_sig);
    dap_assert(rc == 0, "ht_verify OK");

    DAP_DELETE(l_sig_bytes);
    chipmunk_ht_signature_clear(&l_sig);
    chipmunk_ht_private_key_clear(&l_sk);
    return ok;
}

/* ------------------------------------------------------------------ *
 *  Test 3 — PoP message transcript derivation                        *
 * ------------------------------------------------------------------ */

static bool s_test_pop_message_kat(void)
{
    chipmunk_ht_public_key_t  l_pk;
    chipmunk_ht_private_key_t l_sk;
    memset(&l_pk, 0, sizeof(l_pk));
    memset(&l_sk, 0, sizeof(l_sk));

    int rc = chipmunk_ht_keypair_from_seed(k_seed_iota, &l_pk, &l_sk);
    dap_assert(rc == 0, "ht_keypair_from_seed OK");

    uint8_t l_actual[32];
    rc = chipmunk_ht_pop_message_derive(&l_pk, l_actual);
    dap_assert(rc == 0, "ht_pop_message_derive OK");

    bool ok = s_check_or_dump("k_expected_pop_msg",
                              l_actual, sizeof(l_actual),
                              k_expected_pop_msg);

    chipmunk_ht_private_key_clear(&l_sk);
    return ok;
}

/* ------------------------------------------------------------------ *
 *  Test 4 — PoP create + verify                                      *
 * ------------------------------------------------------------------ */

static bool s_test_pop_create_kat(void)
{
    chipmunk_ht_public_key_t  l_pk;
    chipmunk_ht_private_key_t l_sk;
    memset(&l_pk, 0, sizeof(l_pk));
    memset(&l_sk, 0, sizeof(l_sk));

    int rc = chipmunk_ht_keypair_from_seed(k_seed_iota, &l_pk, &l_sk);
    dap_assert(rc == 0, "ht_keypair_from_seed OK");

    const size_t l_pop_bytes =
        CHIPMUNK_RING_POP_BYTES_FROM_HT_SIG(CHIPMUNK_HT_SIGNATURE_SIZE);
    uint8_t *l_pop = DAP_NEW_Z_SIZE(uint8_t, l_pop_bytes);
    dap_assert(l_pop != NULL, "pop alloc");

    rc = chipmunk_ring_pop_create(&l_sk, l_pop, l_pop_bytes);
    dap_assert(rc == 0, "ring_pop_create OK");

    rc = chipmunk_ring_pop_verify(&l_pk, l_pop, l_pop_bytes);
    dap_assert(rc == 0, "ring_pop_verify OK");

    /* CR-11.E preserves production budget — leaf_index must be 0. */
    dap_assert(l_sk.leaf_index == 0u,
               "PoP does not consume production leaves");

    uint8_t l_actual[32];
    s_sha3_256(l_pop, l_pop_bytes, l_actual);

    bool ok = s_check_or_dump("k_expected_pop_blob_sha3_256",
                              l_actual, sizeof(l_actual),
                              k_expected_pop_blob_sha3_256);

    DAP_DELETE(l_pop);
    chipmunk_ht_private_key_clear(&l_sk);
    return ok;
}

/* ------------------------------------------------------------------ *
 *  Test 5 — Domain hash construction                                 *
 * ------------------------------------------------------------------ */

static bool s_test_domain_hash_kat(void)
{
    uint8_t l_actual[32];
    int rc = chipmunk_ring_domain_hash_internal(k_dh_domain,
                                                k_dh_salt, sizeof(k_dh_salt),
                                                k_dh_input, sizeof(k_dh_input),
                                                l_actual, sizeof(l_actual),
                                                /* iterations */ 1u);
    dap_assert(rc == 0, "domain_hash_internal OK");

    return s_check_or_dump("k_expected_dh_output",
                           l_actual, sizeof(l_actual),
                           k_expected_dh_output);
}

/* ------------------------------------------------------------------ *
 *  Test 6 — Shamir deal/combine round-trip                           *
 * ------------------------------------------------------------------ */

static bool s_test_shamir_roundtrip(void)
{
    enum { N = 5, T = 3 };

    chipmunk_ring_threshold_share_t l_shares[N];
    memset(l_shares, 0, sizeof(l_shares));

    int rc = chipmunk_ring_threshold_deal(k_master_seed, N, T, l_shares);
    dap_assert(rc == 0, "threshold_deal OK");

    /* Combine first T shares — must recover the exact master seed. */
    uint8_t l_recovered[32];
    rc = chipmunk_ring_threshold_combine(l_shares, T, l_recovered);
    dap_assert(rc == 0, "threshold_combine OK");

    bool ok = (memcmp(l_recovered, k_master_seed, sizeof(k_master_seed)) == 0);
    if (!ok) {
        log_it(L_ERROR, "Shamir round-trip mismatch");
        s_print_vector("actual_master", l_recovered, sizeof(l_recovered));
        s_print_vector("expected_master", k_master_seed, sizeof(k_master_seed));
    }

    /* Combine a different T-subset — must also recover the master. */
    chipmunk_ring_threshold_share_t l_subset[T];
    l_subset[0] = l_shares[0];
    l_subset[1] = l_shares[2];
    l_subset[2] = l_shares[4];

    uint8_t l_recovered2[32];
    rc = chipmunk_ring_threshold_combine(l_subset, T, l_recovered2);
    dap_assert(rc == 0, "threshold_combine alt subset OK");
    if (memcmp(l_recovered2, k_master_seed, sizeof(k_master_seed)) != 0) {
        log_it(L_ERROR, "Shamir round-trip (alt subset) mismatch");
        ok = false;
    }

    for (size_t i = 0; i < N; ++i) {
        chipmunk_ring_threshold_share_wipe(&l_shares[i]);
    }
    return ok;
}

/* ------------------------------------------------------------------ *
 *  Test 7 — ChipmunkRing wrapper keypair (sanity vs HT KAT)          *
 * ------------------------------------------------------------------ */

static bool s_test_ring_key_new_generate_kat(void)
{
    dap_assert(dap_enc_chipmunk_ring_init() == 0, "dap_enc_chipmunk_ring_init OK");

    dap_enc_key_t *l_key = dap_enc_key_new_generate(
        DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING,
        /* kex_buf */ NULL, /* kex_size */ 0,
        /* seed */ k_seed_iota, /* seed_size */ sizeof(k_seed_iota),
        /* key_size */ 0);
    dap_assert(l_key != NULL, "ring key generate OK");
    dap_assert(l_key->pub_key_data_size == CHIPMUNK_RING_PUBLIC_KEY_SIZE,
               "ring pk size matches HT pk size");

    uint8_t l_actual[32];
    s_sha3_256(l_key->pub_key_data, l_key->pub_key_data_size, l_actual);

    bool ok = s_check_or_dump("k_expected_ring_pk_sha3_256",
                              l_actual, sizeof(l_actual),
                              k_expected_ring_pk_sha3_256);

    dap_enc_key_delete(l_key);
    return ok;
}

/* ------------------------------------------------------------------ *
 *  Main                                                              *
 * ------------------------------------------------------------------ */

int main(void)
{
    dap_set_appname("test_chipmunk_ring_kat");
    dap_common_init("test_chipmunk_ring_kat", NULL);

    int l_rc = 0;
    bool l_dump = s_dump_mode();

    if (l_dump) {
        log_it(L_WARNING,
               "CHIPMUNK_RING_KAT_DUMP=1: printing actual vectors, "
               "test will exit non-zero by design");
    }

    if (!s_test_ht_keypair_kat())               l_rc = 1;
    if (!s_test_ht_sign_kat())                  l_rc = 1;
    if (!s_test_pop_message_kat())              l_rc = 1;
    if (!s_test_pop_create_kat())               l_rc = 1;
    if (!s_test_domain_hash_kat())              l_rc = 1;
    if (!s_test_shamir_roundtrip())             l_rc = 1;
    if (!s_test_ring_key_new_generate_kat())    l_rc = 1;

    if (l_rc == 0) {
        log_it(L_INFO, "CR-11.B KAT — all vectors PASS");
    } else if (l_dump) {
        log_it(L_WARNING, "CR-11.B KAT — dump complete; not a pass");
    } else {
        log_it(L_ERROR, "CR-11.B KAT — vector mismatch");
    }

    dap_common_deinit();
    return l_rc;
}
