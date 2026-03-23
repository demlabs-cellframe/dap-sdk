/**
 * @file dap_mlkem_ntt_{{ARCH_LOWER}}.c
 * @brief {{ARCH_NAME}} specialized NTT16 for ML-KEM (Kyber)
 * @details Compile-time constants: Q=3329, QINV=-3327, N=256.
 *          Hybrid NTT: loop-based outer layers (128/64/32/16) with L1D
 *          round-trips + shuffle-based inner layers (8/4/2) per-block.
 *          Compact code (~400B each) fits in L1I; data (512B) in L1D.
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

{{TARGET_ATTR}} __attribute__((optimize("Os"), noinline))
void dap_mlkem_ntt_forward_{{ARCH_LOWER}}(int16_t a_coeffs[KYBER_N])
{
#if VEC_LANES == 16 && defined(HVEC_LANES) && HVEC_LANES == 8
    /*
     * Hybrid NTT: loop-based outer layers + shuffle-based inner layers.
     * Outer layers (128/64/32/16) do cross-block butterflies via L1D;
     * inner layers (8/4/2) use per-block shuffles in a tight loop.
     * Data (512B) stays hot in L1D; code (~400B) fits easily in L1I.
     */

    /* Phase 1: outer layers via L1D round-trips */
    unsigned l_k = 1;
    for (unsigned l_len = 128; l_len >= VEC_LANES; l_len >>= 1) {
        for (unsigned l_s = 0; l_s < KYBER_N; l_s += 2 * l_len) {
            VEC_T l_zv = VEC_SET1_16(s_zetas[l_k++]);
            for (unsigned l_j = l_s; l_j < l_s + l_len; l_j += VEC_LANES) {
                VEC_T l_a = VEC_LOAD(a_coeffs + l_j);
                VEC_T l_b = VEC_LOAD(a_coeffs + l_j + l_len);
                VEC_T l_t = s_fqmul(l_zv, l_b);
                VEC_STORE(a_coeffs + l_j,         VEC_ADD16(l_a, l_t));
                VEC_STORE(a_coeffs + l_j + l_len, VEC_SUB16(l_a, l_t));
            }
        }
    }

    /* Phase 2: inner layers 8/4/2 via per-block shuffles */
    for (unsigned l_blk = 0; l_blk < KYBER_N / VEC_LANES; l_blk++) {
        VEC_T v = VEC_LOAD(a_coeffs + l_blk * VEC_LANES);

        /* Layer 8: CT butterfly across 128-bit halves */
        {
            HVEC_T l_lo = VEC_LO_HALF(v), l_hi = VEC_HI_HALF(v);
            HVEC_T l_t = s_fqmul_hvec(HVEC_SET1_16(s_zetas[16 + l_blk]), l_hi);
            v = VEC_FROM_HALVES(HVEC_ADD16(l_lo, l_t), HVEC_SUB16(l_lo, l_t));
        }

        /* Layer 4: CT butterfly across 64-bit groups within 128-bit lanes */
        {
            VEC_T l_lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(1,0,1,0));
            VEC_T l_hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,2,3,2));
            VEC_T l_zv = _mm256_setr_m128i(
                _mm_set1_epi16(s_zetas[32 + 2 * l_blk]),
                _mm_set1_epi16(s_zetas[33 + 2 * l_blk]));
            VEC_T l_t = s_fqmul(l_zv, l_hi);
            v = _mm256_blend_epi32(VEC_ADD16(l_lo, l_t),
                                   VEC_SUB16(l_lo, l_t), 0xCC);
        }

        /* Layer 2: CT butterfly across 32-bit groups */
        {
            VEC_T l_lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(2,2,0,0));
            VEC_T l_hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,3,1,1));
            unsigned l_z2 = 64 + 4 * l_blk;
            __m128i l_z4  = _mm_loadl_epi64((const __m128i *)(s_zetas + l_z2));
            __m128i l_zd  = _mm_unpacklo_epi16(l_z4, l_z4);
            VEC_T l_zv = _mm256_setr_m128i(
                _mm_unpacklo_epi32(l_zd, l_zd),
                _mm_unpackhi_epi32(l_zd, l_zd));
            VEC_T l_t = s_fqmul(l_zv, l_hi);
            v = _mm256_blend_epi32(VEC_ADD16(l_lo, l_t),
                                   VEC_SUB16(l_lo, l_t), 0xAA);
        }

        VEC_STORE(a_coeffs + l_blk * VEC_LANES, s_barrett_reduce(v));
    }

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

