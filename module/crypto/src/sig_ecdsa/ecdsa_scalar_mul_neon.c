/*
 * ARM64 NEON secp256k1 Scalar Multiplication
 * Uses MUL/UMULH instructions for 64-bit multiply-high
 */

#if defined(__aarch64__) || defined(_M_ARM64)

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
// 256x256 -> 512 bit multiplication using ARM64 MUL/UMULH
// MUL: dst = (src1 * src2)[63:0]   (low 64 bits)
// UMULH: dst = (src1 * src2)[127:64] (high 64 bits)
// ============================================================================

void ecdsa_scalar_mul_512_neon(uint64_t l[8], const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    const uint64_t *pa = a->d;
    const uint64_t *pb = b->d;
    
    __asm__ __volatile__(
        // Load a[0..3] into x4-x7
        "ldp x4, x5, [%[pa]]\n"
        "ldp x6, x7, [%[pa], #16]\n"
        // Load b[0..3] into x8-x11
        "ldp x8, x9, [%[pb]]\n"
        "ldp x10, x11, [%[pb], #16]\n"
        
        // === Column 0: l[0] ===
        "mul x12, x4, x8\n"        // a[0]*b[0] low
        "umulh x13, x4, x8\n"      // a[0]*b[0] high
        "str x12, [%[pl]]\n"       // l[0]
        
        // === Column 1: l[1] ===
        "mul x12, x4, x9\n"        // a[0]*b[1] low
        "umulh x14, x4, x9\n"      // a[0]*b[1] high
        "adds x13, x13, x12\n"
        "adc x14, x14, xzr\n"
        
        "mul x12, x5, x8\n"        // a[1]*b[0] low
        "umulh x15, x5, x8\n"      // a[1]*b[0] high
        "adds x13, x13, x12\n"
        "adcs x14, x14, x15\n"
        "adc x16, xzr, xzr\n"
        "str x13, [%[pl], #8]\n"   // l[1]
        
        // === Column 2: l[2] ===
        "mul x12, x4, x10\n"       // a[0]*b[2] low
        "umulh x13, x4, x10\n"     // a[0]*b[2] high
        "adds x14, x14, x12\n"
        "adcs x16, x16, x13\n"
        "adc x17, xzr, xzr\n"
        
        "mul x12, x5, x9\n"        // a[1]*b[1] low
        "umulh x13, x5, x9\n"      // a[1]*b[1] high
        "adds x14, x14, x12\n"
        "adcs x16, x16, x13\n"
        "adc x17, x17, xzr\n"
        
        "mul x12, x6, x8\n"        // a[2]*b[0] low
        "umulh x13, x6, x8\n"      // a[2]*b[0] high
        "adds x14, x14, x12\n"
        "adcs x16, x16, x13\n"
        "adc x17, x17, xzr\n"
        "str x14, [%[pl], #16]\n"  // l[2]
        
        // === Column 3: l[3] ===
        "mul x12, x4, x11\n"       // a[0]*b[3] low
        "umulh x13, x4, x11\n"     // a[0]*b[3] high
        "adds x16, x16, x12\n"
        "adcs x17, x17, x13\n"
        "adc x14, xzr, xzr\n"
        
        "mul x12, x5, x10\n"       // a[1]*b[2] low
        "umulh x13, x5, x10\n"     // a[1]*b[2] high
        "adds x16, x16, x12\n"
        "adcs x17, x17, x13\n"
        "adc x14, x14, xzr\n"
        
        "mul x12, x6, x9\n"        // a[2]*b[1] low
        "umulh x13, x6, x9\n"      // a[2]*b[1] high
        "adds x16, x16, x12\n"
        "adcs x17, x17, x13\n"
        "adc x14, x14, xzr\n"
        
        "mul x12, x7, x8\n"        // a[3]*b[0] low
        "umulh x13, x7, x8\n"      // a[3]*b[0] high
        "adds x16, x16, x12\n"
        "adcs x17, x17, x13\n"
        "adc x14, x14, xzr\n"
        "str x16, [%[pl], #24]\n"  // l[3]
        
        // === Column 4: l[4] ===
        "mul x12, x5, x11\n"       // a[1]*b[3] low
        "umulh x13, x5, x11\n"     // a[1]*b[3] high
        "adds x17, x17, x12\n"
        "adcs x14, x14, x13\n"
        "adc x15, xzr, xzr\n"
        
        "mul x12, x6, x10\n"       // a[2]*b[2] low
        "umulh x13, x6, x10\n"     // a[2]*b[2] high
        "adds x17, x17, x12\n"
        "adcs x14, x14, x13\n"
        "adc x15, x15, xzr\n"
        
        "mul x12, x7, x9\n"        // a[3]*b[1] low
        "umulh x13, x7, x9\n"      // a[3]*b[1] high
        "adds x17, x17, x12\n"
        "adcs x14, x14, x13\n"
        "adc x15, x15, xzr\n"
        "str x17, [%[pl], #32]\n"  // l[4]
        
        // === Column 5: l[5] ===
        "mul x12, x6, x11\n"       // a[2]*b[3] low
        "umulh x13, x6, x11\n"     // a[2]*b[3] high
        "adds x14, x14, x12\n"
        "adcs x15, x15, x13\n"
        "adc x16, xzr, xzr\n"
        
        "mul x12, x7, x10\n"       // a[3]*b[2] low
        "umulh x13, x7, x10\n"     // a[3]*b[2] high
        "adds x14, x14, x12\n"
        "adcs x15, x15, x13\n"
        "adc x16, x16, xzr\n"
        "str x14, [%[pl], #40]\n"  // l[5]
        
        // === Column 6-7: l[6], l[7] ===
        "mul x12, x7, x11\n"       // a[3]*b[3] low
        "umulh x13, x7, x11\n"     // a[3]*b[3] high
        "adds x15, x15, x12\n"
        "adc x16, x16, x13\n"
        "stp x15, x16, [%[pl], #48]\n" // l[6], l[7]
        
        : /* outputs */
        : [pl] "r" (l), [pa] "r" (pa), [pb] "r" (pb)
        : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
          "x12", "x13", "x14", "x15", "x16", "x17", "cc", "memory"
    );
}

