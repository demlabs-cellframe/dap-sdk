// ============================================================================
// Generic (Portable C) Primitives for secp256k1 Scalar Arithmetic
// Uses dap_math_ops.h for cross-platform uint128_t support
// ============================================================================

#include "dap_math_ops.h"

// Helper macros for accessing uint128_t parts (works with both native and emulated)
#ifdef DAP_GLOBAL_IS_INT128
    #define LO64_128(x) ((uint64_t)(x))
    #define HI64_128(x) ((uint64_t)((x) >> 64))
#else
    #define LO64_128(x) ((x).lo)
    #define HI64_128(x) ((x).hi)
#endif

// ============================================================================
// 256x256 -> 512 bit multiplication using schoolbook algorithm
// Input: a[4], b[4] in little-endian 64-bit limbs
// Output: l[8] in little-endian 64-bit limbs
// ============================================================================

#define SCALAR_MUL_512_IMPL(l, a, b) do { \
    uint128_t t; \
    uint64_t c0 = 0, c1 = 0; \
    \
    /* Column 0: l[0] = a[0] * b[0] */ \
    MULT_64_128((a)[0], (b)[0], &t); \
    (l)[0] = LO64_128(t); \
    c0 = HI64_128(t); \
    \
    /* Column 1: l[1] */ \
    MULT_64_128((a)[0], (b)[1], &t); \
    c0 += LO64_128(t); c1 = (c0 < LO64_128(t)) + HI64_128(t); \
    MULT_64_128((a)[1], (b)[0], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    (l)[1] = c0; c0 = c1; c1 = 0; \
    \
    /* Column 2: l[2] */ \
    MULT_64_128((a)[0], (b)[2], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    MULT_64_128((a)[1], (b)[1], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    MULT_64_128((a)[2], (b)[0], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    (l)[2] = c0; c0 = c1; c1 = 0; \
    \
    /* Column 3: l[3] */ \
    MULT_64_128((a)[0], (b)[3], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    MULT_64_128((a)[1], (b)[2], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    MULT_64_128((a)[2], (b)[1], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    MULT_64_128((a)[3], (b)[0], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    (l)[3] = c0; c0 = c1; c1 = 0; \
    \
    /* Column 4: l[4] */ \
    MULT_64_128((a)[1], (b)[3], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    MULT_64_128((a)[2], (b)[2], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    MULT_64_128((a)[3], (b)[1], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    (l)[4] = c0; c0 = c1; c1 = 0; \
    \
    /* Column 5: l[5] */ \
    MULT_64_128((a)[2], (b)[3], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    MULT_64_128((a)[3], (b)[2], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    (l)[5] = c0; c0 = c1; c1 = 0; \
    \
    /* Column 6,7: l[6], l[7] */ \
    MULT_64_128((a)[3], (b)[3], &t); \
    c0 += LO64_128(t); \
    c1 = HI64_128(t) + (c0 < LO64_128(t)); \
    (l)[6] = c0; \
    (l)[7] = c1; \
} while(0)

// ============================================================================
// 512-bit reduction mod n using Barrett reduction approximation
// ============================================================================

#define SCALAR_REDUCE_512_IMPL(r, l) do { \
    uint64_t h0 = (l)[4], h1 = (l)[5], h2 = (l)[6], h3 = (l)[7]; \
    uint64_t r0 = (l)[0], r1 = (l)[1], r2 = (l)[2], r3 = (l)[3]; \
    \
    /* Add h * (2^256 mod n) to r */ \
    if (h0 | h1 | h2 | h3) { \
        uint128_t t; \
        uint64_t c = 0, tmp; \
        \
        /* r += h0 * SCALAR_2P256_MOD_N */ \
        MULT_64_128(h0, SCALAR_2P256_MOD_N[0], &t); \
        tmp = r0 + LO64_128(t); c = (tmp < r0); r0 = tmp; \
        tmp = r1 + HI64_128(t) + c; c = (tmp < r1); r1 = tmp; \
        \
        MULT_64_128(h0, SCALAR_2P256_MOD_N[1], &t); \
        tmp = r1 + LO64_128(t); c = (tmp < r1); r1 = tmp; \
        tmp = r2 + HI64_128(t) + c; c = (tmp < r2); r2 = tmp; \
        tmp = r3 + c; r3 = tmp; \
    } \
    \
    /* Final reduction: if r >= n, subtract n */ \
    int over = 0; \
    if (r3 > SCALAR_N[3]) over = 1; \
    else if (r3 == SCALAR_N[3]) { \
        if (r2 > SCALAR_N[2]) over = 1; \
        else if (r2 == SCALAR_N[2]) { \
            if (r1 > SCALAR_N[1]) over = 1; \
            else if (r1 == SCALAR_N[1]) { \
                if (r0 >= SCALAR_N[0]) over = 1; \
            } \
        } \
    } \
    \
    if (over) { \
        uint64_t borrow = 0, tmp; \
        tmp = r0 - SCALAR_N[0]; borrow = (tmp > r0); r0 = tmp; \
        tmp = r1 - SCALAR_N[1] - borrow; borrow = (r1 < SCALAR_N[1] + borrow); r1 = tmp; \
        tmp = r2 - SCALAR_N[2] - borrow; borrow = (r2 < SCALAR_N[2] + borrow); r2 = tmp; \
        r3 = r3 - SCALAR_N[3] - borrow; \
    } \
    \
    (r)[0] = r0; (r)[1] = r1; (r)[2] = r2; (r)[3] = r3; \
} while(0)
