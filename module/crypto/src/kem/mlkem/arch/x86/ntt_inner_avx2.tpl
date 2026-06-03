/*
 * ML-KEM NTT AVX2 inner-layer primitives (VEC_LANES=16, HVEC_LANES=8).
 *
 * Provides MLKEM_NTT_FWD_INNER / MLKEM_NTT_INV_INNER — per-block shuffle-based
 * butterflies for layers 8/4/2 (forward) and 2/4/8 (inverse).
 * Also provides MLKEM_NTTPACK / MLKEM_NTTUNPACK for even/odd deinterleave.
 *
 * All macros use AVX2 intrinsics (_mm256_shuffle_epi32, _mm256_blend_epi32,
 * _mm256_setr_m128i) for sub-register data movement.
 * The enclosing template supplies: s_fqmul, s_fqmul_hvec, s_barrett_reduce,
 * s_barrett_reduce_hvec, and the VEC_* / HVEC_* abstract primitives.
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

/* nttpack/nttunpack: even/odd deinterleave using AVX2 pack/permute */

#define MLKEM_HAS_NTTPACK 1

#define MLKEM_NTTPACK(coeffs) do {                                             \
    const __m256i _mask = _mm256_set1_epi32(0x0000FFFF);                       \
    for (unsigned _p = 0; _p < 8; _p++) {                                      \
        __m256i _a = _mm256_loadu_si256((const __m256i *)((coeffs) + 32 * _p));\
        __m256i _b = _mm256_loadu_si256((const __m256i *)((coeffs) + 32 * _p + 16));\
        __m256i _ea = _mm256_and_si256(_a, _mask);                             \
        __m256i _oa = _mm256_srli_epi32(_a, 16);                               \
        __m256i _eb = _mm256_and_si256(_b, _mask);                             \
        __m256i _ob = _mm256_srli_epi32(_b, 16);                               \
        __m256i _ep = _mm256_packus_epi32(_ea, _eb);                           \
        __m256i _op = _mm256_packus_epi32(_oa, _ob);                           \
        _mm256_storeu_si256((__m256i *)((coeffs) + 32 * _p),                   \
                            _mm256_permute4x64_epi64(_ep, _MM_SHUFFLE(3,1,2,0)));\
        _mm256_storeu_si256((__m256i *)((coeffs) + 32 * _p + 16),             \
                            _mm256_permute4x64_epi64(_op, _MM_SHUFFLE(3,1,2,0)));\
    }                                                                          \
} while (0)

#define MLKEM_NTTUNPACK(coeffs) do {                                           \
    for (unsigned _p = 0; _p < 8; _p++) {                                      \
        __m256i _evens = _mm256_loadu_si256((const __m256i *)((coeffs) + 32 * _p));\
        __m256i _odds  = _mm256_loadu_si256((const __m256i *)((coeffs) + 32 * _p + 16));\
        __m256i _lo = _mm256_unpacklo_epi16(_evens, _odds);                    \
        __m256i _hi = _mm256_unpackhi_epi16(_evens, _odds);                    \
        _mm256_storeu_si256((__m256i *)((coeffs) + 32 * _p),                   \
                            _mm256_permute2x128_si256(_lo, _hi, 0x20));        \
        _mm256_storeu_si256((__m256i *)((coeffs) + 32 * _p + 16),             \
                            _mm256_permute2x128_si256(_lo, _hi, 0x31));        \
    }                                                                          \
} while (0)
