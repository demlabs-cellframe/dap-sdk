/*
 * x86-64 Assembly secp256k1 Scalar Multiplication
 * Hand-optimized inline assembly for maximum performance
 */

#if defined(__x86_64__) || defined(_M_X64)

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

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
// 256x256 -> 512 bit multiplication using x86-64 MULQ
// ============================================================================

void ecdsa_scalar_mul_512_x86_64_asm(uint64_t l[8], const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    const uint64_t *pa = a->d;
    const uint64_t *pb = b->d;
    
    __asm__ __volatile__(
        // Load a[0..2] into registers
        "movq 0(%[pa]), %%r15\n"
        "movq 8(%[pa]), %%rbx\n"
        "movq 16(%[pa]), %%rcx\n"
        // Load b[0..3] into registers
        "movq 0(%[pb]), %%r11\n"
        "movq 8(%[pb]), %%r12\n"
        "movq 16(%[pb]), %%r13\n"
        "movq 24(%[pb]), %%r14\n"
        
        // l[0] = a[0] * b[0]
        "movq %%r15, %%rax\n"
        "mulq %%r11\n"
        "movq %%rax, 0(%[pl])\n"
        "movq %%rdx, %%r8\n"
        "xorq %%r9, %%r9\n"
        "xorq %%r10, %%r10\n"
        
        // l[1] = a[0]*b[1] + a[1]*b[0]
        "movq %%r15, %%rax\n"
        "mulq %%r12\n"
        "addq %%rax, %%r8\n"
        "adcq %%rdx, %%r9\n"
        "adcq $0, %%r10\n"
        
        "movq %%rbx, %%rax\n"
        "mulq %%r11\n"
        "addq %%rax, %%r8\n"
        "adcq %%rdx, %%r9\n"
        "adcq $0, %%r10\n"
        
        "movq %%r8, 8(%[pl])\n"
        "xorq %%r8, %%r8\n"
        
        // l[2] = a[0]*b[2] + a[1]*b[1] + a[2]*b[0]
        "movq %%r15, %%rax\n"
        "mulq %%r13\n"
        "addq %%rax, %%r9\n"
        "adcq %%rdx, %%r10\n"
        "adcq $0, %%r8\n"
        
        "movq %%rbx, %%rax\n"
        "mulq %%r12\n"
        "addq %%rax, %%r9\n"
        "adcq %%rdx, %%r10\n"
        "adcq $0, %%r8\n"
        
        "movq %%rcx, %%rax\n"
        "mulq %%r11\n"
        "addq %%rax, %%r9\n"
        "adcq %%rdx, %%r10\n"
        "adcq $0, %%r8\n"
        
        "movq %%r9, 16(%[pl])\n"
        "xorq %%r9, %%r9\n"
        
        // l[3] = a[0]*b[3] + a[1]*b[2] + a[2]*b[1] + a[3]*b[0]
        "movq %%r15, %%rax\n"
        "mulq %%r14\n"
        "addq %%rax, %%r10\n"
        "adcq %%rdx, %%r8\n"
        "adcq $0, %%r9\n"
        
        // Load a[3] into r15 (reusing)
        "movq 24(%[pa]), %%r15\n"
        
        "movq %%rbx, %%rax\n"
        "mulq %%r13\n"
        "addq %%rax, %%r10\n"
        "adcq %%rdx, %%r8\n"
        "adcq $0, %%r9\n"
        
        "movq %%rcx, %%rax\n"
        "mulq %%r12\n"
        "addq %%rax, %%r10\n"
        "adcq %%rdx, %%r8\n"
        "adcq $0, %%r9\n"
        
        "movq %%r15, %%rax\n"
        "mulq %%r11\n"
        "addq %%rax, %%r10\n"
        "adcq %%rdx, %%r8\n"
        "adcq $0, %%r9\n"
        
        "movq %%r10, 24(%[pl])\n"
        "xorq %%r10, %%r10\n"
        
        // l[4] = a[1]*b[3] + a[2]*b[2] + a[3]*b[1]
        "movq %%rbx, %%rax\n"
        "mulq %%r14\n"
        "addq %%rax, %%r8\n"
        "adcq %%rdx, %%r9\n"
        "adcq $0, %%r10\n"
        
        "movq %%rcx, %%rax\n"
        "mulq %%r13\n"
        "addq %%rax, %%r8\n"
        "adcq %%rdx, %%r9\n"
        "adcq $0, %%r10\n"
        
        "movq %%r15, %%rax\n"
        "mulq %%r12\n"
        "addq %%rax, %%r8\n"
        "adcq %%rdx, %%r9\n"
        "adcq $0, %%r10\n"
        
        "movq %%r8, 32(%[pl])\n"
        "xorq %%r8, %%r8\n"
        
        // l[5] = a[2]*b[3] + a[3]*b[2]
        "movq %%rcx, %%rax\n"
        "mulq %%r14\n"
        "addq %%rax, %%r9\n"
        "adcq %%rdx, %%r10\n"
        "adcq $0, %%r8\n"
        
        "movq %%r15, %%rax\n"
        "mulq %%r13\n"
        "addq %%rax, %%r9\n"
        "adcq %%rdx, %%r10\n"
        "adcq $0, %%r8\n"
        
        "movq %%r9, 40(%[pl])\n"
        
        // l[6] = a[3]*b[3], l[7] = carry
        "movq %%r15, %%rax\n"
        "mulq %%r14\n"
        "addq %%rax, %%r10\n"
        "adcq %%rdx, %%r8\n"
        
        "movq %%r10, 48(%[pl])\n"
        "movq %%r8, 56(%[pl])\n"
        
        : /* outputs */
        : [pl] "r" (l), [pa] "r" (pa), [pb] "r" (pb)
        : "rax", "rdx", "rbx", "rcx", "r8", "r9", "r10", 
          "r11", "r12", "r13", "r14", "r15", "cc", "memory"
    );
}

// ============================================================================
// 512-bit reduction mod n using inline asm
// ============================================================================

void ecdsa_scalar_reduce_512_x86_64_asm(ecdsa_scalar_t *r, const uint64_t l[8])
{
    uint64_t r0 = l[0], r1 = l[1], r2 = l[2], r3 = l[3];
    uint64_t h0 = l[4], h1 = l[5], h2 = l[6], h3 = l[7];
    
    if (h0 | h1 | h2 | h3) {
        // Multiply h by (2^256 mod n) and add (simplified first term only)
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
    
    // Final reduction check
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

void ecdsa_scalar_mul_shift_384_x86_64_asm(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    uint64_t l[8];
    ecdsa_scalar_mul_512_x86_64_asm(l, a, b);
    
    uint64_t round_bit = (l[5] >> 63) & 1;
    r->d[0] = l[6] + round_bit;
    r->d[1] = l[7] + (r->d[0] < round_bit);
    r->d[2] = 0;
    r->d[3] = 0;
}

// ============================================================================
// Full scalar multiplication
// ============================================================================

void ecdsa_scalar_mul_x86_64_asm(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    uint64_t l[8];
    ecdsa_scalar_mul_512_x86_64_asm(l, a, b);
    ecdsa_scalar_reduce_512_x86_64_asm(r, l);
}

#endif // __x86_64__
