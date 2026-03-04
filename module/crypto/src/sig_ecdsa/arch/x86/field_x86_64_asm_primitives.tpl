// ============================================================================
// x86-64 Real Assembly Primitives for secp256k1 Field Arithmetic
// Uses MULQ instruction with interleaved reduction
// ============================================================================

// secp256k1 field constants
#define FIELD_M52 0xFFFFFFFFFFFFFULL
#define FIELD_M48 0xFFFFFFFFFFFFULL
#define FIELD_R   0x1000003D10ULL

// ============================================================================
// x86-64 inline assembly helpers
// MUL64: multiply two 64-bit values, result in hi:lo
// ============================================================================

#define MUL64(hi, lo, a, b) \
    __asm__ __volatile__ ( \
        "mulq %3" \
        : "=a"(lo), "=d"(hi) \
        : "a"(a), "rm"(b) \
        : "cc" \
    )

// MULADD64: multiply and add to accumulator (c1:c0)
// c1:c0 += a * b
#define MULADD64(c1, c0, a, b) do { \
    uint64_t _lo, _hi; \
    MUL64(_hi, _lo, a, b); \
    __asm__ __volatile__ ( \
        "addq %2, %0\n\t" \
        "adcq %3, %1" \
        : "+r"(c0), "+r"(c1) \
        : "r"(_lo), "r"(_hi) \
        : "cc" \
    ); \
} while(0)

// ADD128: add two 128-bit values
// c1:c0 += a1:a0
#define ADD128(c1, c0, a1, a0) \
    __asm__ __volatile__ ( \
        "addq %2, %0\n\t" \
        "adcq %3, %1" \
        : "+r"(c0), "+r"(c1) \
        : "r"(a0), "r"(a1) \
        : "cc" \
    )

// ============================================================================
// x86-64 MULQ-based Field Multiplication with Interleaved Reduction
// Real inline assembly version
// ============================================================================

