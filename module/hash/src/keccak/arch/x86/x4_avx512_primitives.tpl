// ============================================================================
// AVX-512VL Primitives for Keccak×4 (4-way parallel permutation)
//
// Uses 256-bit vectors (__m256i) holding 4 Keccak lanes, but leverages
// AVX-512VL instructions:
//   - vpternlogq: Chi step a ^ (~b & c) in 1 instruction (was 2 on AVX2)
//   - vprolvq:    native 64-bit rotate in 1 instruction (was 3 on AVX2)
// Saves ~75 instructions per round × 24 rounds = ~1800 instructions per permute.
// ============================================================================

{{#include PRIM_LIB}}

#define LANE_T      __m256i
#define LANE_WIDTH  4

#define LANE_LOAD(p)        _mm256_load_si256((const __m256i *)(p))
#define LANE_STORE(p, v)    _mm256_store_si256((__m256i *)(p), (v))

#define LANE_XOR(a, b)      _mm256_xor_si256((a), (b))
#define LANE_ANDN(a, b)     _mm256_andnot_si256((a), (b))
#define LANE_OR(a, b)       _mm256_or_si256((a), (b))
#define LANE_SET1_64(x)     _mm256_set1_epi64x((long long)(x))

// Chi fused: a ^ (~b & c) via vpternlogq (truth table 0xD2)
#define LANE_CHI(a, b, c)   _mm256_ternarylogic_epi64((a), (b), (c), 0xD2)

// Native 64-bit rotate via AVX-512VL vprolvq
static inline __m256i lane_rol64(__m256i x, int n)
{
    if (__builtin_constant_p(n) && n == 0) return x;
    return _mm256_rolv_epi64(x, _mm256_set1_epi64x(n));
}
#define LANE_ROL64(x, n) lane_rol64((x), (n))
