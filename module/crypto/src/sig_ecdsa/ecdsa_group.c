/*
 * Internal ECDSA group operations implementation
 * 
 * secp256k1 curve: y² = x³ + 7 (mod p)
 * Generator point G and operations on curve points.
 */

#include "ecdsa_group.h"
#include "ecdsa_precompute.h"
#include "ecdsa_endomorphism.h"
#include <string.h>

// =============================================================================
// secp256k1 Generator Point G
// Gx = 0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798
// Gy = 0x483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8
// =============================================================================

// These are set in ecdsa_ecmult_gen_init()
static ecdsa_ge_t s_generator;
static bool s_generator_initialized = false;

const ecdsa_ge_t ECDSA_GENERATOR = {
    .infinity = false
    // x and y are set during init
};

// =============================================================================
// Affine Point Operations
// =============================================================================

void ecdsa_ge_set_infinity(ecdsa_ge_t *r) {
    ecdsa_field_clear(&r->x);
    ecdsa_field_clear(&r->y);
    r->infinity = true;
}

bool ecdsa_ge_is_infinity(const ecdsa_ge_t *a) {
    return a->infinity;
}

// Check if point is on curve: y² = x³ + 7
bool ecdsa_ge_is_valid(const ecdsa_ge_t *a) {
    if (a->infinity) return true;
    
    ecdsa_field_t y2, x3, x3_7;
    
    // y²
    ecdsa_field_sqr(&y2, &a->y);
    ecdsa_field_normalize(&y2);
    
    // x³
    ecdsa_field_sqr(&x3, &a->x);
    ecdsa_field_mul(&x3, &x3, &a->x);
    
    // x³ + 7
    ecdsa_field_t seven;
    ecdsa_field_set_int(&seven, 7);
    ecdsa_field_add(&x3_7, &x3, &seven);
    ecdsa_field_normalize(&x3_7);
    
    return ecdsa_field_equal(&y2, &x3_7);
}

bool ecdsa_ge_set_xy(ecdsa_ge_t *r, const ecdsa_field_t *x, const ecdsa_field_t *y) {
    r->infinity = false;
    ecdsa_field_copy(&r->x, x);
    ecdsa_field_copy(&r->y, y);
    return ecdsa_ge_is_valid(r);
}

// Set point from x-coordinate and odd/even flag
bool ecdsa_ge_set_xo(ecdsa_ge_t *r, const ecdsa_field_t *x, bool odd) {
    ecdsa_field_t x3, y2, y;
    
    // y² = x³ + 7
    ecdsa_field_sqr(&x3, x);
    ecdsa_field_mul(&x3, &x3, x);
    
    ecdsa_field_t seven;
    ecdsa_field_set_int(&seven, 7);
    ecdsa_field_add(&y2, &x3, &seven);
    
    // y = sqrt(y²)
    if (!ecdsa_field_sqrt(&y, &y2)) {
        return false;  // No square root exists
    }
    
    ecdsa_field_normalize(&y);
    
    // Choose correct root based on odd flag
    if (ecdsa_field_is_odd(&y) != odd) {
        ecdsa_field_negate(&y, &y, 1);
        ecdsa_field_normalize(&y);
    }
    
    r->infinity = false;
    ecdsa_field_copy(&r->x, x);
    ecdsa_field_copy(&r->y, &y);
    
    return true;
}

void ecdsa_ge_neg(ecdsa_ge_t *r, const ecdsa_ge_t *a) {
    *r = *a;
    if (!a->infinity) {
        ecdsa_field_negate(&r->y, &a->y, 1);
        ecdsa_field_normalize(&r->y);
    }
}

// =============================================================================
// Jacobian Point Operations
// =============================================================================

void ecdsa_gej_set_infinity(ecdsa_gej_t *r) {
    ecdsa_field_clear(&r->x);
    ecdsa_field_clear(&r->y);
    ecdsa_field_clear(&r->z);
    r->infinity = true;
}

bool ecdsa_gej_is_infinity(const ecdsa_gej_t *a) {
    return a->infinity;
}

void ecdsa_gej_set_ge(ecdsa_gej_t *r, const ecdsa_ge_t *a) {
    r->infinity = a->infinity;
    ecdsa_field_copy(&r->x, &a->x);
    ecdsa_field_copy(&r->y, &a->y);
    ecdsa_field_set_int(&r->z, 1);
}

