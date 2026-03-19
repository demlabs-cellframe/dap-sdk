// ARM NEON primitives for 32-bit NTT (128-bit = 4 x int32_t)
// Builds on shared NEON primitive library.

{{#include PRIM_LIB}}

typedef int32x4_t VEC_T;
#define VEC_LANES 4

#define VEC_LOAD(p)        vld1q_s32((const int32_t *)(p))
#define VEC_STORE(p, v)    vst1q_s32((int32_t *)(p), (v))
#define VEC_SET1_32(x)     vdupq_n_s32(x)
#define VEC_ADD32(a, b)    vaddq_s32(a, b)
#define VEC_SUB32(a, b)    vsubq_s32(a, b)
#define VEC_MULLO32(a, b)  vmulq_s32(a, b)

// Montgomery reduce multiply: (a * b) * R^{-1} mod q, R = 2^32.
// qinv = -q^{-1} mod R, formula: result = (a*b + u*q) >> 32
//
// NEON: process lower 2 and upper 2 elements via widening multiply.
// a*b uses SIGNED widening (vmull_s32).
// u*q uses UNSIGNED widening (vmull_u32) since u is an arbitrary 32-bit value.
#ifdef __aarch64__
#define VEC_MONT_REDUCE_MUL(a, b, qinv, q) ({                               \
    int32x4_t _ab_lo = vmulq_s32((a), (b));                                  \
    int32x4_t _u = vmulq_s32(_ab_lo, (qinv));                                \
    int64x2_t _zb_lo = vmull_s32(vget_low_s32(a), vget_low_s32(b));          \
    uint64x2_t _uq_lo = vmull_u32(vreinterpret_u32_s32(vget_low_s32(_u)),    \
                                   vreinterpret_u32_s32(vget_low_s32(q)));    \
    int64x2_t _sum_lo = vaddq_s64(_zb_lo, vreinterpretq_s64_u64(_uq_lo));    \
    int32x2_t _t_lo = vshrn_n_s64(_sum_lo, 32);                              \
    int64x2_t _zb_hi = vmull_high_s32((a), (b));                             \
    uint64x2_t _uq_hi = vmull_high_u32(vreinterpretq_u32_s32(_u),            \
                                        vreinterpretq_u32_s32(q));            \
    int64x2_t _sum_hi = vaddq_s64(_zb_hi, vreinterpretq_s64_u64(_uq_hi));    \
    int32x2_t _t_hi = vshrn_n_s64(_sum_hi, 32);                              \
    vcombine_s32(_t_lo, _t_hi);                                               \
})
#else
#define VEC_MONT_REDUCE_MUL(a, b, qinv, q) ({                               \
    int32x4_t _ab_lo = vmulq_s32((a), (b));                                  \
    int32x4_t _u = vmulq_s32(_ab_lo, (qinv));                                \
    int64x2_t _zb_lo = vmull_s32(vget_low_s32(a), vget_low_s32(b));          \
    uint64x2_t _uq_lo = vmull_u32(vreinterpret_u32_s32(vget_low_s32(_u)),    \
                                   vreinterpret_u32_s32(vget_low_s32(q)));    \
    int64x2_t _sum_lo = vaddq_s64(_zb_lo, vreinterpretq_s64_u64(_uq_lo));    \
    int32x2_t _t_lo = vshrn_n_s64(_sum_lo, 32);                              \
    int64x2_t _zb_hi = vmull_s32(vget_high_s32(a), vget_high_s32(b));        \
    uint64x2_t _uq_hi = vmull_u32(vreinterpret_u32_s32(vget_high_s32(_u)),   \
                                   vreinterpret_u32_s32(vget_high_s32(q)));   \
    int64x2_t _sum_hi = vaddq_s64(_zb_hi, vreinterpretq_s64_u64(_uq_hi));    \
    int32x2_t _t_hi = vshrn_n_s64(_sum_hi, 32);                              \
    vcombine_s32(_t_lo, _t_hi);                                               \
})
#endif
