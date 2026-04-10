/*
 * Unit tests for dap_math_bignum: NTT, NTT16, poly, and multi-precision arithmetic.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "dap_test.h"
#include "dap_ntt.h"
#include "dap_ntt_internal.h"
#include "dap_poly.h"
#include "dap_bignum.h"
#include "dap_cpu_arch.h"

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

/* ===== NTT32 SIMD correctness: compare SIMD output to reference (Dilithium N=256) ===== */

#define DILITHIUM_N 256
#define DILITHIUM_Q 8380417

static const int32_t s_dil_zetas[DILITHIUM_N] = {
    0, 25847, 5771523, 7861508, 237124, 7602457, 7504169, 466468,
    1826347, 2353451, 8021166, 6288512, 3119733, 5495562, 3111497, 2680103,
    2725464, 1024112, 7300517, 3585928, 7830929, 7260833, 2619752, 6271868,
    6262231, 4520680, 6980856, 5102745, 1757237, 8360995, 4010497,  280005,
    2706023,   95776, 3077325, 3530437, 6718724, 4788269, 5842901, 3915439,
    4519302, 5336701, 3574422, 5512770, 3539968, 8079950, 2348700, 7841118,
    6681150, 6736599, 3505694, 4558682, 3507263, 6239768, 6779997, 3699596,
     811944,  531354,  954230, 3881043, 3900724, 5823537, 2071892, 5582638,
    4450022, 6851714, 4702672, 5339162, 6927966, 3475950, 2176455, 6795196,
    7122806, 1939314, 4296819, 7380215, 5190273, 5223087, 4747489,  126922,
    3412210, 7396998, 2147896, 2715295, 5412772, 4686924, 7969390, 5903370,
    7709315, 7151892, 8357436, 7072248, 7998430, 1349076, 1852771, 6949987,
    5037034,  264944,  508951, 3097992,   44288, 7280319,  904516, 3958618,
    4656075, 8371839, 1653064, 5130689, 2389356, 8169440,  759969, 7063561,
     189548, 4827145, 3159746, 6529015, 5971092, 8202977, 1315589, 1341330,
    1285669, 6795489, 7567685, 6940675, 5361315, 4499357, 4751448, 3839961,
    2091667, 3407706, 2316500, 3817976, 5037939, 2244091, 5933984, 4817955,
     266997, 2434439, 7144689, 3513181, 4860065, 4621053, 7183191, 5187039,
     900702, 1859098,  909542,  819034,  495491, 6767243, 8337157, 7857917,
    7725090, 5257975, 2031748, 3207046, 4823422, 7855319, 7611795, 4784579,
     342297,  286988, 5942594, 4108315, 3437287, 5038140, 1735879,  203044,
    2842341, 2691481, 5790267, 1265009, 4055324, 1247620, 2486353, 1595974,
    4613401, 1250494, 2635921, 4832145, 5386378, 1869119, 1903435, 7329447,
    7047359, 1237275, 5062207, 6950192, 7929317, 1312455, 3306115, 6417775,
    7100756, 1917081, 5834105, 7005614, 1500165,  777191, 2235880, 3406031,
    7838005, 5548557, 6709241, 6533464, 5796124, 4656147,  594136, 4603424,
    6366809, 2432395, 2454455, 8215696, 1957272, 3369112,  185531, 7173032,
    5196991,  162844, 1616392, 3014001,  810149, 1652634, 4686184, 6581310,
    5341501, 3523897, 3866901,  269760, 2213111, 7404533, 1717735,  472078,
    7953734, 1723600, 6577327, 1910376, 6712985, 7276084, 8119771, 4546524,
    5441381, 6144432, 7959518, 6094090,  183443, 7403526, 1612842, 4834730,
    7826001, 3919660, 8332111, 7018208, 3937738, 1400424, 7534263, 1976782
};

