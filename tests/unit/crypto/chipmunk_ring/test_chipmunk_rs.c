/*
 * test_chipmunk_rs.c — Unit tests for Reed-Solomon encoding via coset NTT.
 *
 * Phase 9.5 of the FRI-DEEP polynomial commitment scheme.
 *
 * Test strategy:
 *   - Python reference vectors for constant, linear, quadratic, specific, dense polynomials
 *   - Roundtrip encode→interpolate for all test polynomials
 *   - Horner evaluation cross-check against NTT-based encoding
 *   - Correctness of chipmunk_rs_eval (naive polynomial evaluation)
 *   - Edge cases: zero polynomial, single-coefficient, aliasing
 *   - Injectivity: distinct polynomials yield distinct codewords
 *   - Codeword range [0, q)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <dap_common.h>
#include <dap_test.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <chipmunk_field.h>
#include <chipmunk_fri_ntt.h>
#include <chipmunk_rs.h>

#define LOG_TAG "test_chipmunk_rs"

/* q = 3168257 */
#define Q  CHIPMUNK_Q

/* ========================================================================
 * Test 1: Zero polynomial — all evaluations should be 0.
 * ======================================================================== */
static void test_rs_zero_poly(void)
{
    int32_t poly[512];
    int32_t codeword[2048];

    memset(poly, 0, sizeof(poly));
    int rc = chipmunk_rs_encode(codeword, poly);
    dap_assert(rc == 0, "zero poly encode rc");

    for (int i = 0; i < 2048; ++i) {
        dap_assert(codeword[i] == 0, "zero poly eval == 0");
    }

    /* Roundtrip */
    int32_t recovered[512];
    rc = chipmunk_rs_interpolate(recovered, codeword);
    dap_assert(rc == 0, "zero poly interp rc");
    for (int i = 0; i < 512; ++i) {
        dap_assert(recovered[i] == 0, "zero poly roundtrip coeff");
    }
}

/* ========================================================================
 * Test 2: Constant polynomial f(x) = 42.
 * All 2048 evaluations must equal 42.
 * ======================================================================== */
static void test_rs_constant_poly(void)
{
    int32_t poly[512];
    int32_t codeword[2048];

    memset(poly, 0, sizeof(poly));
    poly[0] = 42;
    int rc = chipmunk_rs_encode(codeword, poly);
    dap_assert(rc == 0, "const poly encode rc");

    for (int i = 0; i < 2048; ++i) {
        dap_assert(codeword[i] == 42, "const poly eval == 42");
    }

    /* Roundtrip */
    int32_t recovered[512];
    rc = chipmunk_rs_interpolate(recovered, codeword);
    dap_assert(rc == 0, "const poly interp rc");
    dap_assert(recovered[0] == 42, "const poly roundtrip c0");
    for (int i = 1; i < 512; ++i) {
        dap_assert(recovered[i] == 0, "const poly roundtrip ci");
    }
}

/* ========================================================================
 * Test 3: Linear polynomial f(x) = x.
 * evals[k] = coset_g * omega^k.
 * Python reference: omega=1550232, coset_g=3.
 * ======================================================================== */
static void test_rs_linear_poly(void)
{
    const int32_t expected[] = {
        3,        /* k=0:  3 * omega^0 */
        1482439,  /* k=1:  3 * omega^1 */
        646585,   /* k=2:  3 * omega^2 */
        2047808,  /* k=511 */
        3168254,  /* k=1024 */
        2160479   /* k=2047 */
    };
    const int k_indices[] = {0, 1, 2, 511, 1024, 2047};

    int32_t poly[512];
    int32_t codeword[2048];

    memset(poly, 0, sizeof(poly));
    poly[1] = 1;  /* f(x) = x */
    int rc = chipmunk_rs_encode(codeword, poly);
    dap_assert(rc == 0, "linear poly encode rc");

    for (int t = 0; t < 6; ++t) {
        dap_assert(codeword[k_indices[t]] == expected[t],
                   "linear poly eval ref");
    }

    /* Roundtrip */
    int32_t recovered[512];
    rc = chipmunk_rs_interpolate(recovered, codeword);
    dap_assert(rc == 0, "linear poly interp rc");
    dap_assert(recovered[0] == 0, "linear roundtrip c0");
    dap_assert(recovered[1] == 1, "linear roundtrip c1");
    for (int i = 2; i < 512; ++i) {
        dap_assert(recovered[i] == 0, "linear roundtrip ci");
    }
}

/* ========================================================================
 * Test 4: Quadratic polynomial f(x) = x^2.
 * ======================================================================== */
