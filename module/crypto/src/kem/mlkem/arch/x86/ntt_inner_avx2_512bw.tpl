/*
 * ML-KEM NTT AVX2+AVX-512BW inner-layer primitives.
 *
 * Inner butterfly layers are identical to the AVX2 variant (Layer 8/4/2 use
 * YMM shuffles). The difference is nttpack/nttunpack: AVX-512BW provides
 * vpermt2w (_mm256_permutex2var_epi16) which replaces the 4-stage
 * mask/shift/pack pipeline with a single cross-lane permute.
 *
 * Included by dap_mlkem_ntt_simd.c.tpl as NTT_INNER_FILE — do not compile standalone.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define MLKEM_HAS_NTT_INNER 1
#define MLKEM_NTT_INV_OUTER_K 112

#define MLKEM_NTT_FWD_INNER(v, zetas, blk) do {                               \
    /* Layer 8: CT butterfly across 128-bit halves */                          \
    {                                                                          \
        HVEC_T _lo = VEC_LO_HALF(v), _hi = VEC_HI_HALF(v);                   \
        HVEC_T _t = s_fqmul_hvec(HVEC_SET1_16((zetas)[16 + (blk)]), _hi);    \
        v = VEC_FROM_HALVES(HVEC_ADD16(_lo, _t), HVEC_SUB16(_lo, _t));        \
    }                                                                          \
    /* Layer 4: CT butterfly across 64-bit groups within 128-bit lanes */      \
    {                                                                          \
        VEC_T _lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(1,0,1,0));            \
        VEC_T _hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,2,3,2));            \
        VEC_T _zv = _mm256_setr_m128i(                                         \
            _mm_set1_epi16((zetas)[32 + 2 * (blk)]),                           \
            _mm_set1_epi16((zetas)[33 + 2 * (blk)]));                          \
        VEC_T _t = s_fqmul(_zv, _hi);                                         \
        v = _mm256_blend_epi32(VEC_ADD16(_lo, _t),                             \
                               VEC_SUB16(_lo, _t), 0xCC);                      \
    }                                                                          \
    /* Layer 2: CT butterfly across 32-bit groups */                           \
    {                                                                          \
        VEC_T _lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(2,2,0,0));            \
        VEC_T _hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,3,1,1));            \
        unsigned _z2 = 64 + 4 * (blk);                                        \
        __m128i _z4  = _mm_loadl_epi64((const __m128i *)((zetas) + _z2));      \
        __m128i _zd  = _mm_unpacklo_epi16(_z4, _z4);                          \
        VEC_T _zv = _mm256_setr_m128i(                                         \
            _mm_unpacklo_epi32(_zd, _zd),                                      \
            _mm_unpackhi_epi32(_zd, _zd));                                     \
        VEC_T _t = s_fqmul(_zv, _hi);                                         \
        v = _mm256_blend_epi32(VEC_ADD16(_lo, _t),                             \
                               VEC_SUB16(_lo, _t), 0xAA);                      \
    }                                                                          \
    v = s_barrett_reduce(v);                                                   \
} while (0)

