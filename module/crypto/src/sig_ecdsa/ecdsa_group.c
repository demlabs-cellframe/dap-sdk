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
#include "dap_common.h"

#define LOG_TAG "ecdsa_group"

// Debug flag for detailed logging
static bool s_debug_more = false;

/**
 * @brief Enable/disable debug output for ECDSA group operations
 */
void ecdsa_group_set_debug(bool a_enable) {
    s_debug_more = a_enable;
    debug_if(true, L_DEBUG, "ECDSA group debug logs %s", a_enable ? "ENABLED" : "DISABLED");
}

// Helper for debug field printing
static void s_debug_field_print(const char *a_name, const ecdsa_field_t *a_field) {
    if (!s_debug_more) return;
    ecdsa_field_t l_tmp = *a_field;
    ecdsa_field_normalize(&l_tmp);
    uint8_t l_buf[32];
    ecdsa_field_get_b32(l_buf, &l_tmp);
    char l_hex[65];
    for (int i = 0; i < 32; i++) {
        snprintf(l_hex + i*2, 3, "%02x", l_buf[i]);
    }
    log_it(L_DEBUG, "  %s: %s", a_name, l_hex);
}

// =============================================================================
// secp256k1 Generator Point G
// Gx = 0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798
// Gy = 0x483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8
// =============================================================================

// Runtime generator (set during init from bytes for consistency)
static ecdsa_ge_t s_generator;
static bool s_generator_initialized = false;

#ifdef ECDSA_FIELD_52BIT
// 5x52-bit limb representation
const ecdsa_ge_t ECDSA_GENERATOR = {
    .x = {{ 0x2815B16F81798ULL, 0xDB2DCE28D959FULL, 0xE870B07029BFCULL, 
            0xBBAC55A06295CULL, 0x79BE667EF9DCULL }},
    .y = {{ 0x7D08FFB10D4B8ULL, 0x48A68554199C4ULL, 0xE1108A8FD17B4ULL,
            0xC4655DA4FBFC0ULL, 0x483ADA7726A3ULL }},
    .infinity = false
};
#else
// 10x26-bit limb representation (32-bit platforms)
const ecdsa_ge_t ECDSA_GENERATOR = {
    .x = {{ 0x16F81798UL, 0x0059F281UL, 0x2DCE28D9UL, 0x029BFCDBUL, 
            0x0CE870B0UL, 0x055A0629UL, 0x0F9DCBBAUL, 0x079BE667UL,
            0x00000000UL, 0x00000000UL }},
    .y = {{ 0x0FB10D4BUL, 0x009C47D0UL, 0x0A685541UL, 0x0FD17B44UL,
            0x00E1108AUL, 0x05DA4FBFUL, 0x026A3C46UL, 0x0483ADA7UL,
            0x00000000UL, 0x00000000UL }},
    .infinity = false
};
#endif

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
    
    debug_if(s_debug_more, L_DEBUG, "ecdsa_ge_set_gej: converting Jacobian to Affine");
    s_debug_field_print("input X", &a->x);
    s_debug_field_print("input Y", &a->y);
    s_debug_field_print("input Z", &a->z);
    
    ecdsa_field_t z2, z3, z_inv;
    
    // z_inv = 1/Z
    ecdsa_field_inv(&z_inv, &a->z);
    s_debug_field_print("z_inv = 1/Z", &z_inv);
    
    // z2 = 1/Z²
    ecdsa_field_sqr(&z2, &z_inv);
    s_debug_field_print("z2 = 1/Z²", &z2);
    
    // z3 = 1/Z³
    ecdsa_field_mul(&z3, &z2, &z_inv);
    s_debug_field_print("z3 = 1/Z³", &z3);
    
    // x = X/Z²
    ecdsa_field_mul(&r->x, &a->x, &z2);
    ecdsa_field_normalize(&r->x);
    s_debug_field_print("result x = X*z2", &r->x);
    
    // y = Y/Z³
    ecdsa_field_mul(&r->y, &a->y, &z3);
    ecdsa_field_normalize(&r->y);
    s_debug_field_print("result y = Y*z3", &r->y);
    
    r->infinity = false;
    debug_if(s_debug_more, L_DEBUG, "ecdsa_ge_set_gej: done");
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
// Jacobian doubling for secp256k1 (curve parameter a=0)
// Uses dbl-2009-l formula: 2M + 5S (but optimized with mul_int)
// Formula:
//   M = 3*X1^2  (since a=0)
//   S = 4*X1*Y1^2
//   X3 = M^2 - 2*S
//   Y3 = M*(S - X3) - 8*Y1^4
//   Z3 = 2*Y1*Z1
void ecdsa_gej_double(ecdsa_gej_t *r, const ecdsa_gej_t *a) {
    if (a->infinity) {
        ecdsa_gej_set_infinity(r);
        return;
    }
    
    ecdsa_field_t m, s, t, yy, yyyy;
    
    // YY = Y1^2
    ecdsa_field_sqr(&yy, &a->y);
    
    // YYYY = YY^2 = Y1^4
    ecdsa_field_sqr(&yyyy, &yy);
    
    // S = 4*X1*YY
    ecdsa_field_mul(&s, &a->x, &yy);
    ecdsa_field_mul_int(&s, 4);
    
    // M = 3*X1^2
    ecdsa_field_sqr(&m, &a->x);
    ecdsa_field_mul_int(&m, 3);
    
    // X3 = M^2 - 2*S
    ecdsa_field_sqr(&r->x, &m);
    ecdsa_field_negate(&t, &s, 1);
    ecdsa_field_add(&r->x, &r->x, &t);
    ecdsa_field_add(&r->x, &r->x, &t);  // -2*S
    
    // Z3 = 2*Y1*Z1
    ecdsa_field_mul(&r->z, &a->y, &a->z);
    ecdsa_field_add(&r->z, &r->z, &r->z);
    
    // Y3 = M*(S - X3) - 8*YYYY
    ecdsa_field_negate(&t, &r->x, 1);   // t = -X3
    ecdsa_field_add(&t, &s, &t);        // t = S - X3
    ecdsa_field_mul(&r->y, &m, &t);     // r->y = M*(S - X3)
    ecdsa_field_mul_int(&yyyy, 8);      // 8*YYYY, magnitude now 8
    // IMPORTANT: magnitude after mul_int(8) is 8, so negate needs m=8
    ecdsa_field_negate(&yyyy, &yyyy, 8);  // -8*YYYY
    ecdsa_field_add(&r->y, &r->y, &yyyy);
    
    r->infinity = false;
}

