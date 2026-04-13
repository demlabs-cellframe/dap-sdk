// AVX-512BW primitives for 16-bit NTT (512-bit = 32 x int16_t)
// Builds on shared AVX-512 primitive library.

{{#include PRIM_LIB}}

#define VEC_LANES 32
#define HVEC_LANES 16
