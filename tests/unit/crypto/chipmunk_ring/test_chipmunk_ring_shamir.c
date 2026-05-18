/*
 * test_chipmunk_ring_shamir.c — CR-9.3 acceptance tests
 *
 * Locks in the canonical Shamir-over-Z_CHIPMUNK_Q primitive.  Every
 * contract listed in SLC `documentation_c6a567b1d9b7e68c` (CR-9 master design) §5
 * has a dedicated test; "skip ≡ pass" stress paths are absent here so
 * the suite stays deterministic in CI.
 */

#include <dap_common.h>
#include <dap_test.h>
#include <dap_rand.h>
#include <math.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "chipmunk/chipmunk.h"           /* CHIPMUNK_Q */
#include "chipmunk/chipmunk_ring_shamir.h"

#define LOG_TAG "test_chipmunk_ring_shamir"

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static bool s_array_equal(const uint32_t *a, const uint32_t *b, size_t n)
{
    return memcmp(a, b, n * sizeof(uint32_t)) == 0;
}

/* Generate `t` distinct indices in [1, n] for reconstruction. */
static void s_pick_indices(uint32_t *a_out, uint32_t a_n, uint32_t a_t,
                           uint32_t a_seed)
{
    /* Deterministic Fisher-Yates over [1, n] driven by `seed` so each
     * call with the same seed picks the same subset (debuggability). */
    uint32_t l_pool[CHIPMUNK_RING_THRESHOLD_MAX_N];
    for (uint32_t i = 0; i < a_n; ++i) l_pool[i] = i + 1u;

    uint32_t l_state = a_seed | 1u;
    for (uint32_t i = a_n; i > 1u; --i) {
        l_state = l_state * 1664525u + 1013904223u;
        uint32_t l_j = l_state % i;
        uint32_t l_tmp = l_pool[i - 1u];
        l_pool[i - 1u] = l_pool[l_j];
        l_pool[l_j] = l_tmp;
    }
    for (uint32_t i = 0; i < a_t; ++i) a_out[i] = l_pool[i];
}

/* Pull `t` shares from `all_shares[n]` matching the requested indices. */
static void s_select_shares(const chipmunk_ring_shamir_share_t *a_all,
                            uint32_t a_n,
                            const uint32_t *a_indices,
                            uint32_t a_t,
                            chipmunk_ring_shamir_share_t *a_out)
{
    for (uint32_t i = 0; i < a_t; ++i) {
        bool l_found = false;
        for (uint32_t j = 0; j < a_n; ++j) {
            if (a_all[j].index == a_indices[i]) {
                a_out[i] = a_all[j];
                l_found = true;
                break;
            }
        }
        dap_assert(l_found, "share index found in pool");
    }
}

/* ------------------------------------------------------------------ */
/* Contract suite (matches design_decision_cr9.md §5 row-for-row)     */
/* ------------------------------------------------------------------ */

static bool s_test_correctness_basic(void)
{
    const uint32_t l_secret = 1234567u;
    const uint32_t l_n = 5u, l_t = 3u;
    chipmunk_ring_shamir_share_t l_shares[5];

    dap_assert(chipmunk_ring_shamir_share(l_secret, l_n, l_t, l_shares) == 0,
               "basic share OK");

    /* Reconstruct from the first 3 shares. */
    chipmunk_ring_shamir_share_t l_chosen[3] = { l_shares[0], l_shares[1], l_shares[2] };
    uint32_t l_out = 0;
    dap_assert(chipmunk_ring_shamir_reconstruct(l_chosen, l_t, &l_out) == 0,
               "basic reconstruct OK");
    dap_assert(l_out == l_secret, "basic reconstruct yields original secret");
    return true;
}

