/*
 * test_chipmunk_fri_ntt.c — Unit tests for 2048-point FRI NTT.
 *
 * Phase 9.2: Forward/inverse identity, known polynomial evaluation,
 * coset-shifted domain, bit-reversal, linearity.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <dap_common.h>
#include <dap_test.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "sig/chipmunk/chipmunk_fri_ntt.h"
#include "sig/chipmunk/chipmunk_field.h"
#include "sig/chipmunk/chipmunk_poly.h"

static chipmunk_fri_ntt_ctx_t s_ctx;

#define LOG_TAG "test_chipmunk_fri_ntt"

/* ========================================================================= */
/* Test: forward/inverse round-trip (constant polynomial)                     */
/* ========================================================================= */

static void test_ntt_roundtrip_constant(void)
{
    int l_rc = chipmunk_fri_ntt_ctx_init(&s_ctx, (uint64_t)CHIPMUNK_Q, CHIPMUNK_FRI_NTT_LOG);
    dap_assert(l_rc == 0, "FRI NTT init OK");

    /* f(X) = 5  →  f(omega^k) = 5 for all k */
    int32_t a[CHIPMUNK_FRI_NTT_SIZE];
    memset(a, 0, sizeof(a));
    a[0] = 5;

    chipmunk_fri_ntt_forward_q(a, &s_ctx);

    /* All evaluations should be 5 */
    unsigned int l_ok = 1;
    for (unsigned int i = 0; i < CHIPMUNK_FRI_NTT_SIZE; ++i) {
        if (a[i] != 5) {
            char l_msg[64];
            snprintf(l_msg, sizeof(l_msg), "NTT(const=5)[%u] = %d, expected 5", i, a[i]);
            dap_assert(0, l_msg);
            l_ok = 0;
            return;
        }
    }
    dap_assert(l_ok, "forward NTT of constant 5 is all-5");

    /* Inverse should recover [5, 0, 0, ...] */
    chipmunk_fri_ntt_inverse_q(a, &s_ctx);
    dap_assert(a[0] == 5, "invNTT recovers a[0] = 5");

    l_ok = 1;
    for (unsigned int i = 1; i < CHIPMUNK_FRI_NTT_SIZE; ++i) {
        if (a[i] != 0) {
            char l_msg[64];
            snprintf(l_msg, sizeof(l_msg), "invNTT[%u] = %d, expected 0", i, a[i]);
            dap_assert(0, l_msg);
            l_ok = 0;
            return;
        }
    }
    dap_assert(l_ok, "invNTT recovers zeros for constant poly");
}

/* ========================================================================= */
/* Test: forward/inverse round-trip (linear polynomial)                      */
/* ========================================================================= */

static void test_ntt_roundtrip_linear(void)
{
    int32_t a[CHIPMUNK_FRI_NTT_SIZE];
    memset(a, 0, sizeof(a));
    a[0] = 7;
    a[1] = 3;

    int32_t orig[CHIPMUNK_FRI_NTT_SIZE];
    memcpy(orig, a, sizeof(a));

    chipmunk_fri_ntt_forward_q(a, &s_ctx);
    chipmunk_fri_ntt_inverse_q(a, &s_ctx);

    unsigned int l_ok = 1;
    for (unsigned int i = 0; i < CHIPMUNK_FRI_NTT_SIZE; ++i) {
        if (a[i] != orig[i]) {
            char l_msg[64];
            snprintf(l_msg, sizeof(l_msg), "roundtrip[%u] = %d, expected %d", i, a[i], orig[i]);
            dap_assert(0, l_msg);
            l_ok = 0;
            return;
        }
    }
    dap_assert(l_ok, "NTT/invNTT round-trip for linear poly");
}

/* ========================================================================= */
/* Test: forward/inverse round-trip (degree-511 polynomial)                  */
/* ========================================================================= */

static void test_ntt_roundtrip_degree511(void)
{
    int32_t a[CHIPMUNK_FRI_NTT_SIZE];
    memset(a, 0, sizeof(a));

    for (unsigned int i = 0; i < 512; ++i) {
        a[i] = (int32_t)(((uint32_t)(i * 1337 + 42)) % (uint32_t)CHIPMUNK_Q);
    }

    int32_t orig[CHIPMUNK_FRI_NTT_SIZE];
    memcpy(orig, a, sizeof(a));

    chipmunk_fri_ntt_forward_q(a, &s_ctx);
    chipmunk_fri_ntt_inverse_q(a, &s_ctx);

    unsigned int l_ok = 1;
    for (unsigned int i = 0; i < CHIPMUNK_FRI_NTT_SIZE; ++i) {
        if (a[i] != orig[i]) {
            char l_msg[64];
            snprintf(l_msg, sizeof(l_msg), "roundtrip_d511[%u] = %d, expected %d", i, a[i], orig[i]);
            dap_assert(0, l_msg);
            l_ok = 0;
            return;
        }
    }
    dap_assert(l_ok, "NTT/invNTT round-trip for degree-511 poly");
}

