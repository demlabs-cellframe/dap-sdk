/*
 * GLV Endomorphism implementation for secp256k1
 * 
 * References:
 * - Gallant, Lambert, Vanstone: "Faster Point Multiplication on Elliptic Curves"
 * - bitcoin-core/secp256k1 implementation
 */

#include "ecdsa_endomorphism.h"
#include "ecdsa_precompute.h"
#include "dap_common.h"
#include <string.h>

#define LOG_TAG "ecdsa_endomorphism"

// =============================================================================
// GLV Constants for secp256k1
// =============================================================================

// β = 0x7ae96a2b657c07106e64479eac3434e99cf0497512f58995c1396c28719501ee
// This is a primitive cube root of unity in Fp: β³ = 1 (mod p)
// Initialized at runtime via ecdsa_endomorphism_init()
static ecdsa_field_t s_beta;
static ecdsa_scalar_t s_lambda;
static bool s_endo_initialized = false;

// λ = 0x5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72
// This is a primitive cube root of unity in Fn: λ³ = 1 (mod n)

// Big-endian byte arrays for initialization
static const uint8_t s_beta_bytes[32] = {
    0x7a, 0xe9, 0x6a, 0x2b, 0x65, 0x7c, 0x07, 0x10,
    0x6e, 0x64, 0x47, 0x9e, 0xac, 0x34, 0x34, 0xe9,
    0x9c, 0xf0, 0x49, 0x75, 0x12, 0xf5, 0x89, 0x95,
    0xc1, 0x39, 0x6c, 0x28, 0x71, 0x95, 0x01, 0xee
};

static const uint8_t s_lambda_bytes[32] = {
    0x53, 0x63, 0xad, 0x4c, 0xc0, 0x5c, 0x30, 0xe0,
    0xa5, 0x26, 0x1c, 0x02, 0x88, 0x12, 0x64, 0x5a,
    0x12, 0x2e, 0x22, 0xea, 0x20, 0x81, 0x66, 0x78,
    0xdf, 0x02, 0x96, 0x7c, 0x1b, 0x23, 0xbd, 0x72
};

// Initialize endomorphism constants
static void s_endo_init(void) {
    if (s_endo_initialized) return;
    ecdsa_field_set_b32(&s_beta, s_beta_bytes);
    ecdsa_scalar_set_b32(&s_lambda, s_lambda_bytes, NULL);
    s_endo_initialized = true;
}

// Access BETA (ensures initialization)
const ecdsa_field_t *ecdsa_get_beta(void) {
    s_endo_init();
    return &s_beta;
}

// Access LAMBDA (ensures initialization)
const ecdsa_scalar_t *ecdsa_get_lambda(void) {
    s_endo_init();
    return &s_lambda;
}

// =============================================================================
// Lattice basis for scalar decomposition (precomputed)
// =============================================================================

// Basis vectors for GLV decomposition:
// b1 = (a1, -b1) where a1 = 0x3086d221a7d46bcde86c90e49284eb15
// b2 = (-a2, a2) where a2 = 0xe4437ed6010e88286f547fa90abfe4c3

// g1 = 0x3086d221a7d46bcde86c90e49284eb153dba...
// g2 = 0xe4437ed6010e88286f547fa90abfe4c3

// Minus b1 (for decomposition)
static const uint8_t s_minus_b1[16] = {
    0x30, 0x86, 0xd2, 0x21, 0xa7, 0xd4, 0x6b, 0xcd,
    0xe8, 0x6c, 0x90, 0xe4, 0x92, 0x84, 0xeb, 0x15
};

// Minus b2
static const uint8_t s_minus_b2[16] = {
    0xe4, 0x43, 0x7e, 0xd6, 0x01, 0x0e, 0x88, 0x28,
    0x6f, 0x54, 0x7f, 0xa9, 0x0a, 0xbf, 0xe4, 0xc3
};

// g1 for decomposition (divided by 2^384)
static const uint8_t s_g1[16] = {
    0x30, 0x86, 0xd2, 0x21, 0xa7, 0xd4, 0x6b, 0xcd,
    0xe8, 0x6c, 0x90, 0xe4, 0x92, 0x84, 0xeb, 0x15
};

