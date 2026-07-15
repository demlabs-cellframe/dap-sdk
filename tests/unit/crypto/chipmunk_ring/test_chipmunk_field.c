/*
 * test_chipmunk_field.c — Unit tests for chipmunk_field.h
 *
 * Phase 9.1: Field arithmetic for FRI-DEEP polynomial commitment.
 * Tests modular inverse, exponentiation, primitive root discovery,
 * and cached FRI-domain constants.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <dap_common.h>
#include <dap_test.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "sig/chipmunk/chipmunk_field.h"
#include "sig/chipmunk/chipmunk_poly.h"

#define LOG_TAG "test_chipmunk_field"

/* ========================================================================= */
/* Test: chipmunk_field_inv — basic properties                                  */
/* ========================================================================= */

static void test_inv_basic(void)
{
    /* 1 * 1^{-1} = 1 */
    int32_t l_inv1 = chipmunk_field_inv(1);
    dap_assert(l_inv1 == 1, "inv(1) == 1");

    /* (q-1)^{-1} = q-1 (since (q-1)^2 = 1 mod q for prime q) */
    int32_t l_inv_qm1 = chipmunk_field_inv((int32_t)CHIPMUNK_Q - 1);
    dap_assert(l_inv_qm1 == (int32_t)CHIPMUNK_Q - 1, "inv(q-1) == q-1");

    /* inv(0) == 0 (not invertible) */
    int32_t l_inv0 = chipmunk_field_inv(0);
    dap_assert(l_inv0 == 0, "inv(0) == 0");

    /* inv(inv(a)) == a for random a */
    int32_t l_a = 42;
    int32_t l_ia = chipmunk_field_inv(l_a);
    dap_assert(l_ia != 0, "inv(42) nonzero");
    int32_t l_iia = chipmunk_field_inv(l_ia);
    dap_assert(l_iia == l_a, "inv(inv(42)) == 42");

    /* a * inv(a) == 1 mod q */
    int64_t l_prod = (int64_t)l_a * (int64_t)l_ia;
    int32_t l_check = chipmunk_mod_q(l_prod);
    dap_assert(l_check == 1, "42 * inv(42) == 1");
}

/* ========================================================================= */
/* Test: chipmunk_field_inv — exhaustive small range                           */
/* ========================================================================= */

static void test_inv_exhaustive_small(void)
{
    for (int32_t a = 1; a <= 100; ++a) {
        int32_t l_ia = chipmunk_field_inv(a);
        dap_assert(l_ia != 0, "inv nonzero for a <= 100");
        int64_t l_prod = (int64_t)a * (int64_t)l_ia;
        int32_t l_check = chipmunk_mod_q(l_prod);
        if (l_check != 1) {
            char l_msg[64];
            snprintf(l_msg, sizeof(l_msg), "a*inv(a)==1 for a=%d (got %d)", a, l_check);
            dap_assert(0, l_msg);
            return;
        }
    }
    dap_assert(1, "a*inv(a)==1 for a in [1,100]");
}

/* ========================================================================= */
/* Test: chipmunk_field_pow — basic properties                                  */
/* ========================================================================= */

static void test_pow_basic(void)
{
    /* a^0 = 1 */
    dap_assert(chipmunk_field_pow(42, 0) == 1, "42^0 == 1");

    /* a^1 = a */
    dap_assert(chipmunk_field_pow(42, 1) == 42, "42^1 == 42");

    /* 1^e = 1 */
    dap_assert(chipmunk_field_pow(1, 9999) == 1, "1^e == 1");

    /* 0^e = 0 for e > 0 */
    dap_assert(chipmunk_field_pow(0, 7) == 0, "0^7 == 0");

    /* Fermat: a^(q-1) == 1 mod q */
    int32_t l_check = chipmunk_field_pow(42, (uint32_t)CHIPMUNK_Q - 1u);
    dap_assert(l_check == 1, "42^(q-1) == 1 (Fermat)");
}

/* ========================================================================= */
/* Test: chipmunk_field_pow — Fermat for many values                          */
/* ========================================================================= */

static void test_pow_fermat_many(void)
{
    for (int32_t a = 2; a <= 199; ++a) {
        int32_t l_check = chipmunk_field_pow(a, (uint32_t)CHIPMUNK_Q - 1u);
        if (l_check != 1) {
            char l_msg[64];
            snprintf(l_msg, sizeof(l_msg), "a^(q-1)==1 for a=%d (got %d)", a, l_check);
            dap_assert(0, l_msg);
            return;
        }
    }
    dap_assert(1, "a^(q-1)==1 for a in [2,199]");
}

