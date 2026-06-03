/*
 * Dilithium NTT AVX2 inner-layer primitives (VEC_LANES=8, HVEC_LANES=4).
 *
 * Provides DIL_NTT_FWD_INNER / DIL_NTT_INV_INNER — per-block shuffle-based
 * butterflies for inner layers (len=4/2/1 for forward, len=1/2/4 for inverse).
 *
 * All macros use AVX2 intrinsics (_mm256_shuffle_epi32, _mm256_blend_epi32,
 * _mm256_setr_m128i, _mm256_setr_epi32) for sub-register data movement.
 * The enclosing template supplies: s_mont_reduce_mul_vec, s_mont_reduce_mul_hvec,
 * and the VEC_* / HVEC_* abstract primitives.
 *
 * Included by dap_dilithium_ntt_simd.c.tpl as NTT_INNER_FILE — do not compile standalone.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define DIL_HAS_NTT_INNER 1

#define DIL_NTT_FWD_INNER(v, zetas, k_base, blk, qinv_v, q_v) do {           \
    unsigned _k4 = (k_base);                                                   \
    unsigned _k2 = (k_base) + DIL_N / 8;                                       \
    unsigned _k1 = (k_base) + DIL_N / 8 + DIL_N / 4;                           \
    _k4 += (blk); _k2 += 2 * (blk); _k1 += 4 * (blk);                        \
    /* len=4: butterfly across 128-bit halves */                               \
    {                                                                          \
        HVEC_T _lo = VEC_LO_HALF(v), _hi = VEC_HI_HALF(v);                   \
        HVEC_T _hqinv = HVEC_SET1_32((int32_t)DIL_QINV);                     \
        HVEC_T _hq    = HVEC_SET1_32(DIL_Q);                                  \
        HVEC_T _t = s_mont_reduce_mul_hvec(HVEC_SET1_32((zetas)[_k4]),        \
                                            _hi, _hqinv, _hq);                \
        v = VEC_FROM_HALVES(HVEC_ADD32(_lo, _t), HVEC_SUB32(_lo, _t));        \
    }                                                                          \
    /* len=2: butterfly across 64-bit groups within 128-bit lanes */           \
    {                                                                          \
        VEC_T _lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(1,0,1,0));            \
        VEC_T _hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,2,3,2));            \
        VEC_T _zv = _mm256_setr_m128i(                                         \
            _mm_set1_epi32((zetas)[_k2]),                                      \
            _mm_set1_epi32((zetas)[_k2 + 1]));                                 \
        VEC_T _t = s_mont_reduce_mul_vec(_zv, _hi, (qinv_v), (q_v));          \
        v = _mm256_blend_epi32(VEC_ADD32(_lo, _t),                             \
                               VEC_SUB32(_lo, _t), 0xCC);                      \
    }                                                                          \
    /* len=1: butterfly within each pair */                                    \
    {                                                                          \
        VEC_T _lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(2,2,0,0));            \
        VEC_T _hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,3,1,1));            \
        VEC_T _zv = _mm256_setr_epi32(                                         \
            (zetas)[_k1], (zetas)[_k1], (zetas)[_k1+1], (zetas)[_k1+1],       \
            (zetas)[_k1+2], (zetas)[_k1+2], (zetas)[_k1+3], (zetas)[_k1+3]);  \
        VEC_T _t = s_mont_reduce_mul_vec(_zv, _hi, (qinv_v), (q_v));          \
        v = _mm256_blend_epi32(VEC_ADD32(_lo, _t),                             \
                               VEC_SUB32(_lo, _t), 0xAA);                      \
    }                                                                          \
} while (0)

#define DIL_NTT_INV_INNER(v, zetas_inv, k_base, blk, qinv_v, q_v) do {        \
    unsigned _k1 = (k_base);                                                   \
    unsigned _k2 = (k_base) + DIL_N / 2;                                       \
    unsigned _k4 = (k_base) + DIL_N / 2 + DIL_N / 4;                           \
    _k1 += 4 * (blk); _k2 += 2 * (blk); _k4 += (blk);                        \
    /* len=1: GS butterfly within each pair */                                 \
    {                                                                          \
        VEC_T _lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(2,2,0,0));            \
        VEC_T _hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,3,1,1));            \
        VEC_T _sum = VEC_ADD32(_lo, _hi);                                      \
        VEC_T _dif = VEC_SUB32(_lo, _hi);                                      \
        VEC_T _zv = _mm256_setr_epi32(                                         \
            (zetas_inv)[_k1], (zetas_inv)[_k1], (zetas_inv)[_k1+1], (zetas_inv)[_k1+1],\
            (zetas_inv)[_k1+2], (zetas_inv)[_k1+2], (zetas_inv)[_k1+3], (zetas_inv)[_k1+3]);\
        v = _mm256_blend_epi32(_sum,                                           \
            s_mont_reduce_mul_vec(_zv, _dif, (qinv_v), (q_v)), 0xAA);         \
    }                                                                          \
    /* len=2: GS butterfly across 64-bit groups */                             \
    {                                                                          \
        VEC_T _lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(1,0,1,0));            \
        VEC_T _hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,2,3,2));            \
        VEC_T _sum = VEC_ADD32(_lo, _hi);                                      \
        VEC_T _dif = VEC_SUB32(_lo, _hi);                                      \
        VEC_T _zv = _mm256_setr_m128i(                                         \
            _mm_set1_epi32((zetas_inv)[_k2]),                                  \
            _mm_set1_epi32((zetas_inv)[_k2 + 1]));                              \
        v = _mm256_blend_epi32(_sum,                                           \
            s_mont_reduce_mul_vec(_zv, _dif, (qinv_v), (q_v)), 0xCC);         \
    }                                                                          \
    /* len=4: GS butterfly across 128-bit halves */                            \
    {                                                                          \
        HVEC_T _lo = VEC_LO_HALF(v), _hi = VEC_HI_HALF(v);                   \
        HVEC_T _sum = HVEC_ADD32(_lo, _hi);                                    \
        HVEC_T _dif = HVEC_SUB32(_lo, _hi);                                    \
        HVEC_T _hqinv = HVEC_SET1_32((int32_t)DIL_QINV);                     \
        HVEC_T _hq    = HVEC_SET1_32(DIL_Q);                                  \
        v = VEC_FROM_HALVES(_sum,                                              \
            s_mont_reduce_mul_hvec(HVEC_SET1_32((zetas_inv)[_k4]),             \
                                   _dif, _hqinv, _hq));                        \
    }                                                                          \
} while (0)