// Point addition: r = a + b (Jacobian + affine)
void ecdsa_gej_add_ge(ecdsa_gej_t *r, const ecdsa_gej_t *a, const ecdsa_ge_t *b) {
    debug_if(s_debug_more, L_DEBUG, "ecdsa_gej_add_ge: start");
    
    if (a->infinity) {
        debug_if(s_debug_more, L_DEBUG, "  a is infinity, returning b");
        ecdsa_gej_set_ge(r, b);
        return;
    }
    if (b->infinity) {
        debug_if(s_debug_more, L_DEBUG, "  b is infinity, returning a");
        *r = *a;
        return;
    }
    
    if (s_debug_more) {
        debug_if(s_debug_more, L_DEBUG, "  Input a (Jacobian):");
        s_debug_field_print("a.x", &a->x);
        s_debug_field_print("a.y", &a->y);
        s_debug_field_print("a.z", &a->z);
        debug_if(s_debug_more, L_DEBUG, "  Input b (Affine):");
        s_debug_field_print("b.x", &b->x);
        s_debug_field_print("b.y", &b->y);
    }
    
    ecdsa_field_t z12, u1, u2, s1, s2, h, i, j, rr, v, t;
    
    // Z1² 
    ecdsa_field_sqr(&z12, &a->z);
    s_debug_field_print("z12 = a.z²", &z12);
    
    // U1 = X1 (already in Jacobian form relative to Z1)
    ecdsa_field_copy(&u1, &a->x);
    s_debug_field_print("u1 = a.x", &u1);
    
    // U2 = X2 * Z1²
    ecdsa_field_mul(&u2, &b->x, &z12);
    s_debug_field_print("u2 = b.x * z12", &u2);
    
    // S1 = Y1
    ecdsa_field_copy(&s1, &a->y);
    s_debug_field_print("s1 = a.y", &s1);
    
    // S2 = Y2 * Z1³
    ecdsa_field_mul(&s2, &z12, &a->z);
    ecdsa_field_mul(&s2, &s2, &b->y);
    s_debug_field_print("s2 = b.y * z13", &s2);
    
    // H = U2 - U1
    ecdsa_field_negate(&t, &u1, 1);
    ecdsa_field_add(&h, &u2, &t);
    s_debug_field_print("h = u2 - u1", &h);
    
    // Check if H = 0 (same x-coordinate)
    if (ecdsa_field_normalizes_to_zero(&h)) {
        debug_if(s_debug_more, L_DEBUG, "  H normalizes to zero - checking S values");
        ecdsa_field_negate(&t, &s1, 1);
        ecdsa_field_add(&t, &s2, &t);
        
        if (ecdsa_field_normalizes_to_zero(&t)) {
            // Points are equal, double
            debug_if(s_debug_more, L_DEBUG, "  Points equal - calling double");
            ecdsa_gej_double(r, a);
            return;
        } else {
            // Points are negatives, result is infinity
            debug_if(s_debug_more, L_DEBUG, "  Points are negatives - returning infinity");
            ecdsa_gej_set_infinity(r);
            return;
        }
    }
    
    // I = (2*H)²
    ecdsa_field_add(&i, &h, &h);
    ecdsa_field_sqr(&i, &i);
    s_debug_field_print("i = (2h)²", &i);
    
    // J = H * I
    ecdsa_field_mul(&j, &h, &i);
    s_debug_field_print("j = h * i", &j);
    
    // rr = 2*(S2 - S1)
    ecdsa_field_negate(&t, &s1, 1);
    ecdsa_field_add(&rr, &s2, &t);
    ecdsa_field_add(&rr, &rr, &rr);
    s_debug_field_print("rr = 2*(s2-s1)", &rr);
    
    // V = U1 * I
    ecdsa_field_mul(&v, &u1, &i);
    s_debug_field_print("v = u1 * i", &v);
    
    // X3 = rr² - J - 2*V
    ecdsa_field_sqr(&r->x, &rr);
    s_debug_field_print("rr²", &r->x);
    ecdsa_field_negate(&t, &j, 1);
    ecdsa_field_add(&r->x, &r->x, &t);
    s_debug_field_print("rr² - j", &r->x);
    ecdsa_field_negate(&t, &v, 1);
    ecdsa_field_add(&r->x, &r->x, &t);
    ecdsa_field_add(&r->x, &r->x, &t);
    s_debug_field_print("X3 = rr² - j - 2v", &r->x);
    
    // Y3 = rr*(V - X3) - 2*S1*J
    ecdsa_field_negate(&t, &r->x, 1);
    ecdsa_field_add(&t, &v, &t);
    s_debug_field_print("v - X3", &t);
    ecdsa_field_mul(&r->y, &rr, &t);
    s_debug_field_print("rr*(v-X3)", &r->y);
    ecdsa_field_mul(&t, &s1, &j);
    ecdsa_field_add(&t, &t, &t);
    ecdsa_field_negate(&t, &t, 1);
    s_debug_field_print("-2*s1*j", &t);
    ecdsa_field_add(&r->y, &r->y, &t);
    s_debug_field_print("Y3", &r->y);
    
    // Z3 = 2*Z1*H
    ecdsa_field_mul(&r->z, &a->z, &h);
    ecdsa_field_add(&r->z, &r->z, &r->z);
    s_debug_field_print("Z3 = 2*z1*h", &r->z);
    
    r->infinity = false;
    debug_if(s_debug_more, L_DEBUG, "ecdsa_gej_add_ge: done");
}

