/*
 * Internal ECDSA field arithmetic implementation (mod p)
 * 
 * secp256k1 prime: p = 2^256 - 2^32 - 977
 * 
 * Uses 5x52-bit limb representation on 64-bit platforms.
 * Each limb can exceed 52 bits during computation, normalized before output.
 */

#include "ecdsa_field.h"
#include <string.h>

// =============================================================================
// secp256k1 prime p = 2^256 - 2^32 - 977
// In hex: FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F
// =============================================================================

#ifdef ECDSA_FIELD_52BIT

// 5x52-bit representation of p = 2^256 - 2^32 - 977
// p = n[0] + n[1]*2^52 + n[2]*2^104 + n[3]*2^156 + n[4]*2^208
// p in hex: FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F
// Limb 0 = p & ((1<<52)-1) = 0xFFFFEFFFFFC2F (52 bits)
const ecdsa_field_t ECDSA_FIELD_P = {{
    0xFFFFEFFFFFC2FULL,   // bits 0-51: p & M52
    0xFFFFFFFFFFFFFULL,   // bits 52-103
    0xFFFFFFFFFFFFFULL,   // bits 104-155
    0xFFFFFFFFFFFFFULL,   // bits 156-207
    0xFFFFFFFFFFFFULL     // bits 208-255 (48 bits)
}};

const ecdsa_field_t ECDSA_FIELD_ZERO = {{0, 0, 0, 0, 0}};
const ecdsa_field_t ECDSA_FIELD_ONE = {{1, 0, 0, 0, 0}};

// Masks
#define M52 0xFFFFFFFFFFFFFULL
#define M48 0xFFFFFFFFFFFFULL

void ecdsa_field_clear(ecdsa_field_t *r) {
    r->n[0] = r->n[1] = r->n[2] = r->n[3] = r->n[4] = 0;
}

void ecdsa_field_set_int(ecdsa_field_t *r, int a) {
    r->n[0] = (uint64_t)(a >= 0 ? a : 0);
    r->n[1] = r->n[2] = r->n[3] = r->n[4] = 0;
}

void ecdsa_field_copy(ecdsa_field_t *r, const ecdsa_field_t *a) {
    *r = *a;
}

// Normalize: reduce to canonical form [0, p)
void ecdsa_field_normalize(ecdsa_field_t *r) {
    uint64_t t0 = r->n[0], t1 = r->n[1], t2 = r->n[2], t3 = r->n[3], t4 = r->n[4];
    uint64_t m;
    
    // Reduce carries
    t1 += t0 >> 52; t0 &= M52;
    t2 += t1 >> 52; t1 &= M52;
    t3 += t2 >> 52; t2 &= M52;
    t4 += t3 >> 52; t3 &= M52;
    
    // t4 may exceed 48 bits, reduce mod p
    // If t4 >= 2^48, subtract p (add 2^32 + 977 to low bits)
    m = t4 >> 48;
    t4 &= M48;
    t0 += m * 0x1000003D1ULL;
    
    // Propagate carry again
    t1 += t0 >> 52; t0 &= M52;
    t2 += t1 >> 52; t1 &= M52;
    t3 += t2 >> 52; t2 &= M52;
    t4 += t3 >> 52; t3 &= M52;
    
    // Final reduction: if >= p, subtract p
    // Check if result >= p
    // p.n[0] = 0xFFFFEFFFFFC2F (52 bits)
    m = (t4 == M48) & (t3 == M52) & (t2 == M52) & (t1 == M52) & (t0 >= 0xFFFFEFFFFFC2FULL);
    if (m) {
        t0 -= 0xFFFFEFFFFFC2FULL;
        t1 -= M52 + (t0 >> 63);
        t0 &= M52;
        t2 -= M52 + (t1 >> 63);
        t1 &= M52;
        t3 -= M52 + (t2 >> 63);
        t2 &= M52;
        t4 -= M48 + (t3 >> 63);
        t3 &= M52;
    }
    
    r->n[0] = t0; r->n[1] = t1; r->n[2] = t2; r->n[3] = t3; r->n[4] = t4;
}

