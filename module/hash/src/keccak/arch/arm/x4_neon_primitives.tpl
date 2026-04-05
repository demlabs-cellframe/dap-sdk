// ============================================================================
// NEON Primitives for Keccak×4 (2-way parallel permutation, 2 passes for x4)
//
// Each LANE_T (uint64x2_t) holds the same Keccak lane from 2 instances.
// LANE_WIDTH = 2: two passes needed to cover all 4 instances.
// ============================================================================

{{#include PRIM_LIB}}

#define LANE_T      uint64x2_t
#define LANE_WIDTH  2

#define LANE_LOAD(p)        VEC_LOAD_U64(p)
#define LANE_STORE(p, v)    VEC_STORE_U64(p, v)

#define LANE_XOR(a, b)      VEC_XOR_U64(a, b)
#define LANE_ANDN(a, b)     vbicq_u64((b), (a))    /* ~a & b */
#define LANE_OR(a, b)       vorrq_u64((a), (b))
#define LANE_SET1_64(x)     VEC_SET1_U64(x)

/* Variable-shift rotate via vshlq_u64 (works with runtime n) */
static inline uint64x2_t lane_rol64(uint64x2_t x, int n)
{
    if (__builtin_constant_p(n) && n == 0) return x;
    int64x2_t vn = vdupq_n_s64(n);
    int64x2_t vneg = vdupq_n_s64(n - 64);
    return vorrq_u64(vshlq_u64(x, vn), vshlq_u64(x, vneg));
}
#define LANE_ROL64(x, n) lane_rol64((x), (n))