{{TARGET_ATTR}} __attribute__((optimize("Os"), noinline))
void dap_mlkem_ntt_inverse_{{ARCH_LOWER}}(int16_t a_coeffs[KYBER_N])
{
#if VEC_LANES == 16 && defined(HVEC_LANES) && HVEC_LANES == 8
    /*
     * Hybrid inverse NTT: shuffle-based inner layers + loop-based outer.
     * Mirror of forward: inner 2/4/8 per-block, then outer 16/32/64/128.
     */

    /* Phase 1: inner layers 2/4/8 via per-block GS shuffles */
    for (unsigned l_blk = 0; l_blk < KYBER_N / VEC_LANES; l_blk++) {
        VEC_T v = VEC_LOAD(a_coeffs + l_blk * VEC_LANES);

        /* Layer 2 (GS): merge 32-bit groups */
        {
            VEC_T l_lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(2,2,0,0));
            VEC_T l_hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,3,1,1));
            VEC_T l_sum = VEC_ADD16(l_lo, l_hi);
            VEC_T l_dif = VEC_SUB16(l_lo, l_hi);
            unsigned l_z2 = 4 * l_blk;
            __m128i l_z4  = _mm_loadl_epi64((const __m128i *)(s_zetas_inv + l_z2));
            __m128i l_zd  = _mm_unpacklo_epi16(l_z4, l_z4);
            VEC_T l_zv = _mm256_setr_m128i(
                _mm_unpacklo_epi32(l_zd, l_zd),
                _mm_unpackhi_epi32(l_zd, l_zd));
            v = _mm256_blend_epi32(s_barrett_reduce(l_sum),
                                   s_fqmul(l_zv, l_dif), 0xAA);
        }

        /* Layer 4 (GS): merge 64-bit groups within 128-bit lanes */
        {
            VEC_T l_lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(1,0,1,0));
            VEC_T l_hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,2,3,2));
            VEC_T l_sum = VEC_ADD16(l_lo, l_hi);
            VEC_T l_dif = VEC_SUB16(l_lo, l_hi);
            VEC_T l_zv = _mm256_setr_m128i(
                _mm_set1_epi16(s_zetas_inv[64 + 2 * l_blk]),
                _mm_set1_epi16(s_zetas_inv[65 + 2 * l_blk]));
            v = _mm256_blend_epi32(s_barrett_reduce(l_sum),
                                   s_fqmul(l_zv, l_dif), 0xCC);
        }

        /* Layer 8 (GS): merge 128-bit halves */
        {
            HVEC_T l_lo = VEC_LO_HALF(v), l_hi = VEC_HI_HALF(v);
            HVEC_T l_sum = HVEC_ADD16(l_lo, l_hi);
            HVEC_T l_dif = HVEC_SUB16(l_lo, l_hi);
            v = VEC_FROM_HALVES(
                s_barrett_reduce_hvec(l_sum),
                s_fqmul_hvec(HVEC_SET1_16(s_zetas_inv[96 + l_blk]), l_dif));
        }

        VEC_STORE(a_coeffs + l_blk * VEC_LANES, v);
    }

    /* Phase 2: outer layers 16/32/64/128 via L1D round-trips */
    unsigned l_k = 112;
    for (unsigned l_len = VEC_LANES; l_len <= 128; l_len <<= 1) {
        for (unsigned l_s = 0; l_s < KYBER_N; l_s += 2 * l_len) {
            VEC_T l_zv = VEC_SET1_16(s_zetas_inv[l_k++]);
            for (unsigned l_j = l_s; l_j < l_s + l_len; l_j += VEC_LANES) {
                VEC_T l_a   = VEC_LOAD(a_coeffs + l_j);
                VEC_T l_b   = VEC_LOAD(a_coeffs + l_j + l_len);
                VEC_T l_sum = VEC_ADD16(l_a, l_b);
                VEC_T l_dif = VEC_SUB16(l_a, l_b);
                VEC_STORE(a_coeffs + l_j,         s_barrett_reduce(l_sum));
                VEC_STORE(a_coeffs + l_j + l_len, s_fqmul(l_zv, l_dif));
            }
        }
    }

    /* Final scaling by zetas_inv[127] = 1441 */
    {
        VEC_T l_sv = VEC_SET1_16(s_zetas_inv[127]);
        for (unsigned l_j = 0; l_j < KYBER_N; l_j += VEC_LANES) {
            VEC_T l_c = VEC_LOAD(a_coeffs + l_j);
            VEC_STORE(a_coeffs + l_j, s_fqmul(l_c, l_sv));
        }
    }

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

/* ======== nttpack (even/odd deinterleave) ======== */