static const int32_t s_dil_zetas_inv[DILITHIUM_N] = {
    6403635,  846154, 6979993, 4442679, 1362209,   48306, 4460757,  554416,
    3545687, 6767575,  976891, 8196974, 2286327,  420899, 2235985, 2939036,
    3833893,  260646, 1104333, 1667432, 6470041, 1803090, 6656817,  426683,
    7908339, 6662682,  975884, 6167306, 8110657, 4513516, 4856520, 3038916,
    1799107, 3694233, 6727783, 7570268, 5366416, 6764025, 8217573, 3183426,
    1207385, 8194886, 5011305, 6423145,  164721, 5925962, 5948022, 2013608,
    3776993, 7786281, 3724270, 2584293, 1846953, 1671176, 2831860,  542412,
    4974386, 6144537, 7603226, 6880252, 1374803, 2546312, 6463336, 1279661,
    1962642, 5074302, 7067962,  451100, 1430225, 3318210, 7143142, 1333058,
    1050970, 6476982, 6511298, 2994039, 3548272, 5744496, 7129923, 3767016,
    6784443, 5894064, 7132797, 4325093, 7115408, 2590150, 5688936, 5538076,
    8177373, 6644538, 3342277, 4943130, 4272102, 2437823, 8093429, 8038120,
    3595838,  768622,  525098, 3556995, 5173371, 6348669, 3122442,  655327,
     522500,   43260, 1613174, 7884926, 7561383, 7470875, 6521319, 7479715,
    3193378, 1197226, 3759364, 3520352, 4867236, 1235728, 5945978, 8113420,
    3562462, 2446433, 6136326, 3342478, 4562441, 6063917, 4972711, 6288750,
    4540456, 3628969, 3881060, 3019102, 1439742,  812732, 1584928, 7094748,
    7039087, 7064828,  177440, 2409325, 1851402, 5220671, 3553272, 8190869,
    1316856, 7620448,  210977, 5991061, 3249728, 6727353,    8578, 3724342,
    4421799, 7475901, 1100098, 8336129, 5282425, 7871466, 8115473, 3343383,
    1430430, 6527646, 7031341,  381987, 1308169,   22981, 1228525,  671102,
    2477047,  411027, 3693493, 2967645, 5665122, 6232521,  983419, 4968207,
    8253495, 3632928, 3157330, 3190144, 1000202, 4083598, 6441103, 1257611,
    1585221, 6203962, 4904467, 1452451, 3041255, 3677745, 1528703, 3930395,
    2797779, 6308525, 2556880, 4479693, 4499374, 7426187, 7849063, 7568473,
    4680821, 1600420, 2140649, 4873154, 3821735, 4874723, 1643818, 1699267,
     539299, 6031717,  300467, 4840449, 2867647, 4805995, 3043716, 3861115,
    4464978, 2537516, 3592148, 1661693, 4849980, 5303092, 8284641, 5674394,
    8100412, 4369920,   19422, 6623180, 3277672, 1399561, 3859737, 2118186,
    2108549, 5760665, 1119584,  549488, 4794489, 1079900, 7356305, 5654953,
    5700314, 5268920, 2884855, 5260684, 2091905,  359251, 6026966, 6554070,
    7913949,  876248,  777960, 8143293,  518909, 2608894, 8354570
};

static const dap_ntt_params_t s_dil_ntt_params = {
    .n            = DILITHIUM_N,
    .q            = DILITHIUM_Q,
    .qinv         = 4236238847U,
    .mont_r_bits  = 32,
    .mont_r_mask  = 0xFFFFFFFF,
    .one_over_n   = 8347681,
    .zetas        = s_dil_zetas,
    .zetas_inv    = s_dil_zetas_inv,
    .zetas_len    = DILITHIUM_N,
};

