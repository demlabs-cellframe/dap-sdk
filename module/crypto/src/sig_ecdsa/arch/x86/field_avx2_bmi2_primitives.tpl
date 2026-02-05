// ============================================================================
// AVX2 + BMI2 + ADX Primitives for secp256k1 Field Arithmetic
// Uses MULX/ADCX/ADOX for parallel carry chains
// ============================================================================

#include <immintrin.h>
#include <x86intrin.h>

// secp256k1 field constants
#define FIELD_M52 0xFFFFFFFFFFFFFULL
#define FIELD_M48 0xFFFFFFFFFFFFULL
#define FIELD_R   0x1000003D10ULL

// ============================================================================
// BMI2 MULX: RDX * src -> hi:lo (doesn't touch flags!)
// This allows interleaving with ADCX/ADOX which use separate carry flags
// ============================================================================

// MULX: multiply rdx * src, result in hi:lo
// Doesn't modify flags, allowing parallel carry chains
#define MULX64(hi, lo, a, b) do { \
    uint64_t _a = (a), _b = (b); \
    __asm__ __volatile__ ( \
        "mulx %3, %0, %1" \
        : "=r"(lo), "=r"(hi) \
        : "d"(_a), "rm"(_b) \
    ); \
} while(0)

// ADCX: add with carry using OF flag
#define ADCX64(dst, src) \
    __asm__ __volatile__ ("adcx %1, %0" : "+r"(dst) : "rm"(src) : "cc")

// ADOX: add with carry using CF flag  
#define ADOX64(dst, src) \
    __asm__ __volatile__ ("adox %1, %0" : "+r"(dst) : "rm"(src) : "cc")

// Clear carry flags for ADCX/ADOX chains
#define CLEAR_CF() __asm__ __volatile__ ("clc" ::: "cc")
#define CLEAR_OF() __asm__ __volatile__ ("test %%eax, %%eax" ::: "cc", "eax")

// ============================================================================
// Multiply-accumulate with MULX + ADCX/ADOX
// Uses two independent carry chains for better ILP
// ============================================================================

// MULADD_MULX: c1:c0 += a * b using MULX + ADCX
#define MULADD_MULX_ADCX(c1, c0, a, b) do { \
    uint64_t _lo, _hi; \
    MULX64(_hi, _lo, a, b); \
    ADCX64(c0, _lo); \
    ADCX64(c1, _hi); \
} while(0)

// MULADD_MULX using ADOX (alternate carry chain)
#define MULADD_MULX_ADOX(c1, c0, a, b) do { \
    uint64_t _lo, _hi; \
    MULX64(_hi, _lo, a, b); \
    ADOX64(c0, _lo); \
    ADOX64(c1, _hi); \
} while(0)

// ============================================================================
// AVX2+BMI2+ADX Field Multiplication with Interleaved Reduction
// Uses MULX for multiplication and ADCX/ADOX for parallel accumulation
// ============================================================================