// ============================================================================
// 512-bit reduction mod n
// ============================================================================

void ecdsa_scalar_reduce_512_neon(ecdsa_scalar_t *r, const uint64_t l[8])
{
    uint64_t r0 = l[0], r1 = l[1], r2 = l[2], r3 = l[3];
    uint64_t h0 = l[4], h1 = l[5], h2 = l[6], h3 = l[7];
    
    if (h0 | h1 | h2 | h3) {
        __asm__ __volatile__(
            "mul x12, %[h0], %[c0]\n"
            "umulh x13, %[h0], %[c0]\n"
            "adds %[r0], %[r0], x12\n"
            "adcs %[r1], %[r1], x13\n"
            "adcs %[r2], %[r2], xzr\n"
            "adc %[r3], %[r3], xzr\n"
            : [r0] "+r" (r0), [r1] "+r" (r1), [r2] "+r" (r2), [r3] "+r" (r3)
            : [h0] "r" (h0), [c0] "r" (SCALAR_2P256_MOD_N[0])
            : "x12", "x13", "cc"
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
            "subs %[r0], %[r0], %[n0]\n"
            "sbcs %[r1], %[r1], %[n1]\n"
            "sbcs %[r2], %[r2], %[n2]\n"
            "sbc %[r3], %[r3], %[n3]\n"
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

void ecdsa_scalar_mul_shift_384_neon(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    uint64_t l[8];
    ecdsa_scalar_mul_512_neon(l, a, b);
    
    uint64_t round_bit = (l[5] >> 63) & 1;
    r->d[0] = l[6] + round_bit;
    r->d[1] = l[7] + (r->d[0] < round_bit);
    r->d[2] = 0;
    r->d[3] = 0;
}

// ============================================================================
// Full scalar multiplication
// ============================================================================

void ecdsa_scalar_mul_neon(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    uint64_t l[8];
    ecdsa_scalar_mul_512_neon(l, a, b);
    ecdsa_scalar_reduce_512_neon(r, l);
}

#endif // __aarch64__
