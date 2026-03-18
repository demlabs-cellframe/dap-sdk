/* ChaCha20 SIMD primitives — NEON, 4 lanes (4 parallel blocks) */
#include <arm_neon.h>

typedef uint32x4_t chacha_vec_t;
#define CHACHA_LANES 4
#define CHACHA_BLOCK_BYTES (64 * CHACHA_LANES)

#define VEC_ADD32(a, b)   vaddq_u32((a), (b))
#define VEC_XOR(a, b)     veorq_u32((a), (b))
#define VEC_SET1(x)       vdupq_n_u32((x))
#define VEC_LOADU(p)      vld1q_u32((const uint32_t *)(p))
#define VEC_STOREU(p, v)  vst1q_u32((uint32_t *)(p), (v))

#define VEC_ROTL16(v) \
    vreinterpretq_u32_u16(vrev32q_u16(vreinterpretq_u16_u32((v))))
#define VEC_ROTL12(v) \
    vorrq_u32(vshlq_n_u32((v), 12), vshrq_n_u32((v), 20))
#define VEC_ROTL8(v) \
    vorrq_u32(vshlq_n_u32((v), 8), vshrq_n_u32((v), 24))
#define VEC_ROTL7(v) \
    vorrq_u32(vshlq_n_u32((v), 7), vshrq_n_u32((v), 25))

static inline chacha_vec_t s_vec_counter_init(uint32_t a_base)
{
    uint32_t c[4] = {a_base, a_base + 1, a_base + 2, a_base + 3};
    return vld1q_u32(c);
}
