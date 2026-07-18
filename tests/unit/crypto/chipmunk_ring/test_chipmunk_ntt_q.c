/*
 * test_chipmunk_ntt_q.c — E2E test for non-default q NTT operations.
 *
 * Tests that the Montgomery R=2^32 SIMD NTT works correctly with
 * q=4206593 (the SNARK modulus), not just the default CHIPMUNK_Q.
 *
 * Phase 9.12 gate test.
 */

#include <dap_common.h>
#include <dap_enc.h>
#include <dap_test.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "chipmunk/chipmunk_ntt.h"
#include "chipmunk/chipmunk_poly.h"
#include "chipmunk/chipmunk_field.h"

#define LOG_TAG "test_chipmunk_ntt_q"

/* SNARK modulus: q = 4206593, 2-adicity = 12 (supports N=512) */
#define TEST_Q  ((uint64_t)4206593ULL)

/* Deterministic PRNG for reproducible test data */
static uint64_t s_rng = 0x9E3779B97F4A7C15ULL;
static uint32_t s_rand32(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 7;
    s_rng ^= s_rng << 17;
    return (uint32_t)(s_rng >> 32);
}

/* T1: NTT round-trip — forward then inverse should recover original */
static bool s_test_ntt_roundtrip(void)
{
    chipmunk_ntt_ctx_t ctx;
    int rc = chipmunk_ntt_params_compute(&ctx, TEST_Q);
    dap_assert(rc == 0, "ntt_params_compute for q=4206593");

    for (int trial = 0; trial < 20; ++trial) {
        chipmunk_poly_t orig, work;
        for (int i = 0; i < CHIPMUNK_N; ++i) {
            orig.coeffs[i] = (int32_t)(s_rand32() % (uint32_t)TEST_Q);
        }
        work = orig;

        chipmunk_poly_ntt_q(&work, &ctx);
        chipmunk_poly_invntt_q(&work, &ctx);

        for (int i = 0; i < CHIPMUNK_N; ++i) {
            int32_t exp = orig.coeffs[i];
            if (exp > (int32_t)(TEST_Q / 2)) exp -= (int32_t)TEST_Q;
            dap_assert(work.coeffs[i] == exp, "NTT roundtrip coeff mismatch");
        }
    }

    chipmunk_ntt_ctx_free(&ctx);
    return true;
}

/* T2: NTT-based polynomial multiplication round-trip */
static bool s_test_ntt_mul_roundtrip(void)
{
    chipmunk_ntt_ctx_t ctx;
    int rc = chipmunk_ntt_params_compute(&ctx, TEST_Q);
    dap_assert(rc == 0, "ntt_params_compute");

    /* a * 1 = a (multiply by identity) */
    chipmunk_poly_t a, one, result;
    for (int i = 0; i < CHIPMUNK_N; ++i) {
        a.coeffs[i] = (int32_t)(s_rand32() % (uint32_t)TEST_Q);
    }
    memset(&one, 0, sizeof(one));
    one.coeffs[0] = 1;

    chipmunk_poly_t a_ntt = a, one_ntt = one;
    chipmunk_poly_ntt_q(&a_ntt, &ctx);
    chipmunk_poly_ntt_q(&one_ntt, &ctx);
    chipmunk_poly_mul_ntt_q(&result, &a_ntt, &one_ntt, TEST_Q);
    chipmunk_poly_invntt_q(&result, &ctx);

    for (int i = 0; i < CHIPMUNK_N; ++i) {
        int32_t exp = a.coeffs[i];
        if (exp > (int32_t)(TEST_Q / 2)) exp -= (int32_t)TEST_Q;
        dap_assert(result.coeffs[i] == exp, "a*1 should equal a");
    }

    chipmunk_ntt_ctx_free(&ctx);
    return true;
}

/* T3: Global NTT == per-q NTT for q=CHIPMUNK_Q (consistency check) */
static bool s_test_global_vs_perq(void)
{
    chipmunk_ntt_ctx_t ctx;
    int rc = chipmunk_ntt_params_compute(&ctx, (uint64_t)CHIPMUNK_Q);
    dap_assert(rc == 0, "ntt_params_compute for CHIPMUNK_Q");

    for (int trial = 0; trial < 5; ++trial) {
        chipmunk_poly_t a1, a2;
        for (int i = 0; i < CHIPMUNK_N; ++i) {
            a1.coeffs[i] = (int32_t)(s_rand32() % (uint32_t)CHIPMUNK_Q);
        }
        a2 = a1;

        chipmunk_poly_ntt(&a1);
        chipmunk_poly_ntt_q(&a2, &ctx);

        for (int i = 0; i < CHIPMUNK_N; ++i) {
            dap_assert(a1.coeffs[i] == a2.coeffs[i], "global vs per-q NTT mismatch");
        }
    }

    chipmunk_ntt_ctx_free(&ctx);
    return true;
}

int main(void)
{
    dap_set_appname("test_chipmunk_ntt_q");
    dap_common_init("test_chipmunk_ntt_q", NULL);
    dap_enc_init();

    int rc = 0;
    if (!s_test_ntt_roundtrip())     rc = 1;
    if (!s_test_ntt_mul_roundtrip()) rc = 1;
    if (!s_test_global_vs_perq())    rc = 1;

    if (rc == 0) {
        log_it(L_INFO, "=== ALL chipmunk NTT non-default q tests PASSED ===");
    }
    dap_common_deinit();
    return rc;
}