static void s_test_ntt32_mont_simd_correctness(void)
{
    dap_print_module_name("NTT32 Mont SIMD correctness vs reference (Dilithium N=256)");

    int32_t l_ref[DILITHIUM_N], l_simd[DILITHIUM_N];
    srand(12345);
    for (int i = 0; i < DILITHIUM_N; i++)
        l_ref[i] = (int32_t)(rand() % DILITHIUM_Q);
    memcpy(l_simd, l_ref, sizeof(l_ref));

    dap_ntt_forward_mont_ref(l_ref, &s_dil_ntt_params);
    dap_ntt_forward_mont(l_simd, &s_dil_ntt_params);
    dap_assert(memcmp(l_ref, l_simd, sizeof(l_ref)) == 0,
               "NTT32 forward_mont: dispatch matches reference");

    memcpy(l_simd, l_ref, sizeof(l_ref));
    int32_t l_ref2[DILITHIUM_N];
    memcpy(l_ref2, l_ref, sizeof(l_ref));

    dap_ntt_inverse_mont_ref(l_ref2, &s_dil_ntt_params);
    dap_ntt_inverse_mont(l_simd, &s_dil_ntt_params);
    dap_assert(memcmp(l_ref2, l_simd, sizeof(l_ref2)) == 0,
               "NTT32 inverse_mont: dispatch matches reference");

    srand(54321);
    int32_t l_a[DILITHIUM_N], l_b[DILITHIUM_N], l_c_ref[DILITHIUM_N], l_c_simd[DILITHIUM_N];
    for (int i = 0; i < DILITHIUM_N; i++) {
        l_a[i] = (int32_t)(rand() % DILITHIUM_Q);
        l_b[i] = (int32_t)(rand() % DILITHIUM_Q);
    }
    dap_ntt_pointwise_montgomery_ref(l_c_ref, l_a, l_b, &s_dil_ntt_params);
    dap_ntt_pointwise_montgomery(l_c_simd, l_a, l_b, &s_dil_ntt_params);
    dap_assert(memcmp(l_c_ref, l_c_simd, sizeof(l_c_ref)) == 0,
               "NTT32 pointwise_montgomery: dispatch matches reference");

#if DAP_PLATFORM_X86
    srand(12345);
    for (int i = 0; i < DILITHIUM_N; i++)
        l_ref[i] = (int32_t)(rand() % DILITHIUM_Q);
    memcpy(l_simd, l_ref, sizeof(l_ref));

    dap_ntt_forward_mont_ref(l_ref, &s_dil_ntt_params);
    dap_ntt_forward_mont_avx2(l_simd, &s_dil_ntt_params);
    dap_assert(memcmp(l_ref, l_simd, sizeof(l_ref)) == 0,
               "NTT32 forward_mont: AVX2 matches reference");

    memcpy(l_simd, l_ref, sizeof(l_ref));
    memcpy(l_ref2, l_ref, sizeof(l_ref));
    dap_ntt_inverse_mont_ref(l_ref2, &s_dil_ntt_params);
    dap_ntt_inverse_mont_avx2(l_simd, &s_dil_ntt_params);
    dap_assert(memcmp(l_ref2, l_simd, sizeof(l_ref2)) == 0,
               "NTT32 inverse_mont: AVX2 matches reference");

    srand(54321);
    for (int i = 0; i < DILITHIUM_N; i++) {
        l_a[i] = (int32_t)(rand() % DILITHIUM_Q);
        l_b[i] = (int32_t)(rand() % DILITHIUM_Q);
    }
    dap_ntt_pointwise_montgomery_ref(l_c_ref, l_a, l_b, &s_dil_ntt_params);
    dap_ntt_pointwise_montgomery_avx2(l_c_simd, l_a, l_b, &s_dil_ntt_params);
    dap_assert(memcmp(l_c_ref, l_c_simd, sizeof(l_c_ref)) == 0,
               "NTT32 pointwise_montgomery: AVX2 matches reference");

    srand(12345);
    for (int i = 0; i < DILITHIUM_N; i++)
        l_ref[i] = (int32_t)(rand() % DILITHIUM_Q);
    memcpy(l_simd, l_ref, sizeof(l_ref));

    dap_ntt_forward_mont_ref(l_ref, &s_dil_ntt_params);
    dap_ntt_forward_mont_avx512(l_simd, &s_dil_ntt_params);
    dap_assert(memcmp(l_ref, l_simd, sizeof(l_ref)) == 0,
               "NTT32 forward_mont: AVX-512 matches reference");

    memcpy(l_simd, l_ref, sizeof(l_ref));
    memcpy(l_ref2, l_ref, sizeof(l_ref));
    dap_ntt_inverse_mont_ref(l_ref2, &s_dil_ntt_params);
    dap_ntt_inverse_mont_avx512(l_simd, &s_dil_ntt_params);
    dap_assert(memcmp(l_ref2, l_simd, sizeof(l_ref2)) == 0,
               "NTT32 inverse_mont: AVX-512 matches reference");

    srand(54321);
    for (int i = 0; i < DILITHIUM_N; i++) {
        l_a[i] = (int32_t)(rand() % DILITHIUM_Q);
        l_b[i] = (int32_t)(rand() % DILITHIUM_Q);
    }
    dap_ntt_pointwise_montgomery_ref(l_c_ref, l_a, l_b, &s_dil_ntt_params);
    dap_ntt_pointwise_montgomery_avx512(l_c_simd, l_a, l_b, &s_dil_ntt_params);
    dap_assert(memcmp(l_c_ref, l_c_simd, sizeof(l_c_ref)) == 0,
               "NTT32 pointwise_montgomery: AVX-512 matches reference");
#endif
}

