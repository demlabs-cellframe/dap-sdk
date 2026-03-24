/* ChaCha20 SIMD primitives — AVX2, 8 lanes (8 parallel blocks) */

{{#include PRIM_LIB}}

typedef VEC_T chacha_vec_t;
#define CHACHA_LANES VEC_LANES_32
#define CHACHA_BLOCK_BYTES (64 * CHACHA_LANES)

#define VEC_SET1(x)       VEC_SET1_32(x)
#define VEC_LOADU(p)      VEC_LOAD(p)
#define VEC_STOREU(p, v)  VEC_STORE(p, v)

#define VEC_ROTL16(v) VEC_OR(VEC_SLLI32(v, 16), VEC_SRLI32(v, 16))
#define VEC_ROTL12(v) VEC_OR(VEC_SLLI32(v, 12), VEC_SRLI32(v, 20))
#define VEC_ROTL8(v)  VEC_OR(VEC_SLLI32(v, 8),  VEC_SRLI32(v, 24))
#define VEC_ROTL7(v)  VEC_OR(VEC_SLLI32(v, 7),  VEC_SRLI32(v, 25))

static inline chacha_vec_t s_vec_counter_init(uint32_t a_base)
{
    return VEC_SET_32((int)(a_base + 7), (int)(a_base + 6),
                      (int)(a_base + 5), (int)(a_base + 4),
                      (int)(a_base + 3), (int)(a_base + 2),
                      (int)(a_base + 1), (int)(a_base));
}

/*
 * 8×8 transpose + XOR plaintext + store for one half (8 state words).
 * Fuses transpose with load-XOR-store to minimize memory traffic.
 * 3-stage approach: unpacklo/hi 32 → unpacklo/hi 64 → permute2x128.
 */
#define CHACHA_TRANSPOSE_XOR_STORE_8(V0,V1,V2,V3,V4,V5,V6,V7, BOFF, OUTP, INP) \
do { \
    __m256i _t0 = _mm256_unpacklo_epi32(V0, V1); \
    __m256i _t1 = _mm256_unpackhi_epi32(V0, V1); \
    __m256i _t2 = _mm256_unpacklo_epi32(V2, V3); \
    __m256i _t3 = _mm256_unpackhi_epi32(V2, V3); \
    __m256i _t4 = _mm256_unpacklo_epi32(V4, V5); \
    __m256i _t5 = _mm256_unpackhi_epi32(V4, V5); \
    __m256i _t6 = _mm256_unpacklo_epi32(V6, V7); \
    __m256i _t7 = _mm256_unpackhi_epi32(V6, V7); \
    __m256i _u0 = _mm256_unpacklo_epi64(_t0, _t2); \
    __m256i _u1 = _mm256_unpackhi_epi64(_t0, _t2); \
    __m256i _u2 = _mm256_unpacklo_epi64(_t1, _t3); \
    __m256i _u3 = _mm256_unpackhi_epi64(_t1, _t3); \
    __m256i _u4 = _mm256_unpacklo_epi64(_t4, _t6); \
    __m256i _u5 = _mm256_unpackhi_epi64(_t4, _t6); \
    __m256i _u6 = _mm256_unpacklo_epi64(_t5, _t7); \
    __m256i _u7 = _mm256_unpackhi_epi64(_t5, _t7); \
    _mm256_storeu_si256((__m256i *)((OUTP) + 0*64 + (BOFF)), _mm256_xor_si256(_mm256_permute2x128_si256(_u0, _u4, 0x20), _mm256_loadu_si256((const __m256i *)((INP) + 0*64 + (BOFF))))); \
    _mm256_storeu_si256((__m256i *)((OUTP) + 1*64 + (BOFF)), _mm256_xor_si256(_mm256_permute2x128_si256(_u1, _u5, 0x20), _mm256_loadu_si256((const __m256i *)((INP) + 1*64 + (BOFF))))); \
    _mm256_storeu_si256((__m256i *)((OUTP) + 2*64 + (BOFF)), _mm256_xor_si256(_mm256_permute2x128_si256(_u2, _u6, 0x20), _mm256_loadu_si256((const __m256i *)((INP) + 2*64 + (BOFF))))); \
    _mm256_storeu_si256((__m256i *)((OUTP) + 3*64 + (BOFF)), _mm256_xor_si256(_mm256_permute2x128_si256(_u3, _u7, 0x20), _mm256_loadu_si256((const __m256i *)((INP) + 3*64 + (BOFF))))); \
    _mm256_storeu_si256((__m256i *)((OUTP) + 4*64 + (BOFF)), _mm256_xor_si256(_mm256_permute2x128_si256(_u0, _u4, 0x31), _mm256_loadu_si256((const __m256i *)((INP) + 4*64 + (BOFF))))); \
    _mm256_storeu_si256((__m256i *)((OUTP) + 5*64 + (BOFF)), _mm256_xor_si256(_mm256_permute2x128_si256(_u1, _u5, 0x31), _mm256_loadu_si256((const __m256i *)((INP) + 5*64 + (BOFF))))); \
    _mm256_storeu_si256((__m256i *)((OUTP) + 6*64 + (BOFF)), _mm256_xor_si256(_mm256_permute2x128_si256(_u2, _u6, 0x31), _mm256_loadu_si256((const __m256i *)((INP) + 6*64 + (BOFF))))); \
    _mm256_storeu_si256((__m256i *)((OUTP) + 7*64 + (BOFF)), _mm256_xor_si256(_mm256_permute2x128_si256(_u3, _u7, 0x31), _mm256_loadu_si256((const __m256i *)((INP) + 7*64 + (BOFF))))); \
} while (0)

#define CHACHA_XOR_STORE_FULL(W0,W1,W2,W3,W4,W5,W6,W7,W8,W9,W10,W11,W12,W13,W14,W15, OUTP, INP) \
do { \
    CHACHA_TRANSPOSE_XOR_STORE_8(W0,W1,W2,W3,W4,W5,W6,W7, 0, OUTP, INP); \
    CHACHA_TRANSPOSE_XOR_STORE_8(W8,W9,W10,W11,W12,W13,W14,W15, 32, OUTP, INP); \
} while (0)
