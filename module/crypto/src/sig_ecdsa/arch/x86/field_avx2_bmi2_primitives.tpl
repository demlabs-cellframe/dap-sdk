// ============================================================================
// AVX2 + BMI2 Primitives for secp256k1 Field Arithmetic
// Uses MULX for fast multiplication, __uint128_t for accumulation
// ============================================================================

#include <immintrin.h>
#include <x86intrin.h>

// secp256k1 field constants
#define FIELD_M52 0xFFFFFFFFFFFFFULL
#define FIELD_R   0x1000003D10ULL

// ============================================================================
// BMI2 MULX: a * b -> hi:lo (faster than regular MUL on modern CPUs)
// ============================================================================

#if defined(__BMI2__)
#define MULX64(hi, lo, a, b) do { \
    uint64_t _a = (a), _b = (b); \
    __asm__ __volatile__ ( \
        "mulx %3, %0, %1" \
        : "=r"(lo), "=r"(hi) \
        : "d"(_a), "rm"(_b) \
    ); \
} while(0)
#else
#define MULX64(hi, lo, a, b) do { \
    __uint128_t _p = (__uint128_t)(a) * (b); \
    (lo) = (uint64_t)_p; \
    (hi) = (uint64_t)(_p >> 64); \
} while(0)
#endif

// ============================================================================
// Helper: accumulate product into 128-bit accumulator
// ============================================================================
static inline void accum_mul(__uint128_t *acc, uint64_t a, uint64_t b) {
    uint64_t lo, hi;
    MULX64(hi, lo, a, b);
    *acc += ((__uint128_t)hi << 64) | lo;
}

// ============================================================================
// AVX2+BMI2 Field Multiplication with Interleaved Reduction
// Uses MULX for multiplication, __uint128_t for correct accumulation
// ============================================================================

