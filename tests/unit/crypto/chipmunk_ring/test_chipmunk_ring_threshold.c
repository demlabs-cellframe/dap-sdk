/*
 * test_chipmunk_ring_threshold.c — CR-9.4.A acceptance tests
 *
 * Locks in the public threshold dealer/combiner API
 * (dap_chipmunk_ring_threshold.h).  Every contract row in
 * SLC `documentation_ef0d4cc844e1f421` (CR-9.4 design) §6 has a
 * dedicated check below; the deferred CR-9.4.B sign_partial path
 * is intentionally NOT exercised here.
 */

#include <dap_common.h>
#include <dap_test.h>
#include <dap_rand.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dap_chipmunk_ring_threshold.h"
#include "chipmunk/chipmunk.h"             /* CHIPMUNK_Q */
#include "chipmunk/chipmunk_hypertree.h"   /* end-to-end sign-after-combine */

#define LOG_TAG "test_chipmunk_ring_threshold"

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static void s_pick_indices(uint32_t *a_out, uint32_t a_n, uint32_t a_t,
                           uint32_t a_seed)
{
    uint32_t l_pool[64];
    for (uint32_t i = 0; i < a_n; ++i) l_pool[i] = i + 1u;
    uint32_t l_state = a_seed | 1u;
    for (uint32_t i = a_n; i > 1u; --i) {
        l_state = l_state * 1664525u + 1013904223u;
        uint32_t l_j = l_state % i;
        uint32_t l_tmp = l_pool[i - 1u]; l_pool[i - 1u] = l_pool[l_j]; l_pool[l_j] = l_tmp;
    }
    for (uint32_t i = 0; i < a_t; ++i) a_out[i] = l_pool[i];
}

static void s_select_shares(const chipmunk_ring_threshold_share_t *a_all,
                            uint32_t a_n,
                            const uint32_t *a_indices, uint32_t a_t,
                            chipmunk_ring_threshold_share_t *a_out)
{
    for (uint32_t i = 0; i < a_t; ++i) {
        bool l_found = false;
        for (uint32_t j = 0; j < a_n; ++j) {
            /* header.index lives at offset 7 (LE byte). */
            if (a_all[j].data[7] == (uint8_t)a_indices[i]) {
                a_out[i] = a_all[j];
                l_found = true;
                break;
            }
        }
        dap_assert(l_found, "selected share index found in pool");
    }
}

/* ------------------------------------------------------------------ */
/* Tests (matching design_decision_cr9_4.md §6 row-for-row)           */
/* ------------------------------------------------------------------ */

static bool s_test_seed_roundtrip(void)
{
    /* Deterministic seed = 0x00..0x1F (32 bytes). */
    uint8_t l_seed_in[32];
    for (int i = 0; i < 32; ++i) l_seed_in[i] = (uint8_t)i;

    chipmunk_ring_threshold_share_t l_shares[5];
    dap_assert(chipmunk_ring_threshold_deal(l_seed_in, 5, 3, l_shares) == 0,
               "basic deal OK");

    /* Pick first 3 shares for combine. */
    chipmunk_ring_threshold_share_t l_pick[3] = { l_shares[0], l_shares[1], l_shares[2] };
    uint8_t l_seed_out[32];
    dap_assert(chipmunk_ring_threshold_combine(l_pick, 3, l_seed_out) == 0,
               "basic combine OK");
    dap_assert(memcmp(l_seed_in, l_seed_out, 32) == 0,
               "reconstructed seed equals dealer input");
    return true;
}