// Set from 32-byte big-endian
bool ecdsa_field_set_b32(ecdsa_field_t *r, const uint8_t *a) {
    // Read big-endian into limbs
    r->n[0] = (uint64_t)a[31] | ((uint64_t)a[30] << 8) | ((uint64_t)a[29] << 16) |
              ((uint64_t)a[28] << 24) | ((uint64_t)a[27] << 32) | ((uint64_t)a[26] << 40) |
              ((uint64_t)(a[25] & 0xF) << 48);
    r->n[1] = (uint64_t)(a[25] >> 4) | ((uint64_t)a[24] << 4) | ((uint64_t)a[23] << 12) |
              ((uint64_t)a[22] << 20) | ((uint64_t)a[21] << 28) | ((uint64_t)a[20] << 36) |
              ((uint64_t)a[19] << 44);
    r->n[2] = (uint64_t)a[18] | ((uint64_t)a[17] << 8) | ((uint64_t)a[16] << 16) |
              ((uint64_t)a[15] << 24) | ((uint64_t)a[14] << 32) | ((uint64_t)a[13] << 40) |
              ((uint64_t)(a[12] & 0xF) << 48);
    r->n[3] = (uint64_t)(a[12] >> 4) | ((uint64_t)a[11] << 4) | ((uint64_t)a[10] << 12) |
              ((uint64_t)a[9] << 20) | ((uint64_t)a[8] << 28) | ((uint64_t)a[7] << 36) |
              ((uint64_t)a[6] << 44);
    r->n[4] = (uint64_t)a[5] | ((uint64_t)a[4] << 8) | ((uint64_t)a[3] << 16) |
              ((uint64_t)a[2] << 24) | ((uint64_t)a[1] << 32) | ((uint64_t)a[0] << 40);
    
    // Check overflow (>= p)
    bool overflow = (r->n[4] > M48) ||
                    ((r->n[4] == M48) && (r->n[3] == M52) && (r->n[2] == M52) && 
                     (r->n[1] == M52) && (r->n[0] >= 0xFFFFFEFFFFFC2FULL));
    
    ecdsa_field_normalize(r);
    return !overflow;
}

// Get 32-byte big-endian (assumes normalized)
void ecdsa_field_get_b32(uint8_t *r, const ecdsa_field_t *a) {
    r[31] = a->n[0] & 0xFF;
    r[30] = (a->n[0] >> 8) & 0xFF;
    r[29] = (a->n[0] >> 16) & 0xFF;
    r[28] = (a->n[0] >> 24) & 0xFF;
    r[27] = (a->n[0] >> 32) & 0xFF;
    r[26] = (a->n[0] >> 40) & 0xFF;
    r[25] = ((a->n[0] >> 48) & 0xF) | ((a->n[1] & 0xF) << 4);
    r[24] = (a->n[1] >> 4) & 0xFF;
    r[23] = (a->n[1] >> 12) & 0xFF;
    r[22] = (a->n[1] >> 20) & 0xFF;
    r[21] = (a->n[1] >> 28) & 0xFF;
    r[20] = (a->n[1] >> 36) & 0xFF;
    r[19] = (a->n[1] >> 44) & 0xFF;
    r[18] = a->n[2] & 0xFF;
    r[17] = (a->n[2] >> 8) & 0xFF;
    r[16] = (a->n[2] >> 16) & 0xFF;
    r[15] = (a->n[2] >> 24) & 0xFF;
    r[14] = (a->n[2] >> 32) & 0xFF;
    r[13] = (a->n[2] >> 40) & 0xFF;
    r[12] = ((a->n[2] >> 48) & 0xF) | ((a->n[3] & 0xF) << 4);
    r[11] = (a->n[3] >> 4) & 0xFF;
    r[10] = (a->n[3] >> 12) & 0xFF;
    r[9] = (a->n[3] >> 20) & 0xFF;
    r[8] = (a->n[3] >> 28) & 0xFF;
    r[7] = (a->n[3] >> 36) & 0xFF;
    r[6] = (a->n[3] >> 44) & 0xFF;
    r[5] = a->n[4] & 0xFF;
    r[4] = (a->n[4] >> 8) & 0xFF;
    r[3] = (a->n[4] >> 16) & 0xFF;
    r[2] = (a->n[4] >> 24) & 0xFF;
    r[1] = (a->n[4] >> 32) & 0xFF;
    r[0] = (a->n[4] >> 40) & 0xFF;
}