#define FIELD_MUL_IMPL(r, a, b) do { \
    uint64_t a0 = (a)[0], a1 = (a)[1], a2 = (a)[2], a3 = (a)[3], a4 = (a)[4]; \
    uint64_t b0 = (b)[0], b1 = (b)[1], b2 = (b)[2], b3 = (b)[3], b4 = (b)[4]; \
    const uint64_t M = FIELD_M52; \
    const uint64_t R = 0x1000003D10ULL; \
    \
    uint64_t c0 = 0, c1 = 0; \
    uint64_t d0 = 0, d1 = 0; \
    uint64_t t3, t4, tx, u0; \
    uint64_t lo, hi; \
    \
    /* Compute d = p3 = a0*b3 + a1*b2 + a2*b1 + a3*b0 */ \
    CLEAR_CF(); \
    MULX64(hi, lo, a0, b3); d0 = lo; d1 = hi; \
    MULX64(hi, lo, a1, b2); ADCX64(d0, lo); ADCX64(d1, hi); \
    MULX64(hi, lo, a2, b1); ADCX64(d0, lo); ADCX64(d1, hi); \
    MULX64(hi, lo, a3, b0); ADCX64(d0, lo); ADCX64(d1, hi); \
    \
    /* c = p8 = a4*b4; d += lo(c) * R */ \
    MULX64(c1, c0, a4, b4); \
    MULX64(hi, lo, c0, R); \
    ADCX64(d0, lo); ADCX64(d1, hi); \
    c0 = c1; c1 = 0; \
    \
    /* t3 = d & M; d >>= 52 */ \
    t3 = d0 & M; \
    d0 = (d0 >> 52) | (d1 << 12); \
    d1 >>= 52; \
    \
    /* d += p4 */ \
    CLEAR_CF(); \
    MULX64(hi, lo, a0, b4); ADCX64(d0, lo); ADCX64(d1, hi); \
    MULX64(hi, lo, a1, b3); ADCX64(d0, lo); ADCX64(d1, hi); \
    MULX64(hi, lo, a2, b2); ADCX64(d0, lo); ADCX64(d1, hi); \
    MULX64(hi, lo, a3, b1); ADCX64(d0, lo); ADCX64(d1, hi); \
    MULX64(hi, lo, a4, b0); ADCX64(d0, lo); ADCX64(d1, hi); \
    MULX64(hi, lo, c0, R << 12); ADCX64(d0, lo); ADCX64(d1, hi); \
    \
    t4 = d0 & M; \
    d0 = (d0 >> 52) | (d1 << 12); \
    d1 >>= 52; \
    tx = t4 >> 48; \
    t4 &= (M >> 4); \
    \
    /* c = p0 = a0*b0 */ \
    MULX64(c1, c0, a0, b0); \
    \
    /* d += p5 */ \
    CLEAR_CF(); \
    MULX64(hi, lo, a1, b4); ADCX64(d0, lo); ADCX64(d1, hi); \
    MULX64(hi, lo, a2, b3); ADCX64(d0, lo); ADCX64(d1, hi); \
    MULX64(hi, lo, a3, b2); ADCX64(d0, lo); ADCX64(d1, hi); \
    MULX64(hi, lo, a4, b1); ADCX64(d0, lo); ADCX64(d1, hi); \
    \
    u0 = d0 & M; \
    d0 = (d0 >> 52) | (d1 << 12); \
    d1 >>= 52; \
    u0 = (u0 << 4) | tx; \
    \
    /* c += u0 * (R >> 4) */ \
    MULX64(hi, lo, u0, R >> 4); \
    CLEAR_CF(); \
    ADCX64(c0, lo); ADCX64(c1, hi); \
    \
    (r)[0] = c0 & M; \
    c0 = (c0 >> 52) | (c1 << 12); \
    c1 >>= 52; \
    \
    /* c += p1 */ \
    CLEAR_CF(); \
    MULX64(hi, lo, a0, b1); ADCX64(c0, lo); ADCX64(c1, hi); \
    MULX64(hi, lo, a1, b0); ADCX64(c0, lo); ADCX64(c1, hi); \
    \
    /* d += p6 */ \
    CLEAR_CF(); \
    MULX64(hi, lo, a2, b4); ADCX64(d0, lo); ADCX64(d1, hi); \
    MULX64(hi, lo, a3, b3); ADCX64(d0, lo); ADCX64(d1, hi); \
    MULX64(hi, lo, a4, b2); ADCX64(d0, lo); ADCX64(d1, hi); \
    \
    /* c += (d & M) * R */ \
    MULX64(hi, lo, d0 & M, R); \
    CLEAR_CF(); \
    ADCX64(c0, lo); ADCX64(c1, hi); \
    d0 = (d0 >> 52) | (d1 << 12); \
    d1 >>= 52; \
    \
    (r)[1] = c0 & M; \
    c0 = (c0 >> 52) | (c1 << 12); \
    c1 >>= 52; \
    \
    /* c += p2 */ \
    CLEAR_CF(); \
    MULX64(hi, lo, a0, b2); ADCX64(c0, lo); ADCX64(c1, hi); \
    MULX64(hi, lo, a1, b1); ADCX64(c0, lo); ADCX64(c1, hi); \
    MULX64(hi, lo, a2, b0); ADCX64(c0, lo); ADCX64(c1, hi); \
    \
    /* d += p7 */ \
    CLEAR_CF(); \
    MULX64(hi, lo, a3, b4); ADCX64(d0, lo); ADCX64(d1, hi); \
    MULX64(hi, lo, a4, b3); ADCX64(d0, lo); ADCX64(d1, hi); \
    \
    /* c += d0 * R */ \
    MULX64(hi, lo, d0, R); \
    CLEAR_CF(); \
    ADCX64(c0, lo); ADCX64(c1, hi); \
    d0 = d1; \
    \
    (r)[2] = c0 & M; \
    c0 = (c0 >> 52) | (c1 << 12); \
    c1 >>= 52; \
    \
    /* c += d0 * (R << 12) + t3 */ \
    MULX64(hi, lo, d0, R << 12); \
    CLEAR_CF(); \
    ADCX64(c0, lo); ADCX64(c1, hi); \
    ADCX64(c0, t3); \
    \
    (r)[3] = c0 & M; \
    c0 = (c0 >> 52) | (c1 << 12); \
    \
    (r)[4] = c0 + t4; \
} while(0)

// ============================================================================
// AVX2+BMI2+ADX Field Squaring
// ============================================================================