/* ===== NTT32 Dilithium N=256 benchmark data ===== */

static int32_t s_bench32_poly[DILITHIUM_N];
static int32_t s_bench32_poly_b[DILITHIUM_N];
static int32_t s_bench32_poly_c[DILITHIUM_N];

static void s_bench32_fill(void)
{
    for (int i = 0; i < DILITHIUM_N; i++) {
        s_bench32_poly[i] = (int32_t)(rand() % DILITHIUM_Q);
        s_bench32_poly_b[i] = (int32_t)(rand() % DILITHIUM_Q);
    }
}

static void s_bench32_ntt_ref(void)
{
    dap_ntt_forward_mont_ref(s_bench32_poly, &s_dil_ntt_params);
}

static void s_bench32_intt_ref(void)
{
    dap_ntt_inverse_mont_ref(s_bench32_poly, &s_dil_ntt_params);
}

static void s_bench32_ntt_dispatch(void)
{
    dap_ntt_forward_mont(s_bench32_poly, &s_dil_ntt_params);
}

static void s_bench32_intt_dispatch(void)
{
    dap_ntt_inverse_mont(s_bench32_poly, &s_dil_ntt_params);
}

static void s_bench32_pointwise_ref(void)
{
    dap_ntt_pointwise_montgomery_ref(s_bench32_poly_c, s_bench32_poly,
                                     s_bench32_poly_b, &s_dil_ntt_params);
}

static void s_bench32_pointwise_dispatch(void)
{
    dap_ntt_pointwise_montgomery(s_bench32_poly_c, s_bench32_poly,
                                 s_bench32_poly_b, &s_dil_ntt_params);
}

#if DAP_PLATFORM_X86
static void s_bench32_ntt_avx2(void)
{
    dap_ntt_forward_mont_avx2(s_bench32_poly, &s_dil_ntt_params);
}

static void s_bench32_intt_avx2(void)
{
    dap_ntt_inverse_mont_avx2(s_bench32_poly, &s_dil_ntt_params);
}

static void s_bench32_pointwise_avx2(void)
{
    dap_ntt_pointwise_montgomery_avx2(s_bench32_poly_c, s_bench32_poly,
                                      s_bench32_poly_b, &s_dil_ntt_params);
}

static void s_bench32_ntt_avx512(void)
{
    dap_ntt_forward_mont_avx512(s_bench32_poly, &s_dil_ntt_params);
}

static void s_bench32_intt_avx512(void)
{
    dap_ntt_inverse_mont_avx512(s_bench32_poly, &s_dil_ntt_params);
}