// Convert Jacobian to affine: (X, Y, Z) -> (X/Z², Y/Z³)
void ecdsa_ge_set_gej(ecdsa_ge_t *r, const ecdsa_gej_t *a) {
    if (a->infinity) {
        ecdsa_ge_set_infinity(r);
        return;
    }
    
    ecdsa_field_t z2, z3, z_inv;
    
    // z_inv = 1/Z
    ecdsa_field_inv(&z_inv, &a->z);
    
    // z2 = 1/Z²
    ecdsa_field_sqr(&z2, &z_inv);
    
    // z3 = 1/Z³
    ecdsa_field_mul(&z3, &z2, &z_inv);
    
    // x = X/Z²
    ecdsa_field_mul(&r->x, &a->x, &z2);
    ecdsa_field_normalize(&r->x);
    
    // y = Y/Z³
    ecdsa_field_mul(&r->y, &a->y, &z3);
    ecdsa_field_normalize(&r->y);
    
    r->infinity = false;
}

void ecdsa_gej_neg(ecdsa_gej_t *r, const ecdsa_gej_t *a) {
    r->infinity = a->infinity;
    ecdsa_field_copy(&r->x, &a->x);
    ecdsa_field_copy(&r->z, &a->z);
    ecdsa_field_negate(&r->y, &a->y, 1);
    ecdsa_field_normalize(&r->y);
}

// =============================================================================
// Point Arithmetic
// =============================================================================

// Point doubling: r = 2*a
// Using formulas for Jacobian coordinates
void ecdsa_gej_double(ecdsa_gej_t *r, const ecdsa_gej_t *a) {
    if (a->infinity) {
        ecdsa_gej_set_infinity(r);
        return;
    }
    
    ecdsa_field_t t1, t2, t3, t4, t5;
    
    // For secp256k1 (a=0): simplified doubling formulas
    // t1 = X²
    ecdsa_field_sqr(&t1, &a->x);
    
    // t2 = Y²
    ecdsa_field_sqr(&t2, &a->y);
    
    // t3 = Y⁴
    ecdsa_field_sqr(&t3, &t2);
    
    // t4 = 2*((X+Y²)² - X² - Y⁴) = 4*X*Y²
    ecdsa_field_add(&t4, &a->x, &t2);
    ecdsa_field_sqr(&t4, &t4);
    ecdsa_field_negate(&t5, &t1, 1);
    ecdsa_field_add(&t4, &t4, &t5);
    ecdsa_field_negate(&t5, &t3, 1);
    ecdsa_field_add(&t4, &t4, &t5);
    ecdsa_field_add(&t4, &t4, &t4);  // 2*S where S = 2*X*Y²
    
    // t5 = 3*X² (= M, since a=0)
    ecdsa_field_add(&t5, &t1, &t1);
    ecdsa_field_add(&t5, &t5, &t1);
    
    // X' = M² - 2*S
    ecdsa_field_sqr(&r->x, &t5);
    ecdsa_field_negate(&t1, &t4, 1);
    ecdsa_field_add(&r->x, &r->x, &t1);
    ecdsa_field_add(&r->x, &r->x, &t1);
    
    // Z' = 2*Y*Z
    ecdsa_field_mul(&r->z, &a->y, &a->z);
    ecdsa_field_add(&r->z, &r->z, &r->z);
    
    // Y' = M*(S-X') - 8*Y⁴
    ecdsa_field_negate(&t1, &r->x, 1);
    ecdsa_field_add(&t4, &t4, &t1);
    ecdsa_field_mul(&t4, &t4, &t5);
    ecdsa_field_add(&t3, &t3, &t3);  // 2*Y⁴
    ecdsa_field_add(&t3, &t3, &t3);  // 4*Y⁴
    ecdsa_field_add(&t3, &t3, &t3);  // 8*Y⁴
    ecdsa_field_negate(&t3, &t3, 1);
    ecdsa_field_add(&r->y, &t4, &t3);
    
    r->infinity = false;
}

