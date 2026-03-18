/*
 * Unit tests for dap_math_bignum: NTT, NTT16, poly, and multi-precision arithmetic.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <stdint.h>

#include "dap_test.h"
#include "dap_ntt.h"
#include "dap_poly.h"
#include "dap_bignum.h"

/* ===== Test NTT32 parameters (N=8, q=17, omega=3, Chipmunk-style) ===== */

static const int32_t s_test_zetas_fwd[16] = {
    1, 16, 13, 4, 9, 8, 15, 2, 3, 14, 5, 12, 10, 7, 11, 6
};
static const int32_t s_test_zetas_inv[16] = {
    1, 16, 4, 13, 2, 15, 8, 9, 6, 11, 7, 10, 12, 5, 14, 3
};
static const dap_ntt_params_t s_test_ntt_params = {
    .n            = 8,
    .q            = 17,
    .qinv         = 15,
    .mont_r_bits  = 4,
    .mont_r_mask  = 0xF,
    .one_over_n   = 15,
    .zetas        = s_test_zetas_fwd,
    .zetas_inv    = s_test_zetas_inv,
    .zetas_len    = 16,
};

/* ===== Test NTT16 parameters (N=8, q=17, psi=3, Kyber-style) ===== */

static const int16_t s_test16_zetas[4] = {1, 13, 9, 15};
static const int16_t s_test16_zetas_inv[4] = {2, 8, 4, 13};
static const dap_ntt_params16_t s_test16_ntt_params = {
    .n          = 8,
    .q          = 17,
    .qinv       = -3855,
    .zetas      = s_test16_zetas,
    .zetas_inv  = s_test16_zetas_inv,
    .zetas_len  = 4,
};

/* ===== NTT32 tests ===== */

static void s_test_ntt_symmetry(void)
{
    dap_print_module_name("NTT32 forward/inverse symmetry");
    const dap_ntt_params_t *l_p = &s_test_ntt_params;

    int32_t l_poly[8] = {0};
    int32_t l_orig[8] = {0};

    l_poly[0] = 1; l_orig[0] = 1;
    dap_ntt_forward(l_poly, l_p);

    int l_all_same = 1;
    for (uint32_t i = 1; i < l_p->n; i++)
        if (l_poly[i] != l_poly[0]) { l_all_same = 0; break; }
    dap_assert(l_all_same, "NTT(delta) yields constant polynomial");

    dap_ntt_inverse(l_poly, l_p);
    int l_match = 1;
    for (uint32_t i = 0; i < l_p->n; i++)
        if (l_poly[i] != l_orig[i]) { l_match = 0; break; }
    dap_assert(l_match, "invNTT(NTT(delta)) == delta");

    for (uint32_t i = 0; i < l_p->n; i++) { l_poly[i] = 42; l_orig[i] = 42; }
    dap_ntt_forward(l_poly, l_p);
    dap_ntt_inverse(l_poly, l_p);
    l_match = 1;
    for (uint32_t i = 0; i < l_p->n; i++) {
        int32_t l_a = ((l_orig[i] % l_p->q) + l_p->q) % l_p->q;
        int32_t l_b = ((l_poly[i] % l_p->q) + l_p->q) % l_p->q;
        if (l_a != l_b) { l_match = 0; break; }
    }
    dap_assert(l_match, "invNTT(NTT([42,42,...])) == [42,42,...] (mod q)");

    srand(12345);
    for (uint32_t i = 0; i < l_p->n; i++) {
        l_orig[i] = rand() % l_p->q;
        l_poly[i] = l_orig[i];
    }
    dap_ntt_forward(l_poly, l_p);
    dap_ntt_inverse(l_poly, l_p);
    l_match = 1;
    for (uint32_t i = 0; i < l_p->n; i++) {
        int32_t l_a = l_orig[i] % l_p->q;
        int32_t l_b = ((l_poly[i] % l_p->q) + l_p->q) % l_p->q;
        if (l_a < 0) l_a += l_p->q;
        if (l_a != l_b) { l_match = 0; break; }
    }
    dap_assert(l_match, "invNTT(NTT(random)) == random (mod q)");
}

