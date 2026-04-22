/*
 * CR-D15.B regression test: Chipmunk Hypertree API.
 *
 * Pins down the following invariants for the hypertree construction:
 *
 *   1. keypair_from_seed is deterministic — two calls with identical
 *      seeds produce identical (rho_seed, hasher_seed, root) pk bytes.
 *   2. Every leaf_index in [0, CHIPMUNK_HT_LEAF_COUNT) produces a
 *      verifying signature that binds to exactly that index.
 *   3. Exhausting the tree returns CHIPMUNK_ERROR_KEY_EXHAUSTED and
 *      does not further advance the counter.
 *   4. Tamper detection:
 *        (a) replacing leaf_pk in a valid signature must break verify,
 *        (b) swapping two signatures' auth paths must break verify,
 *        (c) bumping a HOTS sigma coefficient must break verify,
 *        (d) verifying a signature under a wrong root (different keypair)
 *            must break verify.
 *   5. Serialise → deserialise round-trip preserves all signature
 *      fields and the deserialised signature verifies identically.
 *   6. Private-key serialise → deserialise round-trip rebuilds the
 *      same tree and can continue signing from the persisted leaf_index.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "dap_common.h"
#include "chipmunk/chipmunk.h"
#include "chipmunk/chipmunk_hypertree.h"

#define MSG_PREFIX "cr-d15.b probe leaf_index="

static int s_fails = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); ++s_fails; } \
    else         { fprintf(stderr, "OK:   %s\n", msg); } \
} while (0)

static void s_make_msg(char *a_out, size_t a_cap, uint32_t a_idx)
{
    snprintf(a_out, a_cap, "%s%u", MSG_PREFIX, (unsigned)a_idx);
}

/* ---------------------------------------------------------------------- *
 *  1. Deterministic keygen                                               *
 * ---------------------------------------------------------------------- */

static void s_test_deterministic_keygen(void)
{
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = (uint8_t)(0x11u + i);

    chipmunk_ht_public_key_t  l_pk_a; memset(&l_pk_a, 0, sizeof(l_pk_a));
    chipmunk_ht_public_key_t  l_pk_b; memset(&l_pk_b, 0, sizeof(l_pk_b));
    chipmunk_ht_private_key_t l_sk_a; memset(&l_sk_a, 0, sizeof(l_sk_a));
    chipmunk_ht_private_key_t l_sk_b; memset(&l_sk_b, 0, sizeof(l_sk_b));

    CHECK(chipmunk_ht_keypair_from_seed(l_seed, &l_pk_a, &l_sk_a) == CHIPMUNK_ERROR_SUCCESS,
          "keypair_from_seed (A) succeeds");
    CHECK(chipmunk_ht_keypair_from_seed(l_seed, &l_pk_b, &l_sk_b) == CHIPMUNK_ERROR_SUCCESS,
          "keypair_from_seed (B) succeeds");

    CHECK(memcmp(l_pk_a.rho_seed,    l_pk_b.rho_seed,    32) == 0,
          "rho_seed deterministic across calls");
    CHECK(memcmp(l_pk_a.hasher_seed, l_pk_b.hasher_seed, 32) == 0,
          "hasher_seed deterministic across calls");
    CHECK(memcmp(&l_pk_a.root, &l_pk_b.root, sizeof(l_pk_a.root)) == 0,
          "Merkle root deterministic across calls");
    CHECK(l_sk_a.leaf_index == 0u && l_sk_b.leaf_index == 0u,
          "fresh hypertree sk carries leaf_index == 0");

    chipmunk_ht_private_key_clear(&l_sk_a);
    chipmunk_ht_private_key_clear(&l_sk_b);
}

/* ---------------------------------------------------------------------- *
 *  2. Every leaf signs and verifies                                      *
 * ---------------------------------------------------------------------- */

static chipmunk_ht_signature_t *s_all_sigs = NULL;

