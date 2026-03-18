/*
 * Internal ECDSA scalar arithmetic (mod n)
 * 
 * Operations in the scalar field Z_n where:
 *   n = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
 *
 * Scalars are 256-bit integers used as private keys, nonces, signature components.
 * 
 * NOTE: Internal API, not exposed outside sig_ecdsa/
 */

#ifndef ECDSA_SCALAR_H
#define ECDSA_SCALAR_H

#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// Scalar Representation
// =============================================================================

#if defined(__LP64__) || defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__)
    #define ECDSA_SCALAR_64BIT 1
    #define ECDSA_SCALAR_LIMBS 4
    typedef uint64_t ecdsa_scalar_limb_t;
#else
    #define ECDSA_SCALAR_32BIT 1
    #define ECDSA_SCALAR_LIMBS 8
    typedef uint32_t ecdsa_scalar_limb_t;
#endif

typedef struct {
    ecdsa_scalar_limb_t d[ECDSA_SCALAR_LIMBS];
} ecdsa_scalar_t;

// =============================================================================
// Constants
// =============================================================================

extern const ecdsa_scalar_t ECDSA_SCALAR_N;         // Curve order
extern const ecdsa_scalar_t ECDSA_SCALAR_N_HALF;    // n/2 for low-S check
extern const ecdsa_scalar_t ECDSA_SCALAR_ZERO;
extern const ecdsa_scalar_t ECDSA_SCALAR_ONE;

// =============================================================================
// Basic Operations
// =============================================================================

void ecdsa_scalar_clear(ecdsa_scalar_t *r);
void ecdsa_scalar_set_b32(ecdsa_scalar_t *r, const uint8_t *b32, int *overflow);
void ecdsa_scalar_get_b32(uint8_t *b32, const ecdsa_scalar_t *a);
void ecdsa_scalar_set_int(ecdsa_scalar_t *r, unsigned int v);
void ecdsa_scalar_copy(ecdsa_scalar_t *r, const ecdsa_scalar_t *a);
bool ecdsa_scalar_is_zero(const ecdsa_scalar_t *a);
bool ecdsa_scalar_is_high(const ecdsa_scalar_t *a);
bool ecdsa_scalar_equal(const ecdsa_scalar_t *a, const ecdsa_scalar_t *b);

// =============================================================================
// Arithmetic
// =============================================================================

void ecdsa_scalar_negate(ecdsa_scalar_t *r, const ecdsa_scalar_t *a);
int  ecdsa_scalar_add(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b);
void ecdsa_scalar_mul(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b);
void ecdsa_scalar_inv(ecdsa_scalar_t *r, const ecdsa_scalar_t *a);

// =============================================================================
// Split Operations (for optimized scalar multiplication)
// =============================================================================

// Split k into k1 + k2 * 2^128 (for 128-bit wNAF optimization)
void ecdsa_scalar_split_128(ecdsa_scalar_t *k1, ecdsa_scalar_t *k2, const ecdsa_scalar_t *k);

// Get bits from scalar at position 'offset', 'count' bits (max 32)
unsigned int ecdsa_scalar_get_bits(const ecdsa_scalar_t *a, unsigned int offset, unsigned int count);

// =============================================================================
// Utility
// =============================================================================

bool ecdsa_scalar_check_seckey(const uint8_t *seckey);

#endif // ECDSA_SCALAR_H