static void s_test_ntt_montgomery_reduce(void)
{
    dap_print_module_name("NTT32 Montgomery reduction");
    const dap_ntt_params_t *l_p = &s_test_ntt_params;

    int32_t l_r = dap_ntt_montgomery_reduce(0, l_p);
    dap_assert(l_r == 0, "montgomery_reduce(0) == 0");

    l_r = dap_ntt_montgomery_reduce(l_p->q, l_p);
    dap_assert(l_r >= 0 && l_r < l_p->q, "montgomery_reduce(q) in [0, q)");
}

static void s_test_ntt_barrett_reduce(void)
{
    dap_print_module_name("NTT32 Barrett reduction");
    const dap_ntt_params_t *l_p = &s_test_ntt_params;

    dap_assert(dap_ntt_barrett_reduce(0, l_p) == 0, "barrett(0) == 0");
    dap_assert(dap_ntt_barrett_reduce(l_p->q, l_p) == 0, "barrett(q) == 0");
    dap_assert(dap_ntt_barrett_reduce(1, l_p) == 1, "barrett(1) == 1");
    dap_assert(dap_ntt_barrett_reduce(l_p->q + 5, l_p) == 5, "barrett(q+5) == 5");
}

static void s_test_ntt_pointwise(void)
{
    dap_print_module_name("NTT32 pointwise multiply");
    const dap_ntt_params_t *l_p = &s_test_ntt_params;

    int32_t l_a[8], l_b[8], l_c[8];
    for (uint32_t i = 0; i < l_p->n; i++) { l_a[i] = 1; l_b[i] = 1; }
    dap_ntt_pointwise_montgomery(l_c, l_a, l_b, l_p);

    int l_all_valid = 1;
    for (uint32_t i = 0; i < l_p->n; i++)
        if (l_c[i] < 0 || l_c[i] >= l_p->q) { l_all_valid = 0; break; }
    dap_assert(l_all_valid, "pointwise(1,1) yields values in [0, q)");
}

/* ===== NTT16 tests ===== */

static void s_test_ntt16_symmetry(void)
{
    dap_print_module_name("NTT16 forward/inverse symmetry");
    const dap_ntt_params16_t *l_p = &s_test16_ntt_params;

    int16_t l_poly[8], l_orig[8];
    memset(l_poly, 0, sizeof(l_poly));
    l_poly[0] = 1;
    memcpy(l_orig, l_poly, sizeof(l_poly));

    dap_ntt16_forward(l_poly, l_p);
    int l_ok = 1;
    for (int i = 0; i < 8; i += 2)
        if (l_poly[i] != 1 || l_poly[i + 1] != 0) l_ok = 0;
    dap_assert(l_ok, "NTT16(delta) yields [1,0,1,0,1,0,1,0]");

    dap_ntt16_inverse(l_poly, l_p);
    l_ok = 1;
    for (int i = 0; i < 8; i++) {
        int16_t l_v = ((l_poly[i] % l_p->q) + l_p->q) % l_p->q;
        int16_t l_e = ((l_orig[i] % l_p->q) + l_p->q) % l_p->q;
        if (l_v != l_e) l_ok = 0;
    }
    dap_assert(l_ok, "INTT16(NTT16(delta)) == delta (mod q)");

    srand(42);
    for (int i = 0; i < 8; i++) {
        l_orig[i] = (int16_t)(rand() % l_p->q);
        l_poly[i] = l_orig[i];
    }
    dap_ntt16_forward(l_poly, l_p);
    dap_ntt16_inverse(l_poly, l_p);
    l_ok = 1;
    for (int i = 0; i < 8; i++) {
        int16_t l_v = ((l_poly[i] % l_p->q) + l_p->q) % l_p->q;
        int16_t l_e = ((l_orig[i] % l_p->q) + l_p->q) % l_p->q;
        if (l_v != l_e) { l_ok = 0; break; }
    }
    dap_assert(l_ok, "INTT16(NTT16(random)) == random (mod q)");
}

static void s_test_ntt16_montgomery_reduce(void)
{
    dap_print_module_name("NTT16 Montgomery reduction");
    const dap_ntt_params16_t *l_p = &s_test16_ntt_params;

    int16_t l_r = dap_ntt16_montgomery_reduce(0, l_p);
    dap_assert(l_r == 0, "mont16_reduce(0) == 0");

    l_r = dap_ntt16_fqmul(5, 3, l_p);
    int16_t l_expected = (int16_t)(((int32_t)5 * 3) % l_p->q);
    dap_assert(((l_r % l_p->q) + l_p->q) % l_p->q == l_expected,
               "fqmul16(5,3) correct in Montgomery domain");
}

