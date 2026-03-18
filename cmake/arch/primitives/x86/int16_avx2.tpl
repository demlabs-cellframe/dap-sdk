// AVX2 primitives for 16-bit NTT (256-bit = 16 x int16_t)
// Builds on shared AVX2 primitive library.

{{#include PRIM_LIB}}

#define VEC_LANES 16
#define HVEC_LANES 8