{{TARGET_ATTR}} __attribute__((noinline))
void dap_mlkem_ntt_nttpack_{{ARCH_LOWER}}(int16_t a_coeffs[KYBER_N])
{
#if VEC_LANES == 16
#ifdef __AVX512BW__
    const __m256i l_even = _mm256_setr_epi16(
         0,  2,  4,  6,  8, 10, 12, 14,
        16, 18, 20, 22, 24, 26, 28, 30);
    const __m256i l_odd = _mm256_setr_epi16(
         1,  3,  5,  7,  9, 11, 13, 15,
        17, 19, 21, 23, 25, 27, 29, 31);
    for (unsigned l_p = 0; l_p < 8; l_p++) {
        __m256i l_a = _mm256_loadu_si256((const __m256i *)(a_coeffs + 32 * l_p));
        __m256i l_b = _mm256_loadu_si256((const __m256i *)(a_coeffs + 32 * l_p + 16));
        _mm256_storeu_si256((__m256i *)(a_coeffs + 32 * l_p),
                            _mm256_permutex2var_epi16(l_a, l_even, l_b));
        _mm256_storeu_si256((__m256i *)(a_coeffs + 32 * l_p + 16),
                            _mm256_permutex2var_epi16(l_a, l_odd, l_b));
    }
#else
    const __m256i l_mask = _mm256_set1_epi32(0x0000FFFF);
    for (unsigned l_p = 0; l_p < 8; l_p++) {
        __m256i l_a = _mm256_loadu_si256((const __m256i *)(a_coeffs + 32 * l_p));
        __m256i l_b = _mm256_loadu_si256((const __m256i *)(a_coeffs + 32 * l_p + 16));
        __m256i l_ea = _mm256_and_si256(l_a, l_mask);
        __m256i l_oa = _mm256_srli_epi32(l_a, 16);
        __m256i l_eb = _mm256_and_si256(l_b, l_mask);
        __m256i l_ob = _mm256_srli_epi32(l_b, 16);
        __m256i l_ep = _mm256_packus_epi32(l_ea, l_eb);
        __m256i l_op = _mm256_packus_epi32(l_oa, l_ob);
        _mm256_storeu_si256((__m256i *)(a_coeffs + 32 * l_p),
                            _mm256_permute4x64_epi64(l_ep, _MM_SHUFFLE(3,1,2,0)));
        _mm256_storeu_si256((__m256i *)(a_coeffs + 32 * l_p + 16),
                            _mm256_permute4x64_epi64(l_op, _MM_SHUFFLE(3,1,2,0)));
    }
#endif
#else
    int16_t l_tmp[32];
    for (unsigned l_p = 0; l_p < 8; l_p++) {
        int16_t *l_blk = a_coeffs + 32 * l_p;
        for (unsigned l_j = 0; l_j < 16; l_j++) {
            l_tmp[l_j]      = l_blk[2 * l_j];
            l_tmp[16 + l_j] = l_blk[2 * l_j + 1];
        }
        memcpy(l_blk, l_tmp, 64);
    }
#endif
}

/* ======== nttunpack (even/odd interleave) ======== */

{{TARGET_ATTR}} __attribute__((noinline))
void dap_mlkem_ntt_nttunpack_{{ARCH_LOWER}}(int16_t a_coeffs[KYBER_N])
{
#if VEC_LANES == 16
#ifdef __AVX512BW__
    const __m256i l_lo = _mm256_setr_epi16(
         0, 16,  1, 17,  2, 18,  3, 19,
         4, 20,  5, 21,  6, 22,  7, 23);
    const __m256i l_hi = _mm256_setr_epi16(
         8, 24,  9, 25, 10, 26, 11, 27,
        12, 28, 13, 29, 14, 30, 15, 31);
    for (unsigned l_p = 0; l_p < 8; l_p++) {
        __m256i l_evens = _mm256_loadu_si256((const __m256i *)(a_coeffs + 32 * l_p));
        __m256i l_odds  = _mm256_loadu_si256((const __m256i *)(a_coeffs + 32 * l_p + 16));
        _mm256_storeu_si256((__m256i *)(a_coeffs + 32 * l_p),
                            _mm256_permutex2var_epi16(l_evens, l_lo, l_odds));
        _mm256_storeu_si256((__m256i *)(a_coeffs + 32 * l_p + 16),
                            _mm256_permutex2var_epi16(l_evens, l_hi, l_odds));
    }
#else
    for (unsigned l_p = 0; l_p < 8; l_p++) {
        __m256i l_evens = _mm256_loadu_si256((const __m256i *)(a_coeffs + 32 * l_p));
        __m256i l_odds  = _mm256_loadu_si256((const __m256i *)(a_coeffs + 32 * l_p + 16));
        __m256i l_lo = _mm256_unpacklo_epi16(l_evens, l_odds);
        __m256i l_hi = _mm256_unpackhi_epi16(l_evens, l_odds);
        _mm256_storeu_si256((__m256i *)(a_coeffs + 32 * l_p),
                            _mm256_permute2x128_si256(l_lo, l_hi, 0x20));
        _mm256_storeu_si256((__m256i *)(a_coeffs + 32 * l_p + 16),
                            _mm256_permute2x128_si256(l_lo, l_hi, 0x31));
    }
#endif
#else
    int16_t l_tmp[32];
    for (unsigned l_p = 0; l_p < 8; l_p++) {
        int16_t *l_blk = a_coeffs + 32 * l_p;
        for (unsigned l_j = 0; l_j < 16; l_j++) {
            l_tmp[2 * l_j]     = l_blk[l_j];
            l_tmp[2 * l_j + 1] = l_blk[16 + l_j];
        }
        memcpy(l_blk, l_tmp, 64);
    }
#endif
}
