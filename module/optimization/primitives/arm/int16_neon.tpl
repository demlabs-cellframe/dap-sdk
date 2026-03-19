// ARM NEON primitives for 16-bit NTT (128-bit = 8 x int16_t)
// Builds on shared NEON primitive library.

{{#include PRIM_LIB}}

typedef int16x8_t VEC_T;
#define VEC_LANES 8

#define VEC_LOAD(p)        VEC_LOAD_S16(p)
#define VEC_STORE(p, v)    VEC_STORE_S16(p, v)

// No half-width needed — VEC_LANES == 8 is already the minimum SIMD width
// The template will use scalar fallback for layers below VEC_LANES