// g2 for decomposition
static const uint8_t s_g2[16] = {
    0xe4, 0x43, 0x7e, 0xd6, 0x01, 0x0e, 0x88, 0x28,
    0x6f, 0x54, 0x7f, 0xa9, 0x0a, 0xbf, 0xe4, 0xc4
};

// =============================================================================
// 128-bit arithmetic helpers for decomposition
// =============================================================================

typedef struct {
    uint64_t lo;
    uint64_t hi;
} uint128_split_t;

// Multiply 256-bit by 128-bit, return top 128 bits (approximately)
static void mul256x128_hi(uint128_split_t *r, const ecdsa_scalar_t *a, const uint8_t *b) {
    // Simplified: we need k * g1 >> 256 and k * g2 >> 256
    // This gives us the approximate coefficients for decomposition
    
    uint8_t a_bytes[32];
    ecdsa_scalar_get_b32(a_bytes, a);
    
    // Read b as 128-bit big-endian
    uint64_t b_hi = ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) | 
                    ((uint64_t)b[2] << 40) | ((uint64_t)b[3] << 32) |
                    ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) |
                    ((uint64_t)b[6] << 8) | (uint64_t)b[7];
    uint64_t b_lo = ((uint64_t)b[8] << 56) | ((uint64_t)b[9] << 48) | 
                    ((uint64_t)b[10] << 40) | ((uint64_t)b[11] << 32) |
                    ((uint64_t)b[12] << 24) | ((uint64_t)b[13] << 16) |
                    ((uint64_t)b[14] << 8) | (uint64_t)b[15];
    
    // Read high 128 bits of a
    uint64_t a_hi = ((uint64_t)a_bytes[0] << 56) | ((uint64_t)a_bytes[1] << 48) |
                    ((uint64_t)a_bytes[2] << 40) | ((uint64_t)a_bytes[3] << 32) |
                    ((uint64_t)a_bytes[4] << 24) | ((uint64_t)a_bytes[5] << 16) |
                    ((uint64_t)a_bytes[6] << 8) | (uint64_t)a_bytes[7];
    uint64_t a_lo = ((uint64_t)a_bytes[8] << 56) | ((uint64_t)a_bytes[9] << 48) |
                    ((uint64_t)a_bytes[10] << 40) | ((uint64_t)a_bytes[11] << 32) |
                    ((uint64_t)a_bytes[12] << 24) | ((uint64_t)a_bytes[13] << 16) |
                    ((uint64_t)a_bytes[14] << 8) | (uint64_t)a_bytes[15];

    // Approximate: (a_hi:a_lo) * (b_hi:b_lo) >> 128
    // We only need the high 128 bits of a 384-bit product, shifted right by 256
    // Simplified approximation using high parts
    
#ifdef __SIZEOF_INT128__
    __uint128_t prod_hh = (__uint128_t)a_hi * b_hi;
    __uint128_t prod_hl = (__uint128_t)a_hi * b_lo;
    __uint128_t prod_lh = (__uint128_t)a_lo * b_hi;
    
    // Add with carry
    __uint128_t sum = prod_hh + (prod_hl >> 64) + (prod_lh >> 64);
    r->hi = (uint64_t)(sum >> 64);
    r->lo = (uint64_t)sum;
#else
    // Fallback without __int128
    r->hi = (a_hi >> 32) * (b_hi >> 32);
    r->lo = a_hi * b_hi;
#endif
}

// =============================================================================
// GLV Scalar Decomposition
// =============================================================================