static void test_rs_quadratic_poly(void)
{
    const int32_t expected[] = {
        9,        /* k=0:  (3)^2 */
        1939755,  /* k=1 */
        1641533,  /* k=2 */
        707854,   /* k=10 */
        3134893,  /* k=511 */
        9         /* k=1024: (-1)^2 = 1, but (3*w^1024)^2 = 9 */
    };
    const int k_indices[] = {0, 1, 2, 10, 511, 1024};

    int32_t poly[512];
    int32_t codeword[2048];

    memset(poly, 0, sizeof(poly));
    poly[2] = 1;  /* f(x) = x^2 */
    int rc = chipmunk_rs_encode(codeword, poly);
    dap_assert(rc == 0, "quadratic poly encode rc");

    for (int t = 0; t < 6; ++t) {
        dap_assert(codeword[k_indices[t]] == expected[t],
                   "quadratic poly eval ref");
    }

    /* Roundtrip */
    int32_t recovered[512];
    rc = chipmunk_rs_interpolate(recovered, codeword);
    dap_assert(rc == 0, "quadratic poly interp rc");
    dap_assert(recovered[0] == 0, "quadratic roundtrip c0");
    dap_assert(recovered[1] == 0, "quadratic roundtrip c1");
    dap_assert(recovered[2] == 1, "quadratic roundtrip c2");
    for (int i = 3; i < 512; ++i) {
        dap_assert(recovered[i] == 0, "quadratic roundtrip ci");
    }
}

/* ========================================================================
 * Test 5: Specific polynomial f(x) = 5 + 3x + 7x^2 + 2x^5.
 * ======================================================================== */
static void test_rs_specific_poly(void)
{
    const int32_t expected[] = {
        563,     /* k=0 */
        2015349, /* k=1 */
        2208781, /* k=42 */
        1847814, /* k=511 */
        3167830, /* k=1024 */
        1728949  /* k=2047 */
    };
    const int k_indices[] = {0, 1, 42, 511, 1024, 2047};

    int32_t poly[512];
    int32_t codeword[2048];

    memset(poly, 0, sizeof(poly));
    poly[0] = 5;
    poly[1] = 3;
    poly[2] = 7;
    poly[5] = 2;
    int rc = chipmunk_rs_encode(codeword, poly);
    dap_assert(rc == 0, "specific poly encode rc");

    for (int t = 0; t < 6; ++t) {
        dap_assert(codeword[k_indices[t]] == expected[t],
                   "specific poly eval ref");
    }

    /* Cross-check with Horner evaluation at each coset domain point */
    int32_t domain[2048];
    chipmunk_fri_ntt_coset_domain(domain, CHIPMUNK_RS_COSET_G);
    for (int t = 0; t < 6; ++t) {
        int32_t h = chipmunk_rs_eval(poly, 6, domain[k_indices[t]]);
        dap_assert(h == expected[t], "specific poly Horner cross-check");
    }

    /* Roundtrip */
    int32_t recovered[512];
    rc = chipmunk_rs_interpolate(recovered, codeword);
    dap_assert(rc == 0, "specific poly interp rc");
    for (int i = 0; i < 512; ++i) {
        int32_t exp = (i < 6) ? ((int[]){5, 3, 7, 0, 0, 2})[i] : 0;
        dap_assert(recovered[i] == exp, "specific poly roundtrip coeff");
    }
}

/* ========================================================================
 * Test 6: Dense polynomial — all 512 coefficients nonzero (poly[i] = i+1).
 * ======================================================================== */
static void test_rs_dense_poly(void)
{
    const int32_t expected[] = {
        2773443, /* k=0 */
        2241342, /* k=1 */
        2302153, /* k=42 */
        2450976, /* k=100 */
        2192946, /* k=511 */
        546112,  /* k=1024 */
        162610,  /* k=1500 */
        2778091  /* k=2047 */
    };
    const int k_indices[] = {0, 1, 42, 100, 511, 1024, 1500, 2047};

    int32_t poly[512];
    int32_t codeword[2048];

    for (int i = 0; i < 512; ++i)
        poly[i] = i + 1;

    int rc = chipmunk_rs_encode(codeword, poly);
    dap_assert(rc == 0, "dense poly encode rc");

    for (int t = 0; t < 8; ++t) {
        dap_assert(codeword[k_indices[t]] == expected[t],
                   "dense poly eval ref");
    }

    /* Roundtrip */
    int32_t recovered[512];
    rc = chipmunk_rs_interpolate(recovered, codeword);
    dap_assert(rc == 0, "dense poly interp rc");
    for (int i = 0; i < 512; ++i) {
        dap_assert(recovered[i] == (int32_t)(i + 1), "dense poly roundtrip coeff");
    }
}

/* ========================================================================
 * Test 7: Horner evaluation (chipmunk_rs_eval) correctness.
 * Cross-check against direct computation for known values.
 * ======================================================================== */
