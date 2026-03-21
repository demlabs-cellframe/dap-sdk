/**
 * @file dap_mlkem_ntt_{{ARCH_LOWER}}.c
 * @brief {{ARCH_NAME}} specialized NTT16 for ML-KEM (Kyber)
 * @details Compile-time constants: Q=3329, QINV=-3327, N=256.
 *          Fully in-register NTT: all 256 coefficients held in 16 SIMD
 *          registers, all 7 butterfly layers processed without intermediate
 *          stores. Single load/store pass eliminates 3x memory traffic vs
 *          the layer-by-layer approach.
 *          Generated from dap_mlkem_ntt_simd.c.tpl by dap_tpl.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @generated
 */

#include <stdint.h>
#include <string.h>
{{ARCH_INCLUDES}}

{{#include PRIMITIVES_FILE}}

#define KYBER_N    256
#define KYBER_Q    3329
#define KYBER_QINV ((int16_t)-3327)

static const int16_t s_zetas[128] = {
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

static const int16_t s_zetas_inv[128] = {
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

{{TARGET_ATTR}}
static inline VEC_T s_fqmul(VEC_T a_a, VEC_T a_b)
{
    const VEC_T l_qinv = VEC_SET1_16(KYBER_QINV);
    const VEC_T l_q    = VEC_SET1_16(KYBER_Q);
    VEC_T l_lo  = VEC_MULLO16(a_a, a_b);
    VEC_T l_hi  = VEC_MULHI16(a_a, a_b);
    VEC_T l_u   = VEC_MULLO16(l_lo, l_qinv);
    VEC_T l_uq  = VEC_MULHI16(l_u, l_q);
    return VEC_SUB16(l_hi, l_uq);
}

{{TARGET_ATTR}}
static inline VEC_T s_barrett_reduce(VEC_T a_val)
{
    const VEC_T l_v = VEC_SET1_16(20159);
    const VEC_T l_q = VEC_SET1_16(KYBER_Q);
    VEC_T l_bt = VEC_MULHI16(l_v, a_val);
    l_bt = VEC_SRAI16(l_bt, 10);
    l_bt = VEC_MULLO16(l_bt, l_q);
    return VEC_SUB16(a_val, l_bt);
}

static inline int16_t s_fqmul_scalar(int16_t a, int16_t b)
{
    int32_t t = (int32_t)a * b;
    int16_t u = (int16_t)t * KYBER_QINV;
    return (int16_t)((t - (int32_t)u * KYBER_Q) >> 16);
}

static inline int16_t s_barrett_reduce_scalar(int16_t a)
{
    int16_t t = (int16_t)((int32_t)20159 * a >> 26);
    return a - t * KYBER_Q;
}

#ifdef HVEC_LANES
{{TARGET_ATTR}}
static inline HVEC_T s_fqmul_hvec(HVEC_T a_a, HVEC_T a_b)
{
    const HVEC_T l_qinv = HVEC_SET1_16(KYBER_QINV);
    const HVEC_T l_q    = HVEC_SET1_16(KYBER_Q);
    HVEC_T l_lo = HVEC_MULLO16(a_a, a_b);
    HVEC_T l_hi = HVEC_MULHI16(a_a, a_b);
    HVEC_T l_u  = HVEC_MULLO16(l_lo, l_qinv);
    HVEC_T l_uq = HVEC_MULHI16(l_u, l_q);
    return HVEC_SUB16(l_hi, l_uq);
}

{{TARGET_ATTR}}
static inline HVEC_T s_barrett_reduce_hvec(HVEC_T a_val)
{
    const HVEC_T l_v = HVEC_SET1_16(20159);
    const HVEC_T l_q = HVEC_SET1_16(KYBER_Q);
    HVEC_T l_bt = HVEC_MULHI16(l_v, a_val);
    l_bt = HVEC_SRAI16(l_bt, 10);
    l_bt = HVEC_MULLO16(l_bt, l_q);
    return HVEC_SUB16(a_val, l_bt);
}
#endif

/* ======== Forward NTT (Cooley-Tukey) ======== */

{{TARGET_ATTR}}
void dap_mlkem_ntt_forward_{{ARCH_LOWER}}(int16_t a_coeffs[KYBER_N])
{
#if VEC_LANES == 16 && defined(HVEC_LANES) && HVEC_LANES == 8
    /*
     * Fully in-register forward NTT: all 256 coefficients in 16 YMM regs.
     * Layers 128/64/32/16 via cross-register butterflies,
     * layers 8/4/2 via in-register shuffles.
     * Single load/store pass eliminates ~100 intermediate memory ops.
     */

    VEC_T r0  = VEC_LOAD(a_coeffs);
    VEC_T r1  = VEC_LOAD(a_coeffs + 1 * VEC_LANES);
    VEC_T r2  = VEC_LOAD(a_coeffs + 2 * VEC_LANES);
    VEC_T r3  = VEC_LOAD(a_coeffs + 3 * VEC_LANES);
    VEC_T r4  = VEC_LOAD(a_coeffs + 4 * VEC_LANES);
    VEC_T r5  = VEC_LOAD(a_coeffs + 5 * VEC_LANES);
    VEC_T r6  = VEC_LOAD(a_coeffs + 6 * VEC_LANES);
    VEC_T r7  = VEC_LOAD(a_coeffs + 7 * VEC_LANES);
    VEC_T r8  = VEC_LOAD(a_coeffs + 8 * VEC_LANES);
    VEC_T r9  = VEC_LOAD(a_coeffs + 9 * VEC_LANES);
    VEC_T r10 = VEC_LOAD(a_coeffs + 10 * VEC_LANES);
    VEC_T r11 = VEC_LOAD(a_coeffs + 11 * VEC_LANES);
    VEC_T r12 = VEC_LOAD(a_coeffs + 12 * VEC_LANES);
    VEC_T r13 = VEC_LOAD(a_coeffs + 13 * VEC_LANES);
    VEC_T r14 = VEC_LOAD(a_coeffs + 14 * VEC_LANES);
    VEC_T r15 = VEC_LOAD(a_coeffs + 15 * VEC_LANES);

    /* Cooley-Tukey butterfly: t = fqmul(zeta, b); b = a - t; a = a + t */
#define CT_BF(a, b, zi) do {                         \
    VEC_T _t = s_fqmul(VEC_SET1_16(s_zetas[zi]), b); \
    b = VEC_SUB16(a, _t); a = VEC_ADD16(a, _t);      \
} while(0)

    /* Layer 128: pairs at distance 8 registers, zeta[1] */
    CT_BF(r0, r8,  1); CT_BF(r1, r9,  1);
    CT_BF(r2, r10, 1); CT_BF(r3, r11, 1);
    CT_BF(r4, r12, 1); CT_BF(r5, r13, 1);
    CT_BF(r6, r14, 1); CT_BF(r7, r15, 1);

    /* Layer 64: pairs at distance 4 registers */
    CT_BF(r0, r4, 2); CT_BF(r1, r5, 2);
    CT_BF(r2, r6, 2); CT_BF(r3, r7, 2);
    CT_BF(r8, r12, 3); CT_BF(r9, r13, 3);
    CT_BF(r10, r14, 3); CT_BF(r11, r15, 3);

    /* Layer 32: pairs at distance 2 registers */
    CT_BF(r0, r2, 4);  CT_BF(r1, r3, 4);
    CT_BF(r4, r6, 5);  CT_BF(r5, r7, 5);
    CT_BF(r8, r10, 6); CT_BF(r9, r11, 6);
    CT_BF(r12, r14, 7); CT_BF(r13, r15, 7);

    /* Layer 16: adjacent register pairs */
    CT_BF(r0, r1, 8);   CT_BF(r2, r3, 9);
    CT_BF(r4, r5, 10);  CT_BF(r6, r7, 11);
    CT_BF(r8, r9, 12);  CT_BF(r10, r11, 13);
    CT_BF(r12, r13, 14); CT_BF(r14, r15, 15);

#undef CT_BF

    /*
     * Layers 8/4/2: in-register shuffles per vector.
     * k8 = 16+i, k4 = {32+2i, 33+2i}, k2 = {64+4i .. 67+4i}
     */
#define NTT_INNER(v, k8, k4a, k4b, k2a, k2b, k2c, k2d) do { \
    { \
        HVEC_T _lo = VEC_LO_HALF(v), _hi = VEC_HI_HALF(v);             \
        HVEC_T _t = s_fqmul_hvec(HVEC_SET1_16(s_zetas[k8]), _hi);      \
        v = VEC_FROM_HALVES(HVEC_ADD16(_lo, _t), HVEC_SUB16(_lo, _t));  \
    }                                                                    \
    {                                                                    \
        VEC_T _lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(1,0,1,0));      \
        VEC_T _hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,2,3,2));      \
        VEC_T _zv = _mm256_setr_m128i(                                   \
            _mm_set1_epi16(s_zetas[k4a]),                                \
            _mm_set1_epi16(s_zetas[k4b]));                               \
        VEC_T _t = s_fqmul(_zv, _hi);                                   \
        v = _mm256_blend_epi32(VEC_ADD16(_lo, _t),                       \
                               VEC_SUB16(_lo, _t), 0xCC);               \
    }                                                                    \
    {                                                                    \
        VEC_T _lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(2,2,0,0));      \
        VEC_T _hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,3,1,1));      \
        VEC_T _zv = _mm256_setr_epi16(                                   \
            s_zetas[k2a],s_zetas[k2a],s_zetas[k2a],s_zetas[k2a],        \
            s_zetas[k2b],s_zetas[k2b],s_zetas[k2b],s_zetas[k2b],        \
            s_zetas[k2c],s_zetas[k2c],s_zetas[k2c],s_zetas[k2c],        \
            s_zetas[k2d],s_zetas[k2d],s_zetas[k2d],s_zetas[k2d]);       \
        VEC_T _t = s_fqmul(_zv, _hi);                                   \
        v = _mm256_blend_epi32(VEC_ADD16(_lo, _t),                       \
                               VEC_SUB16(_lo, _t), 0xAA);               \
    }                                                                    \
} while(0)

    NTT_INNER(r0,  16, 32, 33,  64,  65,  66,  67);
    NTT_INNER(r1,  17, 34, 35,  68,  69,  70,  71);
    NTT_INNER(r2,  18, 36, 37,  72,  73,  74,  75);
    NTT_INNER(r3,  19, 38, 39,  76,  77,  78,  79);
    NTT_INNER(r4,  20, 40, 41,  80,  81,  82,  83);
    NTT_INNER(r5,  21, 42, 43,  84,  85,  86,  87);
    NTT_INNER(r6,  22, 44, 45,  88,  89,  90,  91);
    NTT_INNER(r7,  23, 46, 47,  92,  93,  94,  95);
    NTT_INNER(r8,  24, 48, 49,  96,  97,  98,  99);
    NTT_INNER(r9,  25, 50, 51, 100, 101, 102, 103);
    NTT_INNER(r10, 26, 52, 53, 104, 105, 106, 107);
    NTT_INNER(r11, 27, 54, 55, 108, 109, 110, 111);
    NTT_INNER(r12, 28, 56, 57, 112, 113, 114, 115);
    NTT_INNER(r13, 29, 58, 59, 116, 117, 118, 119);
    NTT_INNER(r14, 30, 60, 61, 120, 121, 122, 123);
    NTT_INNER(r15, 31, 62, 63, 124, 125, 126, 127);

