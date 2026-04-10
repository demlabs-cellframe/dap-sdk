// ============================================================================
// ARM SVE Primitives for secp256k1 Scalar Arithmetic
// For SERVER ARM64 only: AWS Graviton3, Fujitsu A64FX, AmpereOne
// SVE provides scalable vectors (128-2048 bits)
//
// NOTE: Apple Silicon (M1/M2/M3/M4) does NOT support SVE!
//       Apple uses NEON + proprietary AMX (undocumented).
//       For Apple Silicon, use neon_primitives.tpl instead.
// ============================================================================

#if defined(__ARM_FEATURE_SVE)

#include <arm_sve.h>

// ============================================================================
// SVE Scalar Multiplication: 256x256 -> 512 bit
// Uses MUL/UMULH instructions with SVE predicates
// Note: For 4x64-bit scalars, standard NEON/scalar path often faster
// ============================================================================

// Helper: 64x64 -> 128 bit multiply using standard ARM64 instructions
#define MUL64_128(hi, lo, a, b) do { \
    uint64_t _a = (a), _b = (b); \
    __asm__ __volatile__ ( \
        "mul   %0, %2, %3\n\t" \
        "umulh %1, %2, %3" \
        : "=&r"(lo), "=&r"(hi) \
        : "r"(_a), "r"(_b) \
    ); \
} while(0)

// Helper: multiply-add to 128-bit accumulator
#define MULADD64_128(c1, c0, a, b) do { \
    uint64_t _lo, _hi; \
    uint64_t _a = (a), _b = (b); \
    __asm__ __volatile__ ( \
        "mul   %0, %4, %5\n\t" \
        "umulh %1, %4, %5\n\t" \
        "adds  %2, %2, %0\n\t" \
        "adc   %3, %3, %1" \
        : "=&r"(_lo), "=&r"(_hi), "+r"(c0), "+r"(c1) \
        : "r"(_a), "r"(_b) \
        : "cc" \
    ); \
} while(0)

#define SCALAR_MUL_512_IMPL(l, a, b) do { \
    uint64_t a0 = (a)[0], a1 = (a)[1], a2 = (a)[2], a3 = (a)[3]; \
    uint64_t b0 = (b)[0], b1 = (b)[1], b2 = (b)[2], b3 = (b)[3]; \
    uint64_t c0 = 0, c1 = 0; \
    uint64_t lo, hi; \
    \
    /* Column 0 */ \
    MUL64_128(c1, (l)[0], a0, b0); \
    c0 = c1; c1 = 0; \
    \
    /* Column 1 */ \
    MULADD64_128(c1, c0, a0, b1); \
    MULADD64_128(c1, c0, a1, b0); \
    (l)[1] = c0; c0 = c1; c1 = 0; \
    \
    /* Column 2 */ \
    MULADD64_128(c1, c0, a0, b2); \
    MULADD64_128(c1, c0, a1, b1); \
    MULADD64_128(c1, c0, a2, b0); \
    (l)[2] = c0; c0 = c1; c1 = 0; \
    \
    /* Column 3 */ \
    MULADD64_128(c1, c0, a0, b3); \
    MULADD64_128(c1, c0, a1, b2); \
    MULADD64_128(c1, c0, a2, b1); \
    MULADD64_128(c1, c0, a3, b0); \
    (l)[3] = c0; c0 = c1; c1 = 0; \
    \
    /* Column 4 */ \
    MULADD64_128(c1, c0, a1, b3); \
    MULADD64_128(c1, c0, a2, b2); \
    MULADD64_128(c1, c0, a3, b1); \
    (l)[4] = c0; c0 = c1; c1 = 0; \
    \
    /* Column 5 */ \
    MULADD64_128(c1, c0, a2, b3); \
    MULADD64_128(c1, c0, a3, b2); \
    (l)[5] = c0; c0 = c1; c1 = 0; \
    \
    /* Column 6,7 */ \
    MULADD64_128(c1, c0, a3, b3); \
    (l)[6] = c0; \
    (l)[7] = c1; \
} while(0)

// ============================================================================
// SVE 512-bit reduction mod n
// ============================================================================

#define SCALAR_REDUCE_512_IMPL(r, l) do { \
    uint64_t r0 = (l)[0], r1 = (l)[1], r2 = (l)[2], r3 = (l)[3]; \
    uint64_t h0 = (l)[4], h1 = (l)[5], h2 = (l)[6], h3 = (l)[7]; \
    \
    if (h0 | h1 | h2 | h3) { \
        uint64_t lo, hi, carry; \
        \
        /* h0 * SCALAR_2P256_MOD_N[0] */ \
        MUL64_128(hi, lo, h0, SCALAR_2P256_MOD_N[0]); \
        __asm__ __volatile__ ( \
            "adds %0, %0, %4\n\t" \
            "adcs %1, %1, %5\n\t" \
            "adcs %2, %2, xzr\n\t" \
            "adc  %3, %3, xzr" \
            : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3) \
            : "r"(lo), "r"(hi) \
            : "cc" \
        ); \
        \
        /* h0 * SCALAR_2P256_MOD_N[1] */ \
        MUL64_128(hi, lo, h0, SCALAR_2P256_MOD_N[1]); \
        __asm__ __volatile__ ( \
            "adds %0, %0, %3\n\t" \
            "adcs %1, %1, %4\n\t" \
            "adc  %2, %2, xzr" \
            : "+r"(r1), "+r"(r2), "+r"(r3) \
            : "r"(lo), "r"(hi) \
            : "cc" \
        ); \
    } \
    \
    /* Final reduction if r >= n */ \
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
        __asm__ __volatile__ ( \
            "subs %0, %0, %4\n\t" \
            "sbcs %1, %1, %5\n\t" \
            "sbcs %2, %2, %6\n\t" \
            "sbc  %3, %3, %7" \
            : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3) \
            : "r"(SCALAR_N[0]), "r"(SCALAR_N[1]), \
              "r"(SCALAR_N[2]), "r"(SCALAR_N[3]) \
            : "cc" \
        ); \
    } \
    \
    (r)[0] = r0; (r)[1] = r1; (r)[2] = r2; (r)[3] = r3; \
} while(0)

#else
#error "SVE primitives require ARM SVE support"
#endif