/* ========================================================================= */
/* Test: known polynomial evaluation (f(X) = X)                              */
/* Output in natural order: a[k] = f(omega^k)                                */
/* ========================================================================= */

static void test_ntt_eval_x(void)
{
    int32_t a[CHIPMUNK_FRI_NTT_SIZE];
    memset(a, 0, sizeof(a));
    a[1] = 1;  /* f(X) = X */

    chipmunk_fri_ntt_forward_q(a, &s_ctx);

    /* a[k] = omega^k in natural order */
    int32_t l_omega = chipmunk_field_omega_2048();
    int32_t l_val = 1;

    unsigned int l_ok = 1;
    for (unsigned int k = 0; k < CHIPMUNK_FRI_NTT_SIZE; ++k) {
        if (a[k] != l_val) {
            char l_msg[80];
            snprintf(l_msg, sizeof(l_msg),
                     "NTT(X)[%u] = %d, expected omega^%u = %d",
                     k, a[k], k, l_val);
            dap_assert(0, l_msg);
            l_ok = 0;
            return;
        }
        l_val = chipmunk_mod_q_q((int64_t)l_val * (int64_t)l_omega, (uint64_t)CHIPMUNK_Q);
    }
    dap_assert(l_ok, "NTT(X) = [omega^0, omega^1, ..., omega^{2047}] natural order");
}

/* ========================================================================= */
/* Test: linearity (NTT(a+b) == NTT(a) + NTT(b))                             */
/* ========================================================================= */

static void test_ntt_linearity(void)
{
    int32_t a[CHIPMUNK_FRI_NTT_SIZE], b[CHIPMUNK_FRI_NTT_SIZE], c[CHIPMUNK_FRI_NTT_SIZE];
    memset(a, 0, sizeof(a));
    memset(b, 0, sizeof(b));

    a[0] = 10; a[3] = 7;
    b[1] = 5;  b[3] = 20;

    for (unsigned int i = 0; i < CHIPMUNK_FRI_NTT_SIZE; ++i) {
        c[i] = chipmunk_mod_q_q((int64_t)a[i] + (int64_t)b[i], (uint64_t)CHIPMUNK_Q);
    }

    chipmunk_fri_ntt_forward_q(a, &s_ctx);
    chipmunk_fri_ntt_forward_q(b, &s_ctx);
    chipmunk_fri_ntt_forward_q(c, &s_ctx);

    unsigned int l_ok = 1;
    for (unsigned int i = 0; i < CHIPMUNK_FRI_NTT_SIZE; ++i) {
        int32_t l_sum = chipmunk_mod_q_q((int64_t)a[i] + (int64_t)b[i], (uint64_t)CHIPMUNK_Q);
        if (c[i] != l_sum) {
            char l_msg[64];
            snprintf(l_msg, sizeof(l_msg), "linearity[%u] = %d, expected %d", i, c[i], l_sum);
            dap_assert(0, l_msg);
            l_ok = 0;
            return;
        }
    }
    dap_assert(l_ok, "NTT(a+b) == NTT(a) + NTT(b)");
}

/* ========================================================================= */
/* Test: coset NTT — evaluates at g*omega^k                                   */
/* ========================================================================= */