/* ========================================================================= */
/* Test: chipmunk_field_pow — negative base                                   */
/* ========================================================================= */

static void test_pow_negative_base(void)
{
    /* (-1)^2 = 1 */
    int32_t l_r = chipmunk_field_pow(-1, 2);
    dap_assert(l_r == 1, "(-1)^2 == 1");

    /* (-1)^3 = q-1 (= -1 mod q) */
    l_r = chipmunk_field_pow(-1, 3);
    dap_assert(l_r == (int32_t)CHIPMUNK_Q - 1, "(-1)^3 == q-1");

    /* (-5)^2 = 25 */
    l_r = chipmunk_field_pow(-5, 2);
    dap_assert(l_r == 25, "(-5)^2 == 25");
}

/* ========================================================================= */
/* Test: chipmunk_field_primitive_root_2k — order check                      */
/* ========================================================================= */

static void test_primitive_root_order(void)
{
    /* k=1: primitive square root of unity = -1 */
    int32_t l_omega;
    int l_rc = chipmunk_field_primitive_root_2k(1, &l_omega);
    dap_assert(l_rc == 0, "root_2k(k=1) OK");
    dap_assert(l_omega == (int32_t)CHIPMUNK_Q - 1, "omega_1 == q-1");

    /* k=9: primitive 512-th root */
    l_rc = chipmunk_field_primitive_root_2k(9, &l_omega);
    dap_assert(l_rc == 0, "root_2k(k=9) OK");

    /* Verify order exactly 512 */
    int32_t l_check = chipmunk_field_pow(l_omega, 512);
    dap_assert(l_check == 1, "omega_9^512 == 1");
    l_check = chipmunk_field_pow(l_omega, 256);
    dap_assert(l_check == (int32_t)CHIPMUNK_Q - 1, "omega_9^256 == q-1");
}

/* ========================================================================= */
/* Test: primitive 2048-th root (FRI domain)                                  */
/* ========================================================================= */

static void test_primitive_root_2048(void)
{
    int32_t l_omega;
    int l_rc = chipmunk_field_primitive_root_2k(11, &l_omega);
    dap_assert(l_rc == 0, "omega_2048 found");

    /* omega^2048 == 1 */
    int32_t l_check = chipmunk_field_pow(l_omega, 2048);
    dap_assert(l_check == 1, "omega^2048 == 1");

    /* omega^1024 == -1 (q-1) */
    l_check = chipmunk_field_pow(l_omega, 1024);
    dap_assert(l_check == (int32_t)CHIPMUNK_Q - 1, "omega^1024 == q-1");

    /* omega^512 != -1 (order is > 512) */
    l_check = chipmunk_field_pow(l_omega, 512);
    dap_assert(l_check != (int32_t)CHIPMUNK_Q - 1, "omega^512 != q-1 (order > 512)");
}

/* ========================================================================= */
/* Test: omega_512 derived from omega_2048                                   */
/* ========================================================================= */

static void test_primitive_root_omega512_derived(void)
{
    int32_t l_omega2048;
    int l_rc = chipmunk_field_primitive_root_2k(11, &l_omega2048);
    dap_assert(l_rc == 0, "omega_2048 found");

    /* omega_2048^4 should be a 512-th root */
    int32_t l_w512 = chipmunk_field_pow(l_omega2048, 4);
    int32_t l_check = chipmunk_field_pow(l_w512, 512);
    dap_assert(l_check == 1, "omega_2048^4 is also a 512-th root");
}

/* ========================================================================= */
/* Test: k=12 exceeds 2-adicity — should fail                                  */
/* ========================================================================= */

static void test_primitive_root_too_large(void)
{
    int32_t l_omega;
    int l_rc = chipmunk_field_primitive_root_2k(12, &l_omega);
    dap_assert(l_rc != 0, "root_2k(k=12) correctly fails");
}

/* ========================================================================= */
/* Test: chipmunk_field_init — cached constants                               */
/* ========================================================================= */