static void s_test_ntt16_basemul(void)
{
    dap_print_module_name("NTT16 basemul");
    const dap_ntt_params16_t *l_p = &s_test16_ntt_params;

    int16_t l_a[2] = {1, 0};
    int16_t l_b[2] = {1, 0};
    int16_t l_r[2];
    dap_ntt16_basemul(l_r, l_a, l_b, s_test16_zetas[2], l_p);

    int16_t l_r0 = ((l_r[0] % l_p->q) + l_p->q) % l_p->q;
    int16_t l_r1 = ((l_r[1] % l_p->q) + l_p->q) % l_p->q;
    dap_assert(l_r0 != 0 || l_r1 == 0,
               "basemul16([1,0]*[1,0]) produces valid result");
}

/* ===== Poly tests ===== */

static void s_test_poly_add_sub(void)
{
    dap_print_module_name("Poly add/sub");
    const int32_t l_q = 3168257;
    const uint32_t l_n = 8;
    int32_t l_a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    int32_t l_b[8] = {8, 7, 6, 5, 4, 3, 2, 1};
    int32_t l_r[8];

    dap_poly_add(l_r, l_a, l_b, l_n, l_q);
    int l_ok = 1;
    for (uint32_t i = 0; i < l_n; i++)
        if (l_r[i] != 9) l_ok = 0;
    dap_assert(l_ok, "add: [1..8]+[8..1] == [9,9,...,9]");

    dap_poly_sub(l_r, l_a, l_a, l_n, l_q);
    l_ok = 1;
    for (uint32_t i = 0; i < l_n; i++)
        if (l_r[i] != 0) l_ok = 0;
    dap_assert(l_ok, "sub: a - a == 0");

    int32_t l_c[8] = {0};
    int32_t l_d[8] = {1, 0, 0, 0, 0, 0, 0, 0};
    dap_poly_sub(l_r, l_c, l_d, l_n, l_q);
    dap_assert(l_r[0] == l_q - 1, "sub: 0 - 1 == q-1 (mod q)");
}

static void s_test_poly_reduce(void)
{
    dap_print_module_name("Poly reduce");
    const int32_t l_q = 17;
    int32_t l_r[4] = {20, -3, 34, 0};
    dap_poly_reduce(l_r, 4, l_q);
    dap_assert(l_r[0] == 3, "reduce(20) mod 17 == 3");
    dap_assert(l_r[1] == 14, "reduce(-3) mod 17 == 14");
    dap_assert(l_r[2] == 0, "reduce(34) mod 17 == 0");
    dap_assert(l_r[3] == 0, "reduce(0) mod 17 == 0");
}

/* ===== Bignum tests ===== */

static void s_test_bignum_set_cmp(void)
{
    dap_print_module_name("Bignum set/cmp");
    BN(l_a); BN(l_b);

    BN_SET_U64(l_a, 0);
    dap_assert(BN_IS_ZERO(l_a), "set_u64(0) is zero");

    BN_SET_U64(l_a, 42);
    BN_SET_U64(l_b, 42);
    dap_assert(BN_CMP(l_a, l_b) == 0, "42 == 42");

    BN_SET_U64(l_b, 100);
    dap_assert(BN_CMP(l_a, l_b) < 0, "42 < 100");
    dap_assert(BN_CMP(l_b, l_a) > 0, "100 > 42");

    BN_SET_I32(l_a, -5);
    BN_SET_I32(l_b, 3);
    dap_assert(BN_CMP(l_a, l_b) < 0, "-5 < 3");
}

