/*
 * test_chipmunk_mring_subtractive.c — MRNG fold challenge set (G3.1 §9.3).
 *
 * CR-11.G Phase 7.7 / task_ac273cea.  Validates the Fiat-Shamir challenge
 * sampler over the subtractive set
 *
 *     S = F_{q⁶} \ {0}   (diagonal scalar elements of R_q^{(e)}),
 *
 * on which Option-B fold soundness rests (MRNG_G3_1_EXTENSION_SOUNDNESS.md
 * §2/§4).  The defining "subtractive" property is that EVERY pairwise
 * difference of distinct challenges is invertible in R_q^{(e)} — this is
 * what makes a single transcript extract a witness (no parallel
 * repetition).  Coverage:
 *
 *   T1  determinism: same (fs_hash, counter) ⇒ identical challenge;
 *       challenge is scalar, nonzero, and invertible.
 *   T2  distinctness: a large batch of challenges are pairwise distinct.
 *   T3  SUBTRACTIVE: every pairwise difference is invertible (scalar
 *       invert == 0), with a full ext-multiply spot-check that
 *       diff · diff⁻¹ = 1.
 */

#include <dap_common.h>
#include <dap_enc.h>
#include <dap_test.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "chipmunk/chipmunk_fq6_ext.h"
#include "chipmunk/chipmunk_poly.h"

#define LOG_TAG "test_chipmunk_mring_subtractive"

/* Batch size for the pairwise-difference sweep: N·(N−1)/2 differences. */
#define SUB_BATCH 200

static int32_t s_canon(int32_t a_v)
{
    int32_t r = a_v % (int32_t)CHIPMUNK_Q;
    if (r < 0) { r += (int32_t)CHIPMUNK_Q; }
    return r;
}

static bool s_ext_eq(const chipmunk_fq6_ext_t *a, const chipmunk_fq6_ext_t *b)
{
    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        for (int i = 0; i < CHIPMUNK_N; ++i) {
            if (s_canon(a->c[j].coeffs[i]) != s_canon(b->c[j].coeffs[i])) {
                return false;
            }
        }
    }
    return true;
}

static bool s_ext_is_one(const chipmunk_fq6_ext_t *a)
{
    chipmunk_fq6_ext_t one;
    chipmunk_fq6_ext_one(&one);
    return s_ext_eq(a, &one);
}

static void s_make_hash(uint8_t a_h[32], uint8_t a_salt)
{
    for (int i = 0; i < 32; ++i) {
        a_h[i] = (uint8_t)(0x11u * (uint8_t)(i + 1) ^ a_salt);
    }
}

/* T1 — determinism, scalar/nonzero/invertible. */
static bool s_test_determinism(void)
{
    uint8_t h[32];
    s_make_hash(h, 0x5Au);

    for (uint32_t ctr = 0; ctr < 8u; ++ctr) {
        chipmunk_fq6_ext_t a, b;
        dap_assert(chipmunk_fq6_ext_sample_challenge(&a, h, ctr) == 0,
                   "sample challenge a");
        dap_assert(chipmunk_fq6_ext_sample_challenge(&b, h, ctr) == 0,
                   "sample challenge b (replay)");
        dap_assert(s_ext_eq(&a, &b),
                   "same (fs_hash, counter) ⇒ identical challenge");

        /* challenge must be scalar (diagonal F_{q⁶}) */
        int32_t coords[CHIPMUNK_FQ6_EXT_DEG];
        dap_assert(chipmunk_fq6_ext_scalar_get(coords, &a) == 0,
                   "challenge is a scalar element");
        bool nonzero = false;
        for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
            if (coords[j] != 0) { nonzero = true; }
        }
        dap_assert(nonzero, "challenge is nonzero (zero is rejection-sampled)");

        /* and invertible (since nonzero in a field) */
        chipmunk_fq6_ext_t inv, prod;
        dap_assert(chipmunk_fq6_ext_scalar_invert(&inv, &a) == 0,
                   "challenge invertible");
        dap_assert(chipmunk_fq6_ext_mul(&prod, &a, &inv) == 0, "mul");
        dap_assert(s_ext_is_one(&prod), "x·x⁻¹ == 1");
    }

    /* different fs_hash ⇒ different challenge (overwhelming probability) */
    uint8_t h2[32];
    s_make_hash(h2, 0xA5u);
    chipmunk_fq6_ext_t c1, c2;
    dap_assert(chipmunk_fq6_ext_sample_challenge(&c1, h, 0u) == 0, "sample");
    dap_assert(chipmunk_fq6_ext_sample_challenge(&c2, h2, 0u) == 0, "sample");
    dap_assert(!s_ext_eq(&c1, &c2),
               "distinct fs_hash ⇒ distinct challenge");
    return true;
}

