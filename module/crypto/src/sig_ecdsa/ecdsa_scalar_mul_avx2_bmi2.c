/*
 * AVX2 + BMI2 secp256k1 Scalar Multiplication
 * Uses MULX, ADCX, ADOX for parallel carry chains
 * Requires Haswell+ (Intel 2013) or Zen+ (AMD 2018)
 */

#if defined(__x86_64__) || defined(_M_X64)

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __BMI2__
#include <immintrin.h>
#endif

#include "ecdsa_scalar.h"
#include "ecdsa_scalar_mul_arch.h"

// ============================================================================
// secp256k1 curve order n
// ============================================================================

static const uint64_t SCALAR_N[4] = {
    0xBFD25E8CD0364141ULL,
    0xBAAEDCE6AF48A03BULL,
    0xFFFFFFFFFFFFFFFEULL,
    0xFFFFFFFFFFFFFFFFULL
};

static const uint64_t SCALAR_2P256_MOD_N[4] = {
    0x402DA1732FC9BEBFULL,
    0x4551231950B75FC4ULL,
    0x0000000000000001ULL,
    0x0000000000000000ULL
};

// ============================================================================
// 256x256 -> 512 bit multiplication using BMI2 MULX + ADX
// MULX: dst_hi:dst_lo = src1 * rdx (no flags modified!)
// ADCX: dst += src + CF (only affects CF)
// ADOX: dst += src + OF (only affects OF)
// ============================================================================

__attribute__((target("bmi2,adx")))
void ecdsa_scalar_mul_512_avx2_bmi2(uint64_t l[8], const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    const uint64_t *pa = a->d;
    const uint64_t *pb = b->d;
    
    // Use MULX with parallel ADCX/ADOX carry chains
    __asm__ __volatile__(
        // Load b[0] into rdx for first MULX sequence
        "movq 0(%[pb]), %%rdx\n"
        
        // === a[*] * b[0] ===
        "mulx 0(%[pa]), %%rax, %%r8\n"   // a[0]*b[0]
        "movq %%rax, 0(%[pl])\n"          // l[0] done
        
        "mulx 8(%[pa]), %%rax, %%r9\n"   // a[1]*b[0]
        "xorl %%r10d, %%r10d\n"           // Clear r10 and flags
        "adcx %%rax, %%r8\n"
        
        "mulx 16(%[pa]), %%rax, %%r10\n" // a[2]*b[0]
        "adcx %%rax, %%r9\n"
        
        "mulx 24(%[pa]), %%rax, %%r11\n" // a[3]*b[0]
        "adcx %%rax, %%r10\n"
        "movq $0, %%r12\n"
        "adcx %%r12, %%r11\n"
        
        // === a[*] * b[1] ===
        "movq 8(%[pb]), %%rdx\n"
        
        "mulx 0(%[pa]), %%rax, %%rcx\n"
        "adox %%rax, %%r8\n"
        "movq %%r8, 8(%[pl])\n"           // l[1] done
        
        "mulx 8(%[pa]), %%rax, %%r8\n"
        "adcx %%rcx, %%r9\n"
        "adox %%rax, %%r9\n"
        
        "mulx 16(%[pa]), %%rax, %%rcx\n"
        "adcx %%r8, %%r10\n"
        "adox %%rax, %%r10\n"
        
        "mulx 24(%[pa]), %%rax, %%r8\n"
        "adcx %%rcx, %%r11\n"
        "adox %%rax, %%r11\n"
        "movq $0, %%r12\n"
        "adcx %%r8, %%r12\n"
        "adox %%r12, %%r12\n"
        "movq $0, %%r12\n"
        "adcx %%r12, %%r12\n"
        
        // === a[*] * b[2] ===
        "movq 16(%[pb]), %%rdx\n"
        
        "mulx 0(%[pa]), %%rax, %%rcx\n"
        "adox %%rax, %%r9\n"
        "movq %%r9, 16(%[pl])\n"          // l[2] done
        
        "mulx 8(%[pa]), %%rax, %%r8\n"
        "adcx %%rcx, %%r10\n"
        "adox %%rax, %%r10\n"
        
        "mulx 16(%[pa]), %%rax, %%rcx\n"
        "adcx %%r8, %%r11\n"
        "adox %%rax, %%r11\n"
        
        "mulx 24(%[pa]), %%rax, %%r8\n"
        "adcx %%rcx, %%r12\n"
        "adox %%rax, %%r12\n"
        "movq $0, %%r13\n"
        "adcx %%r8, %%r13\n"
        "adox %%r13, %%r13\n"
        "movq $0, %%r13\n"
        "adcx %%r13, %%r13\n"
        
        // === a[*] * b[3] ===
        "movq 24(%[pb]), %%rdx\n"
        
        "mulx 0(%[pa]), %%rax, %%rcx\n"
        "adox %%rax, %%r10\n"
        "movq %%r10, 24(%[pl])\n"         // l[3] done
        
        "mulx 8(%[pa]), %%rax, %%r8\n"
        "adcx %%rcx, %%r11\n"
        "adox %%rax, %%r11\n"
        "movq %%r11, 32(%[pl])\n"         // l[4] done
        
        "mulx 16(%[pa]), %%rax, %%rcx\n"
        "adcx %%r8, %%r12\n"
        "adox %%rax, %%r12\n"
        "movq %%r12, 40(%[pl])\n"         // l[5] done
        
        "mulx 24(%[pa]), %%rax, %%r8\n"
        "adcx %%rcx, %%r13\n"
        "adox %%rax, %%r13\n"
        "movq %%r13, 48(%[pl])\n"         // l[6] done
        
        "movq $0, %%rax\n"
        "adcx %%r8, %%rax\n"
        "movq %%rax, 56(%[pl])\n"         // l[7] done
        
        : /* outputs */
        : [pl] "r" (l), [pa] "r" (pa), [pb] "r" (pb)
        : "rax", "rcx", "rdx", "r8", "r9", "r10", "r11", "r12", "r13", 
          "cc", "memory"
    );
}