static void s_test_bignum_add_sub(void)
{
    dap_print_module_name("Bignum add/sub");
    BN(l_a); BN(l_b); BN(l_r); BN(l_expected);

    BN_SET_U64(l_a, 100);
    BN_SET_U64(l_b, 200);
    BN_ADD(l_r, l_a, l_b);
    BN_SET_U64(l_expected, 300);
    dap_assert(BN_CMP(l_r, l_expected) == 0, "100 + 200 == 300");

    BN_SUB(l_r, l_b, l_a);
    BN_SET_U64(l_expected, 100);
    dap_assert(BN_CMP(l_r, l_expected) == 0, "200 - 100 == 100");

    BN_SUB(l_r, l_a, l_b);
    BN_SET_I32(l_expected, -100);
    dap_assert(BN_CMP(l_r, l_expected) == 0, "100 - 200 == -100");

    BN_SET_U64(l_a, 0xFFFFFFFF);
    BN_SET_U64(l_b, 1);
    BN_ADD(l_r, l_a, l_b);
    BN_SET_U64(l_expected, 0x100000000ULL);
    dap_assert(BN_CMP(l_r, l_expected) == 0, "0xFFFFFFFF + 1 == 0x100000000");
}

static void s_test_bignum_mul(void)
{
    dap_print_module_name("Bignum mul");
    BN(l_a); BN(l_b); BN(l_r); BN(l_expected);

    BN_SET_U64(l_a, 12345);
    BN_SET_U64(l_b, 67890);
    BN_MUL(l_r, l_a, l_b);
    BN_SET_U64(l_expected, 12345ULL * 67890ULL);
    dap_assert(BN_CMP(l_r, l_expected) == 0, "12345 * 67890 == 838102050");

    BN_SET_U64(l_a, (uint64_t)1 << 32);
    BN_SET_U64(l_b, (uint64_t)1 << 32);
    BN_MUL(l_r, l_a, l_b);
    dap_assert(l_r.nlimbs == 3, "2^32 * 2^32 has 3 limbs");
    dap_assert(l_r.limbs[2] == 1 && l_r.limbs[1] == 0 && l_r.limbs[0] == 0,
               "2^32 * 2^32 == 2^64");

    BN_SET_I32(l_a, -7);
    BN_SET_U64(l_b, 3);
    BN_MUL(l_r, l_a, l_b);
    BN_SET_I32(l_expected, -21);
    dap_assert(BN_CMP(l_r, l_expected) == 0, "-7 * 3 == -21");
}

static void s_test_bignum_mod(void)
{
    dap_print_module_name("Bignum mod");
    BN(l_a); BN(l_m); BN(l_r); BN(l_expected);

    BN_SET_U64(l_a, 100);
    BN_SET_U64(l_m, 7);
    BN_MOD(l_r, l_a, l_m);
    BN_SET_U64(l_expected, 2);
    dap_assert(BN_CMP(l_r, l_expected) == 0, "100 mod 7 == 2");

    BN_SET_U64(l_a, 0);
    BN_MOD(l_r, l_a, l_m);
    dap_assert(BN_IS_ZERO(l_r), "0 mod 7 == 0");

    BN_SET_I32(l_a, -1);
    BN_MOD(l_r, l_a, l_m);
    BN_SET_U64(l_expected, 6);
    dap_assert(BN_CMP(l_r, l_expected) == 0, "-1 mod 7 == 6");
}

static void s_test_bignum_mod_exp(void)
{
    dap_print_module_name("Bignum mod_exp");
    BN(l_base); BN(l_exp); BN(l_mod); BN(l_r); BN(l_expected);

    BN_SET_U64(l_base, 2);
    BN_SET_U64(l_exp, 10);
    BN_SET_U64(l_mod, 1000);
    BN_MOD_EXP(l_r, l_base, l_exp, l_mod);
    BN_SET_U64(l_expected, 24);
    dap_assert(BN_CMP(l_r, l_expected) == 0, "2^10 mod 1000 == 24");

    BN_SET_U64(l_base, 3);
    BN_SET_U64(l_exp, 0);
    BN_SET_U64(l_mod, 17);
    BN_MOD_EXP(l_r, l_base, l_exp, l_mod);
    BN_SET_U64(l_expected, 1);
    dap_assert(BN_CMP(l_r, l_expected) == 0, "3^0 mod 17 == 1");

    BN_SET_U64(l_base, 7);
    BN_SET_U64(l_exp, 16);
    BN_SET_U64(l_mod, 17);
    BN_MOD_EXP(l_r, l_base, l_exp, l_mod);
    BN_SET_U64(l_expected, 1);
    dap_assert(BN_CMP(l_r, l_expected) == 0, "Fermat: 7^16 mod 17 == 1");
}