#define MLKEM_NTT_INV_INNER(v, zetas_inv, blk) do {                           \
    /* Layer 2 (GS): merge 32-bit groups */                                    \
    {                                                                          \
        VEC_T _lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(2,2,0,0));            \
        VEC_T _hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,3,1,1));            \
        VEC_T _sum = VEC_ADD16(_lo, _hi);                                      \
        VEC_T _dif = VEC_SUB16(_lo, _hi);                                      \
        unsigned _z2 = 4 * (blk);                                             \
        __m128i _z4  = _mm_loadl_epi64((const __m128i *)((zetas_inv) + _z2)); \
        __m128i _zd  = _mm_unpacklo_epi16(_z4, _z4);                          \
        VEC_T _zv = _mm256_setr_m128i(                                         \
            _mm_unpacklo_epi32(_zd, _zd),                                      \
            _mm_unpackhi_epi32(_zd, _zd));                                     \
        v = _mm256_blend_epi32(s_barrett_reduce(_sum),                         \
                               s_fqmul(_zv, _dif), 0xAA);                     \
    }                                                                          \
    /* Layer 4 (GS): merge 64-bit groups within 128-bit lanes */               \
    {                                                                          \
        VEC_T _lo = _mm256_shuffle_epi32(v, _MM_SHUFFLE(1,0,1,0));            \
        VEC_T _hi = _mm256_shuffle_epi32(v, _MM_SHUFFLE(3,2,3,2));            \
        VEC_T _sum = VEC_ADD16(_lo, _hi);                                      \
        VEC_T _dif = VEC_SUB16(_lo, _hi);                                      \
        VEC_T _zv = _mm256_setr_m128i(                                         \
            _mm_set1_epi16((zetas_inv)[64 + 2 * (blk)]),                       \
            _mm_set1_epi16((zetas_inv)[65 + 2 * (blk)]));                      \
        v = _mm256_blend_epi32(s_barrett_reduce(_sum),                         \
                               s_fqmul(_zv, _dif), 0xCC);                     \
    }                                                                          \
    /* Layer 8 (GS): merge 128-bit halves */                                   \
    {                                                                          \
        HVEC_T _lo = VEC_LO_HALF(v), _hi = VEC_HI_HALF(v);                   \
        HVEC_T _sum = HVEC_ADD16(_lo, _hi);                                    \
        HVEC_T _dif = HVEC_SUB16(_lo, _hi);                                    \
        v = VEC_FROM_HALVES(                                                   \
            s_barrett_reduce_hvec(_sum),                                        \
            s_fqmul_hvec(HVEC_SET1_16((zetas_inv)[96 + (blk)]), _dif));        \
    }                                                                          \
} while (0)

/* nttpack/nttunpack: AVX-512BW vpermt2w for single-instruction deinterleave */

#define MLKEM_HAS_NTTPACK 1

#define MLKEM_NTTPACK(coeffs) do {                                             \
    const __m256i _even = _mm256_setr_epi16(                                   \
         0,  2,  4,  6,  8, 10, 12, 14,                                       \
        16, 18, 20, 22, 24, 26, 28, 30);                                      \
    const __m256i _odd = _mm256_setr_epi16(                                    \
         1,  3,  5,  7,  9, 11, 13, 15,                                       \
        17, 19, 21, 23, 25, 27, 29, 31);                                      \
    for (unsigned _p = 0; _p < 8; _p++) {                                      \
        __m256i _a = _mm256_loadu_si256((const __m256i *)((coeffs) + 32 * _p));\
        __m256i _b = _mm256_loadu_si256((const __m256i *)((coeffs) + 32 * _p + 16));\
        _mm256_storeu_si256((__m256i *)((coeffs) + 32 * _p),                   \
                            _mm256_permutex2var_epi16(_a, _even, _b));         \
        _mm256_storeu_si256((__m256i *)((coeffs) + 32 * _p + 16),             \
                            _mm256_permutex2var_epi16(_a, _odd, _b));          \
    }                                                                          \
} while (0)

#define MLKEM_NTTUNPACK(coeffs) do {                                           \
    const __m256i _lo = _mm256_setr_epi16(                                     \
         0, 16,  1, 17,  2, 18,  3, 19,                                       \
         4, 20,  5, 21,  6, 22,  7, 23);                                      \
    const __m256i _hi = _mm256_setr_epi16(                                     \
         8, 24,  9, 25, 10, 26, 11, 27,                                       \
        12, 28, 13, 29, 14, 30, 15, 31);                                      \
    for (unsigned _p = 0; _p < 8; _p++) {                                      \
        __m256i _evens = _mm256_loadu_si256((const __m256i *)((coeffs) + 32 * _p));\
        __m256i _odds  = _mm256_loadu_si256((const __m256i *)((coeffs) + 32 * _p + 16));\
        _mm256_storeu_si256((__m256i *)((coeffs) + 32 * _p),                   \
                            _mm256_permutex2var_epi16(_evens, _lo, _odds));    \
        _mm256_storeu_si256((__m256i *)((coeffs) + 32 * _p + 16),             \
                            _mm256_permutex2var_epi16(_evens, _hi, _odds));    \
    }                                                                          \
} while (0)