// ============================================================================
// 512-bit reduction mod n
// ============================================================================

__attribute__((target("bmi2,adx")))
void ecdsa_scalar_reduce_512_avx2_bmi2(ecdsa_scalar_t *r, const uint64_t l[8])
{
    uint64_t r0 = l[0], r1 = l[1], r2 = l[2], r3 = l[3];
    uint64_t h0 = l[4], h1 = l[5], h2 = l[6], h3 = l[7];
    
    if (h0 | h1 | h2 | h3) {
        __asm__ __volatile__(
            "movq %[h0], %%rax\n"
            "mulq %[c0]\n"
            "addq %%rax, %[r0]\n"
            "adcq %%rdx, %[r1]\n"
            "adcq $0, %[r2]\n"
            "adcq $0, %[r3]\n"
            : [r0] "+r" (r0), [r1] "+r" (r1), [r2] "+r" (r2), [r3] "+r" (r3)
            : [h0] "r" (h0), [c0] "r" (SCALAR_2P256_MOD_N[0])
            : "rax", "rdx", "cc"
        );
    }
    
    int over = 0;
    if (r3 > SCALAR_N[3]) over = 1;
    else if (r3 == SCALAR_N[3]) {
        if (r2 > SCALAR_N[2]) over = 1;
        else if (r2 == SCALAR_N[2]) {
            if (r1 > SCALAR_N[1]) over = 1;
            else if (r1 == SCALAR_N[1]) {
                if (r0 >= SCALAR_N[0]) over = 1;
            }
        }
    }
    
    if (over) {
        __asm__ __volatile__(
            "subq %[n0], %[r0]\n"
            "sbbq %[n1], %[r1]\n"
            "sbbq %[n2], %[r2]\n"
            "sbbq %[n3], %[r3]\n"
            : [r0] "+r" (r0), [r1] "+r" (r1), [r2] "+r" (r2), [r3] "+r" (r3)
            : [n0] "r" (SCALAR_N[0]), [n1] "r" (SCALAR_N[1]),
              [n2] "r" (SCALAR_N[2]), [n3] "r" (SCALAR_N[3])
            : "cc"
        );
    }
    
    r->d[0] = r0; r->d[1] = r1; r->d[2] = r2; r->d[3] = r3;
}

// ============================================================================
// mul_shift_384
// ============================================================================

__attribute__((target("bmi2,adx")))
void ecdsa_scalar_mul_shift_384_avx2_bmi2(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    uint64_t l[8];
    ecdsa_scalar_mul_512_avx2_bmi2(l, a, b);
    
    uint64_t round_bit = (l[5] >> 63) & 1;
    r->d[0] = l[6] + round_bit;
    r->d[1] = l[7] + (r->d[0] < round_bit);
    r->d[2] = 0;
    r->d[3] = 0;
}

// ============================================================================
// Full scalar multiplication
// ============================================================================

__attribute__((target("bmi2,adx")))
void ecdsa_scalar_mul_avx2_bmi2(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    uint64_t l[8];
    ecdsa_scalar_mul_512_avx2_bmi2(l, a, b);
    ecdsa_scalar_reduce_512_avx2_bmi2(r, l);
}

#endif // __x86_64__