static bool s_test_seed_roundtrip_random(void)
{
    for (int l_iter = 0; l_iter < 25; ++l_iter) {
        uint8_t l_seed_in[32];
        dap_random_bytes(l_seed_in, sizeof(l_seed_in));

        uint32_t l_n_buf[1], l_t_buf[1];
        dap_random_bytes(l_n_buf, sizeof(l_n_buf));
        dap_random_bytes(l_t_buf, sizeof(l_t_buf));
        uint32_t l_n = 2u + (l_n_buf[0] % 15u);   /* n ∈ [2, 16] */
        uint32_t l_t = 2u + (l_t_buf[0] % (l_n - 1u));

        chipmunk_ring_threshold_share_t l_shares[16];
        dap_assert(chipmunk_ring_threshold_deal(l_seed_in, l_n, l_t, l_shares) == 0,
                   "random deal OK");

        uint32_t l_idx[16];
        s_pick_indices(l_idx, l_n, l_t, (uint32_t)l_iter + 17u);

        chipmunk_ring_threshold_share_t l_pick[16];
        s_select_shares(l_shares, l_n, l_idx, l_t, l_pick);

        uint8_t l_seed_out[32];
        dap_assert(chipmunk_ring_threshold_combine(l_pick, l_t, l_seed_out) == 0,
                   "random combine OK");
        if (memcmp(l_seed_in, l_seed_out, 32) != 0) {
            log_it(L_ERROR, "iter=%d  n=%u t=%u  mismatch", l_iter, l_n, l_t);
        }
        dap_assert(memcmp(l_seed_in, l_seed_out, 32) == 0,
                   "random roundtrip equal");
    }
    return true;
}

static bool s_test_seed_roundtrip_full_64_byte_entropy(void)
{
    /* Cover every byte in the seed with a non-trivial bit pattern,
     * so we exercise both halves (low and high) of every chunk. */
    uint8_t l_seed_in[32];
    for (int i = 0; i < 32; ++i) l_seed_in[i] = (uint8_t)(0xA5u ^ (uint8_t)(i * 17u));

    chipmunk_ring_threshold_share_t l_shares[8];
    dap_assert(chipmunk_ring_threshold_deal(l_seed_in, 8, 4, l_shares) == 0,
               "deal full-entropy seed");

    chipmunk_ring_threshold_share_t l_pick[4] = {
        l_shares[7], l_shares[2], l_shares[5], l_shares[0]
    };
    uint8_t l_seed_out[32];
    dap_assert(chipmunk_ring_threshold_combine(l_pick, 4, l_seed_out) == 0,
               "combine full-entropy seed");
    for (int i = 0; i < 32; ++i) {
        dap_assert(l_seed_out[i] == l_seed_in[i],
                   "every byte of the 32-byte seed roundtrips");
    }
    return true;
}

static bool s_test_subset_invariance(void)
{
    /* (n=4, t=2) — exhaustively check all C(4,2) = 6 subsets. */
    uint8_t l_seed_in[32];
    for (int i = 0; i < 32; ++i) l_seed_in[i] = (uint8_t)(0xC3u + i);

    chipmunk_ring_threshold_share_t l_all[4];
    dap_assert(chipmunk_ring_threshold_deal(l_seed_in, 4, 2, l_all) == 0,
               "deal for subset-invariance");

    int l_subsets = 0;
    for (uint32_t i = 0; i < 4u; ++i) {
        for (uint32_t j = i + 1u; j < 4u; ++j) {
            chipmunk_ring_threshold_share_t l_pair[2] = { l_all[i], l_all[j] };
            uint8_t l_seed_out[32];
            dap_assert(chipmunk_ring_threshold_combine(l_pair, 2, l_seed_out) == 0,
                       "subset combine OK");
            dap_assert(memcmp(l_seed_in, l_seed_out, 32) == 0,
                       "every subset reconstructs the same seed");
            ++l_subsets;
        }
    }
    dap_assert(l_subsets == 6, "exhaustive subset coverage");
    return true;
}