#define FIELD_MUL_IMPL(r, a, b) do { \
    uint64_t a0 = (a)[0], a1 = (a)[1], a2 = (a)[2], a3 = (a)[3], a4 = (a)[4]; \
    uint64_t b0 = (b)[0], b1 = (b)[1], b2 = (b)[2], b3 = (b)[3], b4 = (b)[4]; \
    const uint64_t M = FIELD_M52, R = FIELD_R; \
    __uint128_t c, d; \
    uint64_t t3, t4, tx, u0; \
    \
    /* [d 0 0 0] = [p3 0 0 0] */ \
    d = (__uint128_t)a0 * b3; \
    d += (__uint128_t)a1 * b2; \
    d += (__uint128_t)a2 * b1; \
    d += (__uint128_t)a3 * b0; \
    \
    /* [c 0 0 0 0 d 0 0 0] = [p8 0 0 0 0 p3 0 0 0] */ \
    c = (__uint128_t)a4 * b4; \
    \
    /* Reduce p8: d += (p8 mod 2^64) * R */ \
    d += (__uint128_t)((uint64_t)c) * R; c >>= 64; \
    /* [(c<<12) 0 0 0 0 0 d 0 0 0] */ \
    t3 = (uint64_t)d & M; d >>= 52; \
    /* [(c<<12) 0 0 0 0 d t3 0 0 0] */ \
    \
    /* Compute p4 = a0*b4 + a1*b3 + a2*b2 + a3*b1 + a4*b0 */ \
    d += (__uint128_t)a0 * b4; \
    d += (__uint128_t)a1 * b3; \
    d += (__uint128_t)a2 * b2; \
    d += (__uint128_t)a3 * b1; \
    d += (__uint128_t)a4 * b0; \
    /* Add remaining high bits: (c<<12) * R */ \
    d += (__uint128_t)((uint64_t)c) * (R << 12); \
    /* [d t3 0 0 0] */ \
    t4 = (uint64_t)d & M; d >>= 52; \
    /* [d t4 t3 0 0 0] */ \
    tx = t4 >> 48; t4 &= (M >> 4); \
    /* [d t4+(tx<<48) t3 0 0 0] */ \
    \
    /* [d t4+(tx<<48) t3 0 0 c] = start computing p0 */ \
    c = (__uint128_t)a0 * b0; \
    \
    /* Compute p5 = a1*b4 + a2*b3 + a3*b2 + a4*b1 */ \
    d += (__uint128_t)a1 * b4; \
    d += (__uint128_t)a2 * b3; \
    d += (__uint128_t)a3 * b2; \
    d += (__uint128_t)a4 * b1; \
    /* [d t4+(tx<<48) t3 0 0 c] */ \
    u0 = (uint64_t)d & M; d >>= 52; \
    /* [d u0 t4+(tx<<48) t3 0 0 c] */ \
    u0 = (u0 << 4) | tx; \
    /* [d 0 t4+(u0<<48) t3 0 0 c] */ \
    /* Reduce: c += u0 * (R >> 4) */ \
    c += (__uint128_t)u0 * (R >> 4); \
    /* [d 0 t4 t3 0 0 c] */ \
    (r)[0] = (uint64_t)c & M; c >>= 52; \
    /* [d 0 t4 t3 0 c r0] */ \
    \
    /* Compute p1 = a0*b1 + a1*b0 */ \
    c += (__uint128_t)a0 * b1; \
    c += (__uint128_t)a1 * b0; \
    /* [d 0 t4 t3 0 c r0] = [p8 0 0 p5 p4 p3 0 p1 p0] */ \
    \
    /* Compute p6 = a2*b4 + a3*b3 + a4*b2 */ \
    d += (__uint128_t)a2 * b4; \
    d += (__uint128_t)a3 * b3; \
    d += (__uint128_t)a4 * b2; \
    /* [d 0 t4 t3 0 c r0] = [p8 0 p6 p5 p4 p3 0 p1 p0] */ \
    /* Reduce p6: c += (d mod M) * R */ \
    c += ((uint64_t)d & M) * R; d >>= 52; \
    /* [d 0 0 t4 t3 0 c r0] */ \
    (r)[1] = (uint64_t)c & M; c >>= 52; \
    /* [d 0 0 t4 t3 c r1 r0] */ \
    \
    /* Compute p2 = a0*b2 + a1*b1 + a2*b0 */ \
    c += (__uint128_t)a0 * b2; \
    c += (__uint128_t)a1 * b1; \
    c += (__uint128_t)a2 * b0; \
    /* [d 0 0 t4 t3 c r1 r0] = [p8 0 p6 p5 p4 p3 p2 p1 p0] */ \
    \
    /* Compute p7 = a3*b4 + a4*b3 */ \
    d += (__uint128_t)a3 * b4; \
    d += (__uint128_t)a4 * b3; \
    /* [d 0 0 t4 t3 c r1 r0] = [p8 p7 p6 p5 p4 p3 p2 p1 p0] */ \
    /* Reduce p7: c += (d mod 2^64) * R */ \
    c += (__uint128_t)((uint64_t)d) * R; d >>= 64; \
    /* [(d<<12) 0 0 0 t4 t3 c r1 r0] */ \
    (r)[2] = (uint64_t)c & M; c >>= 52; \
    /* [(d<<12) 0 0 0 t4 t3+c r2 r1 r0] */ \
    \
    /* Final: add remaining high bits and t3 */ \
    c += (__uint128_t)((uint64_t)d) * (R << 12); \
    c += t3; \
    /* [t4 c r2 r1 r0] */ \
    (r)[3] = (uint64_t)c & M; c >>= 52; \
    /* [t4+c r3 r2 r1 r0] */ \
    (r)[4] = (uint64_t)c + t4; \
    /* [r4 r3 r2 r1 r0] */ \
} while(0)

// ============================================================================
// AVX2+BMI2 Field Squaring with Interleaved Reduction
// ============================================================================

