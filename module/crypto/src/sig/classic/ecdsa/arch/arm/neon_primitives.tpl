// ============================================================================
// ARM64 NEON Primitives for secp256k1 Scalar Arithmetic
// Uses UMULH for high-part multiplication and NEON for parallel operations
// ============================================================================

#if defined(__aarch64__)

#include <arm_neon.h>

// ARM64 has native 64x64->128 multiplication via MUL + UMULH
// MUL: low 64 bits
// UMULH: high 64 bits

// ============================================================================
// 256x256 -> 512 bit multiplication using ARM64 MUL/UMULH
// ============================================================================

#define SCALAR_MUL_512_IMPL(l, a, b) do { \
    /* Use ARM64 intrinsics where available, else inline asm */ \
    uint64_t a0 = (a)[0], a1 = (a)[1], a2 = (a)[2], a3 = (a)[3]; \
    uint64_t b0 = (b)[0], b1 = (b)[1], b2 = (b)[2], b3 = (b)[3]; \
    \
    __asm__ __volatile__( \
        /* Column 0: a0*b0 */ \
        "mul x8, %[a0], %[b0]\n" \
        "umulh x9, %[a0], %[b0]\n" \
        "str x8, [%[pl], #0]\n" \
        \
        /* Column 1: a0*b1 + a1*b0 */ \
        "mul x8, %[a0], %[b1]\n" \
        "umulh x10, %[a0], %[b1]\n" \
        "adds x9, x9, x8\n" \
        \
        "mul x8, %[a1], %[b0]\n" \
        "umulh x11, %[a1], %[b0]\n" \
        "adcs x10, x10, x11\n" \
        "adc x11, xzr, xzr\n" \
        "adds x9, x9, x8\n" \
        "str x9, [%[pl], #8]\n" \
        \
        /* Column 2: a0*b2 + a1*b1 + a2*b0 */ \
        "mul x8, %[a0], %[b2]\n" \
        "umulh x9, %[a0], %[b2]\n" \
        "adcs x10, x10, x8\n" \
        "adc x11, x11, x9\n" \
        \
        "mul x8, %[a1], %[b1]\n" \
        "umulh x9, %[a1], %[b1]\n" \
        "adds x10, x10, x8\n" \
        "adcs x11, x11, x9\n" \
        "adc x12, xzr, xzr\n" \
        \
        "mul x8, %[a2], %[b0]\n" \
        "umulh x9, %[a2], %[b0]\n" \
        "adds x10, x10, x8\n" \
        "adcs x11, x11, x9\n" \
        "adc x12, x12, xzr\n" \
        "str x10, [%[pl], #16]\n" \
        \
        /* Column 3: a0*b3 + a1*b2 + a2*b1 + a3*b0 */ \
        "mul x8, %[a0], %[b3]\n" \
        "umulh x9, %[a0], %[b3]\n" \
        "adds x11, x11, x8\n" \
        "adcs x12, x12, x9\n" \
        "adc x13, xzr, xzr\n" \
        \
        "mul x8, %[a1], %[b2]\n" \
        "umulh x9, %[a1], %[b2]\n" \
        "adds x11, x11, x8\n" \
        "adcs x12, x12, x9\n" \
        "adc x13, x13, xzr\n" \
        \
        "mul x8, %[a2], %[b1]\n" \
        "umulh x9, %[a2], %[b1]\n" \
        "adds x11, x11, x8\n" \
        "adcs x12, x12, x9\n" \
        "adc x13, x13, xzr\n" \
        \
        "mul x8, %[a3], %[b0]\n" \
        "umulh x9, %[a3], %[b0]\n" \
        "adds x11, x11, x8\n" \
        "adcs x12, x12, x9\n" \
        "adc x13, x13, xzr\n" \
        "str x11, [%[pl], #24]\n" \
        \
        /* Column 4: a1*b3 + a2*b2 + a3*b1 */ \
        "mul x8, %[a1], %[b3]\n" \
        "umulh x9, %[a1], %[b3]\n" \
        "adds x12, x12, x8\n" \
        "adcs x13, x13, x9\n" \
        "adc x14, xzr, xzr\n" \
        \
        "mul x8, %[a2], %[b2]\n" \
        "umulh x9, %[a2], %[b2]\n" \
        "adds x12, x12, x8\n" \
        "adcs x13, x13, x9\n" \
        "adc x14, x14, xzr\n" \
        \
        "mul x8, %[a3], %[b1]\n" \
        "umulh x9, %[a3], %[b1]\n" \
        "adds x12, x12, x8\n" \
        "adcs x13, x13, x9\n" \
        "adc x14, x14, xzr\n" \
        "str x12, [%[pl], #32]\n" \
        \
        /* Column 5: a2*b3 + a3*b2 */ \
        "mul x8, %[a2], %[b3]\n" \
        "umulh x9, %[a2], %[b3]\n" \
        "adds x13, x13, x8\n" \
        "adcs x14, x14, x9\n" \
        "adc x15, xzr, xzr\n" \
        \
        "mul x8, %[a3], %[b2]\n" \
        "umulh x9, %[a3], %[b2]\n" \
        "adds x13, x13, x8\n" \
        "adcs x14, x14, x9\n" \
        "adc x15, x15, xzr\n" \
        "str x13, [%[pl], #40]\n" \
        \
        /* Column 6: a3*b3 */ \
        "mul x8, %[a3], %[b3]\n" \
        "umulh x9, %[a3], %[b3]\n" \
        "adds x14, x14, x8\n" \
        "adc x15, x15, x9\n" \
        "str x14, [%[pl], #48]\n" \
        "str x15, [%[pl], #56]\n" \
        \
        : /* outputs */ \
        : [pl] "r" (l), \
          [a0] "r" (a0), [a1] "r" (a1), [a2] "r" (a2), [a3] "r" (a3), \
          [b0] "r" (b0), [b1] "r" (b1), [b2] "r" (b2), [b3] "r" (b3) \
        : "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "cc", "memory" \
    ); \
} while(0)

// ============================================================================
// 512-bit reduction mod n for ARM64
// ============================================================================

#define SCALAR_REDUCE_512_IMPL(r, l) do { \
    uint64_t r0 = (l)[0], r1 = (l)[1], r2 = (l)[2], r3 = (l)[3]; \
    uint64_t h0 = (l)[4], h1 = (l)[5], h2 = (l)[6], h3 = (l)[7]; \
    \
    if (h0 | h1 | h2 | h3) { \
        /* Simplified reduction - multiply h by (2^256 mod n) and add */ \
        __asm__ __volatile__( \
            "mul x8, %[h0], %[c0]\n" \
            "umulh x9, %[h0], %[c0]\n" \
            "adds %[r0], %[r0], x8\n" \
            "adcs %[r1], %[r1], x9\n" \
            "adcs %[r2], %[r2], xzr\n" \
            "adc %[r3], %[r3], xzr\n" \
            : [r0] "+r" (r0), [r1] "+r" (r1), [r2] "+r" (r2), [r3] "+r" (r3) \
            : [h0] "r" (h0), [c0] "r" (SCALAR_2P256_MOD_N[0]) \
            : "x8", "x9", "cc" \
        ); \
    } \
    \
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
        __asm__ __volatile__( \
            "subs %[r0], %[r0], %[n0]\n" \
            "sbcs %[r1], %[r1], %[n1]\n" \
            "sbcs %[r2], %[r2], %[n2]\n" \
            "sbc %[r3], %[r3], %[n3]\n" \
            : [r0] "+r" (r0), [r1] "+r" (r1), [r2] "+r" (r2), [r3] "+r" (r3) \
            : [n0] "r" (SCALAR_N[0]), [n1] "r" (SCALAR_N[1]), \
              [n2] "r" (SCALAR_N[2]), [n3] "r" (SCALAR_N[3]) \
            : "cc" \
        ); \
    } \
    \
    (r)[0] = r0; (r)[1] = r1; (r)[2] = r2; (r)[3] = r3; \
} while(0)

#else
#error "ARM64 NEON requires aarch64 architecture"
#endif