/* T2 + T3 — distinctness and the subtractive property over a batch. */
static bool s_test_subtractive(void)
{
    uint8_t h[32];
    s_make_hash(h, 0x33u);

    static chipmunk_fq6_ext_t s_chal[SUB_BATCH];
    static int32_t s_coords[SUB_BATCH][CHIPMUNK_FQ6_EXT_DEG];
    for (int i = 0; i < SUB_BATCH; ++i) {
        dap_assert(chipmunk_fq6_ext_sample_challenge(&s_chal[i], h,
                                                       (uint32_t)i) == 0,
                   "sample batch challenge");
        dap_assert(chipmunk_fq6_ext_scalar_get(s_coords[i], &s_chal[i]) == 0,
                   "batch challenge is scalar");
    }

    uint64_t l_pairs = 0;
    uint32_t l_full_checks = 0;
    for (int i = 0; i < SUB_BATCH; ++i) {
        for (int k = i + 1; k < SUB_BATCH; ++k) {
            /* distinctness */
            bool l_equal = true;
            for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
                if (s_coords[i][j] != s_coords[k][j]) { l_equal = false; break; }
            }
            dap_assert(!l_equal, "batch challenges are pairwise distinct");

            /* subtractive: diff = c_i − c_k must be invertible */
            chipmunk_fq6_ext_t l_diff, l_inv;
            dap_assert(chipmunk_fq6_ext_sub(&l_diff, &s_chal[i], &s_chal[k]) == 0,
                       "sub");
            const int rc = chipmunk_fq6_ext_scalar_invert(&l_inv, &l_diff);
            dap_assert(rc == 0,
                       "EVERY pairwise difference must be invertible "
                       "(subtractive-set property)");
            ++l_pairs;

            /* full ext-multiply spot-check on a sparse sample */
            if (((i * 7 + k) % 997) == 0 && l_full_checks < 16u) {
                chipmunk_fq6_ext_t l_prod;
                dap_assert(chipmunk_fq6_ext_mul(&l_prod, &l_diff, &l_inv) == 0,
                           "mul");
                dap_assert(s_ext_is_one(&l_prod),
                           "diff · diff⁻¹ == 1 (full R_q^{(e)} multiply)");
                ++l_full_checks;
            }
        }
    }
    log_it(L_INFO,
           "MRNG G3.1 §9.3: %llu pairwise differences over %d challenges all "
           "invertible (%u verified by full R_q^{(e)} multiply); subtractive "
           "set confirmed",
           (unsigned long long)l_pairs, SUB_BATCH, l_full_checks);
    dap_assert(l_full_checks >= 4u, "must full-verify ≥4 difference inverses");
    return true;
}

int main(void)
{
    dap_set_appname("test_chipmunk_mring_subtractive");
    dap_common_init("test_chipmunk_mring_subtractive", NULL);
    /* Initialise crypto subsystem (SIMD dispatch, chipmunk, etc.) */
    dap_enc_init();

    int rc = 0;
    if (!s_test_determinism()) rc = 1;
    if (!s_test_subtractive()) rc = 1;

    if (rc == 0) {
        log_it(L_INFO,
               "MRNG G3.1 §9.3 subtractive-set challenge tests PASSED "
               "(deterministic FS sampler, nonzero/invertible challenges, "
               "all pairwise differences invertible)");
    }
    dap_common_deinit();
    return rc;
}
