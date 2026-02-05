/*
 * Precomputed tables for secp256k1 ECDSA operations
 * 
 * For efficient scalar multiplication, we use:
 * 1. Precomputed multiples of G for ecmult_gen (signing/keygen)
 * 2. wNAF representation for general scalar mult
 * 3. Strauss/Shamir for simultaneous multiplication (verify)
 */

#ifndef ECDSA_PRECOMPUTE_H
#define ECDSA_PRECOMPUTE_H

#include "ecdsa_group.h"

// =============================================================================
// Configuration
// =============================================================================

// Window size for wNAF (2^w points per table, larger = faster but more memory)
// w=4: 16 points, ~4x speedup vs binary
// w=5: 32 points, ~5x speedup (used by bitcoin-core)
#define ECDSA_WNAF_WINDOW 5
#define ECDSA_WNAF_TABLE_SIZE (1 << (ECDSA_WNAF_WINDOW - 1))  // 16 points

// For ecmult_gen: use comb method with pre-computed table
// 256 bits / 4 bits per tooth = 64 teeth
// 16 tables * 16 points = 256 affine points
#define ECDSA_ECMULT_GEN_BITS 4
#define ECDSA_ECMULT_GEN_TEETH 64  // 256 / 4
#define ECDSA_ECMULT_GEN_TABLE_SIZE 16  // 2^4

// =============================================================================
// Precomputed Table Types
// =============================================================================

// Pre-computed table for ecmult_gen: G * (1, 2, ..., 15) at various bit positions
// Table[i][j] = j * 2^(4*i) * G for i in [0,63], j in [1,15]
typedef struct {
    ecdsa_ge_t comb_table[ECDSA_ECMULT_GEN_TEETH][ECDSA_ECMULT_GEN_TABLE_SIZE];
    ecdsa_ge_t wnaf_table[ECDSA_WNAF_TABLE_SIZE];  // wNAF table for G: (2*i+1)*G
    bool initialized;
} ecdsa_ecmult_gen_ctx_t;

// Pre-computed table for wNAF: stores odd multiples of a point
// table[i] = (2*i + 1) * P for i in [0, 2^(w-1) - 1]
typedef struct {
    ecdsa_ge_t table[ECDSA_WNAF_TABLE_SIZE];
} ecdsa_wnaf_table_t;

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

// =============================================================================
// Addition Chains for Fast Inversion
// =============================================================================

// Optimized field inversion using addition chain
// ~256 squarings + ~15 multiplications (vs ~256 + ~128 for naive)
void ecdsa_field_inv_fast(ecdsa_field_t *r, const ecdsa_field_t *a);

// Optimized scalar inversion using addition chain
void ecdsa_scalar_inv_fast(ecdsa_scalar_t *r, const ecdsa_scalar_t *a);

#endif // ECDSA_PRECOMPUTE_H