#undef NTT_INNER

    /* Fused Barrett reduction — eliminates separate poly_reduce pass */
    r0  = s_barrett_reduce(r0);  r1  = s_barrett_reduce(r1);
    r2  = s_barrett_reduce(r2);  r3  = s_barrett_reduce(r3);
    r4  = s_barrett_reduce(r4);  r5  = s_barrett_reduce(r5);
    r6  = s_barrett_reduce(r6);  r7  = s_barrett_reduce(r7);
    r8  = s_barrett_reduce(r8);  r9  = s_barrett_reduce(r9);
    r10 = s_barrett_reduce(r10); r11 = s_barrett_reduce(r11);
    r12 = s_barrett_reduce(r12); r13 = s_barrett_reduce(r13);
    r14 = s_barrett_reduce(r14); r15 = s_barrett_reduce(r15);

    VEC_STORE(a_coeffs,                r0);
    VEC_STORE(a_coeffs + 1 * VEC_LANES, r1);
    VEC_STORE(a_coeffs + 2 * VEC_LANES, r2);
    VEC_STORE(a_coeffs + 3 * VEC_LANES, r3);
    VEC_STORE(a_coeffs + 4 * VEC_LANES, r4);
    VEC_STORE(a_coeffs + 5 * VEC_LANES, r5);
    VEC_STORE(a_coeffs + 6 * VEC_LANES, r6);
    VEC_STORE(a_coeffs + 7 * VEC_LANES, r7);
    VEC_STORE(a_coeffs + 8 * VEC_LANES, r8);
    VEC_STORE(a_coeffs + 9 * VEC_LANES, r9);
    VEC_STORE(a_coeffs + 10 * VEC_LANES, r10);
    VEC_STORE(a_coeffs + 11 * VEC_LANES, r11);
    VEC_STORE(a_coeffs + 12 * VEC_LANES, r12);
    VEC_STORE(a_coeffs + 13 * VEC_LANES, r13);
    VEC_STORE(a_coeffs + 14 * VEC_LANES, r14);
    VEC_STORE(a_coeffs + 15 * VEC_LANES, r15);

