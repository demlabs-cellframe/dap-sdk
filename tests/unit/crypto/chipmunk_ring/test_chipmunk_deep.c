/*
 * test_chipmunk_deep.c — Unit tests for DEEP composition polynomial.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <dap_common.h>
#include <dap_test.h>

#include "chipmunk.h"
#include "chipmunk_poly.h"
#include "chipmunk_field.h"
#include "chipmunk_deep.h"

#define LOG_TAG "test_chipmunk_deep"

/* Helper: Horner evaluation in F_q. */
static int32_t s_horner(const int32_t c[CHIPMUNK_N], int32_t x)
{
    int32_t r = 0;
    for (int i = CHIPMUNK_N - 1; i >= 0; --i)
        r = chipmunk_mod_q((int64_t)x * r + c[i]);
    return r;
}

/* Helper: evaluate poly at x with Python-verified values. */
static void s_fill_poly(chipmunk_poly_t *p, int32_t seed)
{
    for (int i = 0; i < CHIPMUNK_N; ++i)
        p->coeffs[i] = (int32_t)((uint32_t)(seed + i * 7919 + 1) % (uint32_t)CHIPMUNK_Q);
}

/* ========================================================================
 * Test 1: Zero polynomial.
 * ======================================================================== */
static void test_deep_zero_poly(void)
{
    chipmunk_deep_prover_t prov;
    int rc = chipmunk_deep_prover_init(&prov);
    prov.q = (uint64_t)CHIPMUNK_Q;
    dap_assert(rc == 0, "zero init");

    chipmunk_poly_t poly;
    memset(&poly, 0, sizeof(poly));

    int32_t z = 42;
    int32_t gamma = 7;

    rc = chipmunk_deep_compose(&prov, &poly, 1, z, &gamma);
    dap_assert(rc == 0, "zero compose");
    dap_assert(prov.composed, "zero composed");

    /* H(X) should be zero for zero input poly. */
    const int32_t *h = chipmunk_deep_prover_composition(&prov);
    dap_assert(h != NULL, "zero composition not null");
    for (unsigned i = 0; i < CHIPMUNK_N; ++i)
        dap_assert(h[i] == 0, "zero H coeff");

    /* Verify at random test points. */
    chipmunk_deep_opening_t opening;
    rc = chipmunk_deep_build_opening(&opening, &prov);
    dap_assert(rc == 0, "zero opening");

    for (int x = 1; x < 10; ++x) {
        bool ok = chipmunk_deep_verify_q(&opening, &poly, 1, &gamma, h, x, (uint64_t)CHIPMUNK_Q);
        dap_assert(ok, "zero verify");
    }

    chipmunk_deep_prover_free(&prov);
}

/* ========================================================================
 * Test 2: Constant polynomial f(X) = c.
 * ======================================================================== */
static void test_deep_constant_poly(void)
{
    chipmunk_deep_prover_t prov;
    int rc = chipmunk_deep_prover_init(&prov);
    prov.q = (uint64_t)CHIPMUNK_Q;
    dap_assert(rc == 0, "const init");

    chipmunk_poly_t poly;
    memset(&poly, 0, sizeof(poly));
    /* True constant: only coeff[0] set. */
    int32_t c = 12345;
    poly.coeffs[0] = c;

    int32_t z = 100;
    int32_t gamma = 13;

    rc = chipmunk_deep_compose(&prov, &poly, 1, z, &gamma);
    dap_assert(rc == 0, "const compose");

    /* For constant poly f(X) = c: f(z) = c, [f(X)-f(z)] = 0, so H(X) = 0. */
    const int32_t *h = chipmunk_deep_prover_composition(&prov);
    for (unsigned i = 0; i < CHIPMUNK_N; ++i)
        dap_assert(h[i] == 0, "const H is zero");

    chipmunk_deep_opening_t opening;
    chipmunk_deep_build_opening(&opening, &prov);

    bool ok = chipmunk_deep_verify_q(&opening, &poly, 1, &gamma, h, 1, (uint64_t)CHIPMUNK_Q);
    dap_assert(ok, "const verify");

    chipmunk_deep_prover_free(&prov);
}

