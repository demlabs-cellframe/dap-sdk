/*
 * Internal ECDSA group operations (elliptic curve points)
 * 
 * Operations on secp256k1 curve: y² = x³ + 7 (mod p)
 * 
 * Points represented in Jacobian coordinates for efficiency:
 *   (X, Y, Z) represents affine (X/Z², Y/Z³)
 * 
 * NOTE: Internal API, not exposed outside sig_ecdsa/
 */

#ifndef ECDSA_GROUP_H
#define ECDSA_GROUP_H

#include <stddef.h>
#include <stdbool.h>
#include "ecdsa_field.h"
#include "ecdsa_scalar.h"

// =============================================================================
// Debug Control
// =============================================================================

/**
 * @brief Enable/disable debug output for ECDSA group operations
 * @param a_enable true to enable, false to disable
 */
void ecdsa_group_set_debug(bool a_enable);

// =============================================================================
// Point Representations
// =============================================================================

// Jacobian coordinates (for computation)
typedef struct {
    ecdsa_field_t x;
    ecdsa_field_t y;
    ecdsa_field_t z;
    bool infinity;
} ecdsa_gej_t;

// Affine coordinates (for I/O)
typedef struct {
    ecdsa_field_t x;
    ecdsa_field_t y;
    bool infinity;
} ecdsa_ge_t;

// =============================================================================
// Generator Point
// =============================================================================

extern const ecdsa_ge_t ECDSA_GENERATOR;

// =============================================================================
// Affine Operations
// =============================================================================

void ecdsa_ge_set_infinity(ecdsa_ge_t *r);
bool ecdsa_ge_set_xy(ecdsa_ge_t *r, const ecdsa_field_t *x, const ecdsa_field_t *y);
bool ecdsa_ge_set_xo(ecdsa_ge_t *r, const ecdsa_field_t *x, bool odd);
bool ecdsa_ge_is_valid(const ecdsa_ge_t *a);
bool ecdsa_ge_is_infinity(const ecdsa_ge_t *a);
void ecdsa_ge_neg(ecdsa_ge_t *r, const ecdsa_ge_t *a);

// =============================================================================
// Jacobian Operations
// =============================================================================

void ecdsa_gej_set_infinity(ecdsa_gej_t *r);
void ecdsa_gej_set_ge(ecdsa_gej_t *r, const ecdsa_ge_t *a);
void ecdsa_ge_set_gej(ecdsa_ge_t *r, const ecdsa_gej_t *a);
bool ecdsa_gej_is_infinity(const ecdsa_gej_t *a);
void ecdsa_gej_neg(ecdsa_gej_t *r, const ecdsa_gej_t *a);

// =============================================================================
// Point Arithmetic
// =============================================================================

void ecdsa_gej_double(ecdsa_gej_t *r, const ecdsa_gej_t *a);
void ecdsa_gej_add_ge(ecdsa_gej_t *r, const ecdsa_gej_t *a, const ecdsa_ge_t *b);
void ecdsa_gej_add(ecdsa_gej_t *r, const ecdsa_gej_t *a, const ecdsa_gej_t *b);

// =============================================================================
// Scalar Multiplication
// =============================================================================

void ecdsa_ecmult_gen(ecdsa_gej_t *r, const ecdsa_scalar_t *n);
void ecdsa_ecmult(ecdsa_gej_t *r, const ecdsa_gej_t *a, const ecdsa_scalar_t *na, const ecdsa_scalar_t *ng);
void ecdsa_ecmult_const(ecdsa_gej_t *r, const ecdsa_ge_t *a, const ecdsa_scalar_t *n);

// =============================================================================
// Serialization
// =============================================================================

bool ecdsa_ge_serialize(const ecdsa_ge_t *a, bool compressed, uint8_t *output, size_t *outputlen);
bool ecdsa_ge_parse(ecdsa_ge_t *r, const uint8_t *input, size_t inputlen);

// =============================================================================
// Precomputation
// =============================================================================

void ecdsa_ecmult_gen_init(void);
void ecdsa_ecmult_gen_deinit(void);

#endif // ECDSA_GROUP_H