static void test_rs_horner_eval(void)
{
    /* f(x) = 5 + 3x + 7x^2 + 2x^5 at x = 100 */
    int32_t poly[] = {5, 3, 7, 0, 0, 2};
    /* 5 + 300 + 70000 + 2*100^5 = 5 + 300 + 70000 + 20000000000
     * = 20000703005 mod 3168257 = ? */
    int32_t val = chipmunk_rs_eval(poly, 6, 100);
    /* Python: (5 + 3*100 + 7*10000 + 2*100**5) % 3168257 */
    /* 100^5 = 10000000000.  2*10000000000 = 20000000000.
     * 20000000000 % 3168257 = 20000000000 - 6315*3168257 = 20000000000 - 20000004855
     * That's negative, let me compute properly */
    int64_t check = 5LL + 3LL*100 + 7LL*10000LL + 2LL*10000000000LL;
    check = check % (int64_t)Q;
    if (check < 0) check += (int64_t)Q;
    dap_assert(val == (int32_t)check, "Horner eval at x=100");

    /* f(x) = 1 at x = anything should be 1 */
    int32_t poly1[] = {1};
    val = chipmunk_rs_eval(poly1, 1, 0);
    dap_assert(val == 1, "Horner constant at x=0");
    val = chipmunk_rs_eval(poly1, 1, Q - 1);
    dap_assert(val == 1, "Horner constant at x=q-1");

    /* f(x) = x at x = 42 should be 42 */
    int32_t poly_x[] = {0, 1};
    val = chipmunk_rs_eval(poly_x, 2, 42);
    dap_assert(val == 42, "Horner x at x=42");

    /* f(x) = x at x = q-1 should be q-1 */
    val = chipmunk_rs_eval(poly_x, 2, Q - 1);
    dap_assert(val == Q - 1, "Horner x at x=q-1");
}

/* ========================================================================
 * Test 8: Aliasing — codeword and poly point to same buffer.
 * ======================================================================== */
static void test_rs_aliasing(void)
{
    int32_t buf[2048];  /* poly lives in first 512 elements */

    /* Set poly coefficients */
    for (int i = 0; i < 512; ++i)
        buf[i] = i + 1;
    memset(buf + 512, 0, 1536 * sizeof(int32_t));

    /* Encode in-place */
    int rc = chipmunk_rs_encode(buf, buf);
    dap_assert(rc == 0, "alias encode rc");

    /* Verify first few evaluations match the dense poly reference */
    const int32_t expected[] = {2773443, 2241342, 2302153};
    const int k_indices[] = {0, 1, 42};
    for (int t = 0; t < 3; ++t) {
        dap_assert(buf[k_indices[t]] == expected[t], "alias eval ref");
    }

    /* Interpolate back */
    int32_t poly[512];
    rc = chipmunk_rs_interpolate(poly, buf);
    dap_assert(rc == 0, "alias interp rc");
    for (int i = 0; i < 512; ++i) {
        dap_assert(poly[i] == (int32_t)(i + 1), "alias roundtrip coeff");
    }
}

/* ========================================================================
 * Test 9: Codeword values are in [0, q).
 * ======================================================================== */
static void test_rs_codeword_range(void)
{
    int32_t poly[512];
    int32_t codeword[2048];

    for (int i = 0; i < 512; ++i)
        poly[i] = (i * 12345 + 67890) % Q;

    int rc = chipmunk_rs_encode(codeword, poly);
    dap_assert(rc == 0, "range encode rc");

    for (int i = 0; i < 2048; ++i) {
        dap_assert(codeword[i] >= 0 && codeword[i] < Q, "codeword in [0,q)");
    }
}

/* ========================================================================
 * Test 10: Injectivity — distinct polynomials yield distinct codewords.
 * Check first 100 random-ish polynomials pairwise.
 * ======================================================================== */
static void test_rs_injectivity(void)
{
    int32_t poly[512], poly2[512];
    int32_t cw1[2048], cw2[2048];

    int collisions = 0;

    for (int trial = 0; trial < 100; ++trial) {
        /* poly1: coeff[i] = trial * 1000 + i */
        for (int i = 0; i < 512; ++i)
            poly[i] = ((trial * 1000 + i) % Q + Q) % Q;

        /* poly2: coeff[0] = poly[0] + 1 (different constant term) */
        memcpy(poly2, poly, sizeof(poly));
        poly2[0] = (poly2[0] + 1) % Q;

        chipmunk_rs_encode(cw1, poly);
        chipmunk_rs_encode(cw2, poly2);

        int same = 1;
        for (int i = 0; i < 2048 && same; ++i) {
            if (cw1[i] != cw2[i]) same = 0;
        }
        if (same) collisions++;
    }

    dap_assert(collisions == 0, "injectivity: no collisions");
}

/* ========================================================================
 * Test 11: Determinism — encoding the same poly twice yields identical output.
 * ======================================================================== */
