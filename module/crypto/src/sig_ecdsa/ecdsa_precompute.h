/*
 * Precomputed tables for secp256k1 ECDSA operations
 * 
 * For efficient scalar multiplication, we use:
 * 1. Static precomputed tables for G (like bitcoin-core)
 * 2. Split-128 optimization: ng = ng_1 + ng_128 * 2^128
 * 3. wNAF representation for general scalar mult
 * 4. Strauss/Shamir for simultaneous multiplication (verify)
 */

#ifndef ECDSA_PRECOMPUTE_H
#define ECDSA_PRECOMPUTE_H

#include "ecdsa_group.h"
#include "ecdsa_precomputed_ecmult.h"

// =============================================================================
// Configuration
// =============================================================================

// Window size for arbitrary points (smaller, built at runtime)
// w=5: 16 odd multiples per table
#define ECDSA_WNAF_WINDOW 5
#define ECDSA_WNAF_TABLE_SIZE (1 << (ECDSA_WNAF_WINDOW - 1))  // 16 points

// Window size for G is defined in ecdsa_precomputed_ecmult.h (ECDSA_WINDOW_G = 15)
// This gives 8192 precomputed points for extremely fast G multiplication

// For ecmult_gen: use comb method with pre-computed table
// 256 bits / 4 bits per tooth = 64 teeth
// 16 tables * 16 points = 256 affine points
#define ECDSA_ECMULT_GEN_BITS 4
#define ECDSA_ECMULT_GEN_TEETH 64  // 256 / 4
#define ECDSA_ECMULT_GEN_COMB_SIZE 16  // 2^4

// =============================================================================
// Precomputed Table Types
// =============================================================================

// Pre-computed table for ecmult_gen (comb method): G * (1, 2, ..., 15) at bit positions
// Table[i][j] = j * 2^(4*i) * G for i in [0,63], j in [1,15]
typedef struct {
    ecdsa_ge_t comb_table[ECDSA_ECMULT_GEN_TEETH][ECDSA_ECMULT_GEN_COMB_SIZE];
    ecdsa_ge_t wnaf_table[ECDSA_WNAF_TABLE_SIZE];  // Small wNAF table for G: (2*i+1)*G
    bool initialized;
} ecdsa_ecmult_gen_ctx_t;

// Pre-computed table for wNAF: stores odd multiples of a point (arbitrary points)
// table[i] = (2*i + 1) * P for i in [0, 2^(w-1) - 1]
typedef struct {
    ecdsa_ge_t table[ECDSA_WNAF_TABLE_SIZE];
} ecdsa_wnaf_table_t;

// =============================================================================
// Static Precomputed Table Access (from ecdsa_precomputed_ecmult.c)
// =============================================================================

// Convert storage format to affine point
static inline void ecdsa_ge_from_storage(ecdsa_ge_t *r, const ecdsa_ge_storage_t *a) {
    // Storage uses 4x64-bit representation, convert to field
    ecdsa_field_set_b32_raw(&r->x, (const uint8_t*)a->x);
    ecdsa_field_set_b32_raw(&r->y, (const uint8_t*)a->y);
    r->infinity = false;
}

// Get point from precomputed G table: returns (2*|n| - 1) * G, with sign
// n must be odd and in range [-2^(WINDOW_G-1)+1, 2^(WINDOW_G-1)-1]
static inline void ecdsa_ecmult_table_get_ge(ecdsa_ge_t *r, int n) {
    if (n > 0) {
        ecdsa_ge_from_storage(r, &ecdsa_pre_g[(n-1)/2]);
    } else {
        ecdsa_ge_from_storage(r, &ecdsa_pre_g[(-n-1)/2]);
        ecdsa_field_negate(&r->y, &r->y, 1);
        ecdsa_field_normalize(&r->y);
    }
}

// Get point from precomputed G*2^128 table
static inline void ecdsa_ecmult_table_get_ge_128(ecdsa_ge_t *r, int n) {
    if (n > 0) {
        ecdsa_ge_from_storage(r, &ecdsa_pre_g_128[(n-1)/2]);
    } else {
        ecdsa_ge_from_storage(r, &ecdsa_pre_g_128[(-n-1)/2]);
        ecdsa_field_negate(&r->y, &r->y, 1);
        ecdsa_field_normalize(&r->y);
    }
}

// =============================================================================
// wNAF Representation
// =============================================================================

// wNAF digit: value in [-2^(w-1), 2^(w-1)] or 0
typedef int8_t ecdsa_wnaf_t;

// Maximum wNAF length for 256-bit scalar with window w
#define ECDSA_WNAF_MAX_LEN 257

// Convert scalar to wNAF representation
// Returns the length of the wNAF
int ecdsa_scalar_to_wnaf(ecdsa_wnaf_t *wnaf, const ecdsa_scalar_t *s, int w);

// =============================================================================
// Precomputation Functions
// =============================================================================

// Initialize the generator multiplication context (call once at startup)
void ecdsa_ecmult_gen_ctx_init(ecdsa_ecmult_gen_ctx_t *ctx);

// Clear the context
void ecdsa_ecmult_gen_ctx_clear(ecdsa_ecmult_gen_ctx_t *ctx);

// Get the global generator context (lazy-initialized)
ecdsa_ecmult_gen_ctx_t* ecdsa_ecmult_gen_ctx_get(void);

// Build wNAF precomputation table for arbitrary point P
void ecdsa_wnaf_table_build(ecdsa_wnaf_table_t *table, const ecdsa_ge_t *p, int w);

// =============================================================================
// Optimized Scalar Multiplication
// =============================================================================

// Fast generator multiplication using precomputed tables: r = n * G
// Uses comb method with 4-bit windows, ~16x faster than double-and-add
void ecdsa_ecmult_gen_fast(ecdsa_gej_t *r, const ecdsa_scalar_t *n);

// wNAF scalar multiplication: r = n * p
// ~4-5x faster than double-and-add for window size 5
void ecdsa_ecmult_wnaf(ecdsa_gej_t *r, const ecdsa_ge_t *p, const ecdsa_scalar_t *n);

// Strauss/Shamir simultaneous multiplication: r = na * a + ng * G
// This is the key optimization for ECDSA verify
// ~2x faster than computing na*a and ng*G separately
void ecdsa_ecmult_strauss(ecdsa_gej_t *r, const ecdsa_gej_t *a, 
                          const ecdsa_scalar_t *na, const ecdsa_scalar_t *ng);

// =============================================================================
// Batch Operations
// =============================================================================

// Batch inversion using Montgomery's trick
// Inverts n field elements using only 1 field inversion + 3*(n-1) multiplications
// Much faster than n separate inversions
void ecdsa_field_inv_batch(ecdsa_field_t *r, const ecdsa_field_t *a, size_t n);

// Convert multiple Jacobian points to affine in batch
void ecdsa_ge_set_gej_batch(ecdsa_ge_t *r, const ecdsa_gej_t *a, size_t n);

#endif // ECDSA_PRECOMPUTE_H