#define FIELD_MUL_IMPL(r, a, b) do { \
    uint64_t a0 = (a)[0], a1 = (a)[1], a2 = (a)[2], a3 = (a)[3], a4 = (a)[4]; \
    uint64_t b0 = (b)[0], b1 = (b)[1], b2 = (b)[2], b3 = (b)[3], b4 = (b)[4]; \
    const uint64_t M = FIELD_M52; \
    const uint64_t R = 0x1000003D10ULL; \
    \
    uint64_t c0 = 0, c1 = 0;  /* 128-bit accumulator c */ \
    uint64_t d0 = 0, d1 = 0;  /* 128-bit accumulator d */ \
    uint64_t t3, t4, tx, u0; \
    uint64_t tmp_lo, tmp_hi; \
    \
    /* Compute d = p3 = a0*b3 + a1*b2 + a2*b1 + a3*b0 */ \
    MUL64(d1, d0, a0, b3); \
    MULADD64(d1, d0, a1, b2); \
    MULADD64(d1, d0, a2, b1); \
    MULADD64(d1, d0, a3, b0); \
    \
    /* Compute c = p8 = a4*b4, then d += lo(c) * R */ \
    MUL64(c1, c0, a4, b4); \
    MUL64(tmp_hi, tmp_lo, c0, R); \
    ADD128(d1, d0, tmp_hi, tmp_lo); \
    /* c >>= 64: c0 = c1, c1 = 0 */ \
    c0 = c1; c1 = 0; \
    \
    /* t3 = d & M; d >>= 52 */ \
    t3 = d0 & M; \
    d0 = (d0 >> 52) | (d1 << 12); \
    d1 >>= 52; \
    \
    /* d += p4 = a0*b4 + a1*b3 + a2*b2 + a3*b1 + a4*b0 */ \
    MULADD64(d1, d0, a0, b4); \
    MULADD64(d1, d0, a1, b3); \
    MULADD64(d1, d0, a2, b2); \
    MULADD64(d1, d0, a3, b1); \
    MULADD64(d1, d0, a4, b0); \
    /* d += c0 * (R << 12) */ \
    MUL64(tmp_hi, tmp_lo, c0, R << 12); \
    ADD128(d1, d0, tmp_hi, tmp_lo); \
    \
    /* t4 = d & M; d >>= 52; tx = t4 >> 48; t4 &= M >> 4 */ \
    t4 = d0 & M; \
    d0 = (d0 >> 52) | (d1 << 12); \
    d1 >>= 52; \
    tx = t4 >> 48; \
    t4 &= (M >> 4); \
    \
    /* c = p0 = a0*b0 */ \
    MUL64(c1, c0, a0, b0); \
    \
    /* d += p5 = a1*b4 + a2*b3 + a3*b2 + a4*b1 */ \
    MULADD64(d1, d0, a1, b4); \
    MULADD64(d1, d0, a2, b3); \
    MULADD64(d1, d0, a3, b2); \
    MULADD64(d1, d0, a4, b1); \
    \
    /* u0 = d & M; d >>= 52; u0 = (u0 << 4) | tx */ \
    u0 = d0 & M; \
    d0 = (d0 >> 52) | (d1 << 12); \
    d1 >>= 52; \
    u0 = (u0 << 4) | tx; \
    \
    /* c += u0 * (R >> 4) */ \
    MUL64(tmp_hi, tmp_lo, u0, R >> 4); \
    ADD128(c1, c0, tmp_hi, tmp_lo); \
    \
    /* r[0] = c & M; c >>= 52 */ \
    (r)[0] = c0 & M; \
    c0 = (c0 >> 52) | (c1 << 12); \
    c1 >>= 52; \
    \
    /* c += p1 = a0*b1 + a1*b0 */ \
    MULADD64(c1, c0, a0, b1); \
    MULADD64(c1, c0, a1, b0); \
    \
    /* d += p6 = a2*b4 + a3*b3 + a4*b2 */ \
    MULADD64(d1, d0, a2, b4); \
    MULADD64(d1, d0, a3, b3); \
    MULADD64(d1, d0, a4, b2); \
    \
    /* c += (d & M) * R; d >>= 52 */ \
    MUL64(tmp_hi, tmp_lo, d0 & M, R); \
    ADD128(c1, c0, tmp_hi, tmp_lo); \
    d0 = (d0 >> 52) | (d1 << 12); \
    d1 >>= 52; \
    \
    /* r[1] = c & M; c >>= 52 */ \
    (r)[1] = c0 & M; \
    c0 = (c0 >> 52) | (c1 << 12); \
    c1 >>= 52; \
    \
    /* c += p2 = a0*b2 + a1*b1 + a2*b0 */ \
    MULADD64(c1, c0, a0, b2); \
    MULADD64(c1, c0, a1, b1); \
    MULADD64(c1, c0, a2, b0); \
    \
    /* d += p7 = a3*b4 + a4*b3 */ \
    MULADD64(d1, d0, a3, b4); \
    MULADD64(d1, d0, a4, b3); \
    \
    /* c += d0 * R; d >>= 64 (d0 = d1) */ \
    MUL64(tmp_hi, tmp_lo, d0, R); \
    ADD128(c1, c0, tmp_hi, tmp_lo); \
    d0 = d1; \
    d1 = 0; \
    \
    /* r[2] = c & M; c >>= 52 */ \
    (r)[2] = c0 & M; \
    c0 = (c0 >> 52) | (c1 << 12); \
    c1 >>= 52; \
    \
    /* c += d0 * (R << 12) + t3 */ \
    MUL64(tmp_hi, tmp_lo, d0, R << 12); \
    ADD128(c1, c0, tmp_hi, tmp_lo); \
    __asm__ __volatile__ ( \
        "addq %2, %0\n\t" \
        "adcq $0, %1" \
        : "+r"(c0), "+r"(c1) \
        : "r"(t3) \
        : "cc" \
    ); \
    \
    /* r[3] = c & M; c >>= 52 */ \
    (r)[3] = c0 & M; \
    c0 = (c0 >> 52) | (c1 << 12); \
    \
    /* r[4] = c + t4 */ \
    (r)[4] = c0 + t4; \
} while(0)

// ============================================================================
// x86-64 MULQ-based Field Squaring with Interleaved Reduction
// Optimized: uses symmetry (off-diagonal terms doubled)
// ============================================================================