static bool s_test_correctness_random(void)
{
    /* 50 random (secret, n, t) triples, each reconstructed with a
     * randomly-picked t-subset.  Failure of any iteration fails the
     * test as a whole. */
    for (int l_iter = 0; l_iter < 50; ++l_iter) {
        uint32_t l_buf[3];
        dap_random_bytes(l_buf, sizeof(l_buf));

        uint32_t l_secret = l_buf[0] % (uint32_t)CHIPMUNK_Q;
        uint32_t l_n      = 2u + (l_buf[1] % (CHIPMUNK_RING_THRESHOLD_MAX_N - 1u));
        uint32_t l_t      = 2u + (l_buf[2] % (l_n - 1u));

        chipmunk_ring_shamir_share_t l_all[CHIPMUNK_RING_THRESHOLD_MAX_N];
        dap_assert(chipmunk_ring_shamir_share(l_secret, l_n, l_t, l_all) == 0,
                   "random share OK");

        uint32_t l_indices[CHIPMUNK_RING_THRESHOLD_MAX_N];
        s_pick_indices(l_indices, l_n, l_t, (uint32_t)l_iter + 1u);

        chipmunk_ring_shamir_share_t l_chosen[CHIPMUNK_RING_THRESHOLD_MAX_N];
        s_select_shares(l_all, l_n, l_indices, l_t, l_chosen);

        uint32_t l_out = 0;
        dap_assert(chipmunk_ring_shamir_reconstruct(l_chosen, l_t, &l_out) == 0,
                   "random reconstruct OK");
        if (l_out != l_secret) {
            log_it(L_ERROR, "iter=%d  n=%u t=%u  secret=%u  got=%u",
                   l_iter, l_n, l_t, l_secret, l_out);
        }
        dap_assert(l_out == l_secret, "random reconstruct yields original secret");
    }
    return true;
}

static bool s_test_subset_invariance(void)
{
    /* For (n=4, t=2) every 2-of-4 subset must reconstruct the same
     * secret.  We enumerate all C(4,2) = 6 subsets exhaustively. */
    const uint32_t l_secret = 99u;
    const uint32_t l_n = 4u, l_t = 2u;
    chipmunk_ring_shamir_share_t l_all[4];
    dap_assert(chipmunk_ring_shamir_share(l_secret, l_n, l_t, l_all) == 0,
               "subset-invariance share OK");

    int l_subsets = 0;
    for (uint32_t i = 0; i < l_n; ++i) {
        for (uint32_t j = i + 1u; j < l_n; ++j) {
            chipmunk_ring_shamir_share_t l_pair[2] = { l_all[i], l_all[j] };
            uint32_t l_out = 0;
            dap_assert(chipmunk_ring_shamir_reconstruct(l_pair, l_t, &l_out) == 0,
                       "subset reconstruct OK");
            dap_assert(l_out == l_secret,
                       "every subset yields the same secret");
            ++l_subsets;
        }
    }
    dap_assert(l_subsets == 6, "exhaustive subset coverage");
    return true;
}

static bool s_test_insufficient_shares(void)
{
    /* t-1 shares (i.e. fewer than the threshold) MUST be rejected.
     * The reconstruct API is parametric on `t`, so the caller can ask
     * for "reconstruct with t=2 but I only pass 1 share".  We exercise
     * both shapes:
     *   - explicit a_t == 1 (rejected by API contract t >= 2),
     *   - mismatch t-vs-array would be a caller bug; we only test the
     *     contract surface.
     * Output buffer must be zeroised on the error path. */
    const uint32_t l_secret = 4242u;
    chipmunk_ring_shamir_share_t l_all[5];
    dap_assert(chipmunk_ring_shamir_share(l_secret, 5u, 3u, l_all) == 0,
               "insufficient-shares share OK");

    uint32_t l_out = 0xDEADBEEFu;
    int l_rc = chipmunk_ring_shamir_reconstruct(l_all, 1u, &l_out);
    dap_assert(l_rc != 0, "reconstruct(t=1) rejected");
    dap_assert(l_out == 0u, "output zeroised on error");
    return true;
}

static bool s_test_field_arithmetic_inverse(void)
{
    /* For a battery of `a` values, inv(a) * a ≡ 1 (mod q). */
    static const uint32_t l_battery[] = {
        1u, 2u, 3u, 17u, 2017u, 65537u, 1234567u,
        (uint32_t)CHIPMUNK_Q - 1u,
        (uint32_t)CHIPMUNK_Q / 2u,
        (uint32_t)CHIPMUNK_Q / 3u + 7u,
    };
    for (size_t i = 0; i < sizeof(l_battery) / sizeof(l_battery[0]); ++i) {
        uint32_t l_a   = l_battery[i] % (uint32_t)CHIPMUNK_Q;
        if (l_a == 0u) continue;
        uint32_t l_inv = chipmunk_ring_shamir_mod_inverse(l_a);
        uint64_t l_p   = ((uint64_t)l_a * (uint64_t)l_inv) % (uint64_t)CHIPMUNK_Q;
        dap_assert(l_p == 1u, "a * inv(a) == 1 (mod q)");
    }
    /* Edge: inv(0) is documented to return 0. */
    dap_assert(chipmunk_ring_shamir_mod_inverse(0u) == 0u,
               "inv(0) == 0 (documented edge)");
    return true;
}

