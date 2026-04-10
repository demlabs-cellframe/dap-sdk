// ============================================================================
// Generic (Portable C) Primitives for secp256k1 Field Arithmetic
// Uses bitcoin-core style interleaved multiplication and reduction
// Uses dap_math_ops.h for cross-platform uint128_t support
// ============================================================================

#include "dap_math_ops.h"

// Helper macros for accessing uint128_t parts
#ifdef DAP_GLOBAL_IS_INT128
    #define LO64(x) ((uint64_t)(x))
    #define HI64(x) ((uint64_t)((x) >> 64))
    #define U128_MUL(a, b) ((__uint128_t)(a) * (b))
    #define U128_ACCUM_MUL(acc, a, b) ((acc) += (__uint128_t)(a) * (b))
    #define U128_ACCUM(acc, v) ((acc) += (v))
    #define U128_RSHIFT(x, n) ((x) >>= (n))
#else
    #define LO64(x) ((x).lo)
    #define HI64(x) ((x).hi)
    static inline uint128_t u128_mul(uint64_t a, uint64_t b) {
        uint128_t r;
        MULT_64_128(a, b, &r);
        return r;
    }
    static inline void u128_accum_mul(uint128_t *acc, uint64_t a, uint64_t b) {
        uint128_t t;
        MULT_64_128(a, b, &t);
        uint64_t new_lo = acc->lo + t.lo;
        acc->hi += t.hi + (new_lo < acc->lo);
        acc->lo = new_lo;
    }
    static inline void u128_accum(uint128_t *acc, uint64_t v) {
        uint64_t new_lo = acc->lo + v;
        acc->hi += (new_lo < acc->lo);
        acc->lo = new_lo;
    }
    static inline void u128_rshift(uint128_t *x, int n) {
        if (n >= 64) {
            x->lo = x->hi >> (n - 64);
            x->hi = 0;
        } else if (n > 0) {
            x->lo = (x->lo >> n) | (x->hi << (64 - n));
            x->hi >>= n;
        }
    }
    #define U128_MUL(a, b) u128_mul(a, b)
    #define U128_ACCUM_MUL(acc, a, b) u128_accum_mul(&(acc), a, b)
    #define U128_ACCUM(acc, v) u128_accum(&(acc), v)
    #define U128_RSHIFT(x, n) u128_rshift(&(x), n)
#endif

// secp256k1 field constants
// p = 2^256 - 2^32 - 977
// R = 2^256 mod p = 2^32 + 977 = 0x1000003D1
// For 5x52-bit representation, we use R with shifts
#define FIELD_M52 0xFFFFFFFFFFFFFULL   // 52-bit mask
#define FIELD_M48 0xFFFFFFFFFFFFULL    // 48-bit mask (for top limb)
#define FIELD_R   0x1000003D10ULL      // 2^256 mod p, shifted by 4 bits = (2^32 + 977) * 16

// ============================================================================
// Interleaved Field Multiplication: a * b mod p
// Based on bitcoin-core secp256k1 field_5x52_int128_impl.h
//
// This uses a clever interleaved strategy where high-order products are
// reduced into low-order limbs as they're computed, avoiding the need
// for intermediate 512-bit storage.
// ============================================================================

#ifdef DAP_GLOBAL_IS_INT128

