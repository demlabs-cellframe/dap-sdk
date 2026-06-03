// ============================================================================
// AVX2 + BMI2 Primitives for secp256k1 Scalar Arithmetic
// Uses MULX, ADCX, ADOX for parallel carry chains
// ============================================================================

// Note: BMI2/ADX instructions are enabled via __attribute__((target("avx2,bmi2,adx")))
// on each function, not via compiler flags
#if defined(__x86_64__)

// ============================================================================
// 256x256 -> 512 bit multiplication using BMI2 MULX + ADX ADCX/ADOX
// MULX: dst_hi:dst_lo = src1 * rdx (no flags affected!)
// ADCX: dst += src + CF (only affects CF)
// ADOX: dst += src + OF (only affects OF)
// This allows two parallel carry chains for maximum throughput
// ============================================================================

#define SCALAR_MUL_512_IMPL(l, a, b) do { \
    __asm__ __volatile__( \
        /* Strategy: Use MULX for flag-free multiply, ADCX/ADOX for parallel adds */ \
        /* Load b[0] into rdx for MULX source */ \
        "movq 0(%[pb]), %%rdx\n" \
        \
        /* Column 0: a[0]*b[0] -> l[0], carry to r8 */ \
        "mulx 0(%[pa]), %%rax, %%r8\n" \
        "movq %%rax, 0(%[pl])\n" \
        \
        /* Column 1: a[1]*b[0] + ... */ \
        "mulx 8(%[pa]), %%rax, %%r9\n" \
        "xorq %%r10, %%r10\n"           /* Clear r10 and flags */ \
        "adcx %%rax, %%r8\n" \
        \
        /* Column 2: a[2]*b[0] + ... */ \
        "mulx 16(%[pa]), %%rax, %%r10\n" \
        "adcx %%rax, %%r9\n" \
        \
        /* Column 3: a[3]*b[0] + ... */ \
        "mulx 24(%[pa]), %%rax, %%r11\n" \
        "adcx %%rax, %%r10\n" \
        "adcx %%r11, %%r11\n"           /* r11 = 0 + carry */ \
        "movq $0, %%r11\n" \
        "adcq $0, %%r11\n" \
        \
        /* Load b[1] into rdx */ \
        "movq 8(%[pb]), %%rdx\n" \
        \
        /* a[0..3] * b[1] */ \
        "mulx 0(%[pa]), %%rax, %%rcx\n" \
        "adox %%rax, %%r8\n" \
        "movq %%r8, 8(%[pl])\n"         /* l[1] done */ \
        \
        "mulx 8(%[pa]), %%rax, %%r8\n" \
        "adcx %%rcx, %%r9\n" \
        "adox %%rax, %%r9\n" \
        \
        "mulx 16(%[pa]), %%rax, %%rcx\n" \
        "adcx %%r8, %%r10\n" \
        "adox %%rax, %%r10\n" \
        \
        "mulx 24(%[pa]), %%rax, %%r8\n" \
        "adcx %%rcx, %%r11\n" \
        "adox %%rax, %%r11\n" \
        "movq $0, %%r12\n" \
        "adcx %%r8, %%r12\n" \
        "adox %%r12, %%r12\n" \
        "movq $0, %%r12\n" \
        "adcq $0, %%r12\n" \
        \
        /* Load b[2] into rdx */ \
        "movq 16(%[pb]), %%rdx\n" \
        \
        /* a[0..3] * b[2] */ \
        "mulx 0(%[pa]), %%rax, %%rcx\n" \
        "adox %%rax, %%r9\n" \
        "movq %%r9, 16(%[pl])\n"        /* l[2] done */ \
        \
        "mulx 8(%[pa]), %%rax, %%r8\n" \
        "adcx %%rcx, %%r10\n" \
        "adox %%rax, %%r10\n" \
        \
        "mulx 16(%[pa]), %%rax, %%rcx\n" \
        "adcx %%r8, %%r11\n" \
        "adox %%rax, %%r11\n" \
        \
        "mulx 24(%[pa]), %%rax, %%r8\n" \
        "adcx %%rcx, %%r12\n" \
        "adox %%rax, %%r12\n" \
        "movq $0, %%r13\n" \
        "adcx %%r8, %%r13\n" \
        "adox %%r13, %%r13\n" \
        "movq $0, %%r13\n" \
        "adcq $0, %%r13\n" \
        \
        /* Load b[3] into rdx */ \
        "movq 24(%[pb]), %%rdx\n" \
        \
        /* a[0..3] * b[3] */ \
        "mulx 0(%[pa]), %%rax, %%rcx\n" \
        "adox %%rax, %%r10\n" \
        "movq %%r10, 24(%[pl])\n"       /* l[3] done */ \
        \
        "mulx 8(%[pa]), %%rax, %%r8\n" \
        "adcx %%rcx, %%r11\n" \
        "adox %%rax, %%r11\n" \
        "movq %%r11, 32(%[pl])\n"       /* l[4] done */ \
        \
        "mulx 16(%[pa]), %%rax, %%rcx\n" \
        "adcx %%r8, %%r12\n" \
        "adox %%rax, %%r12\n" \
        "movq %%r12, 40(%[pl])\n"       /* l[5] done */ \
        \
        "mulx 24(%[pa]), %%rax, %%r8\n" \
        "adcx %%rcx, %%r13\n" \
        "adox %%rax, %%r13\n" \
        "movq %%r13, 48(%[pl])\n"       /* l[6] done */ \
        \
        "movq $0, %%rax\n" \
        "adcx %%r8, %%rax\n" \
        "movq %%rax, 56(%[pl])\n"       /* l[7] done */ \
        \
        : /* outputs */ \
        : [pl] "r" (l), [pa] "r" (a), [pb] "r" (b) \
        : "rax", "rcx", "rdx", "r8", "r9", "r10", "r11", "r12", "r13", "cc", "memory" \
    ); \
} while(0)

// Reduction uses same approach as generic x86-64
#define SCALAR_REDUCE_512_IMPL(r, l) do { \
    uint64_t r0 = (l)[0], r1 = (l)[1], r2 = (l)[2], r3 = (l)[3]; \
    uint64_t h0 = (l)[4], h1 = (l)[5], h2 = (l)[6], h3 = (l)[7]; \
    \
    if (h0 | h1 | h2 | h3) { \
        __asm__ __volatile__( \
            "movq %[h0], %%rax\n" \
            "mulq %[c0]\n" \
            "addq %%rax, %[r0]\n" \
            "adcq %%rdx, %[r1]\n" \
            "adcq $0, %[r2]\n" \
            "adcq $0, %[r3]\n" \
            : [r0] "+r" (r0), [r1] "+r" (r1), [r2] "+r" (r2), [r3] "+r" (r3) \
            : [h0] "r" (h0), [c0] "r" (SCALAR_2P256_MOD_N[0]) \
            : "rax", "rdx", "cc" \
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
            "subq %[n0], %[r0]\n" \
            "sbbq %[n1], %[r1]\n" \
            "sbbq %[n2], %[r2]\n" \
            "sbbq %[n3], %[r3]\n" \
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
#error "AVX2+BMI2 primitives require x86-64 architecture"
#endif
