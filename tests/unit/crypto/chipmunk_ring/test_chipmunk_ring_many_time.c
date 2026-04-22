/*
 * CR-D15.C regression test: Chipmunk Ring signature is MANY-TIME.
 *
 * Locks in the post-migration invariants that ensure a single
 * DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING keypair can be safely reused for
 * multiple ring signatures over distinct messages — which was not the
 * case before the migration to the Hypertree API (CR-D3 / CR-D15.B).
 *
 * Invariants covered:
 *
 *   1. One ring keypair produces at least 3 distinct ring signatures
 *      over distinct messages, all of which verify under the same ring
 *      and the same set of ring public keys.
 *   2. Each successive signature is byte-wise distinct (the hypertree
 *      advances leaf_index, so embedded auth paths and leaf HOTS pks
 *      must differ).
 *   3. The private-key buffer held by dap_enc_key_t is updated in
 *      place — its leaf_index advances monotonically after each sign.
 *   4. The ring sign/verify path supports the full hypertree capacity
 *      (CHIPMUNK_HT_MAX_SIGNATURES == 64) without exhaustion; the
 *      (CHIPMUNK_HT_MAX_SIGNATURES+1)-th attempt must fail fast.
 *
 * The test uses ring_size == 3 (signer + 2 decoys) with
 * required_signers == 1 to keep it a traditional anonymous ring
 * signature.  We use embedded_keys=true because that matches the
 * default code path used by dap_sign_create_ring.
 *
 * This test is deliberately end-to-end at the dap_sign_* layer rather
 * than at chipmunk_ring_* to guarantee that every glue layer between
 * dap_enc_key and chipmunk_ht_sign persists leaf_index correctly
 * across calls.
 */

#include <dap_common.h>
#include <dap_test.h>
#include <dap_enc_key.h>
#include <dap_enc_chipmunk_ring.h>
#include <dap_sign.h>
#include <dap_hash.h>
#include <dap_hash_compat.h>
#include "dap_rand.h"

#include "chipmunk/chipmunk_ring.h"
#include "chipmunk/chipmunk_hypertree.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "test_chipmunk_ring_many_time"

#define TEST_RING_SIZE       3u
#define TEST_QUICK_SIGS      4u   /* > 3, required by CR-D15.C spec */
#define TEST_MAX_SIGS        CHIPMUNK_HT_MAX_SIGNATURES

/**
 * Extract the hypertree private key's current leaf_index from the
 * serialised dap_enc_key priv_key_data buffer.  Used to verify that
 * successful signing advances the counter by exactly one.
 */
static uint32_t s_read_leaf_index(const dap_enc_key_t *a_key)
{
    dap_assert(a_key != NULL, "key not null");
    dap_assert(a_key->priv_key_data != NULL, "priv buffer not null");
    dap_assert(a_key->priv_key_data_size == CHIPMUNK_RING_PRIVATE_KEY_SIZE,
               "priv buffer sized to CHIPMUNK_RING_PRIVATE_KEY_SIZE");
    chipmunk_ht_private_key_t l_sk;
    memset(&l_sk, 0, sizeof(l_sk));
    int l_rc = chipmunk_ht_private_key_from_bytes(&l_sk,
                                                  (const uint8_t *)a_key->priv_key_data);
    dap_assert(l_rc == CHIPMUNK_ERROR_SUCCESS, "priv bytes parse clean");
    uint32_t l_idx = l_sk.leaf_index;
    chipmunk_ht_private_key_clear(&l_sk);
    return l_idx;
}

/**
 * Quick many-time smoke test: produce TEST_QUICK_SIGS (>=4) ring
 * signatures from the same signer keypair over distinct messages;
 * each must verify and each must advance the leaf_index counter.
 */
