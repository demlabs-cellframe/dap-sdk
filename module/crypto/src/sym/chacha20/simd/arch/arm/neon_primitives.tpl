/* ChaCha20 SIMD primitives — NEON, 4 lanes (4 parallel blocks) */

{{#include PRIM_LIB}}

typedef uint32x4_t chacha_vec_t;
#define CHACHA_LANES VEC_LANES_32
#define CHACHA_BLOCK_BYTES (64 * CHACHA_LANES)

#define VEC_ADD32(a, b)   VEC_ADD_U32(a, b)
#define VEC_XOR(a, b)     VEC_XOR_U32(a, b)
#define VEC_SET1(x)       VEC_SET1_U32(x)
#define VEC_LOADU(p)      VEC_LOAD_U32(p)
#define VEC_STOREU(p, v)  VEC_STORE_U32(p, v)

#define VEC_ROTL16(v) \
    vreinterpretq_u32_u16(vrev32q_u16(vreinterpretq_u16_u32((v))))
#define VEC_ROTL12(v) \
    VEC_OR_U32(VEC_SHL_U32(v, 12), VEC_SHR_U32(v, 20))
#define VEC_ROTL8(v) \
    VEC_OR_U32(VEC_SHL_U32(v, 8), VEC_SHR_U32(v, 24))
#define VEC_ROTL7(v) \
    VEC_OR_U32(VEC_SHL_U32(v, 7), VEC_SHR_U32(v, 25))

static inline chacha_vec_t s_vec_counter_init(uint32_t a_base)
{
    uint32_t c[4] = {a_base, a_base + 1, a_base + 2, a_base + 3};
    return vld1q_u32(c);
}