#else
    /* Generic loop-based NTT for non-AVX2 architectures */
    unsigned l_start, l_j, l_k = 1;

    for (unsigned l_len = 128; l_len >= VEC_LANES; l_len >>= 1) {
        for (l_start = 0; l_start < KYBER_N; l_start = l_j + l_len) {
            VEC_T l_zv = VEC_SET1_16(s_zetas[l_k++]);
            for (l_j = l_start; l_j < l_start + l_len; l_j += VEC_LANES) {
                VEC_T l_a = VEC_LOAD(a_coeffs + l_j);
                VEC_T l_b = VEC_LOAD(a_coeffs + l_j + l_len);
                VEC_T l_t = s_fqmul(l_zv, l_b);
                VEC_STORE(a_coeffs + l_j,         VEC_ADD16(l_a, l_t));
                VEC_STORE(a_coeffs + l_j + l_len, VEC_SUB16(l_a, l_t));
            }
        }
    }

#ifdef HVEC_LANES
    {
        unsigned l_len = HVEC_LANES;
        for (l_start = 0; l_start < KYBER_N; l_start = l_j + l_len) {
            HVEC_T l_zv = HVEC_SET1_16(s_zetas[l_k++]);
            for (l_j = l_start; l_j < l_start + l_len; l_j += HVEC_LANES) {
                HVEC_T l_a = HVEC_LOAD(a_coeffs + l_j);
                HVEC_T l_b = HVEC_LOAD(a_coeffs + l_j + l_len);
                HVEC_T l_t = s_fqmul_hvec(l_zv, l_b);
                HVEC_STORE(a_coeffs + l_j,         HVEC_ADD16(l_a, l_t));
                HVEC_STORE(a_coeffs + l_j + l_len, HVEC_SUB16(l_a, l_t));
            }
        }
    }
    for (unsigned l_len = HVEC_LANES >> 1; l_len >= 2; l_len >>= 1) {
#else
    for (unsigned l_len = VEC_LANES >> 1; l_len >= 2; l_len >>= 1) {
#endif
        for (l_start = 0; l_start < KYBER_N; l_start = l_j + l_len) {
            int16_t l_zeta = s_zetas[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                int16_t l_t = s_fqmul_scalar(l_zeta, a_coeffs[l_j + l_len]);
                a_coeffs[l_j + l_len] = a_coeffs[l_j] - l_t;
                a_coeffs[l_j]         = a_coeffs[l_j] + l_t;
            }
        }
    }
    /* Fused Barrett reduction for generic path */
    for (unsigned l_i = 0; l_i < KYBER_N; l_i++)
        a_coeffs[l_i] = s_barrett_reduce_scalar(a_coeffs[l_i]);
#endif
}

