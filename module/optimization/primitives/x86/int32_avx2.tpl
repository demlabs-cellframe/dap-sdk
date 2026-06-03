// AVX2 primitives for 32-bit NTT (256-bit = 8 x int32_t)
// Builds on shared AVX2 primitive library.

{{#include PRIM_LIB}}

#define VEC_LANES 8
#define HVEC_LANES 4

// Signed 32x32->64 widening multiply (even-indexed 32-bit elements only)
#define VEC_MUL_EVEN_S32(a, b)   _mm256_mul_epi32(a, b)
// Unsigned 32x32->64 widening multiply (even-indexed 32-bit elements only)
#define VEC_MUL_EVEN_U32(a, b)   _mm256_mul_epu32(a, b)

#define VEC_SHIFT_ODD32(a)       _mm256_srli_epi64(a, 32)
#define VEC_ADD64(a, b)          _mm256_add_epi64(a, b)
#define VEC_SRLI64(a, n)         _mm256_srli_epi64(a, n)
#define VEC_BLEND_EVEN_ODD32(even, odd)  _mm256_blend_epi32(even, odd, 0xAA)

// Montgomery reduce multiply: (a * b) * R^{-1} mod q, R = 2^32.
// qinv = -q^{-1} mod R, formula: result = (a*b + u*q) >> 32
// where u = lo32(a*b) * qinv (mod 2^32).
//
// Even lanes: compute for elements [0],[2],[4],[6], result in lower 32 bits.
// Odd lanes:  compute for elements [1],[3],[5],[7], result in upper 32 bits.
// Blend recombines all 8 results.
//
// a*b uses SIGNED multiply (inputs are signed coefficients).
// u*q uses UNSIGNED multiply (u is an arbitrary 32-bit Montgomery parameter).
#define VEC_MONT_REDUCE_MUL(a, b, qinv, q) ({               \
    VEC_T _ab_lo = VEC_MULLO32((a), (b));                    \
    VEC_T _u = VEC_MULLO32(_ab_lo, (qinv));                  \
    VEC_T _ab_ev = VEC_MUL_EVEN_S32((a), (b));               \
    VEC_T _uq_ev = VEC_MUL_EVEN_U32(_u, (q));                \
    VEC_T _s_ev  = VEC_SRLI64(VEC_ADD64(_ab_ev, _uq_ev), 32);\
    VEC_T _a_od = VEC_SHIFT_ODD32((a));                      \
    VEC_T _b_od = VEC_SHIFT_ODD32((b));                      \
    VEC_T _u_od = VEC_SHIFT_ODD32(_u);                       \
    VEC_T _ab_od = VEC_MUL_EVEN_S32(_a_od, _b_od);           \
    VEC_T _uq_od = VEC_MUL_EVEN_U32(_u_od, (q));             \
    VEC_T _s_od  = VEC_ADD64(_ab_od, _uq_od);                \
    VEC_BLEND_EVEN_ODD32(_s_ev, _s_od);                      \
})

// Half-width (128-bit = 4 x int32) Montgomery reduce multiply
#define HVEC_MUL_EVEN_S32(a, b)  _mm_mul_epi32(a, b)
#define HVEC_MUL_EVEN_U32(a, b)  _mm_mul_epu32(a, b)
#define HVEC_SHIFT_ODD32(a)      _mm_srli_epi64(a, 32)
#define HVEC_ADD64(a, b)         _mm_add_epi64(a, b)
#define HVEC_SRLI64(a, n)        _mm_srli_epi64(a, n)

static inline __m128i s_hvec_blend_even_odd32(__m128i a_even, __m128i a_odd) {
    return _mm_or_si128(a_even,
                        _mm_and_si128(a_odd, _mm_set_epi32(-1, 0, -1, 0)));
}
#define HVEC_BLEND_EVEN_ODD32(even, odd)  s_hvec_blend_even_odd32(even, odd)

#define HVEC_MONT_REDUCE_MUL(a, b, qinv, q) ({               \
    HVEC_T _ab_lo = HVEC_MULLO32((a), (b));                   \
    HVEC_T _u = HVEC_MULLO32(_ab_lo, (qinv));                 \
    HVEC_T _ab_ev = HVEC_MUL_EVEN_S32((a), (b));              \
    HVEC_T _uq_ev = HVEC_MUL_EVEN_U32(_u, (q));               \
    HVEC_T _s_ev  = HVEC_SRLI64(HVEC_ADD64(_ab_ev, _uq_ev), 32);\
    HVEC_T _a_od = HVEC_SHIFT_ODD32((a));                     \
    HVEC_T _b_od = HVEC_SHIFT_ODD32((b));                     \
    HVEC_T _u_od = HVEC_SHIFT_ODD32(_u);                      \
    HVEC_T _ab_od = HVEC_MUL_EVEN_S32(_a_od, _b_od);          \
    HVEC_T _uq_od = HVEC_MUL_EVEN_U32(_u_od, (q));            \
    HVEC_T _s_od  = HVEC_ADD64(_ab_od, _uq_od);               \
    HVEC_BLEND_EVEN_ODD32(_s_ev, _s_od);                      \
})

// Half-width additional ops
#define HVEC_SET1_32(x)     _mm_set1_epi32((int)(x))
#define HVEC_ADD32(a, b)    _mm_add_epi32(a, b)
#define HVEC_SUB32(a, b)    _mm_sub_epi32(a, b)
#define HVEC_MULLO32(a, b)  _mm_mullo_epi32(a, b)
#define HVEC_LOAD(p)        _mm_loadu_si128((const __m128i *)(p))
#define HVEC_STORE(p, v)    _mm_storeu_si128((__m128i *)(p), (v))
