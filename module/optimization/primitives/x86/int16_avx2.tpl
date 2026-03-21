// AVX2 primitives for 16-bit polynomial/NTT ops (256-bit = 16 x int16_t)
// Builds on shared AVX2 primitive library.

{{#include PRIM_LIB}}

#define VEC_LANES 16
#define HVEC_LANES 8

static inline __m256i s_vec_swap_adjacent16(__m256i v) {
    v = _mm256_shufflelo_epi16(v, 0xB1);
    return _mm256_shufflehi_epi16(v, 0xB1);
}
#define VEC_SWAP_ADJACENT16(v) s_vec_swap_adjacent16(v)
#define VEC_BLEND_ODD(a, b) VEC_BLEND16(a, b, 0xAA)