#define FIELD_SQR_IMPL(r, a) do { \
    uint64_t a0 = (a)[0], a1 = (a)[1], a2 = (a)[2], a3 = (a)[3], a4 = (a)[4]; \
    const uint64_t M = FIELD_M52, R = FIELD_R; \
    __uint128_t c, d; \
    uint64_t t3, t4, tx, u0; \
    \
    /* p3 = 2*a0*a3 + 2*a1*a2 */ \
    d = (__uint128_t)(a0*2) * a3; \
    d += (__uint128_t)(a1*2) * a2; \
    /* [d 0 0 0] = [p3 0 0 0] */ \
    \
    /* p8 = a4*a4 */ \
    c = (__uint128_t)a4 * a4; \
    /* [c 0 0 0 0 d 0 0 0] = [p8 0 0 0 0 p3 0 0 0] */ \
    \
    /* Reduce p8 */ \
    d += (__uint128_t)((uint64_t)c) * R; c >>= 64; \
    /* [(c<<12) 0 0 0 0 0 d 0 0 0] */ \
    t3 = (uint64_t)d & M; d >>= 52; \
    /* [(c<<12) 0 0 0 0 d t3 0 0 0] */ \
    \
    /* p4 = 2*a0*a4 + 2*a1*a3 + a2*a2 */ \
    a4 *= 2; \
    d += (__uint128_t)a0 * a4; \
    d += (__uint128_t)(a1*2) * a3; \
    d += (__uint128_t)a2 * a2; \
    /* [(c<<12) 0 0 0 0 d t3 0 0 0] = [p8 0 0 0 p4 p3 0 0 0] */ \
    d += (__uint128_t)((uint64_t)c) * (R << 12); \
    /* [d t3 0 0 0] = [p8 0 0 0 p4 p3 0 0 0] */ \
    t4 = (uint64_t)d & M; d >>= 52; \
    /* [d t4 t3 0 0 0] */ \
    tx = t4 >> 48; t4 &= (M >> 4); \
    /* [d t4+(tx<<48) t3 0 0 0] */ \
    \
    /* p0 = a0*a0 */ \
    c = (__uint128_t)a0 * a0; \
    /* [d t4+(tx<<48) t3 0 0 c] = [p8 0 0 0 p4 p3 0 0 p0] */ \
    \
    /* p5 = 2*a1*a4 + 2*a2*a3 */ \
    d += (__uint128_t)a1 * a4; \
    d += (__uint128_t)(a2*2) * a3; \
    /* [d t4+(tx<<48) t3 0 0 c] = [p8 0 0 p5 p4 p3 0 0 p0] */ \
    u0 = (uint64_t)d & M; d >>= 52; \
    /* [d u0 t4+(tx<<48) t3 0 0 c] */ \
    u0 = (u0 << 4) | tx; \
    /* [d 0 t4+(u0<<48) t3 0 0 c] */ \
    c += (__uint128_t)u0 * (R >> 4); \
    /* [d 0 t4 t3 0 0 c] */ \
    (r)[0] = (uint64_t)c & M; c >>= 52; \
    /* [d 0 t4 t3 0 c r0] */ \
    \
    /* p1 = 2*a0*a1 */ \
    a0 *= 2; \
    c += (__uint128_t)a0 * a1; \
    /* [d 0 t4 t3 0 c r0] = [p8 0 0 p5 p4 p3 0 p1 p0] */ \
    \
    /* p6 = 2*a2*a4 + a3*a3 */ \
    d += (__uint128_t)a2 * a4; \
    d += (__uint128_t)a3 * a3; \
    /* [d 0 t4 t3 0 c r0] = [p8 0 p6 p5 p4 p3 0 p1 p0] */ \
    c += ((uint64_t)d & M) * R; d >>= 52; \
    /* [d 0 0 t4 t3 0 c r0] */ \
    (r)[1] = (uint64_t)c & M; c >>= 52; \
    /* [d 0 0 t4 t3 c r1 r0] */ \
    \
    /* p2 = 2*a0*a2 + a1*a1 */ \
    c += (__uint128_t)a0 * a2; \
    c += (__uint128_t)a1 * a1; \
    /* [d 0 0 t4 t3 c r1 r0] = [p8 0 p6 p5 p4 p3 p2 p1 p0] */ \
    \
    /* p7 = 2*a3*a4 */ \
    d += (__uint128_t)a3 * a4; \
    /* [d 0 0 t4 t3 c r1 r0] = [p8 p7 p6 p5 p4 p3 p2 p1 p0] */ \
    c += (__uint128_t)((uint64_t)d) * R; d >>= 64; \
    /* [(d<<12) 0 0 0 t4 t3 c r1 r0] */ \
    (r)[2] = (uint64_t)c & M; c >>= 52; \
    /* [(d<<12) 0 0 0 t4 t3+c r2 r1 r0] */ \
    \
    c += (__uint128_t)((uint64_t)d) * (R << 12); \
    c += t3; \
    /* [t4 c r2 r1 r0] */ \
    (r)[3] = (uint64_t)c & M; c >>= 52; \
    /* [t4+c r3 r2 r1 r0] */ \
    (r)[4] = (uint64_t)c + t4; \
    /* [r4 r3 r2 r1 r0] */ \
} while(0)