bool ecdsa_field_is_zero(const ecdsa_field_t *a) {
    return (a->n[0] | a->n[1] | a->n[2] | a->n[3] | a->n[4]) == 0;
}

bool ecdsa_field_is_odd(const ecdsa_field_t *a) {
    return a->n[0] & 1;
}

bool ecdsa_field_equal(const ecdsa_field_t *a, const ecdsa_field_t *b) {
    return (a->n[0] == b->n[0]) && (a->n[1] == b->n[1]) && (a->n[2] == b->n[2]) &&
           (a->n[3] == b->n[3]) && (a->n[4] == b->n[4]);
}

// Negate: r = -a (mod p) = p - a
void ecdsa_field_negate(ecdsa_field_t *r, const ecdsa_field_t *a, int m) {
    (void)m;
    // r = p - a (assuming a is normalized)
    uint64_t t0 = 0xFFFFFEFFFFFC2FULL - a->n[0];
    uint64_t t1 = M52 - a->n[1] - (t0 >> 63);
    t0 &= M52;
    uint64_t t2 = M52 - a->n[2] - (t1 >> 63);
    t1 &= M52;
    uint64_t t3 = M52 - a->n[3] - (t2 >> 63);
    t2 &= M52;
    uint64_t t4 = M48 - a->n[4] - (t3 >> 63);
    t3 &= M52;
    
    r->n[0] = t0; r->n[1] = t1; r->n[2] = t2; r->n[3] = t3; r->n[4] = t4;
}

// Add: r = a + b (mod p)
void ecdsa_field_add(ecdsa_field_t *r, const ecdsa_field_t *a, const ecdsa_field_t *b) {
    r->n[0] = a->n[0] + b->n[0];
    r->n[1] = a->n[1] + b->n[1];
    r->n[2] = a->n[2] + b->n[2];
    r->n[3] = a->n[3] + b->n[3];
    r->n[4] = a->n[4] + b->n[4];
    ecdsa_field_normalize(r);
}

// 128-bit multiplication helper
static inline void mul64(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo) {
#ifdef __SIZEOF_INT128__
    __uint128_t p = (__uint128_t)a * b;
    *lo = (uint64_t)p;
    *hi = (uint64_t)(p >> 64);
#else
    uint64_t a_lo = a & 0xFFFFFFFF;
    uint64_t a_hi = a >> 32;
    uint64_t b_lo = b & 0xFFFFFFFF;
    uint64_t b_hi = b >> 32;
    
    uint64_t p0 = a_lo * b_lo;
    uint64_t p1 = a_lo * b_hi;
    uint64_t p2 = a_hi * b_lo;
    uint64_t p3 = a_hi * b_hi;
    
    uint64_t mid = p1 + (p0 >> 32);
    mid += p2;
    if (mid < p2) p3 += 0x100000000ULL;
    
    *lo = (p0 & 0xFFFFFFFF) | (mid << 32);
    *hi = p3 + (mid >> 32);
#endif
}