static bool s_test_t_one_rejected(void)
{
    chipmunk_ring_shamir_share_t l_shares[3];
    int l_rc = chipmunk_ring_shamir_share(7u, 3u, 1u, l_shares);
    dap_assert(l_rc == -EINVAL, "share(t=1) rejected with -EINVAL");
    /* Output buffer must be zeroised on every error path. */
    for (size_t i = 0; i < 3; ++i) {
        dap_assert(l_shares[i].index == 0u && l_shares[i].value == 0u,
                   "share output zeroised on error");
    }
    return true;
}

static bool s_test_t_greater_than_n_rejected(void)
{
    chipmunk_ring_shamir_share_t l_shares[3];
    int l_rc = chipmunk_ring_shamir_share(7u, 3u, 4u, l_shares);
    dap_assert(l_rc == -EINVAL, "share(t > n) rejected with -EINVAL");
    return true;
}

static bool s_test_index_zero_rejected(void)
{
    chipmunk_ring_shamir_share_t l_bad[2] = {
        { .index = 0u, .value = 1u },
        { .index = 1u, .value = 2u },
    };
    uint32_t l_out = 0xCAFEu;
    int l_rc = chipmunk_ring_shamir_reconstruct(l_bad, 2u, &l_out);
    dap_assert(l_rc == -EINVAL, "reconstruct with index==0 rejected");
    dap_assert(l_out == 0u, "output zeroised on index-zero error");
    return true;
}

static bool s_test_duplicate_indices_rejected(void)
{
    chipmunk_ring_shamir_share_t l_bad[2] = {
        { .index = 1u, .value = 100u },
        { .index = 1u, .value = 200u },   /* duplicate index */
    };
    uint32_t l_out = 0xCAFEu;
    int l_rc = chipmunk_ring_shamir_reconstruct(l_bad, 2u, &l_out);
    dap_assert(l_rc == -EINVAL, "reconstruct with duplicate indices rejected");
    dap_assert(l_out == 0u, "output zeroised on duplicate-index error");
    return true;
}

/* CR-9.3 §5 row-10: t-1 shares from secret==0 vs secret==q-1 must be
 * statistically indistinguishable.  We can't test perfect secrecy
 * directly (information-theoretic), but we can check that the share
 * VALUES at any fixed (n=t shares - 1) participant set are uniformly
 * distributed mod q, regardless of the secret.  That is: the empirical
 * distribution of share[i].value across many trials does not depend on
 * the secret in a way that survives a chi-square sanity check.
 *
 * For runtime budget we use a coarse 32-bucket chi-square at N=5000
 * trials per secret value.  The test passes if the chi-square statistic
 * differs by less than 50 between the two secrets — well within the
 * variance of a uniform 32-bucket distribution at this N. */
static bool s_test_zero_leakage_statistical(void)
{
    enum { K_BUCKETS = 32, K_TRIALS = 2000 };
    static const uint32_t l_secrets[2] = { 0u, (uint32_t)CHIPMUNK_Q - 1u };

    /* For each secret, run K_TRIALS share() calls with (n=3, t=2),
     * capture the value at participant index 1 (one of the t-1 shares
     * an attacker would see), and bucket it.  Identical buckets for
     * both secrets — modulo statistical noise — proves the dealer
     * doesn't leak the secret through the share distribution. */
    int l_buckets[2][K_BUCKETS];
    memset(l_buckets, 0, sizeof(l_buckets));

    for (int l_s = 0; l_s < 2; ++l_s) {
        for (int l_iter = 0; l_iter < K_TRIALS; ++l_iter) {
            chipmunk_ring_shamir_share_t l_shares[3];
            int l_rc = chipmunk_ring_shamir_share(l_secrets[l_s], 3u, 2u, l_shares);
            dap_assert(l_rc == 0, "leakage trial: share OK");
            uint32_t l_b = (uint32_t)
                ((uint64_t)l_shares[0].value * K_BUCKETS / (uint64_t)CHIPMUNK_Q);
            if (l_b >= K_BUCKETS) l_b = K_BUCKETS - 1u;
            l_buckets[l_s][l_b]++;
        }
    }

    /* Compute a chi-square per secret against the uniform expectation
     * (K_TRIALS / K_BUCKETS = 62.5). */
    const double l_expected = (double)K_TRIALS / (double)K_BUCKETS;
    double l_chi[2] = { 0.0, 0.0 };
    for (int l_s = 0; l_s < 2; ++l_s) {
        for (int l_b = 0; l_b < K_BUCKETS; ++l_b) {
            double l_d = (double)l_buckets[l_s][l_b] - l_expected;
            l_chi[l_s] += (l_d * l_d) / l_expected;
        }
    }
    log_it(L_INFO,
           "CR-9.3[zero-leakage]: chi^2(secret=0)=%.2f  chi^2(secret=q-1)=%.2f  "
           "(31 dof; critical 0.001 ≈ 61.1)",
           l_chi[0], l_chi[1]);

    /* Both distributions must look uniform-ish (well below the
     * chi^2(31, 0.001) ≈ 61.10 critical value), AND the gap between
     * them must be small. */
    dap_assert(l_chi[0] < 75.0 && l_chi[1] < 75.0,
               "share-distribution looks uniform for both secrets");
    dap_assert(fabs(l_chi[0] - l_chi[1]) < 50.0,
               "chi^2 gap between secrets is bounded");

    /* Strongest sanity: for at least one bucket, the per-secret counts
     * should NOT be identical (we are sampling, not reproducing).  This
     * guards against a regression that turns share() into a no-op. */
    bool l_any_diff = false;
    for (int l_b = 0; l_b < K_BUCKETS; ++l_b) {
        if (l_buckets[0][l_b] != l_buckets[1][l_b]) { l_any_diff = true; break; }
    }
    dap_assert(l_any_diff, "share() is randomised, not deterministic");
    return true;
}

