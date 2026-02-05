/*
 * Internal ECDSA field arithmetic (mod p)
 * 
 * Operations in the finite field F_p where:
 *   p = 2^256 - 2^32 - 977
 *     = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F
 *
 * Uses 5x52-bit limb representation for 64-bit platforms,
 * or 10x26-bit limb representation for 32-bit platforms.
 * 
 * NOTE: Internal API, not exposed outside sig_ecdsa/
 */

#ifndef ECDSA_FIELD_H
#define ECDSA_FIELD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// Field Element Representation
// =============================================================================

#if defined(__LP64__) || defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__)
    #define ECDSA_FIELD_52BIT 1
    #define ECDSA_FIELD_LIMBS 5
    typedef uint64_t ecdsa_field_limb_t;
#else
    #define ECDSA_FIELD_26BIT 1
    #define ECDSA_FIELD_LIMBS 10
    typedef uint32_t ecdsa_field_limb_t;
#endif

typedef struct {
    ecdsa_field_limb_t n[ECDSA_FIELD_LIMBS];
} ecdsa_field_t;

// =============================================================================
// Constants
// =============================================================================

extern const ecdsa_field_t ECDSA_FIELD_P;
extern const ecdsa_field_t ECDSA_FIELD_ZERO;
extern const ecdsa_field_t ECDSA_FIELD_ONE;

// =============================================================================
// Basic Operations
// =============================================================================

void ecdsa_field_clear(ecdsa_field_t *r);
void ecdsa_field_set_int(ecdsa_field_t *r, int a);
bool ecdsa_field_set_b32(ecdsa_field_t *r, const uint8_t *a);  // From big-endian bytes
void ecdsa_field_set_b32_raw(ecdsa_field_t *r, const uint8_t *a);  // From 4x64-bit LE storage
void ecdsa_field_get_b32(uint8_t *r, const ecdsa_field_t *a);
void ecdsa_field_copy(ecdsa_field_t *r, const ecdsa_field_t *a);
bool ecdsa_field_is_zero(const ecdsa_field_t *a);
bool ecdsa_field_is_odd(const ecdsa_field_t *a);
bool ecdsa_field_equal(const ecdsa_field_t *a, const ecdsa_field_t *b);

// =============================================================================
// Arithmetic
// =============================================================================

void ecdsa_field_normalize(ecdsa_field_t *r);       // Full normalize to [0, p)
void ecdsa_field_normalize_weak(ecdsa_field_t *r);  // Just propagate carries
void ecdsa_field_negate(ecdsa_field_t *r, const ecdsa_field_t *a, int m);
void ecdsa_field_add(ecdsa_field_t *r, const ecdsa_field_t *a, const ecdsa_field_t *b);  // Lazy, no normalize
void ecdsa_field_mul(ecdsa_field_t *r, const ecdsa_field_t *a, const ecdsa_field_t *b);
void ecdsa_field_sqr(ecdsa_field_t *r, const ecdsa_field_t *a);
void ecdsa_field_inv(ecdsa_field_t *r, const ecdsa_field_t *a);
bool ecdsa_field_sqrt(ecdsa_field_t *r, const ecdsa_field_t *a);

// =============================================================================
// Batch Operations (Montgomery's trick)
// =============================================================================

// Batch inversion: inverts n field elements using only 1 field inversion + 3*(n-1) multiplications
void ecdsa_field_inv_batch(ecdsa_field_t *r, const ecdsa_field_t *a, size_t n);

#endif // ECDSA_FIELD_H