#define FIELD_MUL_IMPL(r, a, b) do { \
    __uint128_t c, d; \
    uint64_t t3, t4, tx, u0; \
    const uint64_t M = FIELD_M52; \
    const uint64_t R = 0x1000003D10ULL; \
    \
    uint64_t a0 = (a)[0], a1 = (a)[1], a2 = (a)[2], a3 = (a)[3], a4 = (a)[4]; \
    uint64_t b0 = (b)[0], b1 = (b)[1], b2 = (b)[2], b3 = (b)[3], b4 = (b)[4]; \
    \
    /* Compute p3 (column 3: a0*b3 + a1*b2 + a2*b1 + a3*b0) */ \
    d = (__uint128_t)a0 * b3; \
    d += (__uint128_t)a1 * b2; \
    d += (__uint128_t)a2 * b1; \
    d += (__uint128_t)a3 * b0; \
    \
    /* Compute p8 (a4*b4), reduce via R */ \
    c = (__uint128_t)a4 * b4; \
    d += (__uint128_t)((uint64_t)c) * R; c >>= 64; \
    t3 = (uint64_t)d & M; d >>= 52; \
    \
    /* p4: a0*b4 + a1*b3 + a2*b2 + a3*b1 + a4*b0 */ \
    d += (__uint128_t)a0 * b4; \
    d += (__uint128_t)a1 * b3; \
    d += (__uint128_t)a2 * b2; \
    d += (__uint128_t)a3 * b1; \
    d += (__uint128_t)a4 * b0; \
    d += (__uint128_t)((uint64_t)c) * (R << 12); \
    t4 = (uint64_t)d & M; d >>= 52; \
    tx = (t4 >> 48); t4 &= (M >> 4); \
    \
    /* p0: a0*b0 */ \
    c = (__uint128_t)a0 * b0; \
    \
    /* p5: a1*b4 + a2*b3 + a3*b2 + a4*b1 */ \
    d += (__uint128_t)a1 * b4; \
    d += (__uint128_t)a2 * b3; \
    d += (__uint128_t)a3 * b2; \
    d += (__uint128_t)a4 * b1; \
    u0 = (uint64_t)d & M; d >>= 52; \
    u0 = (u0 << 4) | tx; \
    c += (__uint128_t)u0 * (R >> 4); \
    (r)[0] = (uint64_t)c & M; c >>= 52; \
    \
    /* p1: a0*b1 + a1*b0 */ \
    c += (__uint128_t)a0 * b1; \
    c += (__uint128_t)a1 * b0; \
    /* p6: a2*b4 + a3*b3 + a4*b2 */ \
    d += (__uint128_t)a2 * b4; \
    d += (__uint128_t)a3 * b3; \
    d += (__uint128_t)a4 * b2; \
    c += (__uint128_t)((uint64_t)d & M) * R; d >>= 52; \
    (r)[1] = (uint64_t)c & M; c >>= 52; \
    \
    /* p2: a0*b2 + a1*b1 + a2*b0 */ \
    c += (__uint128_t)a0 * b2; \
    c += (__uint128_t)a1 * b1; \
    c += (__uint128_t)a2 * b0; \
    /* p7: a3*b4 + a4*b3 */ \
    d += (__uint128_t)a3 * b4; \
    d += (__uint128_t)a4 * b3; \
    c += (__uint128_t)((uint64_t)d) * R; d >>= 64; \
    (r)[2] = (uint64_t)c & M; c >>= 52; \
    \
    /* Finalize r[3], r[4] */ \
    c += (__uint128_t)((uint64_t)d) * (R << 12); \
    c += t3; \
    (r)[3] = (uint64_t)c & M; c >>= 52; \
    (r)[4] = (uint64_t)c + t4; \
} while(0)

#else