// Multiply: r = a * b (mod p)
// Uses schoolbook multiplication with reduction
void ecdsa_field_mul(ecdsa_field_t *r, const ecdsa_field_t *a, const ecdsa_field_t *b) {
#ifdef __SIZEOF_INT128__
    __uint128_t t[10] = {0};
    __uint128_t c;
    
    // Copy inputs in case r aliases a or b
    uint64_t an[5] = {a->n[0], a->n[1], a->n[2], a->n[3], a->n[4]};
    uint64_t bn[5] = {b->n[0], b->n[1], b->n[2], b->n[3], b->n[4]};
    
    // Step 1: Full 10-limb multiplication
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            t[i+j] += (__uint128_t)an[i] * bn[j];
        }
    }
    
    // Step 2: Normalize intermediate result (propagate carries)
    for (int i = 0; i < 9; i++) {
        t[i+1] += t[i] >> 52;
        t[i] &= M52;
    }
    
    // Step 3: Reduce mod p
    // 2^256 ≡ R (mod p) where R = 0x1000003D1
    // We have a 520-bit number in t[0..9], need to reduce to 256 bits
    const uint64_t R = 0x1000003D1ULL;
    
    // First, reduce t[5..9] into t[0..4]
    // t[i] * 2^(52*i) for i >= 5 needs to be reduced
    // 2^(52*5) = 2^260 = 2^256 * 2^4 = R * 16 (mod p)
    // So t[5] contributes t[5] * R * 16 to limb 0 onwards
    // t[6] contributes t[6] * R * 16 to limb 1 onwards (since 2^312 = R * 2^56 = R * 16 * 2^52)
    // etc.
    
    // t[5] * R * 16 goes to limb 0+
    c = t[5] * R * 16;
    t[0] += c & M52; c >>= 52;
    t[1] += c & M52; c >>= 52;
    t[2] += c;
    
    // t[6] * R * 16 goes to limb 1+
    c = t[6] * R * 16;
    t[1] += c & M52; c >>= 52;
    t[2] += c & M52; c >>= 52;
    t[3] += c;
    
    // t[7] * R * 16 goes to limb 2+
    c = t[7] * R * 16;
    t[2] += c & M52; c >>= 52;
    t[3] += c & M52; c >>= 52;
    t[4] += c;
    
    // t[8] * R * 16 goes to limb 3+
    c = t[8] * R * 16;
    t[3] += c & M52; c >>= 52;
    t[4] += c;
    
    // t[9] * R * 16 goes to limb 4
    t[4] += t[9] * R * 16;
    
    // Propagate carries through t[0..4] - multiple passes may be needed
    for (int pass = 0; pass < 3; pass++) {
        c = 0;
        for (int i = 0; i < 4; i++) {
            c += t[i];
            t[i] = c & M52;
            c >>= 52;
        }
        t[4] += c;
        
        // If t[4] overflows 48 bits, reduce again
        if (t[4] >> 48) {
            __uint128_t overflow = t[4] >> 48;
            t[4] &= M48;
            t[0] += overflow * R;
        }
    }
    
    r->n[0] = (uint64_t)t[0]; 
    r->n[1] = (uint64_t)t[1]; 
    r->n[2] = (uint64_t)t[2]; 
    r->n[3] = (uint64_t)t[3]; 
    r->n[4] = (uint64_t)t[4];
    ecdsa_field_normalize(r);
#else
    // Fallback for no __int128
    uint64_t hi, lo;
    uint64_t t[10] = {0};
    
    // Schoolbook multiplication into 10 limbs
    for (int i = 0; i < 5; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < 5; j++) {
            mul64(a->n[i], b->n[j], &hi, &lo);
            t[i+j] += lo + carry;
            carry = hi + (t[i+j] < lo + carry);
        }
        t[i+5] += carry;
    }
    
    // Reduce using 2^256 ≡ 0x1000003D1 (mod p)
    const uint64_t R = 0x1000003D1ULL;
    for (int i = 5; i < 10; i++) {
        mul64(t[i], R, &hi, &lo);
        t[i-5] += lo;
        if (i < 9) t[i-4] += hi;
    }
    
    // Normalize limbs
    r->n[0] = t[0] & M52;
    r->n[1] = (t[1] + (t[0] >> 52)) & M52;
    r->n[2] = (t[2] + (t[1] >> 52)) & M52;
    r->n[3] = (t[3] + (t[2] >> 52)) & M52;
    r->n[4] = t[4] + (t[3] >> 52);
    
    ecdsa_field_normalize(r);
#endif
}

// Square: r = a^2 (mod p)
void ecdsa_field_sqr(ecdsa_field_t *r, const ecdsa_field_t *a) {
    ecdsa_field_mul(r, a, a);
}

// Modular inverse using Fermat's little theorem: a^(-1) = a^(p-2) mod p
// Uses binary exponentiation with p-2 = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2D
void ecdsa_field_inv(ecdsa_field_t *r, const ecdsa_field_t *a) {
    // p - 2 in big-endian bytes
    static const uint8_t p_minus_2[32] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFC, 0x2D
    };
    
    ecdsa_field_t x = *a;
    ecdsa_field_t result;
    ecdsa_field_set_int(&result, 1);
    
    // Binary exponentiation (square-and-multiply)
    for (int i = 0; i < 32; i++) {
        for (int j = 7; j >= 0; j--) {
            ecdsa_field_sqr(&result, &result);
            if ((p_minus_2[i] >> j) & 1) {
                ecdsa_field_mul(&result, &result, &x);
            }
        }
    }
    
    *r = result;
    ecdsa_field_normalize(r);
}

