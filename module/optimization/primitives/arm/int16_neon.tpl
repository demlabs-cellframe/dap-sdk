// ARM NEON primitives for 16-bit polynomial/NTT ops (128-bit = 8 x int16_t)
// Builds on shared NEON primitive library.

{{#include PRIM_LIB}}

typedef int16x8_t VEC_T;
#define VEC_LANES 8

#define VEC_ZERO()         vdupq_n_s16(0)

#define VEC_LOAD(p)        VEC_LOAD_S16(p)
#define VEC_STORE(p, v)    VEC_STORE_S16(p, v)
#define VEC_AND(a, b)      VEC_AND_S16(a, b)

#define VEC_SWAP_ADJACENT16(v)  vrev32q_s16(v)

static inline int16x8_t s_vec_blend_odd_s16(int16x8_t a, int16x8_t b) {
    static const uint16_t l_mask[8] = {0, 0xFFFF, 0, 0xFFFF, 0, 0xFFFF, 0, 0xFFFF};
    return vbslq_s16(vld1q_u16(l_mask), b, a);
}
#define VEC_BLEND_ODD(a, b) s_vec_blend_odd_s16(a, b)