static void s_bench32_pointwise_avx512(void)
{
    dap_ntt_pointwise_montgomery_avx512(s_bench32_poly_c, s_bench32_poly,
                                        s_bench32_poly_b, &s_dil_ntt_params);
}
#endif

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

/* ===== NTT16 Kyber N=256 benchmark data ===== */

static const int16_t s_kyber_zetas[128] = {
    2285, 2571, 2970, 1812, 1493, 1422, 287, 202, 3158, 622, 1577, 182, 962,
    2127, 1855, 1468, 573, 2004, 264, 383, 2500, 1458, 1727, 3199, 2648, 1017,
    732, 608, 1787, 411, 3124, 1758, 1223, 652, 2777, 1015, 2036, 1491, 3047,
    1785, 516, 3321, 3009, 2663, 1711, 2167, 126, 1469, 2476, 3239, 3058, 830,
    107, 1908, 3082, 2378, 2931, 961, 1821, 2604, 448, 2264, 677, 2054, 2226,
    430, 555, 843, 2078, 871, 1550, 105, 422, 587, 177, 3094, 3038, 2869, 1574,
    1653, 3083, 778, 1159, 3182, 2552, 1483, 2727, 1119, 1739, 644, 2457, 349,
    418, 329, 3173, 3254, 817, 1097, 603, 610, 1322, 2044, 1864, 384, 2114, 3193,
    1218, 1994, 2455, 220, 2142, 1670, 2144, 1799, 2051, 794, 1819, 2475, 2459,
    478, 3221, 3021, 996, 991, 958, 1869, 1522, 1628
};

static const int16_t s_kyber_zetas_inv[128] = {
    1701, 1807, 1460, 2371, 2338, 2333, 308, 108, 2851, 870, 854, 1510, 2535,
    1278, 1530, 1185, 1659, 1187, 3109, 874, 1335, 2111, 136, 1215, 2945, 1465,
    1285, 2007, 2719, 2726, 2232, 2512, 75, 156, 3000, 2911, 2980, 872, 2685,
    1590, 2210, 602, 1846, 777, 147, 2170, 2551, 246, 1676, 1755, 460, 291, 235,
    3152, 2742, 2907, 3224, 1779, 2458, 1251, 2486, 2774, 2899, 1103, 1275, 2652,
    1065, 2881, 725, 1508, 2368, 398, 951, 247, 1421, 3222, 2499, 271, 90, 853,
    1860, 3203, 1162, 1618, 666, 320, 8, 2813, 1544, 282, 1838, 1293, 2314, 552,
    2677, 2106, 1571, 205, 2918, 1542, 2721, 2597, 2312, 681, 130, 1602, 1871,
    829, 2946, 3065, 1325, 2756, 1861, 1474, 1202, 2367, 3147, 1752, 2707, 171,
    3127, 3042, 1907, 1836, 1517, 359, 758, 1441
};

static const dap_ntt_params16_t s_kyber_bench_params = {
    .n = 256, .q = 3329, .qinv = -3327,
    .zetas = s_kyber_zetas, .zetas_inv = s_kyber_zetas_inv, .zetas_len = 128
};

static int16_t s_bench16_poly[256];

static void s_bench16_fill(void)
{
    for (int i = 0; i < 256; i++)
        s_bench16_poly[i] = (int16_t)(rand() % 3329);
}

static void s_bench16_ntt_dispatch(void)
{
    dap_ntt16_forward(s_bench16_poly, &s_kyber_bench_params);
}

static void s_bench16_intt_dispatch(void)
{
    dap_ntt16_inverse(s_bench16_poly, &s_kyber_bench_params);
}

static void s_bench16_ntt_ref(void)
{
    dap_ntt16_forward_ref(s_bench16_poly, &s_kyber_bench_params);
}

static void s_bench16_intt_ref(void)
{
    dap_ntt16_inverse_ref(s_bench16_poly, &s_kyber_bench_params);
}