// Square root using Tonelli-Shanks (secp256k1: p ≡ 3 mod 4, so sqrt(a) = a^((p+1)/4))
bool ecdsa_field_sqrt(ecdsa_field_t *r, const ecdsa_field_t *a) {
    ecdsa_field_t x2, x3, x6, x9, x11, x22, x44, x88, x176, x220, x223, t1;
    
    // Compute a^((p+1)/4) using same chain as inv but different final step
    ecdsa_field_sqr(&x2, a);
    ecdsa_field_mul(&x2, &x2, a);
    
    ecdsa_field_sqr(&x3, &x2);
    ecdsa_field_mul(&x3, &x3, a);
    
    ecdsa_field_sqr(&t1, &x3);
    for (int i = 0; i < 2; i++) ecdsa_field_sqr(&t1, &t1);
    ecdsa_field_mul(&x6, &t1, &x3);
    
    ecdsa_field_sqr(&t1, &x6);
    for (int i = 0; i < 2; i++) ecdsa_field_sqr(&t1, &t1);
    ecdsa_field_mul(&x9, &t1, &x3);
    
    ecdsa_field_sqr(&t1, &x9);
    ecdsa_field_sqr(&t1, &t1);
    ecdsa_field_mul(&x11, &t1, &x2);
    
    ecdsa_field_sqr(&t1, &x11);
    for (int i = 0; i < 10; i++) ecdsa_field_sqr(&t1, &t1);
    ecdsa_field_mul(&x22, &t1, &x11);
    
    ecdsa_field_sqr(&t1, &x22);
    for (int i = 0; i < 21; i++) ecdsa_field_sqr(&t1, &t1);
    ecdsa_field_mul(&x44, &t1, &x22);
    
    ecdsa_field_sqr(&t1, &x44);
    for (int i = 0; i < 43; i++) ecdsa_field_sqr(&t1, &t1);
    ecdsa_field_mul(&x88, &t1, &x44);
    
    ecdsa_field_sqr(&t1, &x88);
    for (int i = 0; i < 87; i++) ecdsa_field_sqr(&t1, &t1);
    ecdsa_field_mul(&x176, &t1, &x88);
    
    ecdsa_field_sqr(&t1, &x176);
    for (int i = 0; i < 43; i++) ecdsa_field_sqr(&t1, &t1);
    ecdsa_field_mul(&x220, &t1, &x44);
    
    ecdsa_field_sqr(&t1, &x220);
    for (int i = 0; i < 2; i++) ecdsa_field_sqr(&t1, &t1);
    ecdsa_field_mul(&x223, &t1, &x3);
    
    // For sqrt: (p+1)/4 = 2^254 - 2^30 - 2^4 - 2^2 - 1 ... different chain
    // Actually for secp256k1: sqrt(a) = a^((p+1)/4)
    // (p+1)/4 in binary ends with ...0001
    for (int i = 0; i < 23; i++) ecdsa_field_sqr(&t1, &x223);
    ecdsa_field_mul(&t1, &t1, &x22);
    for (int i = 0; i < 6; i++) ecdsa_field_sqr(&t1, &t1);
    ecdsa_field_mul(&t1, &t1, &x2);
    ecdsa_field_sqr(&t1, &t1);
    ecdsa_field_sqr(&t1, &t1);
    ecdsa_field_mul(r, &t1, a);
    
    // Verify: r^2 == a
    ecdsa_field_t check;
    ecdsa_field_sqr(&check, r);
    ecdsa_field_normalize(&check);
    
    ecdsa_field_t a_norm = *a;
    ecdsa_field_normalize(&a_norm);
    
    return ecdsa_field_equal(&check, &a_norm);
}

#else // ECDSA_FIELD_26BIT (32-bit platforms)

// 32-bit platforms use 10x26-bit limbs for field elements
// This implementation is provided for completeness but 64-bit is preferred
#error "32-bit field implementation requires manual porting - contact developers"

#endif