static void test_ntt_coset_roundtrip(void)
{
    int32_t g = 7;

    int32_t b[CHIPMUNK_FRI_NTT_SIZE];
    memset(b, 0, sizeof(b));
    b[0] = 42;
    b[1] = 1337;
    b[2] = 999;

    chipmunk_fri_ntt_coset_forward_q(b, g, &s_ctx);

    /* Verify: b[k] = f(g * omega^k) in natural order.
     * b[0] = f(g*omega^0) = f(g) = 42 + 1337*g + 999*g^2 */
    int32_t g2 = chipmunk_mod_q_q((int64_t)g * (int64_t)g, (uint64_t)CHIPMUNK_Q);
    int64_t l_f_at_g = 42LL + 1337LL * g + 999LL * g2;
    int32_t l_expected = chipmunk_mod_q_q(l_f_at_g, (uint64_t)CHIPMUNK_Q);
    dap_assert(b[0] == l_expected, "coset NTT: f(g*omega^0) correct");

    /* b[1] = f(g*omega) = 42 + 1337*g*omega + 999*(g*omega)^2 */
    int32_t l_omega = chipmunk_field_omega_2048();
    int32_t g_omega = chipmunk_mod_q_q((int64_t)g * (int64_t)l_omega, (uint64_t)CHIPMUNK_Q);
    int32_t g_omega2 = chipmunk_mod_q_q((int64_t)g_omega * (int64_t)g_omega, (uint64_t)CHIPMUNK_Q);
    l_f_at_g = 42LL + 1337LL * g_omega + 999LL * g_omega2;
    l_expected = chipmunk_mod_q_q(l_f_at_g, (uint64_t)CHIPMUNK_Q);
    dap_assert(b[1] == l_expected, "coset NTT: f(g*omega^1) correct");

    /* g=1 should give same as plain NTT */
    int32_t d[CHIPMUNK_FRI_NTT_SIZE], e[CHIPMUNK_FRI_NTT_SIZE];
    memset(d, 0, sizeof(d));
    d[0] = 42; d[1] = 1337; d[2] = 999;

    chipmunk_fri_ntt_coset_forward_q(d, 1, &s_ctx);

    memset(e, 0, sizeof(e));
    e[0] = 42; e[1] = 1337; e[2] = 999;

    chipmunk_fri_ntt_forward_q(e, &s_ctx);

    unsigned int l_ok = 1;
    for (unsigned int i = 0; i < CHIPMUNK_FRI_NTT_SIZE; ++i) {
        if (d[i] != e[i]) {
            char l_msg[64];
            snprintf(l_msg, sizeof(l_msg), "coset(g=1)[%u] = %d, expected %d", i, d[i], e[i]);
            dap_assert(0, l_msg);
            l_ok = 0;
            return;
        }
    }
    dap_assert(l_ok, "coset(g=1) == plain NTT");
}

/* ========================================================================= */
/* Test: domain generation                                                     */
/* ========================================================================= */

static void test_ntt_domain(void)
{
    int32_t domain[CHIPMUNK_FRI_NTT_SIZE];
    chipmunk_fri_ntt_domain_q(domain, &s_ctx);

    int32_t l_omega = chipmunk_field_omega_2048();
    int32_t l_val = 1;
    unsigned int l_ok = 1;

    for (unsigned int i = 0; i < CHIPMUNK_FRI_NTT_SIZE; ++i) {
        if (domain[i] != l_val) {
            char l_msg[64];
            snprintf(l_msg, sizeof(l_msg), "domain[%u] = %d, expected %d", i, domain[i], l_val);
            dap_assert(0, l_msg);
            l_ok = 0;
            return;
        }
        l_val = chipmunk_mod_q_q((int64_t)l_val * (int64_t)l_omega, (uint64_t)CHIPMUNK_Q);
    }
    dap_assert(l_ok, "domain generation correct");

    dap_assert(domain[0] == 1, "domain[0] == 1");
    int32_t l_omega_inv = chipmunk_field_omega_2048_inv();
    dap_assert(domain[CHIPMUNK_FRI_NTT_SIZE - 1] == l_omega_inv,
               "domain[2047] == omega^{-1}");
}

/* ========================================================================= */
/* Test: bit-reversal                                                          */
/* ========================================================================= */

static void test_ntt_bitreverse(void)
{
    dap_assert(chipmunk_fri_ntt_brv11(0) == 0, "brv11(0) == 0");
    dap_assert(chipmunk_fri_ntt_brv11(1) == 1024, "brv11(1) == 1024");
    dap_assert(chipmunk_fri_ntt_brv11(1024) == 1, "brv11(1024) == 1");
    dap_assert(chipmunk_fri_ntt_brv11(2047) == 2047, "brv11(2047) == 2047");

    unsigned int l_ok = 1;
    for (uint32_t x = 0; x < 2048; ++x) {
        if (chipmunk_fri_ntt_brv11(chipmunk_fri_ntt_brv11(x)) != x) {
            char l_msg[64];
            snprintf(l_msg, sizeof(l_msg), "brv11(brv11(%u)) != %u", x, x);
            dap_assert(0, l_msg);
            l_ok = 0;
            return;
        }
    }
    dap_assert(l_ok, "brv11(brv11(x)) == x for all x in [0,2048)");
}

/* ========================================================================= */
/* Main                                                                       */
/* ========================================================================= */

int main(void)
{
    dap_set_appname("test_chipmunk_fri_ntt");
    dap_common_init("test_chipmunk_fri_ntt", NULL);

    test_ntt_roundtrip_constant();
    test_ntt_roundtrip_linear();
    test_ntt_roundtrip_degree511();
    test_ntt_eval_x();
    test_ntt_linearity();
    test_ntt_coset_roundtrip();
    test_ntt_domain();
    test_ntt_bitreverse();

    log_it(L_INFO, "=== ALL chipmunk_fri_ntt tests PASSED (Phase 9.2: 2048-point NTT) ===");
    dap_common_deinit();
    return 0;
}