// Non-native uint128_t version (slower but portable)
#define FIELD_MUL_IMPL(r, a, b) do { \
    uint128_t c, d, t; \
    uint64_t t3, t4, tx, u0; \
    const uint64_t M = FIELD_M52; \
    const uint64_t R = 0x1000003D10ULL; \
    \
    uint64_t a0 = (a)[0], a1 = (a)[1], a2 = (a)[2], a3 = (a)[3], a4 = (a)[4]; \
    uint64_t b0 = (b)[0], b1 = (b)[1], b2 = (b)[2], b3 = (b)[3], b4 = (b)[4]; \
    \
    /* Compute p3 */ \
    d = U128_MUL(a0, b3); \
    U128_ACCUM_MUL(d, a1, b2); \
    U128_ACCUM_MUL(d, a2, b1); \
    U128_ACCUM_MUL(d, a3, b0); \
    \
    /* Compute p8, reduce via R */ \
    c = U128_MUL(a4, b4); \
    U128_ACCUM_MUL(d, LO64(c), R); U128_RSHIFT(c, 64); \
    t3 = LO64(d) & M; U128_RSHIFT(d, 52); \
    \
    /* p4 */ \
    U128_ACCUM_MUL(d, a0, b4); \
    U128_ACCUM_MUL(d, a1, b3); \
    U128_ACCUM_MUL(d, a2, b2); \
    U128_ACCUM_MUL(d, a3, b1); \
    U128_ACCUM_MUL(d, a4, b0); \
    U128_ACCUM_MUL(d, LO64(c), R << 12); \
    t4 = LO64(d) & M; U128_RSHIFT(d, 52); \
    tx = (t4 >> 48); t4 &= (M >> 4); \
    \
    /* p0 */ \
    c = U128_MUL(a0, b0); \
    \
    /* p5 */ \
    U128_ACCUM_MUL(d, a1, b4); \
    U128_ACCUM_MUL(d, a2, b3); \
    U128_ACCUM_MUL(d, a3, b2); \
    U128_ACCUM_MUL(d, a4, b1); \
    u0 = LO64(d) & M; U128_RSHIFT(d, 52); \
    u0 = (u0 << 4) | tx; \
    U128_ACCUM_MUL(c, u0, R >> 4); \
    (r)[0] = LO64(c) & M; U128_RSHIFT(c, 52); \
    \
    /* p1 */ \
    U128_ACCUM_MUL(c, a0, b1); \
    U128_ACCUM_MUL(c, a1, b0); \
    /* p6 */ \
    U128_ACCUM_MUL(d, a2, b4); \
    U128_ACCUM_MUL(d, a3, b3); \
    U128_ACCUM_MUL(d, a4, b2); \
    U128_ACCUM_MUL(c, LO64(d) & M, R); U128_RSHIFT(d, 52); \
    (r)[1] = LO64(c) & M; U128_RSHIFT(c, 52); \
    \
    /* p2 */ \
    U128_ACCUM_MUL(c, a0, b2); \
    U128_ACCUM_MUL(c, a1, b1); \
    U128_ACCUM_MUL(c, a2, b0); \
    /* p7 */ \
    U128_ACCUM_MUL(d, a3, b4); \
    U128_ACCUM_MUL(d, a4, b3); \
    U128_ACCUM_MUL(c, LO64(d), R); U128_RSHIFT(d, 64); \
    (r)[2] = LO64(c) & M; U128_RSHIFT(c, 52); \
    \
    /* Finalize */ \
    U128_ACCUM_MUL(c, LO64(d), R << 12); \
    U128_ACCUM(c, t3); \
    (r)[3] = LO64(c) & M; U128_RSHIFT(c, 52); \
    (r)[4] = LO64(c) + t4; \
} while(0)

#endif

// ============================================================================
// Interleaved Field Squaring: a^2 mod p
// Optimized for squaring - uses the symmetry: a[i]*a[j] appears twice for i!=j
// ============================================================================

#ifdef DAP_GLOBAL_IS_INT128

#define FIELD_SQR_IMPL(r, a) do { \
    __uint128_t c, d; \
    uint64_t t3, t4, tx, u0; \
    const uint64_t M = FIELD_M52; \
    const uint64_t R = 0x1000003D10ULL; \
    \
    uint64_t a0 = (a)[0], a1 = (a)[1], a2 = (a)[2], a3 = (a)[3], a4 = (a)[4]; \
    \
    /* p3 = 2*(a0*a3 + a1*a2) (using doubling for off-diagonal) */ \
    d = (__uint128_t)a0 * a3; \
    d += (__uint128_t)a1 * a2; \
    d *= 2; \
    \
    /* p8 = a4^2, reduce */ \
    c = (__uint128_t)a4 * a4; \
    d += (__uint128_t)((uint64_t)c) * R; c >>= 64; \
    t3 = (uint64_t)d & M; d >>= 52; \
    \
    /* p4 = 2*(a0*a4 + a1*a3) + a2^2 */ \
    d += (__uint128_t)a0 * a4 * 2; \
    d += (__uint128_t)a1 * a3 * 2; \
    d += (__uint128_t)a2 * a2; \
    d += (__uint128_t)((uint64_t)c) * (R << 12); \
    t4 = (uint64_t)d & M; d >>= 52; \
    tx = (t4 >> 48); t4 &= (M >> 4); \
    \
    /* p0 = a0^2 */ \
    c = (__uint128_t)a0 * a0; \
    \
    /* p5 = 2*(a1*a4 + a2*a3) */ \
    d += (__uint128_t)a1 * a4 * 2; \
    d += (__uint128_t)a2 * a3 * 2; \
    u0 = (uint64_t)d & M; d >>= 52; \
    u0 = (u0 << 4) | tx; \
    c += (__uint128_t)u0 * (R >> 4); \
    (r)[0] = (uint64_t)c & M; c >>= 52; \
    \
    /* p1 = 2*a0*a1 */ \
    c += (__uint128_t)a0 * a1 * 2; \
    /* p6 = 2*a2*a4 + a3^2 */ \
    d += (__uint128_t)a2 * a4 * 2; \
    d += (__uint128_t)a3 * a3; \
    c += (__uint128_t)((uint64_t)d & M) * R; d >>= 52; \
    (r)[1] = (uint64_t)c & M; c >>= 52; \
    \
    /* p2 = 2*a0*a2 + a1^2 */ \
    c += (__uint128_t)a0 * a2 * 2; \
    c += (__uint128_t)a1 * a1; \
    /* p7 = 2*a3*a4 */ \
    d += (__uint128_t)a3 * a4 * 2; \
    c += (__uint128_t)((uint64_t)d) * R; d >>= 64; \
    (r)[2] = (uint64_t)c & M; c >>= 52; \
    \
    /* Finalize */ \
    c += (__uint128_t)((uint64_t)d) * (R << 12); \
    c += t3; \
    (r)[3] = (uint64_t)c & M; c >>= 52; \
    (r)[4] = (uint64_t)c + t4; \
} while(0)