static bool s_test_ring_key_signs_many_messages(void)
{
    log_it(L_INFO, "CR-D15.C: one ring keypair signs %u distinct messages",
           TEST_QUICK_SIGS);

    dap_enc_key_t *l_ring_keys[TEST_RING_SIZE];
    memset(l_ring_keys, 0, sizeof(l_ring_keys));
    for (size_t i = 0; i < TEST_RING_SIZE; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING,
                                                  NULL, 0, NULL, 0, 256);
        dap_assert(l_ring_keys[i] != NULL, "ring keygen success");
    }
    dap_enc_key_t *l_signer = l_ring_keys[0];

    /* Sanity: fresh sk must start at leaf_index == 0. */
    dap_assert(s_read_leaf_index(l_signer) == 0u, "fresh ring sk has leaf_index==0");

    /* Produce TEST_QUICK_SIGS distinct signatures. */
    dap_sign_t *l_sigs[TEST_QUICK_SIGS];
    dap_hash_fast_t l_msg_hashes[TEST_QUICK_SIGS];
    memset(l_sigs, 0, sizeof(l_sigs));
    memset(l_msg_hashes, 0, sizeof(l_msg_hashes));

    for (size_t i = 0; i < TEST_QUICK_SIGS; i++) {
        char l_payload[64];
        snprintf(l_payload, sizeof(l_payload), "CR-D15.C many-time msg #%zu", i);
        bool l_hr = dap_hash_fast(l_payload, strlen(l_payload), &l_msg_hashes[i]);
        dap_assert(l_hr, "hash msg");

        uint32_t l_pre_idx = s_read_leaf_index(l_signer);
        l_sigs[i] = dap_sign_create_ring(l_signer,
                                         &l_msg_hashes[i], sizeof(l_msg_hashes[i]),
                                         l_ring_keys, TEST_RING_SIZE,
                                         1);
        dap_assert(l_sigs[i] != NULL, "ring sign success");
        uint32_t l_post_idx = s_read_leaf_index(l_signer);
        dap_assert(l_post_idx == l_pre_idx + 1u,
                   "leaf_index advances by exactly one per successful ring sign");

        int l_vr = dap_sign_verify_ring(l_sigs[i],
                                        &l_msg_hashes[i], sizeof(l_msg_hashes[i]),
                                        l_ring_keys, TEST_RING_SIZE);
        dap_assert(l_vr == 0, "ring verify success");
    }

    /* Every pair of signatures must be byte-wise distinct: embedded
     * auth paths and HOTS leaves differ because leaf_index differs. */
    for (size_t i = 0; i < TEST_QUICK_SIGS; i++) {
        for (size_t j = i + 1; j < TEST_QUICK_SIGS; j++) {
            dap_assert(l_sigs[i]->header.sign_size == l_sigs[j]->header.sign_size,
                       "all ring sigs share the wire size (fixed ring_size, required_signers)");
            dap_assert(memcmp(dap_sign_get_sign(l_sigs[i], NULL),
                              dap_sign_get_sign(l_sigs[j], NULL),
                              l_sigs[i]->header.sign_size) != 0,
                       "successive ring signatures must be byte-wise distinct");
        }
    }

    /* Cross-swap: signature #0 must not verify against message #1. */
    int l_cross = dap_sign_verify_ring(l_sigs[0],
                                       &l_msg_hashes[1], sizeof(l_msg_hashes[1]),
                                       l_ring_keys, TEST_RING_SIZE);
    dap_assert(l_cross != 0, "sig-over-msg#0 must fail to verify against msg#1");

    for (size_t i = 0; i < TEST_QUICK_SIGS; i++) {
        DAP_DELETE(l_sigs[i]);
    }
    for (size_t i = 0; i < TEST_RING_SIZE; i++) {
        dap_enc_key_delete(l_ring_keys[i]);
    }
    log_it(L_INFO, "CR-D15.C many-messages test passed (%u sigs)", TEST_QUICK_SIGS);
    return true;
}

/**
 * Exhaustion test: the very same keypair signs the full hypertree
 * capacity (64 signatures) successfully; the 65-th attempt must fail
 * with CHIPMUNK_ERROR_KEY_EXHAUSTED (surfaced as dap_sign_create_ring
 * returning NULL) and the counter must NOT advance beyond the cap.
 */