static void test_rs_determinism(void)
{
    int32_t poly[512];
    int32_t cw1[2048], cw2[2048];

    for (int i = 0; i < 512; ++i)
        poly[i] = (i * 7919 + 104729) % Q;

    chipmunk_rs_encode(cw1, poly);
    chipmunk_rs_encode(cw2, poly);

    for (int i = 0; i < 2048; ++i) {
        dap_assert(cw1[i] == cw2[i], "determinism");
    }
}

/* ========================================================================
 * Test 12: Coset domain evaluation matches NTT encoding for all points.
 * For each of 20 random polynomials, verify NTT encoding matches
 * Horner evaluation at 10 randomly chosen domain points.
 * ======================================================================== */
static void test_rs_ntt_vs_horner(void)
{
    int32_t poly[512], codeword[2048];
    int32_t domain[2048];

    chipmunk_fri_ntt_coset_domain(domain, CHIPMUNK_RS_COSET_G);

    for (int trial = 0; trial < 20; ++trial) {
        for (int i = 0; i < 512; ++i)
            poly[i] = (int32_t)((uint32_t)(trial * 31337 + i * 7919 + 1) % (uint32_t)Q);

        chipmunk_rs_encode(codeword, poly);

        /* Check 10 random-ish points */
        for (int t = 0; t < 10; ++t) {
            int k = (trial * 97 + t * 211) % 2048;
            int32_t h = chipmunk_rs_eval(poly, 512, domain[k]);
            dap_assert(h == codeword[k], "NTT vs Horner");
        }
    }
}

/* ========================================================================
 * Test 13: Interpolation of codeword from Horner (sanity check for inverse).
 * ======================================================================== */
static void test_rs_interpolate_horner_codeword(void)
{
    int32_t poly[512];
    int32_t codeword[2048];
    int32_t domain[2048];

    chipmunk_fri_ntt_coset_domain(domain, CHIPMUNK_RS_COSET_G);

    /* Build a codeword by Horner evaluation (not NTT) */
    for (int i = 0; i < 512; ++i)
        poly[i] = i + 1;

    for (int k = 0; k < 2048; ++k) {
        codeword[k] = chipmunk_rs_eval(poly, 512, domain[k]);
    }

    /* Interpolate back */
    int32_t recovered[512];
    int rc = chipmunk_rs_interpolate(recovered, codeword);
    dap_assert(rc == 0, "interp horner cw rc");

    for (int i = 0; i < 512; ++i) {
        dap_assert(recovered[i] == (int32_t)(i + 1), "interp horner cw coeff");
    }
}

/* ========================================================================
 * Test 14: Invalid arguments.
 * ======================================================================== */
static void test_rs_invalid_args(void)
{
    int32_t poly[512], cw[2048];

    memset(poly, 0, sizeof(poly));

    /* NULL pointers */
    int rc = chipmunk_rs_encode(NULL, poly);
    dap_assert(rc < 0, "encode NULL codeword");
    rc = chipmunk_rs_encode(cw, NULL);
    dap_assert(rc < 0, "encode NULL poly");
    rc = chipmunk_rs_interpolate(NULL, cw);
    dap_assert(rc < 0, "interp NULL poly");
    rc = chipmunk_rs_interpolate(poly, NULL);
    dap_assert(rc < 0, "interp NULL codeword");

    /* chipmunk_rs_eval with NULL */
    int32_t v = chipmunk_rs_eval(NULL, 10, 42);
    dap_assert(v == 0, "eval NULL poly");
    v = chipmunk_rs_eval(poly, 0, 42);
    dap_assert(v == 0, "eval zero n");
}

/* ========================================================================
 * Main
 * ======================================================================== */
int main(void)
{
    dap_set_appname("test_chipmunk_rs");
    if (0 != dap_common_init("test_chipmunk_rs", NULL)) {
        fprintf(stderr, "dap_common_init failed\n");
        return 1;
    }

    /* Init dependencies */
    int rc = chipmunk_field_init();
    dap_assert(rc == 0, "chipmunk_field_init");
    rc = chipmunk_fri_ntt_init();
    dap_assert(rc == 0, "chipmunk_fri_ntt_init");

    log_it(L_INFO, "=== Reed-Solomon encoding tests ===");

    test_rs_zero_poly();
    test_rs_constant_poly();
    test_rs_linear_poly();
    test_rs_quadratic_poly();
    test_rs_specific_poly();
    test_rs_dense_poly();
    test_rs_horner_eval();
    test_rs_aliasing();
    test_rs_codeword_range();
    test_rs_injectivity();
    test_rs_determinism();
    test_rs_ntt_vs_horner();
    test_rs_interpolate_horner_codeword();
    test_rs_invalid_args();

    log_it(L_INFO, "All RS tests passed");

    dap_common_deinit();
    return 0;
}
