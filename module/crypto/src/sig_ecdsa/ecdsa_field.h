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
// Numeric Constants
// =============================================================================

// secp256k1 prime: p = 2^256 - 2^32 - 977
// In 5x52-bit limbs: [0xFFFFEFFFFFC2F, 0xFFFFFFFFFFFFF, 0xFFFFFFFFFFFFF, 0xFFFFFFFFFFFFF, 0x0FFFFFFFFFFFF]
// In 10x26-bit limbs: [0x3FFFC2F, 0x3FFFFBF, 0x3FFFFFF, 0x3FFFFFF, 0x3FFFFFF, 
//                      0x3FFFFFF, 0x3FFFFFF, 0x3FFFFFF, 0x3FFFFFF, 0x3FFFFF]

#ifdef ECDSA_FIELD_52BIT
    // 52-bit limb mask (for limbs 0-3)
    #define ECDSA_M52           0xFFFFFFFFFFFFFULL
    // 48-bit limb mask (for limb 4)
    #define ECDSA_M48           0xFFFFFFFFFFFFULL
    
    // p limbs (for normalization, negate, overflow checks)
    #define ECDSA_P_LIMB0       0xFFFFEFFFFFC2FULL
    #define ECDSA_P_LIMB1       ECDSA_M52
    #define ECDSA_P_LIMB2       ECDSA_M52
    #define ECDSA_P_LIMB3       ECDSA_M52
    #define ECDSA_P_LIMB4       ECDSA_M48
    
    // Reduction constant R = 2^256 mod p = 2^32 + 977 = 0x1000003D1
    #define ECDSA_R             0x1000003D1ULL
#else
    // 26-bit limb mask
    #define ECDSA_M26           0x3FFFFFFUL
    // 22-bit mask for limb 9
    #define ECDSA_M22           0x3FFFFFUL
    
    // p limbs for 10x26-bit representation
    #define ECDSA_P_LIMB0       0x3FFFC2FUL
    #define ECDSA_P_LIMB1       0x3FFFFBFUL
    #define ECDSA_P_LIMB2       ECDSA_M26
    #define ECDSA_P_LIMB3       ECDSA_M26
    #define ECDSA_P_LIMB4       ECDSA_M26
    #define ECDSA_P_LIMB5       ECDSA_M26
    #define ECDSA_P_LIMB6       ECDSA_M26
    #define ECDSA_P_LIMB7       ECDSA_M26
    #define ECDSA_P_LIMB8       ECDSA_M26
    #define ECDSA_P_LIMB9       ECDSA_M22
    
    // Reduction constant for 32-bit
    #define ECDSA_R_LO          0x3D1UL
    #define ECDSA_R_HI          0x40UL
#endif

// =============================================================================
// Field Element Constants (extern)
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
void ecdsa_field_inv(ecdsa_field_t *r, const ecdsa_field_t *a);
bool ecdsa_field_sqrt(ecdsa_field_t *r, const ecdsa_field_t *a);

// =============================================================================
// Optimized mul/sqr with architecture dispatch
// These are the main entry points - they dispatch to the best available impl
// =============================================================================

void ecdsa_field_mul(ecdsa_field_t *r, const ecdsa_field_t *a, const ecdsa_field_t *b);
void ecdsa_field_sqr(ecdsa_field_t *r, const ecdsa_field_t *a);

// Optimized operations (avoid expensive normalize)
void ecdsa_field_mul_int(ecdsa_field_t *r, int a);  // r = r * a (small constant)
void ecdsa_field_half(ecdsa_field_t *r);             // r = r / 2 (mod p)
void ecdsa_field_add_int(ecdsa_field_t *r, int a);   // r = r + a
bool ecdsa_field_normalizes_to_zero(const ecdsa_field_t *a);  // Check if a ≡ 0 (mod p)

// =============================================================================
// Batch Operations (Montgomery's trick)
// =============================================================================

// Batch inversion: inverts n field elements using only 1 field inversion + 3*(n-1) multiplications
void ecdsa_field_inv_batch(ecdsa_field_t *r, const ecdsa_field_t *a, size_t n);

#endif // ECDSA_FIELD_H
