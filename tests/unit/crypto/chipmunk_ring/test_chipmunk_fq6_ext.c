/*
 * test_chipmunk_fq6_ext.c — MRNG ring-extension arithmetic (G3.1 §9.2).
 *
 * CR-11.G Phase 7.7 / task_ac273cea.  Validates the algebra of
 *
 *     R_q^{(e)} = R_q[Y]/(Φ₉),   Φ₉ = Y⁶ + Y³ + 1,   e = 6,
 *
 * which underpins the Option-B fold soundness (MRNG_G3_1_EXTENSION_
 * SOUNDNESS.md).  Coverage:
 *
 *   T0  Rabin: Φ₉ is irreducible over F_q (re-checked in CI, not trusted
 *       as a constant).
 *   T1  ring axioms (commutativity/associativity/distributivity of
 *       add & mul, additive & multiplicative identities) over random
 *       elements.
 *   T2  scalar F_{q⁶}: x·x⁻¹ = 1, zero scalar → -EDOM, non-scalar → -EINVAL.
 *   T3  general (per-slot F_{q⁶}) inversion: x·x⁻¹ = 1 (the ext one).
 *   T4  embed∘project = id on R_q; embed is in the base ring; a genuine
 *       Y-degree-≥1 element is NOT; ring-hom embed(a)·embed(b)=embed(a·b).
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

#define LOG_TAG "test_chipmunk_fq6_ext"

/* ---- deterministic RNG -------------------------------------------- */

static uint64_t s_rng_state = 0x9E3779B97F4A7C15ULL;

static uint64_t s_rng(void)
{
    uint64_t x = s_rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    s_rng_state = x;
    return x;
}

static int32_t s_rand_fq(void)
{
    return (int32_t)(s_rng() % (uint64_t)CHIPMUNK_Q);
}

static void s_rand_poly(chipmunk_poly_t *a_p)
{
    for (int i = 0; i < CHIPMUNK_N; ++i) {
        a_p->coeffs[i] = s_rand_fq();
    }
}

static void s_rand_ext(chipmunk_fq6_ext_t *a_e)
{
    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        s_rand_poly(&a_e->c[j]);
    }
}

/* ---- canonical comparison ----------------------------------------- */

static int32_t s_canon(int32_t a_v)
{
    int32_t r = a_v % (int32_t)CHIPMUNK_Q;
    if (r < 0) { r += (int32_t)CHIPMUNK_Q; }
    return r;
}

static bool s_poly_eq(const chipmunk_poly_t *a, const chipmunk_poly_t *b)
{
    for (int i = 0; i < CHIPMUNK_N; ++i) {
        if (s_canon(a->coeffs[i]) != s_canon(b->coeffs[i])) { return false; }
    }
    return true;
}

static bool s_ext_eq(const chipmunk_fq6_ext_t *a, const chipmunk_fq6_ext_t *b)
{
    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        if (!s_poly_eq(&a->c[j], &b->c[j])) { return false; }
    }
    return true;
}

static bool s_ext_is_one(const chipmunk_fq6_ext_t *a)
{
    chipmunk_fq6_ext_t one;
    chipmunk_fq6_ext_one(&one);
    return s_ext_eq(a, &one);
}

/* ---- T0: Φ₉ irreducible ------------------------------------------- */

static bool s_test_irreducible(void)
{
    dap_assert(chipmunk_fq6_ext_modulus_is_irreducible_q((uint64_t)CHIPMUNK_Q),
               "Φ₉ = Y⁶+Y³+1 must be irreducible over F_q (Rabin)");
    return true;
}

/* ---- T1: ring axioms ---------------------------------------------- */