#if DAP_PLATFORM_X86
static void s_bench16_ntt_avx2(void)
{
    dap_ntt16_forward_avx2(s_bench16_poly, &s_kyber_bench_params);
}

static void s_bench16_intt_avx2(void)
{
    dap_ntt16_inverse_avx2(s_bench16_poly, &s_kyber_bench_params);
}

static void s_bench16_ntt_avx512(void)
{
    dap_ntt16_forward_avx512(s_bench16_poly, &s_kyber_bench_params);
}

static void s_bench16_intt_avx512(void)
{
    dap_ntt16_inverse_avx512(s_bench16_poly, &s_kyber_bench_params);
}
#endif

/* ===== SIMD NTT16 correctness: compare SIMD output to reference ===== */

static void s_test_ntt16_simd_correctness(void)
{
    dap_print_module_name("NTT16 SIMD correctness vs reference");

    int16_t l_ref[256], l_simd[256];
    srand(42);
    for (int i = 0; i < 256; i++)
        l_ref[i] = (int16_t)(rand() % 3329);
    memcpy(l_simd, l_ref, sizeof(l_ref));

    dap_ntt16_forward_ref(l_ref, &s_kyber_bench_params);
    dap_ntt16_forward(l_simd, &s_kyber_bench_params);
    dap_assert(memcmp(l_ref, l_simd, sizeof(l_ref)) == 0,
               "NTT16 forward: dispatch matches reference");

    memcpy(l_simd, l_ref, sizeof(l_ref));
    int16_t l_ref2[256];
    memcpy(l_ref2, l_ref, sizeof(l_ref));

    dap_ntt16_inverse_ref(l_ref2, &s_kyber_bench_params);
    dap_ntt16_inverse(l_simd, &s_kyber_bench_params);
    dap_assert(memcmp(l_ref2, l_simd, sizeof(l_ref2)) == 0,
               "NTT16 inverse: dispatch matches reference");

#if DAP_PLATFORM_X86
    srand(42);
    for (int i = 0; i < 256; i++)
        l_ref[i] = (int16_t)(rand() % 3329);
    memcpy(l_simd, l_ref, sizeof(l_ref));

    dap_ntt16_forward_ref(l_ref, &s_kyber_bench_params);
    dap_ntt16_forward_avx2(l_simd, &s_kyber_bench_params);
    dap_assert(memcmp(l_ref, l_simd, sizeof(l_ref)) == 0,
               "NTT16 forward: AVX2 matches reference");

    memcpy(l_simd, l_ref, sizeof(l_ref));
    memcpy(l_ref2, l_ref, sizeof(l_ref));
    dap_ntt16_inverse_ref(l_ref2, &s_kyber_bench_params);
    dap_ntt16_inverse_avx2(l_simd, &s_kyber_bench_params);
    dap_assert(memcmp(l_ref2, l_simd, sizeof(l_ref2)) == 0,
               "NTT16 inverse: AVX2 matches reference");

    srand(42);
    for (int i = 0; i < 256; i++)
        l_ref[i] = (int16_t)(rand() % 3329);
    memcpy(l_simd, l_ref, sizeof(l_ref));

    dap_ntt16_forward_ref(l_ref, &s_kyber_bench_params);
    dap_ntt16_forward_avx512(l_simd, &s_kyber_bench_params);
    dap_assert(memcmp(l_ref, l_simd, sizeof(l_ref)) == 0,
               "NTT16 forward: AVX-512 matches reference");

    memcpy(l_simd, l_ref, sizeof(l_ref));
    memcpy(l_ref2, l_ref, sizeof(l_ref));
    dap_ntt16_inverse_ref(l_ref2, &s_kyber_bench_params);
    dap_ntt16_inverse_avx512(l_simd, &s_kyber_bench_params);
    dap_assert(memcmp(l_ref2, l_simd, sizeof(l_ref2)) == 0,
               "NTT16 inverse: AVX-512 matches reference");
#endif
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

    s_test_ntt16_simd_correctness();
    s_test_ntt32_mont_simd_correctness();

    dap_print_module_name("dap_math_bignum benchmarks");
    benchmark_mgs_time("NTT32 forward (N=8)", benchmark_test_time(s_bench_ntt_forward, 10000));
    benchmark_mgs_time("NTT32 inverse (N=8)", benchmark_test_time(s_bench_ntt_inverse, 10000));
    benchmark_mgs_time("bignum_mul (64x64)", benchmark_test_time(s_bench_bignum_mul, 100000));
    benchmark_mgs_time("bignum_mod_exp", benchmark_test_time(s_bench_bignum_mod_exp, 1000));

    dap_print_module_name("NTT16 Kyber N=256 benchmarks");
    s_bench16_fill();

    benchmark_mgs_rate("NTT16 fwd  reference", benchmark_test_rate(s_bench16_ntt_ref, 1.0f));
    benchmark_mgs_rate("NTT16 inv  reference", benchmark_test_rate(s_bench16_intt_ref, 1.0f));

#if DAP_PLATFORM_X86
    s_bench16_fill();
    benchmark_mgs_rate("NTT16 fwd  AVX2", benchmark_test_rate(s_bench16_ntt_avx2, 1.0f));
    benchmark_mgs_rate("NTT16 inv  AVX2", benchmark_test_rate(s_bench16_intt_avx2, 1.0f));

    s_bench16_fill();
    benchmark_mgs_rate("NTT16 fwd  AVX-512", benchmark_test_rate(s_bench16_ntt_avx512, 1.0f));
    benchmark_mgs_rate("NTT16 inv  AVX-512", benchmark_test_rate(s_bench16_intt_avx512, 1.0f));
#endif

    s_bench16_fill();
    benchmark_mgs_rate("NTT16 fwd  dispatch", benchmark_test_rate(s_bench16_ntt_dispatch, 1.0f));
    benchmark_mgs_rate("NTT16 inv  dispatch", benchmark_test_rate(s_bench16_intt_dispatch, 1.0f));

    dap_print_module_name("NTT32 Dilithium N=256 benchmarks");
    s_bench32_fill();

    benchmark_mgs_rate("NTT32 fwd  reference", benchmark_test_rate(s_bench32_ntt_ref, 1.0f));
    benchmark_mgs_rate("NTT32 inv  reference", benchmark_test_rate(s_bench32_intt_ref, 1.0f));
    benchmark_mgs_rate("NTT32 pw   reference", benchmark_test_rate(s_bench32_pointwise_ref, 1.0f));

#if DAP_PLATFORM_X86
    s_bench32_fill();
    benchmark_mgs_rate("NTT32 fwd  AVX2", benchmark_test_rate(s_bench32_ntt_avx2, 1.0f));
    benchmark_mgs_rate("NTT32 inv  AVX2", benchmark_test_rate(s_bench32_intt_avx2, 1.0f));
    benchmark_mgs_rate("NTT32 pw   AVX2", benchmark_test_rate(s_bench32_pointwise_avx2, 1.0f));

    s_bench32_fill();
    benchmark_mgs_rate("NTT32 fwd  AVX-512", benchmark_test_rate(s_bench32_ntt_avx512, 1.0f));
    benchmark_mgs_rate("NTT32 inv  AVX-512", benchmark_test_rate(s_bench32_intt_avx512, 1.0f));
    benchmark_mgs_rate("NTT32 pw   AVX-512", benchmark_test_rate(s_bench32_pointwise_avx512, 1.0f));
#endif

    s_bench32_fill();
    benchmark_mgs_rate("NTT32 fwd  dispatch", benchmark_test_rate(s_bench32_ntt_dispatch, 1.0f));
    benchmark_mgs_rate("NTT32 inv  dispatch", benchmark_test_rate(s_bench32_intt_dispatch, 1.0f));
    benchmark_mgs_rate("NTT32 pw   dispatch", benchmark_test_rate(s_bench32_pointwise_dispatch, 1.0f));

    return 0;
}