/* ========================================================================
 * Test 3: Linear polynomial f(X) = a*X + b.
 * ======================================================================== */
static void test_deep_linear_poly(void)
{
    chipmunk_deep_prover_t prov;
    int rc = chipmunk_deep_prover_init(&prov);
    prov.q = (uint64_t)CHIPMUNK_Q;
    dap_assert(rc == 0, "linear init");

    chipmunk_poly_t poly;
    memset(&poly, 0, sizeof(poly));
    /* f(X) = 100*X + 42 (in R_q). NTT ring is X^512+1, but DEEP treats
     * polys as formal degree-511 over F_q for evaluation purposes. */
    poly.coeffs[0] = 42;   /* constant term */
    poly.coeffs[1] = 100;  /* X coefficient */

    int32_t z = 7;
    int32_t gamma = 3;

    rc = chipmunk_deep_compose(&prov, &poly, 1, z, &gamma);
    dap_assert(rc == 0, "linear compose");

    const int32_t *h = chipmunk_deep_prover_composition(&prov);

    /* Manual check: f(7) = 100*7 + 42 = 742.
     * [f(X) - 742] / (X - 7): coefficients after synthetic div.
     * f(X) = 42 + 100X + 0X^2 + ...
     * f(7) = 742
     * f(X) - 742 = (42-742) + 100X = -700 + 100X
     * q(X) = (-700 + 100X) / (X - 7) = 100 + (-700 + 700) / (X-7) = 100
     * So H(X) = gamma * q(X) = 3 * 100 = 300.
     * H should be constant 300. */
    /* Actually, let me compute more carefully.
     * Synthetic division of degree-511 poly (most coeffs 0 except [0]=42, [1]=100):
     * q[510] = f[511] = 0
     * q[i] = f[i+1] + z * q[i+1]
     * ... all zeros down to:
     * q[1] = f[2] + z * q[2] = 0 + 7*0 = 0
     * q[0] = f[1] + z * q[1] = 100 + 7*0 = 100
     * So q(X) = 100 (constant). H = 3*100 = 300.
     * But wait, we subtracted f(z)=742 from f[0]. Let me re-check:
     * f_adj[0] = 42 - 742 = -700 mod q = q-700 = 3167557
     * The synth div processes adj_f[0..511].
     * acc starts at 0.
     * i=511: acc = f[511] + z*0 = 0; q[510]=0
     * ...
     * i=2: acc = f[2] + z*0 = 0; q[1]=0
     * i=1: acc = f[1] + z*0 = 100; q[0]=100
     * Then: acc = adj_f[0] + z * 100 = -700 + 700 = 0. ✓ Remainder is 0.
     * H = 3 * q = 3 * [100, 0, 0, ...] = [300, 0, 0, ...]
     */
    dap_assert(h[0] == 300, "linear H[0]");
    for (unsigned i = 1; i < CHIPMUNK_N; ++i)
        dap_assert(h[i] == 0, "linear H[i>0] zero");

    chipmunk_deep_opening_t opening;
    chipmunk_deep_build_opening(&opening, &prov);
    dap_assert(opening.evals[0] == 742, "linear f(z)");

    for (int x = 1; x <= 5; ++x) {
        if (x == 7) continue;
        bool ok = chipmunk_deep_verify_q(&opening, &poly, 1, &gamma, h, x, (uint64_t)CHIPMUNK_Q);
        dap_assert(ok, "linear verify");
    }

    chipmunk_deep_prover_free(&prov);
}

/* ========================================================================
 * Test 4: Quadratic polynomial — verify quotient with Python cross-check.
 * ======================================================================== */
