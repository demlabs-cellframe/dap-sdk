// SSE2 primitives for 16-bit NTT (128-bit = 8 x int16_t)
// Builds on shared SSE2 primitive library.

{{#include PRIM_LIB}}

#define VEC_LANES 8