/* ======== Inverse NTT (Gentleman-Sande) ======== */

{{TARGET_ATTR}}
void dap_mlkem_ntt_inverse_{{ARCH_LOWER}}(int16_t a_coeffs[KYBER_N])
{
#if VEC_LANES == 16 && defined(HVEC_LANES) && HVEC_LANES == 8
    /*
     * Fully in-register inverse NTT: all 256 coefficients in 16 YMM regs.
     * Layers 2/4/8 via in-register shuffles, then 16/32/64/128 via
     * cross-register GS butterflies. Final Montgomery scaling inlined.
     */

    VEC_T r0  = VEC_LOAD(a_coeffs);
    VEC_T r1  = VEC_LOAD(a_coeffs + 1 * VEC_LANES);
    VEC_T r2  = VEC_LOAD(a_coeffs + 2 * VEC_LANES);
    VEC_T r3  = VEC_LOAD(a_coeffs + 3 * VEC_LANES);
    VEC_T r4  = VEC_LOAD(a_coeffs + 4 * VEC_LANES);
    VEC_T r5  = VEC_LOAD(a_coeffs + 5 * VEC_LANES);
    VEC_T r6  = VEC_LOAD(a_coeffs + 6 * VEC_LANES);
    VEC_T r7  = VEC_LOAD(a_coeffs + 7 * VEC_LANES);
    VEC_T r8  = VEC_LOAD(a_coeffs + 8 * VEC_LANES);
    VEC_T r9  = VEC_LOAD(a_coeffs + 9 * VEC_LANES);
    VEC_T r10 = VEC_LOAD(a_coeffs + 10 * VEC_LANES);
    VEC_T r11 = VEC_LOAD(a_coeffs + 11 * VEC_LANES);
    VEC_T r12 = VEC_LOAD(a_coeffs + 12 * VEC_LANES);
    VEC_T r13 = VEC_LOAD(a_coeffs + 13 * VEC_LANES);
    VEC_T r14 = VEC_LOAD(a_coeffs + 14 * VEC_LANES);
    VEC_T r15 = VEC_LOAD(a_coeffs + 15 * VEC_LANES);

    /*
     * Inner layers 2/4/8 per vector.
     * zetas_inv: [0..63] for len=2, [64..95] for len=4, [96..111] for len=8
     */
#define INTT_INNER(v, k2a, k2b, k2c, k2d, k4a, k4b, k8) do { \
    {                                                                    \
        VEC_T _lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(2,2,0,0));      \
        VEC_T _hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,3,1,1));      \
        VEC_T _sum = VEC_ADD16(_lo, _hi);                                \
        VEC_T _dif = VEC_SUB16(_lo, _hi);                                \
        VEC_T _zv = _mm256_setr_epi16(                                   \
            s_zetas_inv[k2a],s_zetas_inv[k2a],                           \
            s_zetas_inv[k2a],s_zetas_inv[k2a],                           \
            s_zetas_inv[k2b],s_zetas_inv[k2b],                           \
            s_zetas_inv[k2b],s_zetas_inv[k2b],                           \
            s_zetas_inv[k2c],s_zetas_inv[k2c],                           \
            s_zetas_inv[k2c],s_zetas_inv[k2c],                           \
            s_zetas_inv[k2d],s_zetas_inv[k2d],                           \
            s_zetas_inv[k2d],s_zetas_inv[k2d]);                          \
        v = _mm256_blend_epi32(s_barrett_reduce(_sum),                   \
                               s_fqmul(_zv, _dif), 0xAA);               \
    }                                                                    \
    {                                                                    \
        VEC_T _lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(1,0,1,0));      \
        VEC_T _hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,2,3,2));      \
        VEC_T _sum = VEC_ADD16(_lo, _hi);                                \
        VEC_T _dif = VEC_SUB16(_lo, _hi);                                \
        VEC_T _zv = _mm256_setr_m128i(                                   \
            _mm_set1_epi16(s_zetas_inv[k4a]),                            \
            _mm_set1_epi16(s_zetas_inv[k4b]));                           \
        v = _mm256_blend_epi32(s_barrett_reduce(_sum),                   \
                               s_fqmul(_zv, _dif), 0xCC);               \
    }                                                                    \
    {                                                                    \
        HVEC_T _lo = VEC_LO_HALF(v), _hi = VEC_HI_HALF(v);             \
        HVEC_T _sum = HVEC_ADD16(_lo, _hi);                             \
        HVEC_T _dif = HVEC_SUB16(_lo, _hi);                             \
        v = VEC_FROM_HALVES(                                             \
            s_barrett_reduce_hvec(_sum),                                 \
            s_fqmul_hvec(HVEC_SET1_16(s_zetas_inv[k8]), _dif));         \
    }                                                                    \
} while(0)

    INTT_INNER(r0,   0,  1,  2,  3, 64, 65,  96);
    INTT_INNER(r1,   4,  5,  6,  7, 66, 67,  97);
    INTT_INNER(r2,   8,  9, 10, 11, 68, 69,  98);
    INTT_INNER(r3,  12, 13, 14, 15, 70, 71,  99);
    INTT_INNER(r4,  16, 17, 18, 19, 72, 73, 100);
    INTT_INNER(r5,  20, 21, 22, 23, 74, 75, 101);
    INTT_INNER(r6,  24, 25, 26, 27, 76, 77, 102);
    INTT_INNER(r7,  28, 29, 30, 31, 78, 79, 103);
    INTT_INNER(r8,  32, 33, 34, 35, 80, 81, 104);
    INTT_INNER(r9,  36, 37, 38, 39, 82, 83, 105);
    INTT_INNER(r10, 40, 41, 42, 43, 84, 85, 106);
    INTT_INNER(r11, 44, 45, 46, 47, 86, 87, 107);
    INTT_INNER(r12, 48, 49, 50, 51, 88, 89, 108);
    INTT_INNER(r13, 52, 53, 54, 55, 90, 91, 109);
    INTT_INNER(r14, 56, 57, 58, 59, 92, 93, 110);
    INTT_INNER(r15, 60, 61, 62, 63, 94, 95, 111);