void ecdsa_scalar_split_lambda(
    ecdsa_scalar_t *a_k1, bool *a_k1_neg,
    ecdsa_scalar_t *a_k2, bool *a_k2_neg,
    const ecdsa_scalar_t *a_k)
{
    dap_return_if_fail(a_k1 && a_k1_neg && a_k2 && a_k2_neg && a_k);
    
    // Use Babai's nearest plane algorithm with precomputed lattice basis
    // c1 = round(k * g1 / 2^384)
    // c2 = round(k * g2 / 2^384)
    // k1 = k - c1*a1 - c2*a2
    // k2 = -c1*b1 - c2*b2
    
    uint128_split_t c1, c2;
    mul256x128_hi(&c1, a_k, s_g1);
    mul256x128_hi(&c2, a_k, s_g2);
    
    // Simplified decomposition: approximate values
    // For production, need full precision arithmetic
    
    // k1 = k - c1*a1 - c2*a2 (mod n)
    // k2 = -c1*b1 - c2*b2 (mod n)
    
    // Start with k1 = k
    ecdsa_scalar_copy(a_k1, a_k);
    
    // Build c1 * λ and subtract from k
    ecdsa_scalar_t c1_scalar, c2_scalar, tmp;
    
    // c1 as scalar (128-bit)
    uint8_t c1_bytes[32] = {0};
    c1_bytes[16] = (c1.hi >> 56) & 0xFF;
    c1_bytes[17] = (c1.hi >> 48) & 0xFF;
    c1_bytes[18] = (c1.hi >> 40) & 0xFF;
    c1_bytes[19] = (c1.hi >> 32) & 0xFF;
    c1_bytes[20] = (c1.hi >> 24) & 0xFF;
    c1_bytes[21] = (c1.hi >> 16) & 0xFF;
    c1_bytes[22] = (c1.hi >> 8) & 0xFF;
    c1_bytes[23] = c1.hi & 0xFF;
    c1_bytes[24] = (c1.lo >> 56) & 0xFF;
    c1_bytes[25] = (c1.lo >> 48) & 0xFF;
    c1_bytes[26] = (c1.lo >> 40) & 0xFF;
    c1_bytes[27] = (c1.lo >> 32) & 0xFF;
    c1_bytes[28] = (c1.lo >> 24) & 0xFF;
    c1_bytes[29] = (c1.lo >> 16) & 0xFF;
    c1_bytes[30] = (c1.lo >> 8) & 0xFF;
    c1_bytes[31] = c1.lo & 0xFF;
    ecdsa_scalar_set_b32(&c1_scalar, c1_bytes, NULL);
    
    // k2 = c1 (approximately, for GLV this becomes the second scalar)
    ecdsa_scalar_copy(a_k2, &c1_scalar);
    
    // k1 = k - k2 * λ (mod n)
    ecdsa_scalar_mul(&tmp, a_k2, ecdsa_get_lambda());
    ecdsa_scalar_negate(&tmp, &tmp);
    ecdsa_scalar_add(a_k1, a_k, &tmp);
    
    // Determine signs - if k1 or k2 > n/2, negate
    // For simplicity, check high bit
    uint8_t k1_bytes[32], k2_bytes[32];
    ecdsa_scalar_get_b32(k1_bytes, a_k1);
    ecdsa_scalar_get_b32(k2_bytes, a_k2);
    
    // If high bit is set, value is > n/2, so negate
    *a_k1_neg = (k1_bytes[0] & 0x80) != 0;
    *a_k2_neg = (k2_bytes[0] & 0x80) != 0;
    
    if (*a_k1_neg) {
        ecdsa_scalar_negate(a_k1, a_k1);
    }
    if (*a_k2_neg) {
        ecdsa_scalar_negate(a_k2, a_k2);
    }
}

// =============================================================================
// Endomorphism Application
// =============================================================================

void ecdsa_ge_mul_lambda(ecdsa_ge_t *a_result, const ecdsa_ge_t *a_point) {
    dap_return_if_fail(a_result && a_point);
    
    if (a_point->infinity) {
        ecdsa_ge_set_infinity(a_result);
        return;
    }
    
    // φ(x, y) = (β*x, y)
    ecdsa_field_mul(&a_result->x, &a_point->x, ecdsa_get_beta());
    ecdsa_field_normalize(&a_result->x);
    ecdsa_field_copy(&a_result->y, &a_point->y);
    a_result->infinity = false;
}

void ecdsa_gej_mul_lambda(ecdsa_gej_t *a_result, const ecdsa_gej_t *a_point) {
    dap_return_if_fail(a_result && a_point);
    
    if (a_point->infinity) {
        ecdsa_gej_set_infinity(a_result);
        return;
    }
    
    // φ(X, Y, Z) = (β*X, Y, Z)
    ecdsa_field_mul(&a_result->x, &a_point->x, ecdsa_get_beta());
    ecdsa_field_normalize(&a_result->x);
    ecdsa_field_copy(&a_result->y, &a_point->y);
    ecdsa_field_copy(&a_result->z, &a_point->z);
    a_result->infinity = false;
}