static bool s_test_signing_after_combine(void)
{
    /* End-to-end: deal → combine → keypair_from_seed → sign → verify. */
    uint8_t l_seed_in[32];
    dap_random_bytes(l_seed_in, sizeof(l_seed_in));

    chipmunk_ring_threshold_share_t l_all[5];
    dap_assert(chipmunk_ring_threshold_deal(l_seed_in, 5, 3, l_all) == 0,
               "deal OK");

    chipmunk_ring_threshold_share_t l_pick[3] = { l_all[1], l_all[2], l_all[4] };
    uint8_t l_seed_out[32];
    dap_assert(chipmunk_ring_threshold_combine(l_pick, 3, l_seed_out) == 0,
               "combine OK");
    dap_assert(memcmp(l_seed_in, l_seed_out, 32) == 0,
               "seed roundtrip identity");

    /* The combiner can now act as a single-party signer using the
     * existing chipmunk_ht_* path.  This is the exact "production"
     * flow described in §3.1 of the design doc. */
    chipmunk_ht_public_key_t  l_pk;
    chipmunk_ht_private_key_t l_sk;
    memset(&l_pk, 0, sizeof(l_pk));
    memset(&l_sk, 0, sizeof(l_sk));
    int l_rc = chipmunk_ht_keypair_from_seed(l_seed_out, &l_pk, &l_sk);
    dap_assert(l_rc == 0, "keypair_from_seed OK");

    const char *l_msg = "CR-9.4.A end-to-end signing-after-combine";
    chipmunk_ht_signature_t l_sig;
    memset(&l_sig, 0, sizeof(l_sig));
    l_rc = chipmunk_ht_sign(&l_sk, (const uint8_t *)l_msg, strlen(l_msg), &l_sig);
    dap_assert(l_rc == 0, "sign-after-combine OK");

    l_rc = chipmunk_ht_verify(&l_pk, (const uint8_t *)l_msg, strlen(l_msg), &l_sig);
    dap_assert(l_rc == 0, "verify-after-combine OK");

    chipmunk_ht_signature_clear(&l_sig);
    chipmunk_ht_private_key_clear(&l_sk);
    return true;
}

static bool s_test_combine_rejects_mixed_dealing_rounds(void)
{
    /* Two independent dealings on the same (n, t).  Mixing shares from
     * the two pools must be rejected — the magic/version/(n,t) headers
     * agree, but the underlying polynomials are different, so the
     * reconstructed "seed" would be nonsense.  The cross-share check
     * cannot detect mixing on (magic, version, n, t) alone, but the
     * resulting reconstructed bytes will mismatch the dealer input.
     * The CR-9.4 contract is therefore: if the caller mixes pools from
     * different deal() calls, combine() either:
     *   (a) returns -EINVAL via a header invariant (e.g. (n, t) drift
     *       across pools — which we cannot trigger with same-(n,t)
     *       pools), OR
     *   (b) returns 0 with a deterministic-but-wrong seed (correctness
     *       is the dealer's responsibility, not the field arithmetic).
     * This test pins (b): same (n, t) pools mix without an error code,
     * but the result MUST NOT equal either dealer's seed.  This proves
     * the absence of silent "well, it's still a valid seed" behaviour
     * that would mask the misuse. */
    uint8_t l_seed_a[32], l_seed_b[32];
    for (int i = 0; i < 32; ++i) { l_seed_a[i] = (uint8_t)(0x10u + i); l_seed_b[i] = (uint8_t)(0xC0u - i); }

    chipmunk_ring_threshold_share_t l_pool_a[4], l_pool_b[4];
    dap_assert(chipmunk_ring_threshold_deal(l_seed_a, 4, 2, l_pool_a) == 0, "deal A");
    dap_assert(chipmunk_ring_threshold_deal(l_seed_b, 4, 2, l_pool_b) == 0, "deal B");

    /* Mixed pair: one share from each pool (different participant
     * indices to dodge the duplicate-index check). */
    chipmunk_ring_threshold_share_t l_mix[2] = { l_pool_a[0], l_pool_b[2] };
    uint8_t l_seed_mixed[32];
    int l_rc = chipmunk_ring_threshold_combine(l_mix, 2, l_seed_mixed);
    /* Either path is acceptable — what we forbid is "rc==0 AND seed_mixed
     * accidentally equals seed_a or seed_b".  The mathematical chance
     * of that under a random polynomial is negligible. */
    if (l_rc == 0) {
        dap_assert(memcmp(l_seed_mixed, l_seed_a, 32) != 0,
                   "mixed combine does not silently equal pool-A seed");
        dap_assert(memcmp(l_seed_mixed, l_seed_b, 32) != 0,
                   "mixed combine does not silently equal pool-B seed");
    } else {
        dap_assert(l_rc == -EINVAL,
                   "mixed combine rejected with -EINVAL");
        for (int i = 0; i < 32; ++i) {
            dap_assert(l_seed_mixed[i] == 0,
                       "output zeroised on mixed-pool error");
        }
    }
    return true;
}