#undef INTT_INNER

    /* Gentleman-Sande butterfly: sum = a+b; dif = (a-b)*zeta;
     * a = barrett_reduce(sum); b = dif */
#define GS_BF(a, b, zi) do {                                              \
    VEC_T _sum = VEC_ADD16(a, b);                                          \
    VEC_T _dif = VEC_SUB16(a, b);                                          \
    a = s_barrett_reduce(_sum);                                            \
    b = s_fqmul(VEC_SET1_16(s_zetas_inv[zi]), _dif);                      \
} while(0)

    /* Layer 16: adjacent register pairs, zetas_inv[112..119] */
    GS_BF(r0, r1, 112);  GS_BF(r2, r3, 113);
    GS_BF(r4, r5, 114);  GS_BF(r6, r7, 115);
    GS_BF(r8, r9, 116);  GS_BF(r10, r11, 117);
    GS_BF(r12, r13, 118); GS_BF(r14, r15, 119);

    /* Layer 32: pairs at distance 2, zetas_inv[120..123] */
    GS_BF(r0, r2, 120);  GS_BF(r1, r3, 120);
    GS_BF(r4, r6, 121);  GS_BF(r5, r7, 121);
    GS_BF(r8, r10, 122); GS_BF(r9, r11, 122);
    GS_BF(r12, r14, 123); GS_BF(r13, r15, 123);

    /* Layer 64: pairs at distance 4, zetas_inv[124..125] */
    GS_BF(r0, r4, 124);  GS_BF(r1, r5, 124);
    GS_BF(r2, r6, 124);  GS_BF(r3, r7, 124);
    GS_BF(r8, r12, 125); GS_BF(r9, r13, 125);
    GS_BF(r10, r14, 125); GS_BF(r11, r15, 125);

    /* Layer 128: pairs at distance 8, zetas_inv[126] */
    GS_BF(r0, r8, 126);  GS_BF(r1, r9, 126);
    GS_BF(r2, r10, 126); GS_BF(r3, r11, 126);
    GS_BF(r4, r12, 126); GS_BF(r5, r13, 126);
    GS_BF(r6, r14, 126); GS_BF(r7, r15, 126);