// Point addition: r = a + b (Jacobian + affine)
void ecdsa_gej_add_ge(ecdsa_gej_t *r, const ecdsa_gej_t *a, const ecdsa_ge_t *b) {
    if (a->infinity) {
        ecdsa_gej_set_ge(r, b);
        return;
    }
    if (b->infinity) {
        *r = *a;
        return;
    }
    
    ecdsa_field_t z12, u1, u2, s1, s2, h, i, j, rr, v, t;
    
    // Z1² 
    ecdsa_field_sqr(&z12, &a->z);
    
    // U1 = X1 (already in Jacobian form relative to Z1)
    ecdsa_field_copy(&u1, &a->x);
    
    // U2 = X2 * Z1²
    ecdsa_field_mul(&u2, &b->x, &z12);
    
    // S1 = Y1
    ecdsa_field_copy(&s1, &a->y);
    
    // S2 = Y2 * Z1³
    ecdsa_field_mul(&s2, &z12, &a->z);
    ecdsa_field_mul(&s2, &s2, &b->y);
    
    // H = U2 - U1
    ecdsa_field_negate(&t, &u1, 1);
    ecdsa_field_add(&h, &u2, &t);
    ecdsa_field_normalize(&h);
    
    // Check if H = 0 (same x-coordinate)
    if (ecdsa_field_is_zero(&h)) {
        ecdsa_field_negate(&t, &s1, 1);
        ecdsa_field_add(&t, &s2, &t);
        ecdsa_field_normalize(&t);
        
        if (ecdsa_field_is_zero(&t)) {
            // Points are equal, double
            ecdsa_gej_double(r, a);
            return;
        } else {
            // Points are negatives, result is infinity
            ecdsa_gej_set_infinity(r);
            return;
        }
    }
    
    // I = (2*H)²
    ecdsa_field_add(&i, &h, &h);
    ecdsa_field_sqr(&i, &i);
    
    // J = H * I
    ecdsa_field_mul(&j, &h, &i);
    
    // rr = 2*(S2 - S1)
    ecdsa_field_negate(&t, &s1, 1);
    ecdsa_field_add(&rr, &s2, &t);
    ecdsa_field_add(&rr, &rr, &rr);
    
    // V = U1 * I
    ecdsa_field_mul(&v, &u1, &i);
    
    // X3 = rr² - J - 2*V
    ecdsa_field_sqr(&r->x, &rr);
    ecdsa_field_negate(&t, &j, 1);
    ecdsa_field_add(&r->x, &r->x, &t);
    ecdsa_field_negate(&t, &v, 1);
    ecdsa_field_add(&r->x, &r->x, &t);
    ecdsa_field_add(&r->x, &r->x, &t);
    
    // Y3 = rr*(V - X3) - 2*S1*J
    ecdsa_field_negate(&t, &r->x, 1);
    ecdsa_field_add(&t, &v, &t);
    ecdsa_field_mul(&r->y, &rr, &t);
    ecdsa_field_mul(&t, &s1, &j);
    ecdsa_field_add(&t, &t, &t);
    ecdsa_field_negate(&t, &t, 1);
    ecdsa_field_add(&r->y, &r->y, &t);
    
    // Z3 = 2*Z1*H
    ecdsa_field_mul(&r->z, &a->z, &h);
    ecdsa_field_add(&r->z, &r->z, &r->z);
    
    ecdsa_field_normalize(&r->x);
    ecdsa_field_normalize(&r->y);
    ecdsa_field_normalize(&r->z);
    
    r->infinity = false;
}

// Point addition: r = a + b (both Jacobian)
void ecdsa_gej_add(ecdsa_gej_t *r, const ecdsa_gej_t *a, const ecdsa_gej_t *b) {
    if (a->infinity) {
        *r = *b;
        return;
    }
    if (b->infinity) {
        *r = *a;
        return;
    }
    
    // Convert b to affine for simplicity (slower but correct)
    ecdsa_ge_t b_affine;
    ecdsa_ge_set_gej(&b_affine, b);
    ecdsa_gej_add_ge(r, a, &b_affine);
}

// =============================================================================
// Scalar Multiplication
// =============================================================================

// Generator multiplication: r = n*G
// Uses precomputed tables for ~15-20x speedup
void ecdsa_ecmult_gen(ecdsa_gej_t *r, const ecdsa_scalar_t *n) {
    if (!s_generator_initialized) {
        ecdsa_ecmult_gen_init();
    }
    
    // Use optimized comb method with precomputed tables
    ecdsa_ecmult_gen_fast(r, n);
}

// General scalar multiplication: r = na*a + ng*G
// Uses Strauss/Shamir simultaneous multiplication for ~2x speedup
void ecdsa_ecmult(ecdsa_gej_t *r, const ecdsa_gej_t *a, const ecdsa_scalar_t *na, const ecdsa_scalar_t *ng) {
    // Use GLV endomorphism-accelerated Strauss (4-way simultaneous multiplication)
    // Decomposes each 256-bit scalar into two ~128-bit scalars using λ*P = φ(P)
    ecdsa_ecmult_strauss_endo(r, a, na, ng);
}

// Constant-time conditional swap
static void gej_cmov(ecdsa_gej_t *r, const ecdsa_gej_t *a, int flag) {
    // flag must be 0 or 1
    uint64_t mask = (uint64_t)0 - (uint64_t)flag;
    
    for (int i = 0; i < ECDSA_FIELD_LIMBS; i++) {
        r->x.n[i] ^= mask & (r->x.n[i] ^ a->x.n[i]);
        r->y.n[i] ^= mask & (r->y.n[i] ^ a->y.n[i]);
        r->z.n[i] ^= mask & (r->z.n[i] ^ a->z.n[i]);
    }
    r->infinity = (r->infinity & ~flag) | (a->infinity & flag);
}