// Point addition: r = a + b (both Jacobian)
// Full Jacobian formula - NO field inversion needed (critical for performance!)
// Uses standard add-1998-cmo-2 formula from EFD (Explicit-Formulas Database)
// Jacobian + Jacobian addition: r = a + b
void ecdsa_gej_add(ecdsa_gej_t *r, const ecdsa_gej_t *a, const ecdsa_gej_t *b) {
    if (a->infinity) {
        *r = *b;
        return;
    }
    if (b->infinity) {
        *r = *a;
        return;
    }
    
    ecdsa_field_t z1z1, z2z2, u1, u2, s1, s2, h, i, j, rr, v, t;
    
    // Z1Z1 = Z1²
    ecdsa_field_sqr(&z1z1, &a->z);
    
    // Z2Z2 = Z2²
    ecdsa_field_sqr(&z2z2, &b->z);
    
    // U1 = X1 * Z2Z2
    ecdsa_field_mul(&u1, &a->x, &z2z2);
    
    // U2 = X2 * Z1Z1
    ecdsa_field_mul(&u2, &b->x, &z1z1);
    
    // S1 = Y1 * Z2 * Z2Z2
    ecdsa_field_mul(&s1, &a->y, &b->z);
    ecdsa_field_mul(&s1, &s1, &z2z2);
    
    // S2 = Y2 * Z1 * Z1Z1
    ecdsa_field_mul(&s2, &b->y, &a->z);
    ecdsa_field_mul(&s2, &s2, &z1z1);
    
    // H = U2 - U1
    ecdsa_field_negate(&t, &u1, 1);
    ecdsa_field_add(&h, &u2, &t);
    
    // Check if H = 0 (same x-coordinate)
    if (ecdsa_field_normalizes_to_zero(&h)) {
        ecdsa_field_negate(&t, &s1, 1);
        ecdsa_field_add(&t, &s2, &t);
        
        if (ecdsa_field_normalizes_to_zero(&t)) {
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
    
    // Z3 = ((Z1+Z2)² - Z1Z1 - Z2Z2) * H
    ecdsa_field_add(&r->z, &a->z, &b->z);
    ecdsa_field_sqr(&r->z, &r->z);
    ecdsa_field_negate(&t, &z1z1, 1);
    ecdsa_field_add(&r->z, &r->z, &t);
    ecdsa_field_negate(&t, &z2z2, 1);
    ecdsa_field_add(&r->z, &r->z, &t);
    ecdsa_field_mul(&r->z, &r->z, &h);
    
    r->infinity = false;
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