static bool s_test_ring_key_exhausts_after_max_sigs(void)
{
    log_it(L_INFO,
           "CR-D15.C: ring keypair capacity is exactly CHIPMUNK_HT_MAX_SIGNATURES (%u)",
           (unsigned)TEST_MAX_SIGS);

    dap_enc_key_t *l_ring_keys[TEST_RING_SIZE];
    memset(l_ring_keys, 0, sizeof(l_ring_keys));
    for (size_t i = 0; i < TEST_RING_SIZE; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING,
                                                  NULL, 0, NULL, 0, 256);
        dap_assert(l_ring_keys[i] != NULL, "ring keygen success");
    }
    dap_enc_key_t *l_signer = l_ring_keys[0];
    dap_assert(s_read_leaf_index(l_signer) == 0u, "fresh sk leaf_index==0");

    dap_hash_fast_t l_msg_hash;
    memset(&l_msg_hash, 0, sizeof(l_msg_hash));
    const char l_payload[] = "CR-D15.C exhaustion probe";
    bool l_hr = dap_hash_fast(l_payload, sizeof(l_payload) - 1, &l_msg_hash);
    dap_assert(l_hr, "hash payload");

    /* Consume every slot.  We verify a handful, not all 64, to keep
     * the test under a second: full signing is the expensive part and
     * we already have signature validity covered by the quick test.
     * But every sign must succeed, else the capacity invariant breaks.
     */
    for (uint32_t i = 0; i < TEST_MAX_SIGS; i++) {
        dap_sign_t *l_sig = dap_sign_create_ring(l_signer,
                                                 &l_msg_hash, sizeof(l_msg_hash),
                                                 l_ring_keys, TEST_RING_SIZE, 1);
        dap_assert(l_sig != NULL, "slot-bound ring sign must succeed up to the cap");
        dap_assert(s_read_leaf_index(l_signer) == i + 1u,
                   "leaf_index matches consumed-slots-so-far after each sign");

        /* Spot-check verify: first, middle, last. */
        if (i == 0u || i == TEST_MAX_SIGS / 2u || i + 1u == TEST_MAX_SIGS) {
            int l_vr = dap_sign_verify_ring(l_sig, &l_msg_hash, sizeof(l_msg_hash),
                                            l_ring_keys, TEST_RING_SIZE);
            dap_assert(l_vr == 0, "spot-check verify passes mid-capacity");
        }
        DAP_DELETE(l_sig);
    }

    /* Counter is pinned to the cap. */
    dap_assert(s_read_leaf_index(l_signer) == TEST_MAX_SIGS,
               "after MAX_SIGS signs, leaf_index equals the cap");

    /* 65-th attempt: MUST fail fast, MUST NOT advance the counter,
     * MUST NOT wipe the key state (i.e. the buffer is still parseable
     * as a valid hypertree sk with leaf_index == cap). */
    dap_sign_t *l_overflow = dap_sign_create_ring(l_signer,
                                                  &l_msg_hash, sizeof(l_msg_hash),
                                                  l_ring_keys, TEST_RING_SIZE, 1);
    dap_assert(l_overflow == NULL,
               "the (MAX_SIGS+1)-th ring sign must fail fast");
    dap_assert(s_read_leaf_index(l_signer) == TEST_MAX_SIGS,
               "failed overflow sign must NOT advance leaf_index");

    for (size_t i = 0; i < TEST_RING_SIZE; i++) {
        dap_enc_key_delete(l_ring_keys[i]);
    }
    log_it(L_INFO, "CR-D15.C exhaustion test passed (cap = %u sigs)",
           (unsigned)TEST_MAX_SIGS);
    return true;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    log_it(L_NOTICE, "CR-D15.C: starting many-time ring signature regression");

    if (dap_enc_chipmunk_ring_init() != 0) {
        log_it(L_ERROR, "dap_enc_chipmunk_ring_init failed");
        return -1;
    }

    bool l_ok = true;
    l_ok &= s_test_ring_key_signs_many_messages();
    l_ok &= s_test_ring_key_exhausts_after_max_sigs();

    if (l_ok) {
        log_it(L_NOTICE, "CR-D15.C many-time regression: ALL TESTS PASSED");
        return 0;
    }
    log_it(L_ERROR, "CR-D15.C many-time regression: SOME TESTS FAILED");
    return -1;
}