static void s_test_all_leaves_sign_verify(chipmunk_ht_public_key_t *a_pk_out,
                                          chipmunk_ht_private_key_t *a_sk_out)
{
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = (uint8_t)(0x22u + i);

    CHECK(chipmunk_ht_keypair_from_seed(l_seed, a_pk_out, a_sk_out) == CHIPMUNK_ERROR_SUCCESS,
          "all-leaves keypair_from_seed");

    const uint32_t N = CHIPMUNK_HT_LEAF_COUNT;
    s_all_sigs = DAP_NEW_Z_COUNT(chipmunk_ht_signature_t, N);
    CHECK(s_all_sigs != NULL, "alloc sigs buffer");
    if (!s_all_sigs) return;

    char l_msg[64];
    int l_sign_ok = 1;
    int l_verify_ok = 1;
    for (uint32_t i = 0; i < N; ++i) {
        s_make_msg(l_msg, sizeof(l_msg), i);
        int l_rc_s = chipmunk_ht_sign(a_sk_out, (const uint8_t *)l_msg, strlen(l_msg), &s_all_sigs[i]);
        if (l_rc_s != CHIPMUNK_ERROR_SUCCESS) {
            fprintf(stderr, "   chipmunk_ht_sign failed at leaf %u: %d\n", i, l_rc_s);
            l_sign_ok = 0;
            break;
        }
        if (s_all_sigs[i].leaf_index != i) {
            fprintf(stderr, "   sig %u has leaf_index %u\n", i, s_all_sigs[i].leaf_index);
            l_sign_ok = 0;
            break;
        }
        int l_rc_v = chipmunk_ht_verify(a_pk_out, (const uint8_t *)l_msg, strlen(l_msg), &s_all_sigs[i]);
        if (l_rc_v != CHIPMUNK_ERROR_SUCCESS) {
            fprintf(stderr, "   chipmunk_ht_verify failed at leaf %u: %d\n", i, l_rc_v);
            l_verify_ok = 0;
            break;
        }
    }
    CHECK(l_sign_ok,   "every leaf_index in [0, LEAF_COUNT) signs successfully");
    CHECK(l_verify_ok, "every produced signature verifies under its own message");
    CHECK(a_sk_out->leaf_index == N, "after all leaves, sk->leaf_index == LEAF_COUNT");
}

/* ---------------------------------------------------------------------- *
 *  3. Exhaustion                                                         *
 * ---------------------------------------------------------------------- */

static void s_test_exhaustion(chipmunk_ht_private_key_t *a_sk)
{
    chipmunk_ht_signature_t l_extra;
    memset(&l_extra, 0, sizeof(l_extra));
    const char *l_msg = "overflow attempt";
    uint32_t l_before = a_sk->leaf_index;
    int l_rc = chipmunk_ht_sign(a_sk, (const uint8_t *)l_msg, strlen(l_msg), &l_extra);
    CHECK(l_rc == CHIPMUNK_ERROR_KEY_EXHAUSTED, "overflow attempt returns KEY_EXHAUSTED");
    CHECK(a_sk->leaf_index == l_before, "counter pinned after exhaustion");
    chipmunk_ht_signature_clear(&l_extra);
}

/* ---------------------------------------------------------------------- *
 *  4. Tamper detection                                                   *
 * ---------------------------------------------------------------------- */