// =============================================================================
// Endomorphism-Accelerated Scalar Multiplication
// =============================================================================

void ecdsa_ecmult_endomorphism(
    ecdsa_gej_t *a_result,
    const ecdsa_ge_t *a_point,
    const ecdsa_scalar_t *a_scalar)
{
    dap_return_if_fail(a_result && a_point && a_scalar);
    
    // Decompose: k = k1 + k2*λ (mod n)
    ecdsa_scalar_t k1, k2;
    bool k1_neg, k2_neg;
    ecdsa_scalar_split_lambda(&k1, &k1_neg, &k2, &k2_neg, a_scalar);
    
    // Compute φ(P) = (β*x, y)
    ecdsa_ge_t phi_p;
    ecdsa_ge_mul_lambda(&phi_p, a_point);
    
    // If signs are negative, negate the points instead
    ecdsa_ge_t p = *a_point;
    if (k1_neg) {
        ecdsa_ge_neg(&p, &p);
    }
    if (k2_neg) {
        ecdsa_ge_neg(&phi_p, &phi_p);
    }
    
    // Build wNAF for both scalars
    ecdsa_wnaf_t wnaf1[ECDSA_WNAF_MAX_LEN], wnaf2[ECDSA_WNAF_MAX_LEN];
    int len1 = ecdsa_scalar_to_wnaf(wnaf1, &k1, ECDSA_WNAF_WINDOW);
    int len2 = ecdsa_scalar_to_wnaf(wnaf2, &k2, ECDSA_WNAF_WINDOW);
    int max_len = len1 > len2 ? len1 : len2;
    
    // Build precomputation tables
    ecdsa_wnaf_table_t table1, table2;
    ecdsa_wnaf_table_build(&table1, &p, ECDSA_WNAF_WINDOW);
    ecdsa_wnaf_table_build(&table2, &phi_p, ECDSA_WNAF_WINDOW);
    
    // Simultaneous double-and-add
    ecdsa_gej_set_infinity(a_result);
    
    for (int i = max_len - 1; i >= 0; i--) {
        if (!ecdsa_gej_is_infinity(a_result)) {
            ecdsa_gej_double(a_result, a_result);
        }
        
        // Process k1
        if (i < len1 && wnaf1[i] != 0) {
            int idx = (wnaf1[i] > 0 ? wnaf1[i] : -wnaf1[i]) >> 1;
            if (ecdsa_gej_is_infinity(a_result)) {
                ecdsa_gej_set_ge(a_result, &table1.table[idx]);
                if (wnaf1[i] < 0) {
                    ecdsa_gej_neg(a_result, a_result);
                }
            } else {
                if (wnaf1[i] > 0) {
                    ecdsa_gej_add_ge(a_result, a_result, &table1.table[idx]);
                } else {
                    ecdsa_ge_t neg;
                    ecdsa_ge_neg(&neg, &table1.table[idx]);
                    ecdsa_gej_add_ge(a_result, a_result, &neg);
                }
            }
        }
        
        // Process k2
        if (i < len2 && wnaf2[i] != 0) {
            int idx = (wnaf2[i] > 0 ? wnaf2[i] : -wnaf2[i]) >> 1;
            if (ecdsa_gej_is_infinity(a_result)) {
                ecdsa_gej_set_ge(a_result, &table2.table[idx]);
                if (wnaf2[i] < 0) {
                    ecdsa_gej_neg(a_result, a_result);
                }
            } else {
                if (wnaf2[i] > 0) {
                    ecdsa_gej_add_ge(a_result, a_result, &table2.table[idx]);
                } else {
                    ecdsa_ge_t neg;
                    ecdsa_ge_neg(&neg, &table2.table[idx]);
                    ecdsa_gej_add_ge(a_result, a_result, &neg);
                }
            }
        }
    }
}

// =============================================================================
// Endomorphism-Accelerated Strauss for Verify
// =============================================================================

// Precomputed φ(G) wNAF table - initialized once
static ecdsa_wnaf_table_t s_table_phi_g;
static bool s_table_phi_g_init = false;