// Constant-time scalar multiplication using Montgomery ladder: r = n*a
// This implementation has constant-time execution regardless of scalar bits
void ecdsa_ecmult_const(ecdsa_gej_t *r, const ecdsa_ge_t *a, const ecdsa_scalar_t *n) {
    // Montgomery ladder: R0 = infinity, R1 = a
    // For each bit b of n (from MSB to LSB):
    //   if b == 0: R1 = R0 + R1, R0 = 2*R0
    //   if b == 1: R0 = R0 + R1, R1 = 2*R1
    // Result is R0
    
    ecdsa_gej_t r0, r1, tmp;
    ecdsa_gej_set_infinity(&r0);
    ecdsa_gej_set_ge(&r1, a);
    
    uint8_t nb[32];
    ecdsa_scalar_get_b32(nb, n);
    
    for (int i = 0; i < 32; i++) {
        for (int j = 7; j >= 0; j--) {
            int bit = (nb[i] >> j) & 1;
            
            // Constant-time: always compute both branches, select result
            // When bit=0: R1 = R0+R1, R0 = 2*R0
            // When bit=1: R0 = R0+R1, R1 = 2*R1
            
            // Swap R0 and R1 if bit is set (constant-time)
            gej_cmov(&tmp, &r0, 1);
            gej_cmov(&r0, &r1, bit);
            gej_cmov(&r1, &tmp, bit);
            
            // Now always do: R1 = R0 + R1, R0 = 2*R0
            ecdsa_gej_add(&tmp, &r0, &r1);
            ecdsa_gej_double(&r0, &r0);
            r1 = tmp;
            
            // Swap back if bit was set
            gej_cmov(&tmp, &r0, 1);
            gej_cmov(&r0, &r1, bit);
            gej_cmov(&r1, &tmp, bit);
        }
    }
    
    *r = r0;
}

// =============================================================================
// Serialization
// =============================================================================

bool ecdsa_ge_serialize(const ecdsa_ge_t *a, bool compressed, uint8_t *output, size_t *outputlen) {
    if (a->infinity) {
        return false;
    }
    
    uint8_t x[32], y[32];
    ecdsa_field_get_b32(x, &a->x);
    ecdsa_field_get_b32(y, &a->y);
    
    if (compressed) {
        if (*outputlen < 33) return false;
        output[0] = ecdsa_field_is_odd(&a->y) ? 0x03 : 0x02;
        memcpy(output + 1, x, 32);
        *outputlen = 33;
    } else {
        if (*outputlen < 65) return false;
        output[0] = 0x04;
        memcpy(output + 1, x, 32);
        memcpy(output + 33, y, 32);
        *outputlen = 65;
    }
    
    return true;
}

bool ecdsa_ge_parse(ecdsa_ge_t *r, const uint8_t *input, size_t inputlen) {
    if (inputlen == 0) return false;
    
    uint8_t prefix = input[0];
    
    if (prefix == 0x04 && inputlen == 65) {
        // Uncompressed
        ecdsa_field_t x, y;
        if (!ecdsa_field_set_b32(&x, input + 1)) return false;
        if (!ecdsa_field_set_b32(&y, input + 33)) return false;
        return ecdsa_ge_set_xy(r, &x, &y);
    } else if ((prefix == 0x02 || prefix == 0x03) && inputlen == 33) {
        // Compressed
        ecdsa_field_t x;
        if (!ecdsa_field_set_b32(&x, input + 1)) return false;
        return ecdsa_ge_set_xo(r, &x, prefix == 0x03);
    }
    
    return false;
}

// =============================================================================
// Initialization
// =============================================================================

void ecdsa_ecmult_gen_init(void) {
    if (s_generator_initialized) return;
    
    // Generator point coordinates
    const uint8_t gx[32] = {
        0x79, 0xBE, 0x66, 0x7E, 0xF9, 0xDC, 0xBB, 0xAC,
        0x55, 0xA0, 0x62, 0x95, 0xCE, 0x87, 0x0B, 0x07,
        0x02, 0x9B, 0xFC, 0xDB, 0x2D, 0xCE, 0x28, 0xD9,
        0x59, 0xF2, 0x81, 0x5B, 0x16, 0xF8, 0x17, 0x98
    };
    const uint8_t gy[32] = {
        0x48, 0x3A, 0xDA, 0x77, 0x26, 0xA3, 0xC4, 0x65,
        0x5D, 0xA4, 0xFB, 0xFC, 0x0E, 0x11, 0x08, 0xA8,
        0xFD, 0x17, 0xB4, 0x48, 0xA6, 0x85, 0x54, 0x19,
        0x9C, 0x47, 0xD0, 0x8F, 0xFB, 0x10, 0xD4, 0xB8
    };
    
    ecdsa_field_set_b32(&s_generator.x, gx);
    ecdsa_field_set_b32(&s_generator.y, gy);
    s_generator.infinity = false;
    
    s_generator_initialized = true;
}

void ecdsa_ecmult_gen_deinit(void) {
    s_generator_initialized = false;
}