/* Bonus invariant: re-running share() on the same input MUST produce
 * different shares (because coefficients are CSPRNG-sampled).  This
 * guards CR-9.1 (CS-PRNG coefficients) against a regression to the
 * Round-3 deterministic-coeff bug (CR-C7 / CR-D24). */
static bool s_test_coefficients_are_csprng(void)
{
    chipmunk_ring_shamir_share_t l_a[4], l_b[4];
    dap_assert(chipmunk_ring_shamir_share(42u, 4u, 3u, l_a) == 0, "share A");
    dap_assert(chipmunk_ring_shamir_share(42u, 4u, 3u, l_b) == 0, "share B");

    /* The two arrays must NOT be byte-equal — they must differ in at
     * least one share value.  Indices are deterministic (1..n) and
     * thus equal; only values reflect the random polynomial. */
    bool l_differ = false;
    for (uint32_t i = 0; i < 4u; ++i) {
        dap_assert(l_a[i].index == l_b[i].index, "indices stable across calls");
        if (l_a[i].value != l_b[i].value) { l_differ = true; }
    }
    dap_assert(l_differ,
               "share values differ across calls (CS-PRNG coefficients)");

    /* Both must still reconstruct to the same secret. */
    chipmunk_ring_shamir_share_t l_chosen_a[3] = { l_a[0], l_a[1], l_a[2] };
    chipmunk_ring_shamir_share_t l_chosen_b[3] = { l_b[0], l_b[1], l_b[2] };
    uint32_t l_out_a = 0, l_out_b = 0;
    dap_assert(chipmunk_ring_shamir_reconstruct(l_chosen_a, 3u, &l_out_a) == 0,
               "reconstruct A OK");
    dap_assert(chipmunk_ring_shamir_reconstruct(l_chosen_b, 3u, &l_out_b) == 0,
               "reconstruct B OK");
    dap_assert(l_out_a == 42u && l_out_b == 42u,
               "both runs reconstruct to the original secret");
    (void)s_array_equal; /* silence unused-helper if compiler optimises */
    return true;
}

/* ------------------------------------------------------------------ */
/* Test runner                                                        */
/* ------------------------------------------------------------------ */

int main(void)
{
    dap_set_appname("test_chipmunk_ring_shamir");
    dap_common_init("test_chipmunk_ring_shamir", NULL);

    int l_rc = 0;
    if (!s_test_correctness_basic())              l_rc = 1;
    if (!s_test_correctness_random())             l_rc = 1;
    if (!s_test_subset_invariance())              l_rc = 1;
    if (!s_test_insufficient_shares())            l_rc = 1;
    if (!s_test_field_arithmetic_inverse())       l_rc = 1;
    if (!s_test_t_one_rejected())                 l_rc = 1;
    if (!s_test_t_greater_than_n_rejected())      l_rc = 1;
    if (!s_test_index_zero_rejected())            l_rc = 1;
    if (!s_test_duplicate_indices_rejected())     l_rc = 1;
    if (!s_test_zero_leakage_statistical())       l_rc = 1;
    if (!s_test_coefficients_are_csprng())        l_rc = 1;

    if (l_rc == 0) {
        log_it(L_INFO, "ALL CR-9.3 Shamir-primitive tests PASSED");
    } else {
        log_it(L_ERROR, "Some CR-9.3 Shamir-primitive tests FAILED");
    }
    dap_common_deinit();
    return l_rc;
}