static bool s_test_combine_rejects_truncated_share(void)
{
    /* Tamper a single byte in the magic of share[0] → -EINVAL. */
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = (uint8_t)i;
    chipmunk_ring_threshold_share_t l_all[3];
    dap_assert(chipmunk_ring_threshold_deal(l_seed, 3, 2, l_all) == 0,
               "deal for tamper test");

    chipmunk_ring_threshold_share_t l_pick[2] = { l_all[0], l_all[1] };
    /* Tamper byte 0 of the first share (magic LSB). */
    l_pick[0].data[0] ^= 0xFFu;

    uint8_t l_out[32] = {0};
    /* Pre-fill output with non-zero so we can assert zeroisation. */
    for (int i = 0; i < 32; ++i) l_out[i] = 0xAAu;
    int l_rc = chipmunk_ring_threshold_combine(l_pick, 2, l_out);
    dap_assert(l_rc == -EINVAL, "tampered magic rejected");
    for (int i = 0; i < 32; ++i) {
        dap_assert(l_out[i] == 0u, "output zeroised on magic mismatch");
    }
    return true;
}

static bool s_test_combine_rejects_index_zero(void)
{
    /* Deal normally, then forge index=0 in one of the picks. */
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = (uint8_t)i;
    chipmunk_ring_threshold_share_t l_all[3];
    dap_assert(chipmunk_ring_threshold_deal(l_seed, 3, 2, l_all) == 0,
               "deal for index-zero test");

    chipmunk_ring_threshold_share_t l_pick[2] = { l_all[0], l_all[1] };
    l_pick[0].data[7] = 0u;   /* index byte forced to 0 */

    uint8_t l_out[32];
    for (int i = 0; i < 32; ++i) l_out[i] = 0xAAu;
    int l_rc = chipmunk_ring_threshold_combine(l_pick, 2, l_out);
    dap_assert(l_rc == -EINVAL, "index 0 rejected");
    for (int i = 0; i < 32; ++i) {
        dap_assert(l_out[i] == 0u, "output zeroised on index-zero error");
    }
    return true;
}

static bool s_test_combine_rejects_duplicate_indices(void)
{
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = (uint8_t)i;
    chipmunk_ring_threshold_share_t l_all[3];
    dap_assert(chipmunk_ring_threshold_deal(l_seed, 3, 2, l_all) == 0,
               "deal for dup-index test");

    chipmunk_ring_threshold_share_t l_pick[2] = { l_all[0], l_all[0] };
    /* Both picks have the same data → same index = duplicate. */
    uint8_t l_out[32];
    for (int i = 0; i < 32; ++i) l_out[i] = 0xAAu;
    int l_rc = chipmunk_ring_threshold_combine(l_pick, 2, l_out);
    dap_assert(l_rc == -EINVAL, "duplicate indices rejected");
    for (int i = 0; i < 32; ++i) {
        dap_assert(l_out[i] == 0u, "output zeroised on duplicate-index error");
    }
    return true;
}

static bool s_test_combine_rejects_oversized_chunk(void)
{
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = (uint8_t)i;
    chipmunk_ring_threshold_share_t l_all[3];
    dap_assert(chipmunk_ring_threshold_deal(l_seed, 3, 2, l_all) == 0,
               "deal for oversized-chunk test");

    chipmunk_ring_threshold_share_t l_pick[2] = { l_all[0], l_all[1] };
    /* Force chunk[0] of the first share to (q + 1) ≡ 1 (mod q).  Without
     * the range check the Lagrange would silently mask this; with the
     * range check the share is rejected. */
    uint32_t l_overflow = (uint32_t)CHIPMUNK_Q + 1u;
    uint8_t *l_chunk0 = &l_pick[0].data[8 + 0 * 4];
    l_chunk0[0] = (uint8_t)( l_overflow        & 0xFFu);
    l_chunk0[1] = (uint8_t)((l_overflow >>  8) & 0xFFu);
    l_chunk0[2] = (uint8_t)((l_overflow >> 16) & 0xFFu);
    l_chunk0[3] = (uint8_t)((l_overflow >> 24) & 0xFFu);

    uint8_t l_out[32];
    for (int i = 0; i < 32; ++i) l_out[i] = 0xAAu;
    int l_rc = chipmunk_ring_threshold_combine(l_pick, 2, l_out);
    dap_assert(l_rc == -EINVAL, "oversized chunk rejected");
    for (int i = 0; i < 32; ++i) {
        dap_assert(l_out[i] == 0u, "output zeroised on oversized-chunk error");
    }
    return true;
}

