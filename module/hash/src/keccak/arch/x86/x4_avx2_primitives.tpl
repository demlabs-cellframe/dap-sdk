// ============================================================================
// AVX2 Primitives for Keccak×4 (4-way parallel permutation)
//
// Each LANE_T (__m256i) holds the same Keccak lane from 4 independent states.
// LANE_WIDTH = 4: one AVX2 pass covers all 4 instances.
// ============================================================================

{{#include PRIM_LIB}}

#define LANE_T      VEC_T
#define LANE_WIDTH  4

#define LANE_LOAD(p)        VEC_LOAD(p)
#define LANE_STORE(p, v)    VEC_STORE(p, v)

#define LANE_XOR(a, b)      VEC_XOR(a, b)
#define LANE_ANDN(a, b)     VEC_ANDNOT(a, b)    /* ~a & b */
#define LANE_OR(a, b)       VEC_OR(a, b)
#define LANE_SET1_64(x)     VEC_SET1_64((long long)(x))

/* AVX2 has no native 64-bit rotate; emulate with shift+shift+OR */
static inline __m256i lane_rol64(__m256i x, int n)
{
    if (__builtin_constant_p(n) && n == 0) return x;
    return _mm256_or_si256(_mm256_slli_epi64(x, n),
                           _mm256_srli_epi64(x, 64 - n));
}
#define LANE_ROL64(x, n) lane_rol64((x), (n))