static void test_deep_quadratic_poly(void)
{
    chipmunk_deep_prover_t prov;
    int rc = chipmunk_deep_prover_init(&prov);
    prov.q = (uint64_t)CHIPMUNK_Q;
    dap_assert(rc == 0, "quad init");

    chipmunk_poly_t poly;
    memset(&poly, 0, sizeof(poly));
    /* f(X) = 2 + 3X + 5X^2 */
    poly.coeffs[0] = 2;
    poly.coeffs[1] = 3;
    poly.coeffs[2] = 5;

    int32_t z = 10;
    int32_t gamma = 1;

    rc = chipmunk_deep_compose(&prov, &poly, 1, z, &gamma);
    dap_assert(rc == 0, "quad compose");

    const int32_t *h = chipmunk_deep_prover_composition(&prov);

    /* Python check:
     * f(10) = 2 + 30 + 500 = 532
     * q(X) = [f(X) - 532] / (X - 10)
     * f(X) - 532 = -530 + 3X + 5X^2
     * Divide by (X-10): q(X) = 5X + 53
     * (5X + 53)(X - 10) = 5X^2 - 50X + 53X - 530 = 5X^2 + 3X - 530 ✓
     * H(X) = 1 * q(X) = 5X + 53
     */
    dap_assert(prov.evals[0] == 532, "quad f(z)");
    dap_assert(h[0] == 53, "quad H[0]");
    dap_assert(h[1] == 5, "quad H[1]");
    for (unsigned i = 2; i < CHIPMUNK_N; ++i)
        dap_assert(h[i] == 0, "quad H[i>=2] zero");

    chipmunk_deep_opening_t opening;
    chipmunk_deep_build_opening(&opening, &prov);

    for (int x = 1; x <= 20; ++x) {
        if (x == 10) continue;
        bool ok = chipmunk_deep_verify_q(&opening, &poly, 1, &gamma, h, x, (uint64_t)CHIPMUNK_Q);
        dap_assert(ok, "quad verify");
    }

    chipmunk_deep_prover_free(&prov);
}

/* ========================================================================
 * Test 5: Two polynomials with composition weights.
 * ======================================================================== */
static void test_deep_two_polys(void)
{
    chipmunk_deep_prover_t prov;
    int rc = chipmunk_deep_prover_init(&prov);
    prov.q = (uint64_t)CHIPMUNK_Q;
    dap_assert(rc == 0, "two init");

    chipmunk_poly_t p1, p2;
    s_fill_poly(&p1, 100);
    s_fill_poly(&p2, 200);

    int32_t z = 99;
    int32_t gammas[2] = {1000, 2000};

    rc = chipmunk_deep_compose(&prov, (const chipmunk_poly_t[]){p1, p2}, 2, z, gammas);
    dap_assert(rc == 0, "two compose");

    const int32_t *h = chipmunk_deep_prover_composition(&prov);
    dap_assert(h != NULL, "two composition not null");

    /* Verify composition at multiple test points. */
    chipmunk_deep_opening_t opening;
    chipmunk_deep_build_opening(&opening, &prov);

    int32_t test_xs[] = {1, 2, 50, 100, 500, 1000, 2000};
    for (unsigned t = 0; t < sizeof(test_xs)/sizeof(test_xs[0]); ++t) {
        bool ok = chipmunk_deep_verify_q(&opening,
                                        (const chipmunk_poly_t[]){p1, p2},
                                        2, gammas, h, test_xs[t], (uint64_t)CHIPMUNK_Q);
        dap_assert(ok, "two verify");
    }

    chipmunk_deep_prover_free(&prov);
}

/* ========================================================================
 * Test 6: Composition degree bound — H(X) should have degree ≤ 510.
 * ======================================================================== */