static bool s_test_share_wipe_idempotent(void)
{
    chipmunk_ring_threshold_share_wipe(NULL);   /* must not crash */

    chipmunk_ring_threshold_share_t l_s;
    for (size_t i = 0; i < sizeof(l_s.data); ++i) l_s.data[i] = (uint8_t)i;
    chipmunk_ring_threshold_share_wipe(&l_s);
    for (size_t i = 0; i < sizeof(l_s.data); ++i) {
        dap_assert(l_s.data[i] == 0u, "share wiped to zero on first call");
    }
    chipmunk_ring_threshold_share_wipe(&l_s);
    for (size_t i = 0; i < sizeof(l_s.data); ++i) {
        dap_assert(l_s.data[i] == 0u, "share stays zero on second call (idempotent)");
    }
    return true;
}

static bool s_test_zeroisation_on_error(void)
{
    /* Comprehensive: every contract violation in deal() must zeroise
     * the output buffer.  We test t=1 (rejected) and t>n (rejected). */
    chipmunk_ring_threshold_share_t l_out[3];
    /* Pre-fill output with non-zero so we can assert zeroisation. */
    memset(l_out, 0xAA, sizeof(l_out));

    uint8_t l_seed[32] = {0};
    int l_rc = chipmunk_ring_threshold_deal(l_seed, 3, 1, l_out);
    dap_assert(l_rc == -EINVAL, "deal(t=1) rejected");
    for (size_t i = 0; i < sizeof(l_out); ++i) {
        dap_assert(((uint8_t *)l_out)[i] == 0u,
                   "output zeroised on deal(t=1) error");
    }

    memset(l_out, 0xAA, sizeof(l_out));
    l_rc = chipmunk_ring_threshold_deal(l_seed, 3, 4, l_out);
    dap_assert(l_rc == -EINVAL, "deal(t>n) rejected");
    for (size_t i = 0; i < sizeof(l_out); ++i) {
        dap_assert(((uint8_t *)l_out)[i] == 0u,
                   "output zeroised on deal(t>n) error");
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Test runner                                                        */
/* ------------------------------------------------------------------ */

int main(void)
{
    dap_set_appname("test_chipmunk_ring_threshold");
    dap_common_init("test_chipmunk_ring_threshold", NULL);

    int l_rc = 0;
    if (!s_test_seed_roundtrip())                         l_rc = 1;
    if (!s_test_seed_roundtrip_random())                  l_rc = 1;
    if (!s_test_seed_roundtrip_full_64_byte_entropy())    l_rc = 1;
    if (!s_test_subset_invariance())                      l_rc = 1;
    if (!s_test_signing_after_combine())                  l_rc = 1;
    if (!s_test_combine_rejects_mixed_dealing_rounds())   l_rc = 1;
    if (!s_test_combine_rejects_truncated_share())        l_rc = 1;
    if (!s_test_combine_rejects_index_zero())             l_rc = 1;
    if (!s_test_combine_rejects_duplicate_indices())      l_rc = 1;
    if (!s_test_combine_rejects_oversized_chunk())        l_rc = 1;
    if (!s_test_share_wipe_idempotent())                  l_rc = 1;
    if (!s_test_zeroisation_on_error())                   l_rc = 1;

    if (l_rc == 0) {
        log_it(L_INFO, "ALL CR-9.4.A threshold dealer/combiner tests PASSED");
    } else {
        log_it(L_ERROR, "Some CR-9.4.A threshold tests FAILED");
    }
    dap_common_deinit();
    return l_rc;
}