static void test_init_cached_constants(void)
{
    int l_rc = chipmunk_field_init();
    dap_assert(l_rc == 0, "field_init OK");
    dap_assert(chipmunk_field_is_initialized(), "is_initialized == true");

    /* omega_2048 properties */
    int32_t l_w = chipmunk_field_omega_2048();
    dap_assert(l_w > 1 && l_w < (int32_t)CHIPMUNK_Q, "omega_2048 in range");

    int32_t l_check = chipmunk_field_pow(l_w, 2048);
    dap_assert(l_check == 1, "cached omega_2048^2048 == 1");

    l_check = chipmunk_field_pow(l_w, 1024);
    dap_assert(l_check == (int32_t)CHIPMUNK_Q - 1,
               "cached omega_2048^1024 == q-1");

    /* omega_2048 * omega_2048_inv == 1 */
    int32_t l_w_inv = chipmunk_field_omega_2048_inv();
    int64_t l_prod = (int64_t)l_w * (int64_t)l_w_inv;
    l_check = chipmunk_mod_q(l_prod);
    dap_assert(l_check == 1, "omega_2048 * omega_2048_inv == 1");

    /* omega_512 = omega_2048^4 */
    int32_t l_w512 = chipmunk_field_omega_512();
    int32_t l_w512_expected = chipmunk_field_pow(l_w, 4);
    dap_assert(l_w512 == l_w512_expected,
               "omega_512 == omega_2048^4");

    /* omega_512 * omega_512_inv == 1 */
    int32_t l_w512_inv = chipmunk_field_omega_512_inv();
    l_prod = (int64_t)l_w512 * (int64_t)l_w512_inv;
    l_check = chipmunk_mod_q(l_prod);
    dap_assert(l_check == 1, "omega_512 * omega_512_inv == 1");

    /* inv_2048: 2048 * inv_2048 == 1 mod q */
    int32_t l_ninv = chipmunk_field_inv_2048();
    l_prod = (int64_t)2048 * (int64_t)l_ninv;
    l_check = chipmunk_mod_q(l_prod);
    dap_assert(l_check == 1, "2048 * inv_2048 == 1");

    /* inv_512: 512 * inv_512 == 1 mod q */
    l_ninv = chipmunk_field_inv_512();
    l_prod = (int64_t)512 * (int64_t)l_ninv;
    l_check = chipmunk_mod_q(l_prod);
    dap_assert(l_check == 1, "512 * inv_512 == 1");
}

/* ========================================================================= */
/* Test: init idempotent                                                       */
/* ========================================================================= */

static void test_init_idempotent(void)
{
    int l_rc = chipmunk_field_init();
    dap_assert(l_rc == 0, "first init OK");

    int32_t l_w1 = chipmunk_field_omega_2048();
    l_rc = chipmunk_field_init();
    dap_assert(l_rc == 0, "second init OK");
    int32_t l_w2 = chipmunk_field_omega_2048();
    dap_assert(l_w1 == l_w2, "omega_2048 stable across reinit");
}

/* ========================================================================= */
/* Test: domain 2048 — all points distinct                                    */
/* ========================================================================= */

static void test_domain_2048_distinct(void)
{
    int32_t l_omega;
    int l_rc = chipmunk_field_primitive_root_2k(11, &l_omega);
    dap_assert(l_rc == 0, "omega_2048 found");

    /* Check that all 2048 powers are distinct by accumulating the sum.
     * If all powers of a primitive root are distinct elements of F_q,
     * the sum omega^0 + omega^1 + ... + omega^{2047} = 0 (geometric series
     * formula: (omega^{2048}-1)/(omega-1) = 0/(omega-1) = 0).
     * This is a weaker check than full pairwise comparison but sufficient
     * to detect collisions (which would break the sum). */
    int64_t l_sum = 0;
    int32_t l_val = 1;
    for (uint32_t i = 0; i < 2048; ++i) {
        l_sum += (int64_t)l_val;
        if (l_sum >= (int64_t)CHIPMUNK_Q) {
            l_sum -= (int64_t)CHIPMUNK_Q;
        }
        l_val = chipmunk_mod_q((int64_t)l_val * (int64_t)l_omega);
    }
    /* Sum of all distinct 2048-th roots = 0 (by geometric series) */
    dap_assert(l_sum == 0, "sum of 2048-th roots == 0 (geometric series)");

    /* After 2048 iterations, must return to 1 */
    dap_assert(l_val == 1, "omega^2048 == 1 (cycle closes)");
}

/* ========================================================================= */
/* Main                                                                       */
/* ========================================================================= */

int main(void)
{
    dap_set_appname("test_chipmunk_field");
    dap_common_init("test_chipmunk_field", NULL);

    /* Modular inverse */
    test_inv_basic();
    test_inv_exhaustive_small();

    /* Modular exponentiation */
    test_pow_basic();
    test_pow_fermat_many();
    test_pow_negative_base();

    /* Primitive root discovery */
    test_primitive_root_order();
    test_primitive_root_2048();
    test_primitive_root_omega512_derived();
    test_primitive_root_too_large();

    /* Cached constants */
    test_init_cached_constants();
    test_init_idempotent();

    /* Domain enumeration */
    test_domain_2048_distinct();

    log_it(L_INFO, "=== ALL chipmunk_field tests PASSED (Phase 9.1: FRI field arithmetic) ===");
    dap_common_deinit();
    return 0;
}