static bool s_test_ring_axioms(void)
{
    for (int it = 0; it < 6; ++it) {
        chipmunk_fq6_ext_t a, b, c;
        s_rand_ext(&a); s_rand_ext(&b); s_rand_ext(&c);

        chipmunk_fq6_ext_t lhs, rhs, t1, t2;

        /* add commutativity */
        dap_assert(chipmunk_fq6_ext_add(&lhs, &a, &b) == 0, "add");
        dap_assert(chipmunk_fq6_ext_add(&rhs, &b, &a) == 0, "add");
        dap_assert(s_ext_eq(&lhs, &rhs), "a+b == b+a");

        /* add associativity */
        dap_assert(chipmunk_fq6_ext_add(&t1, &a, &b) == 0, "add");
        dap_assert(chipmunk_fq6_ext_add(&lhs, &t1, &c) == 0, "add");
        dap_assert(chipmunk_fq6_ext_add(&t2, &b, &c) == 0, "add");
        dap_assert(chipmunk_fq6_ext_add(&rhs, &a, &t2) == 0, "add");
        dap_assert(s_ext_eq(&lhs, &rhs), "(a+b)+c == a+(b+c)");

        /* additive identity + inverse via sub */
        chipmunk_fq6_ext_t zero;
        chipmunk_fq6_ext_zero(&zero);
        dap_assert(chipmunk_fq6_ext_add(&lhs, &a, &zero) == 0, "add zero");
        dap_assert(s_ext_eq(&lhs, &a), "a+0 == a");
        dap_assert(chipmunk_fq6_ext_sub(&lhs, &a, &a) == 0, "sub");
        dap_assert(s_ext_eq(&lhs, &zero), "a-a == 0");

        /* mul commutativity */
        dap_assert(chipmunk_fq6_ext_mul(&lhs, &a, &b) == 0, "mul");
        dap_assert(chipmunk_fq6_ext_mul(&rhs, &b, &a) == 0, "mul");
        dap_assert(s_ext_eq(&lhs, &rhs), "a*b == b*a");

        /* mul associativity */
        dap_assert(chipmunk_fq6_ext_mul(&t1, &a, &b) == 0, "mul");
        dap_assert(chipmunk_fq6_ext_mul(&lhs, &t1, &c) == 0, "mul");
        dap_assert(chipmunk_fq6_ext_mul(&t2, &b, &c) == 0, "mul");
        dap_assert(chipmunk_fq6_ext_mul(&rhs, &a, &t2) == 0, "mul");
        dap_assert(s_ext_eq(&lhs, &rhs), "(a*b)*c == a*(b*c)");

        /* distributivity: a*(b+c) == a*b + a*c */
        dap_assert(chipmunk_fq6_ext_add(&t1, &b, &c) == 0, "add");
        dap_assert(chipmunk_fq6_ext_mul(&lhs, &a, &t1) == 0, "mul");
        dap_assert(chipmunk_fq6_ext_mul(&t1, &a, &b) == 0, "mul");
        dap_assert(chipmunk_fq6_ext_mul(&t2, &a, &c) == 0, "mul");
        dap_assert(chipmunk_fq6_ext_add(&rhs, &t1, &t2) == 0, "add");
        dap_assert(s_ext_eq(&lhs, &rhs), "a*(b+c) == a*b + a*c");

        /* multiplicative identity */
        chipmunk_fq6_ext_t one;
        chipmunk_fq6_ext_one(&one);
        dap_assert(chipmunk_fq6_ext_mul(&lhs, &a, &one) == 0, "mul one");
        dap_assert(s_ext_eq(&lhs, &a), "a*1 == a");
    }
    return true;
}

/* ---- T2: scalar F_{q⁶} inversion ---------------------------------- */