#define FIELD_SQR_IMPL(r, a) do { \
    uint64_t a0 = (a)[0], a1 = (a)[1], a2 = (a)[2], a3 = (a)[3], a4 = (a)[4]; \
    const uint64_t M = FIELD_M52; \
    const uint64_t R = 0x1000003D10ULL; \
    \
    uint64_t c0 = 0, c1 = 0; \
    uint64_t d0 = 0, d1 = 0; \
    uint64_t t3, t4, tx, u0; \
    uint64_t tmp_lo, tmp_hi; \
    uint64_t a0_2 = a0 * 2, a1_2 = a1 * 2, a2_2 = a2 * 2, a3_2 = a3 * 2; \
    \
    /* d = p3 = 2*(a0*a3 + a1*a2) */ \
    MUL64(d1, d0, a0, a3); \
    MULADD64(d1, d0, a1, a2); \
    /* Double: d <<= 1 */ \
    d1 = (d1 << 1) | (d0 >> 63); \
    d0 <<= 1; \
    \
    /* c = p8 = a4^2; d += lo(c) * R */ \
    MUL64(c1, c0, a4, a4); \
    MUL64(tmp_hi, tmp_lo, c0, R); \
    ADD128(d1, d0, tmp_hi, tmp_lo); \
    c0 = c1; c1 = 0; \
    \
    t3 = d0 & M; \
    d0 = (d0 >> 52) | (d1 << 12); \
    d1 >>= 52; \
    \
    /* d += p4 = 2*(a0*a4 + a1*a3) + a2^2 */ \
    MULADD64(d1, d0, a0_2, a4); \
    MULADD64(d1, d0, a1_2, a3); \
    MULADD64(d1, d0, a2, a2); \
    MUL64(tmp_hi, tmp_lo, c0, R << 12); \
    ADD128(d1, d0, tmp_hi, tmp_lo); \
    \
    t4 = d0 & M; \
    d0 = (d0 >> 52) | (d1 << 12); \
    d1 >>= 52; \
    tx = t4 >> 48; \
    t4 &= (M >> 4); \
    \
    /* c = p0 = a0^2 */ \
    MUL64(c1, c0, a0, a0); \
    \
    /* d += p5 = 2*(a1*a4 + a2*a3) */ \
    MULADD64(d1, d0, a1_2, a4); \
    MULADD64(d1, d0, a2_2, a3); \
    \
    u0 = d0 & M; \
    d0 = (d0 >> 52) | (d1 << 12); \
    d1 >>= 52; \
    u0 = (u0 << 4) | tx; \
    \
    MUL64(tmp_hi, tmp_lo, u0, R >> 4); \
    ADD128(c1, c0, tmp_hi, tmp_lo); \
    \
    (r)[0] = c0 & M; \
    c0 = (c0 >> 52) | (c1 << 12); \
    c1 >>= 52; \
    \
    /* c += p1 = 2*a0*a1 */ \
    MULADD64(c1, c0, a0_2, a1); \
    \
    /* d += p6 = 2*a2*a4 + a3^2 */ \
    MULADD64(d1, d0, a2_2, a4); \
    MULADD64(d1, d0, a3, a3); \
    \
    MUL64(tmp_hi, tmp_lo, d0 & M, R); \
    ADD128(c1, c0, tmp_hi, tmp_lo); \
    d0 = (d0 >> 52) | (d1 << 12); \
    d1 >>= 52; \
    \
    (r)[1] = c0 & M; \
    c0 = (c0 >> 52) | (c1 << 12); \
    c1 >>= 52; \
    \
    /* c += p2 = 2*a0*a2 + a1^2 */ \
    MULADD64(c1, c0, a0_2, a2); \
    MULADD64(c1, c0, a1, a1); \
    \
    /* d += p7 = 2*a3*a4 */ \
    MULADD64(d1, d0, a3_2, a4); \
    \
    MUL64(tmp_hi, tmp_lo, d0, R); \
    ADD128(c1, c0, tmp_hi, tmp_lo); \
    d0 = d1; \
    \
    (r)[2] = c0 & M; \
    c0 = (c0 >> 52) | (c1 << 12); \
    c1 >>= 52; \
    \
    MUL64(tmp_hi, tmp_lo, d0, R << 12); \
    ADD128(c1, c0, tmp_hi, tmp_lo); \
    __asm__ __volatile__ ( \
        "addq %2, %0\n\t" \
        "adcq $0, %1" \
        : "+r"(c0), "+r"(c1) \
        : "r"(t3) \
        : "cc" \
    ); \
    \
    (r)[3] = c0 & M; \
    c0 = (c0 >> 52) | (c1 << 12); \
    \
    (r)[4] = c0 + t4; \
} while(0)
