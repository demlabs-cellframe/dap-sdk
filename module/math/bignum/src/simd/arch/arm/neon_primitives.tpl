// ARM NEON primitives for 16-bit NTT (128-bit = 8 × int16_t)

typedef int16x8_t VEC_T;
#define VEC_LANES 8

#define VEC_LOAD(p)        vld1q_s16((const int16_t *)(p))
#define VEC_STORE(p, v)    vst1q_s16((int16_t *)(p), (v))
#define VEC_SET1_16(x)     vdupq_n_s16(x)
#define VEC_ADD16(a, b)    vaddq_s16(a, b)
#define VEC_SUB16(a, b)    vsubq_s16(a, b)

static inline int16x8_t neon_mullo_s16(int16x8_t a, int16x8_t b) {
    return vmulq_s16(a, b);
}
static inline int16x8_t neon_mulhi_s16(int16x8_t a, int16x8_t b) {
    int16x4_t a_lo = vget_low_s16(a),  a_hi = vget_high_s16(a);
    int16x4_t b_lo = vget_low_s16(b),  b_hi = vget_high_s16(b);
    int32x4_t p_lo = vmull_s16(a_lo, b_lo);
    int32x4_t p_hi = vmull_s16(a_hi, b_hi);
    return vcombine_s16(vshrn_n_s32(p_lo, 16), vshrn_n_s32(p_hi, 16));
}

#define VEC_MULLO16(a, b)  neon_mullo_s16(a, b)
#define VEC_MULHI16(a, b)  neon_mulhi_s16(a, b)
#define VEC_SRAI16(a, n)   vshrq_n_s16(a, n)

// No half-width needed — VEC_LANES == 8 is already the minimum SIMD width
// The template will use scalar fallback for layers below VEC_LANES