static bool s_test_scalar_inverse(void)
{
    uint32_t checked = 0u;
    for (int it = 0; it < 200 && checked < 64u; ++it) {
        int32_t coords[CHIPMUNK_FQ6_EXT_DEG];
        bool all_zero = true;
        for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
            coords[j] = s_rand_fq();
            if (coords[j] != 0) { all_zero = false; }
        }
        if (all_zero) { continue; }

        chipmunk_fq6_ext_t x, xinv, prod;
        chipmunk_fq6_ext_scalar_set_q(&x, coords, (uint64_t)CHIPMUNK_Q);

        /* a nonzero element of F_{q⁶} is always invertible (it is a field) */
        const int rc = chipmunk_fq6_ext_scalar_invert_q(&xinv, &x, (uint64_t)CHIPMUNK_Q);
        dap_assert(rc == 0, "nonzero F_{q⁶} scalar must be invertible");

        dap_assert(chipmunk_fq6_ext_mul(&prod, &x, &xinv) == 0, "mul");
        dap_assert(s_ext_is_one(&prod), "x · x⁻¹ == 1 in F_{q⁶}");

        /* the inverse must itself be scalar (degree-0 in X per Y-coeff) */
        int32_t back[CHIPMUNK_FQ6_EXT_DEG];
        dap_assert(chipmunk_fq6_ext_scalar_get_q(back, &xinv, (uint64_t)CHIPMUNK_Q) == 0,
                   "inverse of a scalar is scalar");
        ++checked;
    }
    dap_assert(checked >= 32u, "must verify ≥32 scalar inverses");

    /* zero scalar → -EDOM */
    int32_t zc[CHIPMUNK_FQ6_EXT_DEG] = {0};
    chipmunk_fq6_ext_t z, zi;
    chipmunk_fq6_ext_scalar_set_q(&z, zc, (uint64_t)CHIPMUNK_Q);
    dap_assert(chipmunk_fq6_ext_scalar_invert_q(&zi, &z, (uint64_t)CHIPMUNK_Q) == -EDOM,
               "zero scalar must report -EDOM");

    /* non-scalar element → -EINVAL on scalar_get/invert */
    chipmunk_fq6_ext_t nonscalar;
    chipmunk_fq6_ext_zero(&nonscalar);
    nonscalar.c[0].coeffs[1] = 7; /* X¹ term ⇒ not a scalar */
    int32_t tmp[CHIPMUNK_FQ6_EXT_DEG];
    dap_assert(chipmunk_fq6_ext_scalar_get_q(tmp, &nonscalar, (uint64_t)CHIPMUNK_Q) == -EINVAL,
               "non-scalar element must report -EINVAL");
    return true;
}

/* ---- T3: general per-slot inversion ------------------------------- */

static bool s_test_general_inverse(void)
{
    uint32_t checked = 0u;
    for (int it = 0; it < 16 && checked < 6u; ++it) {
        chipmunk_fq6_ext_t x, xinv, prod;
        s_rand_ext(&x);
        const int rc = chipmunk_fq6_ext_invert_q(&xinv, &x, (uint64_t)CHIPMUNK_Q, NULL);
        if (rc == -EDOM) {
            continue; /* astronomically unlikely for random x; skip if hit */
        }
        dap_assert(rc == 0, "general invert must succeed on random x");
        dap_assert(chipmunk_fq6_ext_mul(&prod, &x, &xinv) == 0, "mul");
        dap_assert(s_ext_is_one(&prod),
                   "x · x⁻¹ == 1 in R_q^{(e)} (per-slot F_{q⁶})");
        ++checked;
    }
    dap_assert(checked >= 4u, "must verify ≥4 general inverses");
    return true;
}

/* ---- T4: embed / project / is_in_base / ring-hom ------------------ */

static bool s_test_embed_project(void)
{
    for (int it = 0; it < 8; ++it) {
        chipmunk_poly_t a, b, ab, proj;
        s_rand_poly(&a);
        s_rand_poly(&b);

        chipmunk_fq6_ext_t ea, eb, eab_lhs, eab_rhs;
        chipmunk_fq6_ext_embed(&ea, &a);
        chipmunk_fq6_ext_embed(&eb, &b);

        /* embed∘project = id */
        chipmunk_fq6_ext_project(&proj, &ea);
        dap_assert(s_poly_eq(&proj, &a), "project(embed(a)) == a");

        /* embedded element is in the base ring */
        dap_assert(chipmunk_fq6_ext_is_in_base_q(&ea, (uint64_t)CHIPMUNK_Q),
                   "embedded R_q element must be in base ring");

        /* ring homomorphism: embed(a)·embed(b) == embed(a·b)
         * where a·b is the R_q product (computed via NTT here). */
        chipmunk_fq6_ext_mul(&eab_lhs, &ea, &eb);
        {
            chipmunk_poly_t la = a, lb = b;
            dap_assert(chipmunk_poly_ntt(&la) == 0, "ntt");
            dap_assert(chipmunk_poly_ntt(&lb) == 0, "ntt");
            chipmunk_poly_mul_ntt(&ab, &la, &lb);
            dap_assert(chipmunk_poly_invntt(&ab) == 0, "invntt");
        }
        chipmunk_fq6_ext_embed(&eab_rhs, &ab);
        dap_assert(s_ext_eq(&eab_lhs, &eab_rhs),
                   "embed(a)·embed(b) == embed(a·b)");
    }

    /* a genuine Y-degree-≥1 element is NOT in the base ring */
    chipmunk_fq6_ext_t y1;
    chipmunk_fq6_ext_zero(&y1);
    y1.c[1].coeffs[0] = 1; /* the element Y */
    dap_assert(!chipmunk_fq6_ext_is_in_base_q(&y1, (uint64_t)CHIPMUNK_Q),
               "element Y must NOT be in the base ring");
    return true;
}

