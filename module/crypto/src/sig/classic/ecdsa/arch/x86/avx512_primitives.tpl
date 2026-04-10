// ============================================================================
// AVX-512 Primitives for secp256k1 Scalar Arithmetic
// Uses MULX + ADCX/ADOX (available on all AVX-512 capable CPUs)
// For 4x64-bit scalars, IFMA (52-bit) is not ideal - use MULX instead
// ============================================================================

#if defined(__x86_64__) || defined(_M_X64)

// ============================================================================
// AVX-512 capable CPUs always have BMI2 (MULX) and ADX (ADCX/ADOX)
// MULX: no flags affected, allows using ADCX/ADOX in parallel
// ADCX: add with CF only
// ADOX: add with OF only  
// This allows two independent carry chains!
// ============================================================================

// Clear CF and OF for ADCX/ADOX chains
#define CLEAR_FLAGS() \
    __asm__ __volatile__ ("xorq %%rax, %%rax" ::: "rax", "cc")

// MULX: hi:lo = a * b (uses RDX as implicit multiplicand)
#define MULX64(hi, lo, a, b) do { \
    uint64_t _a = (a), _b = (b); \
    __asm__ __volatile__ ( \
        "mulx %3, %0, %1" \
        : "=r"(lo), "=r"(hi) \
        : "d"(_a), "rm"(_b) \
    ); \
} while(0)

// ADCX: dst += src + CF (only uses/modifies CF)
#define ADCX64(dst, src) \
    __asm__ __volatile__ ("adcx %1, %0" : "+r"(dst) : "rm"(src) : "cc")

// ADOX: dst += src + OF (only uses/modifies OF)  
#define ADOX64(dst, src) \
    __asm__ __volatile__ ("adox %1, %0" : "+r"(dst) : "rm"(src) : "cc")

// ============================================================================
// 256x256 -> 512 bit multiplication using MULX + ADCX/ADOX
// Two parallel carry chains for maximum throughput
// ============================================================================

#define SCALAR_MUL_512_IMPL(l, a, b) do { \
    uint64_t a0 = (a)[0], a1 = (a)[1], a2 = (a)[2], a3 = (a)[3]; \
    uint64_t b0 = (b)[0], b1 = (b)[1], b2 = (b)[2], b3 = (b)[3]; \
    uint64_t r0, r1, r2, r3, r4, r5, r6, r7; \
    uint64_t t0, t1, t2, t3, t4, t5, t6, t7; \
    uint64_t h0, h1, h2, h3; \
    \
    /* First pass: a[0] * b[0..3] */ \
    MULX64(t1, r0, a0, b0); \
    MULX64(t2, t0, a0, b1); \
    MULX64(t3, h0, a0, b2); \
    MULX64(r4, h1, a0, b3); \
    \
    CLEAR_FLAGS(); \
    ADCX64(t1, t0); \
    ADCX64(t2, h0); \
    ADCX64(t3, h1); \
    ADCX64(r4, (uint64_t)0); \
    \
    /* Second pass: a[1] * b[0..3] */ \
    MULX64(h1, h0, a1, b0); \
    MULX64(h3, h2, a1, b1); \
    CLEAR_FLAGS(); \
    ADCX64(t1, h0); \
    ADOX64(t2, h1); \
    ADCX64(t2, h2); \
    ADOX64(t3, h3); \
    \
    MULX64(h1, h0, a1, b2); \
    MULX64(r5, h2, a1, b3); \
    ADCX64(t3, h0); \
    ADOX64(r4, h1); \
    ADCX64(r4, h2); \
    ADOX64(r5, (uint64_t)0); \
    ADCX64(r5, (uint64_t)0); \
    \
    r1 = t1; \
    \
    /* Third pass: a[2] * b[0..3] */ \
    MULX64(h1, h0, a2, b0); \
    MULX64(h3, h2, a2, b1); \
    CLEAR_FLAGS(); \
    ADCX64(t2, h0); \
    ADOX64(t3, h1); \
    ADCX64(t3, h2); \
    ADOX64(r4, h3); \
    \
    MULX64(h1, h0, a2, b2); \
    MULX64(r6, h2, a2, b3); \
    ADCX64(r4, h0); \
    ADOX64(r5, h1); \
    ADCX64(r5, h2); \
    ADOX64(r6, (uint64_t)0); \
    ADCX64(r6, (uint64_t)0); \
    \
    r2 = t2; \
    \
    /* Fourth pass: a[3] * b[0..3] */ \
    MULX64(h1, h0, a3, b0); \
    MULX64(h3, h2, a3, b1); \
    CLEAR_FLAGS(); \
    ADCX64(t3, h0); \
    ADOX64(r4, h1); \
    ADCX64(r4, h2); \
    ADOX64(r5, h3); \
    \
    MULX64(h1, h0, a3, b2); \
    MULX64(r7, h2, a3, b3); \
    ADCX64(r5, h0); \
    ADOX64(r6, h1); \
    ADCX64(r6, h2); \
    ADOX64(r7, (uint64_t)0); \
    ADCX64(r7, (uint64_t)0); \
    \
    r3 = t3; \
    \
    (l)[0] = r0; (l)[1] = r1; (l)[2] = r2; (l)[3] = r3; \
    (l)[4] = r4; (l)[5] = r5; (l)[6] = r6; (l)[7] = r7; \
} while(0)

// ============================================================================
// 512-bit reduction mod n using MULX
// ============================================================================

#define SCALAR_REDUCE_512_IMPL(r, l) do { \
    uint64_t r0 = (l)[0], r1 = (l)[1], r2 = (l)[2], r3 = (l)[3]; \
    uint64_t h0 = (l)[4], h1 = (l)[5], h2 = (l)[6], h3 = (l)[7]; \
    \
    if (h0 | h1 | h2 | h3) { \
        uint64_t lo, hi, c; \
        \
        /* Multiply h by (2^256 mod n) and add to r */ \
        /* 2^256 mod n = 0x14551231950B75FC4402DA1732FC9BEBF */ \
        \
        /* h0 * c0 */ \
        MULX64(hi, lo, h0, SCALAR_2P256_MOD_N[0]); \
        __asm__ __volatile__ ( \
            "addq %2, %0\n\t" \
            "adcq %3, %1\n\t" \
            "adcq $0, %4\n\t" \
            "adcq $0, %5\n\t" \
            : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3) \
            : "r"(lo), "r"(hi) \
            : "cc" \
        ); \
        \
        /* h0 * c1 */ \
        MULX64(hi, lo, h0, SCALAR_2P256_MOD_N[1]); \
        __asm__ __volatile__ ( \
            "addq %2, %0\n\t" \
            "adcq %3, %1\n\t" \
            "adcq $0, %4\n\t" \
            : "+r"(r1), "+r"(r2), "+r"(r3) \
            : "r"(lo), "r"(hi) \
            : "cc" \
        ); \
        \
        /* Simplified: only handle h0 for now, full impl needed for h1-h3 */ \
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
            "subq %4, %0\n\t" \
            "sbbq %5, %1\n\t" \
            "sbbq %6, %2\n\t" \
            "sbbq %7, %3\n\t" \
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
#error "AVX-512 primitives require x86-64 architecture"
#endif