static void s_test_tamper(chipmunk_ht_public_key_t *a_pk)
{
    if (!s_all_sigs) return;

    /* (a) replace leaf_pk — should no longer match the Merkle path. */
    chipmunk_ht_signature_t l_tmp = s_all_sigs[0];
    /* deep-copy the path so we don't share ownership with s_all_sigs[0] */
    l_tmp.auth_path.nodes = NULL;
    l_tmp.auth_path.path_length = s_all_sigs[0].auth_path.path_length;
    l_tmp.auth_path.index       = s_all_sigs[0].auth_path.index;
    l_tmp.auth_path.nodes = DAP_NEW_Z_COUNT(chipmunk_path_node_t, l_tmp.auth_path.path_length);
    memcpy(l_tmp.auth_path.nodes, s_all_sigs[0].auth_path.nodes,
           sizeof(chipmunk_path_node_t) * l_tmp.auth_path.path_length);

    /* substitute leaf_pk with leaf #1's pk */
    memcpy(&l_tmp.leaf_pk, &s_all_sigs[1].leaf_pk, sizeof(chipmunk_hots_pk_t));

    char l_msg[64]; s_make_msg(l_msg, sizeof(l_msg), 0);
    int l_rc = chipmunk_ht_verify(a_pk, (const uint8_t *)l_msg, strlen(l_msg), &l_tmp);
    CHECK(l_rc != CHIPMUNK_ERROR_SUCCESS, "tamper-(a): substitute leaf_pk breaks verify");
    chipmunk_ht_signature_clear(&l_tmp);

    /* (b) swap auth path of leaf 0 and leaf 1 — both must fail. */
    memset(&l_tmp, 0, sizeof(l_tmp));
    l_tmp.hots_sig   = s_all_sigs[0].hots_sig;
    l_tmp.leaf_index = s_all_sigs[0].leaf_index;
    l_tmp.leaf_pk    = s_all_sigs[0].leaf_pk;
    l_tmp.auth_path.path_length = s_all_sigs[1].auth_path.path_length;
    l_tmp.auth_path.index       = s_all_sigs[1].auth_path.index; /* wrong index */
    l_tmp.auth_path.nodes = DAP_NEW_Z_COUNT(chipmunk_path_node_t, l_tmp.auth_path.path_length);
    memcpy(l_tmp.auth_path.nodes, s_all_sigs[1].auth_path.nodes,
           sizeof(chipmunk_path_node_t) * l_tmp.auth_path.path_length);
    s_make_msg(l_msg, sizeof(l_msg), 0);
    l_rc = chipmunk_ht_verify(a_pk, (const uint8_t *)l_msg, strlen(l_msg), &l_tmp);
    CHECK(l_rc != CHIPMUNK_ERROR_SUCCESS, "tamper-(b): swap auth path breaks verify");
    chipmunk_ht_signature_clear(&l_tmp);

    /* (c) flip one sigma coefficient — HOTS-verify must fail. */
    memset(&l_tmp, 0, sizeof(l_tmp));
    l_tmp.hots_sig   = s_all_sigs[2].hots_sig;
    l_tmp.leaf_index = s_all_sigs[2].leaf_index;
    l_tmp.leaf_pk    = s_all_sigs[2].leaf_pk;
    l_tmp.auth_path.path_length = s_all_sigs[2].auth_path.path_length;
    l_tmp.auth_path.index       = s_all_sigs[2].auth_path.index;
    l_tmp.auth_path.nodes = DAP_NEW_Z_COUNT(chipmunk_path_node_t, l_tmp.auth_path.path_length);
    memcpy(l_tmp.auth_path.nodes, s_all_sigs[2].auth_path.nodes,
           sizeof(chipmunk_path_node_t) * l_tmp.auth_path.path_length);
    l_tmp.hots_sig.sigma[0].coeffs[0] ^= 0x1;  /* one-bit perturbation */
    s_make_msg(l_msg, sizeof(l_msg), 2);
    l_rc = chipmunk_ht_verify(a_pk, (const uint8_t *)l_msg, strlen(l_msg), &l_tmp);
    CHECK(l_rc != CHIPMUNK_ERROR_SUCCESS, "tamper-(c): sigma bit-flip breaks verify");
    chipmunk_ht_signature_clear(&l_tmp);

    /* (d) verify under a wrong (independent) keypair's root. */
    chipmunk_ht_public_key_t  l_pk2; memset(&l_pk2, 0, sizeof(l_pk2));
    chipmunk_ht_private_key_t l_sk2; memset(&l_sk2, 0, sizeof(l_sk2));
    uint8_t l_other_seed[32];
    for (int i = 0; i < 32; ++i) l_other_seed[i] = (uint8_t)(0xA0u + i);
    CHECK(chipmunk_ht_keypair_from_seed(l_other_seed, &l_pk2, &l_sk2) == CHIPMUNK_ERROR_SUCCESS,
          "tamper-(d): fresh keypair for cross-verify");
    s_make_msg(l_msg, sizeof(l_msg), 3);
    l_rc = chipmunk_ht_verify(&l_pk2, (const uint8_t *)l_msg, strlen(l_msg), &s_all_sigs[3]);
    CHECK(l_rc != CHIPMUNK_ERROR_SUCCESS, "tamper-(d): wrong pk rejects valid sig");
    chipmunk_ht_private_key_clear(&l_sk2);
}

/* ---------------------------------------------------------------------- *
 *  5. Signature serialise round-trip                                     *
 * ---------------------------------------------------------------------- */

static void s_test_sig_roundtrip(chipmunk_ht_public_key_t *a_pk)
{
    if (!s_all_sigs) return;
    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, CHIPMUNK_HT_SIGNATURE_SIZE);
    CHECK(l_buf != NULL, "sig roundtrip: alloc buffer");
    if (!l_buf) return;

    int l_rc = chipmunk_ht_signature_to_bytes(l_buf, &s_all_sigs[5]);
    CHECK(l_rc == CHIPMUNK_ERROR_SUCCESS, "sig roundtrip: serialise");

    chipmunk_ht_signature_t l_rs;
    memset(&l_rs, 0, sizeof(l_rs));
    l_rc = chipmunk_ht_signature_from_bytes(&l_rs, l_buf);
    CHECK(l_rc == CHIPMUNK_ERROR_SUCCESS, "sig roundtrip: deserialise");

    char l_msg[64]; s_make_msg(l_msg, sizeof(l_msg), 5);
    l_rc = chipmunk_ht_verify(a_pk, (const uint8_t *)l_msg, strlen(l_msg), &l_rs);
    CHECK(l_rc == CHIPMUNK_ERROR_SUCCESS, "sig roundtrip: deserialised sig verifies");

    chipmunk_ht_signature_clear(&l_rs);
    DAP_DEL_MULTY(l_buf);
}

/* ---------------------------------------------------------------------- *
 *  6. Private-key serialise round-trip                                   *
 * ---------------------------------------------------------------------- */