#undef GS_BF

    /* Final scaling: multiply all coefficients by zetas_inv[127] = 1441 */
    {
        VEC_T l_sv = VEC_SET1_16(s_zetas_inv[127]);
        r0  = s_fqmul(r0,  l_sv); r1  = s_fqmul(r1,  l_sv);
        r2  = s_fqmul(r2,  l_sv); r3  = s_fqmul(r3,  l_sv);
        r4  = s_fqmul(r4,  l_sv); r5  = s_fqmul(r5,  l_sv);
        r6  = s_fqmul(r6,  l_sv); r7  = s_fqmul(r7,  l_sv);
        r8  = s_fqmul(r8,  l_sv); r9  = s_fqmul(r9,  l_sv);
        r10 = s_fqmul(r10, l_sv); r11 = s_fqmul(r11, l_sv);
        r12 = s_fqmul(r12, l_sv); r13 = s_fqmul(r13, l_sv);
        r14 = s_fqmul(r14, l_sv); r15 = s_fqmul(r15, l_sv);
    }

    VEC_STORE(a_coeffs,                 r0);
    VEC_STORE(a_coeffs + 1 * VEC_LANES, r1);
    VEC_STORE(a_coeffs + 2 * VEC_LANES, r2);
    VEC_STORE(a_coeffs + 3 * VEC_LANES, r3);
    VEC_STORE(a_coeffs + 4 * VEC_LANES, r4);
    VEC_STORE(a_coeffs + 5 * VEC_LANES, r5);
    VEC_STORE(a_coeffs + 6 * VEC_LANES, r6);
    VEC_STORE(a_coeffs + 7 * VEC_LANES, r7);
    VEC_STORE(a_coeffs + 8 * VEC_LANES, r8);
    VEC_STORE(a_coeffs + 9 * VEC_LANES, r9);
    VEC_STORE(a_coeffs + 10 * VEC_LANES, r10);
    VEC_STORE(a_coeffs + 11 * VEC_LANES, r11);
    VEC_STORE(a_coeffs + 12 * VEC_LANES, r12);
    VEC_STORE(a_coeffs + 13 * VEC_LANES, r13);
    VEC_STORE(a_coeffs + 14 * VEC_LANES, r14);
    VEC_STORE(a_coeffs + 15 * VEC_LANES, r15);