/* ---- T5: Frobenius σ:Y↦Y² and trace (consistency lane, NOGAP §4) -- */

static bool s_test_frobenius_trace(void)
{
    /* σ is a ring automorphism on random elements */
    for (int it = 0; it < 6; ++it) {
        chipmunk_fq6_ext_t a, b, sa, sb, t1, t2, lhs, rhs;
        s_rand_ext(&a); s_rand_ext(&b);
        dap_assert(chipmunk_fq6_ext_frobenius(&sa, &a) == 0, "σ(a)");
        dap_assert(chipmunk_fq6_ext_frobenius(&sb, &b) == 0, "σ(b)");

        /* additive: σ(a+b) = σa + σb */
        dap_assert(chipmunk_fq6_ext_add(&t1, &a, &b) == 0, "add");
        dap_assert(chipmunk_fq6_ext_frobenius(&lhs, &t1) == 0, "σ(a+b)");
        dap_assert(chipmunk_fq6_ext_add(&rhs, &sa, &sb) == 0, "add");
        dap_assert(s_ext_eq(&lhs, &rhs), "σ(a+b) == σa + σb");

        /* multiplicative: σ(a·b) = σa · σb */
        dap_assert(chipmunk_fq6_ext_mul(&t2, &a, &b) == 0, "mul");
        dap_assert(chipmunk_fq6_ext_frobenius(&lhs, &t2) == 0, "σ(ab)");
        dap_assert(chipmunk_fq6_ext_mul(&rhs, &sa, &sb) == 0, "mul");
        dap_assert(s_ext_eq(&lhs, &rhs), "σ(a·b) == σa · σb");

        /* order 6: σ⁶ = id, and σ¹..σ⁵ ≠ id on a random element */
        chipmunk_fq6_ext_t cur = a;
        for (int i = 1; i <= 6; ++i) {
            dap_assert(chipmunk_fq6_ext_frobenius(&cur, &cur) == 0, "σ^i");
            if (i < 6) {
                dap_assert(!s_ext_eq(&cur, &a),
                           "σ^i ≠ id for 0 < i < 6 (σ generates order-6 group)");
            } else {
                dap_assert(s_ext_eq(&cur, &a), "σ⁶ == id");
            }
        }
    }

    /* consistency equivalence  w ∈ base ⟺ σ(w) = w */
    for (int it = 0; it < 6; ++it) {
        /* embedded base element: σ-fixed AND in base */
        chipmunk_poly_t p;
        s_rand_poly(&p);
        chipmunk_fq6_ext_t ep, sep;
        chipmunk_fq6_ext_embed(&ep, &p);
        dap_assert(chipmunk_fq6_ext_frobenius(&sep, &ep) == 0, "σ(embed)");
        dap_assert(s_ext_eq(&sep, &ep), "σ(embed(p)) == embed(p) (base ⇒ fixed)");
        dap_assert(chipmunk_fq6_ext_is_in_base_q(&ep, (uint64_t)CHIPMUNK_Q), "embed(p) in base");

        /* random general element: NOT σ-fixed AND NOT in base */
        chipmunk_fq6_ext_t g, sg;
        s_rand_ext(&g);
        dap_assert(chipmunk_fq6_ext_frobenius(&sg, &g) == 0, "σ(g)");
        dap_assert(!s_ext_eq(&sg, &g),
                   "random general element is not σ-fixed (¬fixed ⇒ ¬base)");
        dap_assert(!chipmunk_fq6_ext_is_in_base_q(&g, (uint64_t)CHIPMUNK_Q),
                   "random general element not in base");
    }

    /* deliberate non-base element (only Y¹ term): not fixed, not in base */
    chipmunk_fq6_ext_t y;
    chipmunk_fq6_ext_zero(&y);
    y.c[1].coeffs[0] = 1;
    chipmunk_fq6_ext_t sy;
    dap_assert(chipmunk_fq6_ext_frobenius(&sy, &y) == 0, "σ(Y)");
    dap_assert(!s_ext_eq(&sy, &y), "σ(Y) ≠ Y");
    dap_assert(!chipmunk_fq6_ext_is_in_base_q(&y, (uint64_t)CHIPMUNK_Q), "Y not in base");

    /* trace: Tr(embed(p)) = e·p, and additivity Tr(a+b)=Tr(a)+Tr(b) */
    for (int it = 0; it < 6; ++it) {
        chipmunk_poly_t p, tr;
        s_rand_poly(&p);
        chipmunk_fq6_ext_t ep;
        chipmunk_fq6_ext_embed(&ep, &p);
        dap_assert(chipmunk_fq6_ext_trace(&tr, &ep) == 0, "Tr(embed)");
        chipmunk_poly_t ep_times_e;
        for (int i = 0; i < CHIPMUNK_N; ++i) {
            ep_times_e.coeffs[i] =
                s_canon((int32_t)((int64_t)CHIPMUNK_FQ6_EXT_DEG * p.coeffs[i]
                                  % (int64_t)CHIPMUNK_Q));
        }
        dap_assert(s_poly_eq(&tr, &ep_times_e), "Tr(embed(p)) == e·p");

        chipmunk_fq6_ext_t a, b, ab;
        chipmunk_poly_t ta, tb, tab, ta_plus_tb;
        s_rand_ext(&a); s_rand_ext(&b);
        dap_assert(chipmunk_fq6_ext_add(&ab, &a, &b) == 0, "add");
        dap_assert(chipmunk_fq6_ext_trace(&ta, &a) == 0, "Tr(a)");
        dap_assert(chipmunk_fq6_ext_trace(&tb, &b) == 0, "Tr(b)");
        dap_assert(chipmunk_fq6_ext_trace(&tab, &ab) == 0, "Tr(a+b)");
        dap_assert(chipmunk_poly_add(&ta_plus_tb, &ta, &tb) == 0, "poly add");
        dap_assert(s_poly_eq(&tab, &ta_plus_tb), "Tr(a+b) == Tr(a)+Tr(b)");
    }
    return true;
}

int main(void)
{
    dap_set_appname("test_chipmunk_fq6_ext");
    dap_common_init("test_chipmunk_fq6_ext", NULL);
    /* Initialise crypto subsystem (SIMD dispatch, chipmunk, etc.) */
    dap_enc_init();

    int rc = 0;
    if (!s_test_irreducible())     rc = 1;
    if (!s_test_ring_axioms())     rc = 1;
    if (!s_test_scalar_inverse())  rc = 1;
    if (!s_test_general_inverse()) rc = 1;
    if (!s_test_embed_project())   rc = 1;
    if (!s_test_frobenius_trace()) rc = 1;

    if (rc == 0) {
        log_it(L_INFO,
               "MRNG G3.1 §9.2 + M4.0a R_q^{(e)} arithmetic tests PASSED "
               "(Φ₉ irreducible, ring axioms, scalar & general inversion, "
               "embed/project/is_in_base, ring homomorphism, Frobenius "
               "σ:Y↦Y² order-6 automorphism + trace consistency lane)");
    }
    dap_common_deinit();
    return rc;
}