static void s_test_sk_roundtrip(void)
{
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = (uint8_t)(0x44u + i);

    chipmunk_ht_public_key_t  l_pk;  memset(&l_pk,  0, sizeof(l_pk));
    chipmunk_ht_private_key_t l_sk;  memset(&l_sk,  0, sizeof(l_sk));
    CHECK(chipmunk_ht_keypair_from_seed(l_seed, &l_pk, &l_sk) == CHIPMUNK_ERROR_SUCCESS,
          "sk roundtrip: initial keypair_from_seed");

    chipmunk_ht_signature_t l_s0;  memset(&l_s0, 0, sizeof(l_s0));
    chipmunk_ht_signature_t l_s1;  memset(&l_s1, 0, sizeof(l_s1));
    CHECK(chipmunk_ht_sign(&l_sk, (const uint8_t *)"m0", 2, &l_s0) == CHIPMUNK_ERROR_SUCCESS,
          "sk roundtrip: sign-0");
    CHECK(chipmunk_ht_sign(&l_sk, (const uint8_t *)"m1", 2, &l_s1) == CHIPMUNK_ERROR_SUCCESS,
          "sk roundtrip: sign-1");
    CHECK(l_sk.leaf_index == 2u, "sk roundtrip: counter reached 2");

    /* serialise the header */
    uint8_t l_hdr[CHIPMUNK_HT_PRIVATE_KEY_SIZE];
    CHECK(chipmunk_ht_private_key_to_bytes(l_hdr, &l_sk) == CHIPMUNK_ERROR_SUCCESS,
          "sk roundtrip: serialise");

    /* simulate reload */
    chipmunk_ht_private_key_t l_sk2; memset(&l_sk2, 0, sizeof(l_sk2));
    int l_rc = chipmunk_ht_private_key_from_bytes(&l_sk2, l_hdr);
    CHECK(l_rc == CHIPMUNK_ERROR_SUCCESS, "sk roundtrip: deserialise + rebuild tree");
    CHECK(l_sk2.leaf_index == 2u, "sk roundtrip: persisted counter preserved");
    CHECK(memcmp(&l_sk2.pk.root, &l_pk.root, sizeof(l_pk.root)) == 0,
          "sk roundtrip: rebuilt root matches original");

    /* continue signing from the deserialised sk */
    chipmunk_ht_signature_t l_s2;  memset(&l_s2, 0, sizeof(l_s2));
    CHECK(chipmunk_ht_sign(&l_sk2, (const uint8_t *)"m2", 2, &l_s2) == CHIPMUNK_ERROR_SUCCESS,
          "sk roundtrip: sign continues after reload");
    CHECK(l_s2.leaf_index == 2u,    "sk roundtrip: reloaded sk produces leaf_index=2");
    CHECK(chipmunk_ht_verify(&l_pk, (const uint8_t *)"m2", 2, &l_s2) == CHIPMUNK_ERROR_SUCCESS,
          "sk roundtrip: post-reload sig verifies under original pk");

    chipmunk_ht_signature_clear(&l_s0);
    chipmunk_ht_signature_clear(&l_s1);
    chipmunk_ht_signature_clear(&l_s2);
    chipmunk_ht_private_key_clear(&l_sk);
    chipmunk_ht_private_key_clear(&l_sk2);
}

/* ---------------------------------------------------------------------- *
 *  main                                                                  *
 * ---------------------------------------------------------------------- */

int main(void)
{
    dap_common_init("chipmunk-hypertree-regression", NULL);

    fprintf(stderr, "== CR-D15.B Chipmunk Hypertree regression ==\n");
    fprintf(stderr, "   HEIGHT=%u LEAF_COUNT=%u\n",
            (unsigned)CHIPMUNK_HT_HEIGHT, (unsigned)CHIPMUNK_HT_LEAF_COUNT);

    s_test_deterministic_keygen();

    chipmunk_ht_public_key_t  l_pk;  memset(&l_pk,  0, sizeof(l_pk));
    chipmunk_ht_private_key_t l_sk;  memset(&l_sk,  0, sizeof(l_sk));
    s_test_all_leaves_sign_verify(&l_pk, &l_sk);
    s_test_exhaustion(&l_sk);
    s_test_tamper(&l_pk);
    s_test_sig_roundtrip(&l_pk);

    if (s_all_sigs) {
        for (uint32_t i = 0; i < CHIPMUNK_HT_LEAF_COUNT; ++i) {
            chipmunk_ht_signature_clear(&s_all_sigs[i]);
        }
        DAP_DEL_MULTY(s_all_sigs);
        s_all_sigs = NULL;
    }
    chipmunk_ht_private_key_clear(&l_sk);

    s_test_sk_roundtrip();

    if (s_fails) {
        fprintf(stderr, "\nCR-D15.B: %d check(s) FAILED\n", s_fails);
        return 1;
    }
    fprintf(stderr, "\nCR-D15.B: all checks passed\n");
    return 0;
}