#else
    /* Generic loop-based inverse NTT for non-AVX2 architectures */
    unsigned l_start, l_j, l_k = 0;

    unsigned l_simd_start = VEC_LANES;
#ifdef HVEC_LANES
    l_simd_start = HVEC_LANES;
#endif
    for (unsigned l_len = 2; l_len < l_simd_start; l_len <<= 1) {
        for (l_start = 0; l_start < KYBER_N; l_start = l_j + l_len) {
            int16_t l_zeta = s_zetas_inv[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                int16_t l_t = a_coeffs[l_j];
                a_coeffs[l_j]         = s_barrett_reduce_scalar(l_t + a_coeffs[l_j + l_len]);
                a_coeffs[l_j + l_len] = s_fqmul_scalar(l_zeta, l_t - a_coeffs[l_j + l_len]);
            }
        }
    }
#ifdef HVEC_LANES
    {
        unsigned l_len = HVEC_LANES;
        for (l_start = 0; l_start < KYBER_N; l_start = l_j + l_len) {
            HVEC_T l_zv = HVEC_SET1_16(s_zetas_inv[l_k++]);
            for (l_j = l_start; l_j < l_start + l_len; l_j += HVEC_LANES) {
                HVEC_T l_a   = HVEC_LOAD(a_coeffs + l_j);
                HVEC_T l_b   = HVEC_LOAD(a_coeffs + l_j + l_len);
                HVEC_T l_sum = HVEC_ADD16(l_a, l_b);
                HVEC_T l_dif = HVEC_SUB16(l_a, l_b);
                HVEC_STORE(a_coeffs + l_j,         s_barrett_reduce_hvec(l_sum));
                HVEC_STORE(a_coeffs + l_j + l_len, s_fqmul_hvec(l_zv, l_dif));
            }
        }
    }
#endif

    for (unsigned l_len = VEC_LANES; l_len <= 128; l_len <<= 1) {
        for (l_start = 0; l_start < KYBER_N; l_start = l_j + l_len) {
            VEC_T l_zv = VEC_SET1_16(s_zetas_inv[l_k++]);
            for (l_j = l_start; l_j < l_start + l_len; l_j += VEC_LANES) {
                VEC_T l_a   = VEC_LOAD(a_coeffs + l_j);
                VEC_T l_b   = VEC_LOAD(a_coeffs + l_j + l_len);
                VEC_T l_sum = VEC_ADD16(l_a, l_b);
                VEC_T l_dif = VEC_SUB16(l_a, l_b);
                VEC_STORE(a_coeffs + l_j,         s_barrett_reduce(l_sum));
                VEC_STORE(a_coeffs + l_j + l_len, s_fqmul(l_zv, l_dif));
            }
        }
    }

    {
        VEC_T l_sv = VEC_SET1_16(s_zetas_inv[127]);
        for (unsigned l_i = 0; l_i < KYBER_N; l_i += VEC_LANES) {
            VEC_T l_c = VEC_LOAD(a_coeffs + l_i);
            VEC_STORE(a_coeffs + l_i, s_fqmul(l_c, l_sv));
        }
    }
#endif
}