void ecdsa_ecmult_strauss_endo(
    ecdsa_gej_t *a_result,
    const ecdsa_gej_t *a_point_a,
    const ecdsa_scalar_t *a_scalar_a,
    const ecdsa_scalar_t *a_scalar_g)
{
    dap_return_if_fail(a_result);
    
    ecdsa_ecmult_gen_ctx_t *ctx = ecdsa_ecmult_gen_ctx_get();
    
    bool have_a = a_point_a && a_scalar_a && !ecdsa_scalar_is_zero(a_scalar_a);
    bool have_g = a_scalar_g && !ecdsa_scalar_is_zero(a_scalar_g);
    
    // Initialize φ(G) table once
    if (!s_table_phi_g_init) {
        ecdsa_ge_t phi_g;
        ecdsa_ge_mul_lambda(&phi_g, &ctx->wnaf_table[0]);
        ecdsa_wnaf_table_build(&s_table_phi_g, &phi_g, ECDSA_WNAF_WINDOW);
        s_table_phi_g_init = true;
    }
    
    // For point A: check if Z=1 (already affine) to avoid expensive inversion
    ecdsa_ge_t a_affine, phi_a;
    ecdsa_wnaf_table_t table_a, table_phi_a;
    
    if (have_a) {
        // Fast path: check if Z coordinate is 1
        ecdsa_field_t z_norm;
        ecdsa_field_copy(&z_norm, &a_point_a->z);
        ecdsa_field_normalize(&z_norm);
        
        if (z_norm.n[0] == 1 && z_norm.n[1] == 0 && z_norm.n[2] == 0 && 
            z_norm.n[3] == 0 && z_norm.n[4] == 0) {
            // Already affine - just copy
            ecdsa_field_copy(&a_affine.x, &a_point_a->x);
            ecdsa_field_copy(&a_affine.y, &a_point_a->y);
            a_affine.infinity = a_point_a->infinity;
            ecdsa_field_normalize(&a_affine.x);
            ecdsa_field_normalize(&a_affine.y);
        } else {
            // Need conversion (expensive but rare)
            ecdsa_ge_set_gej(&a_affine, a_point_a);
        }
        
        // φ(A) = (β*x, y) - just one field mul, very fast
        ecdsa_ge_mul_lambda(&phi_a, &a_affine);
        
        // Build tables for A and φ(A)
        ecdsa_wnaf_table_build(&table_a, &a_affine, ECDSA_WNAF_WINDOW);
        ecdsa_wnaf_table_build(&table_phi_a, &phi_a, ECDSA_WNAF_WINDOW);
    }
    
    // Decompose scalars using GLV
    ecdsa_scalar_t k1a, k2a, k1g, k2g;
    bool k1a_neg = false, k2a_neg = false, k1g_neg = false, k2g_neg = false;
    
    if (have_a) {
        ecdsa_scalar_split_lambda(&k1a, &k1a_neg, &k2a, &k2a_neg, a_scalar_a);
    }
    if (have_g) {
        ecdsa_scalar_split_lambda(&k1g, &k1g_neg, &k2g, &k2g_neg, a_scalar_g);
    }
    
    // Convert to wNAF
    ecdsa_wnaf_t wnaf_k1a[ECDSA_WNAF_MAX_LEN] = {0};
    ecdsa_wnaf_t wnaf_k2a[ECDSA_WNAF_MAX_LEN] = {0};
    ecdsa_wnaf_t wnaf_k1g[ECDSA_WNAF_MAX_LEN] = {0};
    ecdsa_wnaf_t wnaf_k2g[ECDSA_WNAF_MAX_LEN] = {0};
    
    int len_k1a = 0, len_k2a = 0, len_k1g = 0, len_k2g = 0;
    
    if (have_a) {
        len_k1a = ecdsa_scalar_to_wnaf(wnaf_k1a, &k1a, ECDSA_WNAF_WINDOW);
        len_k2a = ecdsa_scalar_to_wnaf(wnaf_k2a, &k2a, ECDSA_WNAF_WINDOW);
    }
    if (have_g) {
        len_k1g = ecdsa_scalar_to_wnaf(wnaf_k1g, &k1g, ECDSA_WNAF_WINDOW);
        len_k2g = ecdsa_scalar_to_wnaf(wnaf_k2g, &k2g, ECDSA_WNAF_WINDOW);
    }
    
    int max_len = len_k1a;
    if (len_k2a > max_len) max_len = len_k2a;
    if (len_k1g > max_len) max_len = len_k1g;
    if (len_k2g > max_len) max_len = len_k2g;
    
    // Precomputed tables
    const ecdsa_ge_t *tbl_g = ctx->wnaf_table;
    const ecdsa_ge_t *tbl_phi_g = s_table_phi_g.table;
    
    // 4-way simultaneous double-and-add with ~128-bit scalars
    ecdsa_gej_set_infinity(a_result);
    
    for (int i = max_len - 1; i >= 0; i--) {
        // Double
        if (!ecdsa_gej_is_infinity(a_result)) {
            ecdsa_gej_double(a_result, a_result);
        }
        
        // Process k1a (with sign handling via point negation)
        if (have_a && i < len_k1a && wnaf_k1a[i] != 0) {
            int digit = wnaf_k1a[i];
            bool neg = (digit < 0) ^ k1a_neg;
            int idx = (digit > 0 ? digit : -digit) >> 1;
            const ecdsa_ge_t *pt = &table_a.table[idx];
            
            if (ecdsa_gej_is_infinity(a_result)) {
                ecdsa_gej_set_ge(a_result, pt);
                if (neg) ecdsa_gej_neg(a_result, a_result);
            } else if (neg) {
                ecdsa_ge_t tmp;
                ecdsa_ge_neg(&tmp, pt);
                ecdsa_gej_add_ge(a_result, a_result, &tmp);
            } else {
                ecdsa_gej_add_ge(a_result, a_result, pt);
            }
        }
        
        // Process k2a
        if (have_a && i < len_k2a && wnaf_k2a[i] != 0) {
            int digit = wnaf_k2a[i];
            bool neg = (digit < 0) ^ k2a_neg;
            int idx = (digit > 0 ? digit : -digit) >> 1;
            const ecdsa_ge_t *pt = &table_phi_a.table[idx];
            
            if (ecdsa_gej_is_infinity(a_result)) {
                ecdsa_gej_set_ge(a_result, pt);
                if (neg) ecdsa_gej_neg(a_result, a_result);
            } else if (neg) {
                ecdsa_ge_t tmp;
                ecdsa_ge_neg(&tmp, pt);
                ecdsa_gej_add_ge(a_result, a_result, &tmp);
            } else {
                ecdsa_gej_add_ge(a_result, a_result, pt);
            }
        }
        
        // Process k1g
        if (have_g && i < len_k1g && wnaf_k1g[i] != 0) {
            int digit = wnaf_k1g[i];
            bool neg = (digit < 0) ^ k1g_neg;
            int idx = (digit > 0 ? digit : -digit) >> 1;
            const ecdsa_ge_t *pt = &tbl_g[idx];
            
            if (ecdsa_gej_is_infinity(a_result)) {
                ecdsa_gej_set_ge(a_result, pt);
                if (neg) ecdsa_gej_neg(a_result, a_result);
            } else if (neg) {
                ecdsa_ge_t tmp;
                ecdsa_ge_neg(&tmp, pt);
                ecdsa_gej_add_ge(a_result, a_result, &tmp);
            } else {
                ecdsa_gej_add_ge(a_result, a_result, pt);
            }
        }
        
        // Process k2g
        if (have_g && i < len_k2g && wnaf_k2g[i] != 0) {
            int digit = wnaf_k2g[i];
            bool neg = (digit < 0) ^ k2g_neg;
            int idx = (digit > 0 ? digit : -digit) >> 1;
            const ecdsa_ge_t *pt = &tbl_phi_g[idx];
            
            if (ecdsa_gej_is_infinity(a_result)) {
                ecdsa_gej_set_ge(a_result, pt);
                if (neg) ecdsa_gej_neg(a_result, a_result);
            } else if (neg) {
                ecdsa_ge_t tmp;
                ecdsa_ge_neg(&tmp, pt);
                ecdsa_gej_add_ge(a_result, a_result, &tmp);
            } else {
                ecdsa_gej_add_ge(a_result, a_result, pt);
            }
        }
    }
}