static void test_deep_degree_bound(void)
{
    chipmunk_deep_prover_t prov;
    chipmunk_deep_prover_init(&prov);
    prov.q = (uint64_t)CHIPMUNK_Q;

    chipmunk_poly_t poly;
    memset(&poly, 0, sizeof(poly));
    /* Full degree 511 polynomial. */
    for (int i = 0; i < CHIPMUNK_N; ++i)
        poly.coeffs[i] = i + 1;

    int32_t z = 77;
    int32_t gamma = 1;

    chipmunk_deep_compose(&prov, &poly, 1, z, &gamma);

    const int32_t *h = chipmunk_deep_prover_composition(&prov);

    /* Quotient of degree-511 poly has degree ≤ 510.
     * Highest coefficient h[511] must be zero. */
    dap_assert(h[CHIPMUNK_N - 1] == 0, "degree bound: h[511] == 0");

    /* Cross-check: H(x) should equal γ * [f(x) - f(z)] / (x - z)
     * for any x != z. Test at a few points. */
    int32_t x_test = 123;
    int32_t fx = s_horner(poly.coeffs, x_test);
    int32_t inv_xz = chipmunk_field_inv_q(chipmunk_mod_q((int64_t)x_test - (int64_t)z), (uint64_t)CHIPMUNK_Q);
    int32_t expected = chipmunk_mod_q((int64_t)gamma *
        chipmunk_mod_q((int64_t)chipmunk_mod_q((int64_t)fx - prov.evals[0]) * inv_xz));
    int32_t actual = s_horner(h, x_test);
    dap_assert(actual == expected, "degree cross-check");

    chipmunk_deep_prover_free(&prov);
}

/* ========================================================================
 * Test 7: Verify rejects tampered evaluation.
 * ======================================================================== */
static void test_deep_tampered_eval(void)
{
    chipmunk_deep_prover_t prov;
    chipmunk_deep_prover_init(&prov);
    prov.q = (uint64_t)CHIPMUNK_Q;

    chipmunk_poly_t poly;
    s_fill_poly(&poly, 42);

    int32_t z = 33;
    int32_t gamma = 11;

    chipmunk_deep_compose(&prov, &poly, 1, z, &gamma);
    const int32_t *h = chipmunk_deep_prover_composition(&prov);

    chipmunk_deep_opening_t opening;
    chipmunk_deep_build_opening(&opening, &prov);

    /* Honest verify should pass. */
    bool ok = chipmunk_deep_verify_q(&opening, &poly, 1, &gamma, h, 1, (uint64_t)CHIPMUNK_Q);
    dap_assert(ok, "tampered: honest pass");

    /* Tamper with evaluation. */
    opening.evals[0] ^= 1;
    ok = chipmunk_deep_verify_q(&opening, &poly, 1, &gamma, h, 1, (uint64_t)CHIPMUNK_Q);
    dap_assert(!ok, "tampered eval rejected");

    /* Tamper with z point. */
    chipmunk_deep_build_opening(&opening, &prov);
    opening.z_point ^= 1;
    ok = chipmunk_deep_verify_q(&opening, &poly, 1, &gamma, h, 1, (uint64_t)CHIPMUNK_Q);
    dap_assert(!ok, "tampered z rejected");

    chipmunk_deep_prover_free(&prov);
}

/* ========================================================================
 * Test 8: Verify rejects x == z (division by zero).
 * ======================================================================== */
static void test_deep_x_equals_z(void)
{
    chipmunk_deep_prover_t prov;
    chipmunk_deep_prover_init(&prov);
    prov.q = (uint64_t)CHIPMUNK_Q;

    chipmunk_poly_t poly;
    s_fill_poly(&poly, 1);

    int32_t z = 55;
    int32_t gamma = 1;

    chipmunk_deep_compose(&prov, &poly, 1, z, &gamma);
    const int32_t *h = chipmunk_deep_prover_composition(&prov);

    chipmunk_deep_opening_t opening;
    chipmunk_deep_build_opening(&opening, &prov);

    /* x == z should return false (division by zero). */
    bool ok = chipmunk_deep_verify_q(&opening, &poly, 1, &gamma, h, z, (uint64_t)CHIPMUNK_Q);
    dap_assert(!ok, "x==z rejected");

    chipmunk_deep_prover_free(&prov);
}

/* ========================================================================
 * Test 9: Invalid arguments.
 * ======================================================================== */