#define FIELD_SQR_IMPL(r, a) do { \
    uint64_t a0 = (a)[0], a1 = (a)[1], a2 = (a)[2], a3 = (a)[3], a4 = (a)[4]; \
    const uint64_t M = FIELD_M52; \
    const uint64_t R = 0x1000003D10ULL; \
    uint64_t a0_2 = a0 * 2, a1_2 = a1 * 2, a2_2 = a2 * 2, a3_2 = a3 * 2; \
    \
    uint64_t c0 = 0, c1 = 0; \
    uint64_t d0 = 0, d1 = 0; \
    uint64_t t3, t4, tx, u0; \
    uint64_t lo, hi; \
    \
    /* d = p3 = 2*(a0*a3 + a1*a2) */ \
    CLEAR_CF(); \
    MULX64(hi, lo, a0, a3); d0 = lo; d1 = hi; \
    MULX64(hi, lo, a1, a2); ADCX64(d0, lo); ADCX64(d1, hi); \
    /* d <<= 1 */ \
    d1 = (d1 << 1) | (d0 >> 63); \
    d0 <<= 1; \
    \
    /* c = p8 = a4^2; d += lo(c) * R */ \
    MULX64(c1, c0, a4, a4); \
    MULX64(hi, lo, c0, R); \
    CLEAR_CF(); \
    ADCX64(d0, lo); ADCX64(d1, hi); \
    c0 = c1; c1 = 0; \
    \
    t3 = d0 & M; \
    d0 = (d0 >> 52) | (d1 << 12); \
    d1 >>= 52; \
    \
    /* d += p4 = 2*(a0*a4 + a1*a3) + a2^2 */ \
    CLEAR_CF(); \
    MULX64(hi, lo, a0_2, a4); ADCX64(d0, lo); ADCX64(d1, hi); \
    MULX64(hi, lo, a1_2, a3); ADCX64(d0, lo); ADCX64(d1, hi); \
    MULX64(hi, lo, a2, a2); ADCX64(d0, lo); ADCX64(d1, hi); \
    MULX64(hi, lo, c0, R << 12); ADCX64(d0, lo); ADCX64(d1, hi); \
    \
    t4 = d0 & M; \
    d0 = (d0 >> 52) | (d1 << 12); \
    d1 >>= 52; \
    tx = t4 >> 48; \
    t4 &= (M >> 4); \
    \
    /* c = p0 = a0^2 */ \
    MULX64(c1, c0, a0, a0); \
    \
    /* d += p5 = 2*(a1*a4 + a2*a3) */ \
    CLEAR_CF(); \
    MULX64(hi, lo, a1_2, a4); ADCX64(d0, lo); ADCX64(d1, hi); \
    MULX64(hi, lo, a2_2, a3); ADCX64(d0, lo); ADCX64(d1, hi); \
    \
    u0 = d0 & M; \
    d0 = (d0 >> 52) | (d1 << 12); \
    d1 >>= 52; \
    u0 = (u0 << 4) | tx; \
    \
    MULX64(hi, lo, u0, R >> 4); \
    CLEAR_CF(); \
    ADCX64(c0, lo); ADCX64(c1, hi); \
    \
    (r)[0] = c0 & M; \
    c0 = (c0 >> 52) | (c1 << 12); \
    c1 >>= 52; \
    \
    /* c += p1 = 2*a0*a1 */ \
    CLEAR_CF(); \
    MULX64(hi, lo, a0_2, a1); ADCX64(c0, lo); ADCX64(c1, hi); \
    \
    /* d += p6 = 2*a2*a4 + a3^2 */ \
    CLEAR_CF(); \
    MULX64(hi, lo, a2_2, a4); ADCX64(d0, lo); ADCX64(d1, hi); \
    MULX64(hi, lo, a3, a3); ADCX64(d0, lo); ADCX64(d1, hi); \
    \
    MULX64(hi, lo, d0 & M, R); \
    CLEAR_CF(); \
    ADCX64(c0, lo); ADCX64(c1, hi); \
    d0 = (d0 >> 52) | (d1 << 12); \
    d1 >>= 52; \
    \
    (r)[1] = c0 & M; \
    c0 = (c0 >> 52) | (c1 << 12); \
    c1 >>= 52; \
    \
    /* c += p2 = 2*a0*a2 + a1^2 */ \
    CLEAR_CF(); \
    MULX64(hi, lo, a0_2, a2); ADCX64(c0, lo); ADCX64(c1, hi); \
    MULX64(hi, lo, a1, a1); ADCX64(c0, lo); ADCX64(c1, hi); \
    \
    /* d += p7 = 2*a3*a4 */ \
    CLEAR_CF(); \
    MULX64(hi, lo, a3_2, a4); ADCX64(d0, lo); ADCX64(d1, hi); \
    \
    MULX64(hi, lo, d0, R); \
    CLEAR_CF(); \
    ADCX64(c0, lo); ADCX64(c1, hi); \
    d0 = d1; \
    \
    (r)[2] = c0 & M; \
    c0 = (c0 >> 52) | (c1 << 12); \
    c1 >>= 52; \
    \
    MULX64(hi, lo, d0, R << 12); \
    CLEAR_CF(); \
    ADCX64(c0, lo); ADCX64(c1, hi); \
    ADCX64(c0, t3); \
    \
    (r)[3] = c0 & M; \
    c0 = (c0 >> 52) | (c1 << 12); \
    \
    (r)[4] = c0 + t4; \
} while(0)
