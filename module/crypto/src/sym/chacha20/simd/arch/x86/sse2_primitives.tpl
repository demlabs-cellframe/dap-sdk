/* ChaCha20 SIMD primitives — SSE2, 4 lanes (4 parallel blocks) */

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
    return VEC_SET_32((int)(a_base + 3), (int)(a_base + 2),
                      (int)(a_base + 1), (int)(a_base));
}