static void test_deep_invalid_args(void)
{
    chipmunk_deep_prover_t prov;
    chipmunk_deep_prover_init(&prov);
    prov.q = (uint64_t)CHIPMUNK_Q;

    chipmunk_poly_t poly;
    s_fill_poly(&poly, 1);
    int32_t gamma = 1;

    /* NULL prover. */
    int rc = chipmunk_deep_compose(NULL, &poly, 1, 5, &gamma);
    dap_assert(rc < 0, "NULL prov");

    /* NULL polys. */
    rc = chipmunk_deep_compose(&prov, NULL, 1, 5, &gamma);
    dap_assert(rc < 0, "NULL polys");

    /* Zero num_polys. */
    rc = chipmunk_deep_compose(&prov, &poly, 0, 5, &gamma);
    dap_assert(rc < 0, "zero num_polys");

    /* Too many polys. */
    rc = chipmunk_deep_compose(&prov, &poly, CHIPMUNK_DEEP_MAX_POLYS + 1, 5, &gamma);
    dap_assert(rc < 0, "too many polys");

    /* z == 0 (not allowed — would interfere with domain). */
    rc = chipmunk_deep_compose(&prov, &poly, 1, 0, &gamma);
    dap_assert(rc < 0, "z == 0 rejected");

    /* Composition before compose. */
    const int32_t *h = chipmunk_deep_prover_composition(&prov);
    dap_assert(h == NULL, "composition before compose");

    /* Build opening before compose. */
    chipmunk_deep_opening_t opening;
    rc = chipmunk_deep_build_opening(&opening, &prov);
    dap_assert(rc < 0, "opening before compose");

    /* Verify NULL args. */
    rc = chipmunk_deep_compose(&prov, &poly, 1, 5, &gamma);
    dap_assert(rc == 0, "compose ok");
    chipmunk_deep_build_opening(&opening, &prov);
    h = chipmunk_deep_prover_composition(&prov);

    bool ok = chipmunk_deep_verify_q(NULL, &poly, 1, &gamma, h, 1, (uint64_t)CHIPMUNK_Q);
    dap_assert(!ok, "verify NULL opening");
    ok = chipmunk_deep_verify_q(&opening, NULL, 1, &gamma, h, 1, (uint64_t)CHIPMUNK_Q);
    dap_assert(!ok, "verify NULL polys");
    ok = chipmunk_deep_verify_q(&opening, &poly, 1, NULL, h, 1, (uint64_t)CHIPMUNK_Q);
    dap_assert(!ok, "verify NULL gammas");
    ok = chipmunk_deep_verify_q(&opening, &poly, 1, &gamma, NULL, 1, (uint64_t)CHIPMUNK_Q);
    dap_assert(!ok, "verify NULL composition");

    chipmunk_deep_prover_free(&prov);
}

/* ========================================================================
 * Test 10: Three polys — stress test composition.
 * ======================================================================== */
static void test_deep_three_polys(void)
{
    chipmunk_deep_prover_t prov;
    chipmunk_deep_prover_init(&prov);
    prov.q = (uint64_t)CHIPMUNK_Q;

    chipmunk_poly_t p1, p2, p3;
    s_fill_poly(&p1, 111);
    s_fill_poly(&p2, 222);
    s_fill_poly(&p3, 333);

    int32_t z = 1234;
    int32_t gammas[3] = {50, 150, 250};

    const chipmunk_poly_t polys[] = {p1, p2, p3};
    int rc = chipmunk_deep_compose(&prov, polys, 3, z, gammas);
    dap_assert(rc == 0, "three compose");
    dap_assert(prov.num_polys == 3, "three num_polys");

    const int32_t *h = chipmunk_deep_prover_composition(&prov);

    chipmunk_deep_opening_t opening;
    chipmunk_deep_build_opening(&opening, &prov);
    dap_assert(opening.num_polys == 3, "three opening num_polys");

    /* Verify at many test points. */
    for (int x = 1; x < 30; ++x) {
        if (x == z) continue;
        bool ok = chipmunk_deep_verify_q(&opening, polys, 3, gammas, h, x, (uint64_t)CHIPMUNK_Q);
        dap_assert(ok, "three verify");
    }

    chipmunk_deep_prover_free(&prov);
}