static void s_test_bignum_gcd(void)
{
    dap_print_module_name("Bignum gcd");
    BN(l_a); BN(l_b); BN(l_r); BN(l_expected);

    BN_SET_U64(l_a, 48);
    BN_SET_U64(l_b, 18);
    BN_GCD(l_r, l_a, l_b);
    BN_SET_U64(l_expected, 6);
    dap_assert(BN_CMP(l_r, l_expected) == 0, "gcd(48, 18) == 6");

    BN_SET_U64(l_a, 17);
    BN_SET_U64(l_b, 13);
    BN_GCD(l_r, l_a, l_b);
    BN_SET_U64(l_expected, 1);
    dap_assert(BN_CMP(l_r, l_expected) == 0, "gcd(17, 13) == 1 (coprime)");
}

static void s_test_bignum_mod_inv(void)
{
    dap_print_module_name("Bignum mod_inv");
    BN(l_a); BN(l_m); BN(l_inv); BN(l_r); BN(l_expected);

    BN_SET_U64(l_a, 3);
    BN_SET_U64(l_m, 7);
    int l_rc = BN_MOD_INV(l_inv, l_a, l_m);
    dap_assert(l_rc == 0, "mod_inv(3, 7) succeeds");

    BN_MUL(l_r, l_a, l_inv);
    BN_MOD(l_r, l_r, l_m);
    BN_SET_U64(l_expected, 1);
    dap_assert(BN_CMP(l_r, l_expected) == 0, "3 * inv(3) mod 7 == 1");

    BN_SET_U64(l_a, 4);
    BN_SET_U64(l_m, 8);
    l_rc = BN_MOD_INV(l_inv, l_a, l_m);
    dap_assert(l_rc != 0, "mod_inv(4, 8) fails (no inverse)");
}

/* ===== Benchmarks ===== */

static void s_bench_ntt_forward(void)
{
    int32_t l_poly[8];
    for (uint32_t i = 0; i < 8; i++)
        l_poly[i] = (int32_t)(i % s_test_ntt_params.q);
    dap_ntt_forward(l_poly, &s_test_ntt_params);
}

static void s_bench_ntt_inverse(void)
{
    int32_t l_poly[8];
    for (uint32_t i = 0; i < 8; i++)
        l_poly[i] = (int32_t)(i % s_test_ntt_params.q);
    dap_ntt_inverse(l_poly, &s_test_ntt_params);
}

static void s_bench_bignum_mul(void)
{
    BN(l_a); BN(l_b); BN(l_r);
    BN_SET_U64(l_a, 0xDEADBEEFCAFEBABEULL);
    BN_SET_U64(l_b, 0x123456789ABCDEF0ULL);
    BN_MUL(l_r, l_a, l_b);
}

static void s_bench_bignum_mod_exp(void)
{
    BN(l_base); BN(l_exp); BN(l_mod); BN(l_r);
    BN_SET_U64(l_base, 123456789);
    BN_SET_U64(l_exp, 65537);
    BN_SET_U64(l_mod, 3168257);
    BN_MOD_EXP(l_r, l_base, l_exp, l_mod);
}

/* ===== Main ===== */

int main(void)
{
    dap_print_module_name("dap_math_bignum");

    s_test_ntt_symmetry();
    s_test_ntt_montgomery_reduce();
    s_test_ntt_barrett_reduce();
    s_test_ntt_pointwise();

    s_test_ntt16_symmetry();
    s_test_ntt16_montgomery_reduce();
    s_test_ntt16_basemul();

    s_test_poly_add_sub();
    s_test_poly_reduce();

    s_test_bignum_set_cmp();
    s_test_bignum_add_sub();
    s_test_bignum_mul();
    s_test_bignum_mod();
    s_test_bignum_mod_exp();
    s_test_bignum_gcd();
    s_test_bignum_mod_inv();

    dap_print_module_name("dap_math_bignum benchmarks");
    benchmark_mgs_time("NTT32 forward", benchmark_test_time(s_bench_ntt_forward, 10000));
    benchmark_mgs_time("NTT32 inverse", benchmark_test_time(s_bench_ntt_inverse, 10000));
    benchmark_mgs_time("bignum_mul (64x64)", benchmark_test_time(s_bench_bignum_mul, 100000));
    benchmark_mgs_time("bignum_mod_exp", benchmark_test_time(s_bench_bignum_mod_exp, 1000));

    return 0;
}