#else

// Non-native uint128_t squaring
#define FIELD_SQR_IMPL(r, a) do { \
    uint128_t c, d; \
    uint64_t t3, t4, tx, u0; \
    const uint64_t M = FIELD_M52; \
    const uint64_t R = 0x1000003D10ULL; \
    \
    uint64_t a0 = (a)[0], a1 = (a)[1], a2 = (a)[2], a3 = (a)[3], a4 = (a)[4]; \
    \
    /* p3 = 2*(a0*a3 + a1*a2) */ \
    d = U128_MUL(a0, a3); \
    U128_ACCUM_MUL(d, a1, a2); \
    d.lo <<= 1; d.hi = (d.hi << 1) | (d.lo >> 63); d.lo &= ~(1ULL << 63); \
    /* Note: simplified doubling, may need refinement */ \
    \
    /* p8 = a4^2, reduce */ \
    c = U128_MUL(a4, a4); \
    U128_ACCUM_MUL(d, LO64(c), R); U128_RSHIFT(c, 64); \
    t3 = LO64(d) & M; U128_RSHIFT(d, 52); \
    \
    /* p4 = 2*(a0*a4 + a1*a3) + a2^2 */ \
    U128_ACCUM_MUL(d, a0 * 2, a4); \
    U128_ACCUM_MUL(d, a1 * 2, a3); \
    U128_ACCUM_MUL(d, a2, a2); \
    U128_ACCUM_MUL(d, LO64(c), R << 12); \
    t4 = LO64(d) & M; U128_RSHIFT(d, 52); \
    tx = (t4 >> 48); t4 &= (M >> 4); \
    \
    /* p0 = a0^2 */ \
    c = U128_MUL(a0, a0); \
    \
    /* p5 = 2*(a1*a4 + a2*a3) */ \
    U128_ACCUM_MUL(d, a1 * 2, a4); \
    U128_ACCUM_MUL(d, a2 * 2, a3); \
    u0 = LO64(d) & M; U128_RSHIFT(d, 52); \
    u0 = (u0 << 4) | tx; \
    U128_ACCUM_MUL(c, u0, R >> 4); \
    (r)[0] = LO64(c) & M; U128_RSHIFT(c, 52); \
    \
    /* p1 = 2*a0*a1 */ \
    U128_ACCUM_MUL(c, a0 * 2, a1); \
    /* p6 = 2*a2*a4 + a3^2 */ \
    U128_ACCUM_MUL(d, a2 * 2, a4); \
    U128_ACCUM_MUL(d, a3, a3); \
    U128_ACCUM_MUL(c, LO64(d) & M, R); U128_RSHIFT(d, 52); \
    (r)[1] = LO64(c) & M; U128_RSHIFT(c, 52); \
    \
    /* p2 = 2*a0*a2 + a1^2 */ \
    U128_ACCUM_MUL(c, a0 * 2, a2); \
    U128_ACCUM_MUL(c, a1, a1); \
    /* p7 = 2*a3*a4 */ \
    U128_ACCUM_MUL(d, a3 * 2, a4); \
    U128_ACCUM_MUL(c, LO64(d), R); U128_RSHIFT(d, 64); \
    (r)[2] = LO64(c) & M; U128_RSHIFT(c, 52); \
    \
    /* Finalize */ \
    U128_ACCUM_MUL(c, LO64(d), R << 12); \
    U128_ACCUM(c, t3); \
    (r)[3] = LO64(c) & M; U128_RSHIFT(c, 52); \
    (r)[4] = LO64(c) + t4; \
} while(0)

#endif