/* ========================================================================
 * Test 11: Determinism — same inputs give same outputs.
 * ======================================================================== */
static void test_deep_determinism(void)
{
    chipmunk_poly_t poly;
    s_fill_poly(&poly, 777);

    int32_t z = 314;
    int32_t gamma = 159;

    chipmunk_deep_prover_t prov1, prov2;
    chipmunk_deep_prover_init(&prov1);
    prov1.q = (uint64_t)CHIPMUNK_Q;
    chipmunk_deep_prover_init(&prov2);
    prov2.q = (uint64_t)CHIPMUNK_Q;

    chipmunk_deep_compose(&prov1, &poly, 1, z, &gamma);
    chipmunk_deep_compose(&prov2, &poly, 1, z, &gamma);

    const int32_t *h1 = chipmunk_deep_prover_composition(&prov1);
    const int32_t *h2 = chipmunk_deep_prover_composition(&prov2);

    for (unsigned i = 0; i < CHIPMUNK_N; ++i)
        dap_assert(h1[i] == h2[i], "determinism coeff");

    dap_assert(prov1.evals[0] == prov2.evals[0], "determinism eval");

    chipmunk_deep_prover_free(&prov1);
    chipmunk_deep_prover_free(&prov2);
}

/* ========================================================================
 * Test 12: Max polys (4) stress test.
 * ======================================================================== */
static void test_deep_max_polys(void)
{
    chipmunk_deep_prover_t prov;
    chipmunk_deep_prover_init(&prov);
    prov.q = (uint64_t)CHIPMUNK_Q;

    chipmunk_poly_t polys[CHIPMUNK_DEEP_MAX_POLYS];
    int32_t gammas[CHIPMUNK_DEEP_MAX_POLYS];
    for (unsigned i = 0; i < CHIPMUNK_DEEP_MAX_POLYS; ++i) {
        s_fill_poly(&polys[i], (int32_t)(i * 1000 + 1));
        gammas[i] = (int32_t)(i * 100 + 1);
    }

    int32_t z = 999;
    int rc = chipmunk_deep_compose(&prov, polys, CHIPMUNK_DEEP_MAX_POLYS, z, gammas);
    dap_assert(rc == 0, "max compose");

    const int32_t *h = chipmunk_deep_prover_composition(&prov);

    chipmunk_deep_opening_t opening;
    chipmunk_deep_build_opening(&opening, &prov);

    for (int x = 1; x < 20; ++x) {
        if (x == z) continue;
        bool ok = chipmunk_deep_verify_q(&opening, polys, CHIPMUNK_DEEP_MAX_POLYS, gammas, h, x, (uint64_t)CHIPMUNK_Q);
        dap_assert(ok, "max verify");
    }

    chipmunk_deep_prover_free(&prov);
}

/* ========================================================================
 * Main
 * ======================================================================== */
int main(void)
{
    dap_set_appname("test_chipmunk_deep");
    if (0 != dap_common_init("test_chipmunk_deep", NULL)) {
        fprintf(stderr, "dap_common_init failed\n");
        return 1;
    }

    int rc = chipmunk_field_init();
    dap_assert(rc == 0, "chipmunk_field_init");

    log_it(L_INFO, "=== DEEP composition tests ===");

    test_deep_zero_poly();
    test_deep_constant_poly();
    test_deep_linear_poly();
    test_deep_quadratic_poly();
    test_deep_two_polys();
    test_deep_degree_bound();
    test_deep_tampered_eval();
    test_deep_x_equals_z();
    test_deep_invalid_args();
    test_deep_three_polys();
    test_deep_determinism();
    test_deep_max_polys();

    log_it(L_INFO, "All DEEP tests passed");

    dap_common_deinit();
    return 0;
}
