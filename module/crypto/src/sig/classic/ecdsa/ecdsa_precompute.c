/*
 * Precomputed tables and optimized scalar multiplication for secp256k1
 * 
 * Key optimizations:
 * 1. Comb method with precomputed table for ecmult_gen (~15-20x speedup)
 * 2. wNAF representation for general scalar mult (~4-5x speedup)
 * 3. Strauss/Shamir for simultaneous mult in verify (~2x speedup)
 * 4. Addition chains for fast inversion (~2x speedup)
 * 5. Batch inversion using Montgomery's trick
 */

#include "ecdsa_precompute.h"
#include "dap_common.h"
#include <string.h>
#include <pthread.h>

#define LOG_TAG "ecdsa_precompute"

// =============================================================================
// Global Generator Context (lazy-initialized, thread-safe)
// =============================================================================

static ecdsa_ecmult_gen_ctx_t s_gen_ctx = {0};
static pthread_once_t s_gen_ctx_once = PTHREAD_ONCE_INIT;

static void s_gen_ctx_init_callback(void) {
    ecdsa_ecmult_gen_ctx_init(&s_gen_ctx);
}

ecdsa_ecmult_gen_ctx_t *ecdsa_ecmult_gen_ctx_get(void) {
    pthread_once(&s_gen_ctx_once, s_gen_ctx_init_callback);
    return &s_gen_ctx;
}

// =============================================================================
// wNAF Conversion
// =============================================================================

/**
 * @brief Convert scalar to window-NAF representation
 * @param[out] a_wnaf Output wNAF digits array
 * @param[in] a_scalar Input scalar value
 * @param[in] a_window Window size (typically 5)
 * @return Number of wNAF digits
 * 
 * wNAF digits are in range [-2^(w-1)+1, 2^(w-1)-1] and odd, or 0
 */
int ecdsa_scalar_to_wnaf(ecdsa_wnaf_t *a_wnaf, const ecdsa_scalar_t *a_scalar, int a_window) {
    dap_return_val_if_fail(a_wnaf && a_scalar, 0);
    
    // Initialize output to zeros
    memset(a_wnaf, 0, ECDSA_WNAF_MAX_LEN * sizeof(ecdsa_wnaf_t));
    
    // Work with a mutable copy as multi-precision integer (288 bits for carry)
    uint32_t l_limbs[9] = {0};
    
    uint8_t l_bytes[32];
    ecdsa_scalar_get_b32(l_bytes, a_scalar);
    
    // Convert big-endian bytes to little-endian 32-bit limbs
    for (int i = 0; i < 8; i++) {
        l_limbs[i] = ((uint32_t)l_bytes[31 - i * 4]) |
                     ((uint32_t)l_bytes[30 - i * 4] << 8) |
                     ((uint32_t)l_bytes[29 - i * 4] << 16) |
                     ((uint32_t)l_bytes[28 - i * 4] << 24);
    }
    
    int l_half = 1 << (a_window - 1);   // 2^(w-1)
    int l_full = 1 << a_window;          // 2^w
    int l_mask = l_full - 1;
    
    int l_pos = 0;
    int l_len = 0;
    
    // Process until all bits are consumed
    while (l_pos < 257) {
        int l_limb_idx = l_pos / 32;
        int l_bit_idx = l_pos % 32;
        
        if (!((l_limbs[l_limb_idx] >> l_bit_idx) & 1)) {
            // Current bit is 0
            a_wnaf[l_pos] = 0;
            l_pos++;
            l_len = l_pos;
        } else {
            // Current bit is 1, extract window bits
            int l_val;
            if (l_bit_idx + a_window <= 32) {
                l_val = (l_limbs[l_limb_idx] >> l_bit_idx) & l_mask;
            } else {
                // Spans two limbs
                l_val = (l_limbs[l_limb_idx] >> l_bit_idx);
                if (l_limb_idx + 1 < 9) {
                    l_val |= (l_limbs[l_limb_idx + 1] << (32 - l_bit_idx));
                }
                l_val &= l_mask;
            }
            
            // Make odd: if val >= 2^(w-1), subtract 2^w and propagate carry
            if (l_val >= l_half) {
                a_wnaf[l_pos] = (int8_t)(l_val - l_full);
                
                // Propagate carry at position pos + w
                int l_carry_pos = l_pos + a_window;
                int l_carry_limb = l_carry_pos / 32;
                int l_carry_bit = l_carry_pos % 32;
                
                while (l_carry_limb < 9) {
                    uint32_t l_old = l_limbs[l_carry_limb];
                    l_limbs[l_carry_limb] += ((uint32_t)1 << l_carry_bit);
                    if (l_limbs[l_carry_limb] >= l_old) 
                        break;
                    l_carry_limb++;
                    l_carry_bit = 0;
                }
            } else {
                a_wnaf[l_pos] = (int8_t)l_val;
            }
            
            // Next w-1 positions are zeros (implicit in wNAF)
            for (int j = 1; j < a_window && l_pos + j < 257; j++) {
                a_wnaf[l_pos + j] = 0;
            }
            l_pos += a_window;
            l_len = l_pos;
        }
    }
    
    // Find actual length
    l_len = 257;
    while (l_len > 1 && a_wnaf[l_len - 1] == 0) 
        l_len--;
    
    return l_len;
}

// =============================================================================
// Precomputation: Generator Table (Comb Method)
// =============================================================================

// Generator point G coordinates (secp256k1)
static const uint8_t s_gx[32] = {
    0x79, 0xBE, 0x66, 0x7E, 0xF9, 0xDC, 0xBB, 0xAC,
    0x55, 0xA0, 0x62, 0x95, 0xCE, 0x87, 0x0B, 0x07,
    0x02, 0x9B, 0xFC, 0xDB, 0x2D, 0xCE, 0x28, 0xD9,
    0x59, 0xF2, 0x81, 0x5B, 0x16, 0xF8, 0x17, 0x98
};
static const uint8_t s_gy[32] = {
    0x48, 0x3A, 0xDA, 0x77, 0x26, 0xA3, 0xC4, 0x65,
    0x5D, 0xA4, 0xFB, 0xFC, 0x0E, 0x11, 0x08, 0xA8,
    0xFD, 0x17, 0xB4, 0x48, 0xA6, 0x85, 0x54, 0x19,
    0x9C, 0x47, 0xD0, 0x8F, 0xFB, 0x10, 0xD4, 0xB8
};

/**
 * @brief Initialize precomputed tables for generator point multiplication
 * @param[out] a_ctx Context to initialize
 */
void ecdsa_ecmult_gen_ctx_init(ecdsa_ecmult_gen_ctx_t *a_ctx) {
    dap_return_if_fail(a_ctx);
    if (a_ctx->initialized) 
        return;
    
    // Ensure base generator is initialized
    ecdsa_ecmult_gen_init();
    
    // Get generator point G
    ecdsa_ge_t l_g;
    ecdsa_field_set_b32(&l_g.x, s_gx);
    ecdsa_field_set_b32(&l_g.y, s_gy);
    l_g.infinity = false;
    
    // ==========================================================================
    // Build comb table: comb_table[i][j] = (j+1) * 2^(4*i) * G
    // ==========================================================================
    ecdsa_gej_t l_base;
    ecdsa_gej_set_ge(&l_base, &l_g);
    
    for (int i = 0; i < ECDSA_ECMULT_GEN_TEETH; i++) {
        // First entry: 1 * 2^(4*i) * G
        ecdsa_ge_set_gej(&a_ctx->comb_table[i][0], &l_base);
        
        // Build 2, 3, ..., 16 multiples
        ecdsa_gej_t l_accum = l_base;
        for (int j = 1; j < ECDSA_ECMULT_GEN_COMB_SIZE; j++) {
            ecdsa_gej_add_ge(&l_accum, &l_accum, &a_ctx->comb_table[i][0]);
            ecdsa_ge_set_gej(&a_ctx->comb_table[i][j], &l_accum);
        }
        
        // base = base * 2^4 = 16 * base
        for (int k = 0; k < 4; k++) {
            ecdsa_gej_double(&l_base, &l_base);
        }
    }
    
    // ==========================================================================
    // Build wNAF table for G: wnaf_table[i] = (2*i + 1) * G
    // ==========================================================================
    ecdsa_gej_t l_gj, l_g2;
    ecdsa_gej_set_ge(&l_gj, &l_g);
    ecdsa_gej_double(&l_g2, &l_gj);
    
    // table[0] = 1*G
    ecdsa_ge_set_gej(&a_ctx->wnaf_table[0], &l_gj);
    
    // Build 3G, 5G, ..., (2*TABLE_SIZE-1)*G
    ecdsa_gej_t l_accum = l_gj;
    for (int i = 1; i < ECDSA_WNAF_TABLE_SIZE; i++) {
        ecdsa_gej_add(&l_accum, &l_accum, &l_g2);
        ecdsa_ge_set_gej(&a_ctx->wnaf_table[i], &l_accum);
    }
    
    a_ctx->initialized = true;
}

/**
 * @brief Clear generator context
 * @param[out] a_ctx Context to clear
 */
void ecdsa_ecmult_gen_ctx_clear(ecdsa_ecmult_gen_ctx_t *a_ctx) {
    dap_return_if_fail(a_ctx);
    memset(a_ctx, 0, sizeof(*a_ctx));
}

// =============================================================================
// Fast Generator Multiplication (Comb Method)
// =============================================================================

/**
 * @brief Fast generator multiplication using precomputed comb table
 * @param[out] a_result Result point n * G in Jacobian coordinates
 * @param[in] a_scalar Scalar multiplier
 * 
 * Uses comb method with 4-bit windows, ~16x faster than naive double-and-add
 */
void ecdsa_ecmult_gen_fast(ecdsa_gej_t *a_result, const ecdsa_scalar_t *a_scalar) {
    dap_return_if_fail(a_result && a_scalar);
    
    ecdsa_ecmult_gen_ctx_t *l_ctx = ecdsa_ecmult_gen_ctx_get();
    
    // Get scalar as bytes
    uint8_t l_bytes[32];
    ecdsa_scalar_get_b32(l_bytes, a_scalar);
    
    // Convert to nibbles (4-bit chunks), little-endian order
    uint8_t l_nibbles[64];
    for (int i = 0; i < 32; i++) {
        l_nibbles[i * 2] = l_bytes[31 - i] & 0xF;
        l_nibbles[i * 2 + 1] = l_bytes[31 - i] >> 4;
    }
    
    // Initialize result to infinity
    ecdsa_gej_set_infinity(a_result);
    
    // Comb method: add precomputed point for each nibble
    for (int i = 0; i < 64; i++) {
        uint8_t l_digit = l_nibbles[i];
        if (l_digit > 0) {
            if (ecdsa_gej_is_infinity(a_result)) {
                ecdsa_gej_set_ge(a_result, &l_ctx->comb_table[i][l_digit - 1]);
            } else {
                ecdsa_gej_add_ge(a_result, a_result, &l_ctx->comb_table[i][l_digit - 1]);
            }
        }
    }
}

// =============================================================================
// wNAF Scalar Multiplication
// =============================================================================

/**
 * @brief Build wNAF precomputation table for point P
 * @param[out] a_table Output table: table[i] = (2*i + 1) * P
 * @param[in] a_point Input point P
 * @param[in] a_window Window size (unused, fixed at compile time)
 */
void ecdsa_wnaf_table_build(ecdsa_wnaf_table_t *a_table, const ecdsa_ge_t *a_point, int a_window) {
    dap_return_if_fail(a_table && a_point);
    (void)a_window;  // Table size is fixed at compile time
    
    // Use batch conversion to avoid 15+ field inversions
    // Build table in Jacobian coordinates first, then batch-convert to affine
    
    ecdsa_gej_t l_pj, l_p2;
    ecdsa_gej_set_ge(&l_pj, a_point);
    ecdsa_gej_double(&l_p2, &l_pj);
    
    // Build 1*P, 3*P, 5*P, ..., (2*TABLE_SIZE-1)*P in Jacobian coordinates
    ecdsa_gej_t l_table_jac[ECDSA_WNAF_TABLE_SIZE];
    l_table_jac[0] = l_pj;
    
    for (int i = 1; i < ECDSA_WNAF_TABLE_SIZE; i++) {
        ecdsa_gej_add(&l_table_jac[i], &l_table_jac[i-1], &l_p2);
    }
    
    // Batch convert all points from Jacobian to affine (only 1 field inversion!)
    ecdsa_ge_set_gej_batch(a_table->table, l_table_jac, ECDSA_WNAF_TABLE_SIZE);
}

/**
 * @brief Scalar multiplication using wNAF algorithm
 * @param[out] a_result Result point n * P in Jacobian coordinates
 * @param[in] a_point Input point P in affine coordinates
 * @param[in] a_scalar Scalar multiplier n
 */
void ecdsa_ecmult_wnaf(ecdsa_gej_t *a_result, const ecdsa_ge_t *a_point, const ecdsa_scalar_t *a_scalar) {
    dap_return_if_fail(a_result && a_point && a_scalar);
    
    // Build precomputation table for P
    ecdsa_wnaf_table_t l_table;
    ecdsa_wnaf_table_build(&l_table, a_point, ECDSA_WNAF_WINDOW);
    
    // Convert scalar to wNAF
    ecdsa_wnaf_t l_wnaf[ECDSA_WNAF_MAX_LEN];
    int l_len = ecdsa_scalar_to_wnaf(l_wnaf, a_scalar, ECDSA_WNAF_WINDOW);
    
    // Double-and-add using wNAF
    ecdsa_gej_set_infinity(a_result);
    
    for (int i = l_len - 1; i >= 0; i--) {
        if (!ecdsa_gej_is_infinity(a_result)) {
            ecdsa_gej_double(a_result, a_result);
        }
        
        if (l_wnaf[i] != 0) {
            int l_idx = (l_wnaf[i] > 0 ? l_wnaf[i] : -l_wnaf[i]) >> 1;
            
            if (ecdsa_gej_is_infinity(a_result)) {
                ecdsa_gej_set_ge(a_result, &l_table.table[l_idx]);
                if (l_wnaf[i] < 0) {
                    ecdsa_gej_neg(a_result, a_result);
                }
            } else {
                if (l_wnaf[i] > 0) {
                    ecdsa_gej_add_ge(a_result, a_result, &l_table.table[l_idx]);
                } else {
                    ecdsa_ge_t l_neg;
                    ecdsa_ge_neg(&l_neg, &l_table.table[l_idx]);
                    ecdsa_gej_add_ge(a_result, a_result, &l_neg);
                }
            }
        }
    }
}

// =============================================================================
// Strauss/Shamir Simultaneous Multiplication with Split-128 Optimization
// =============================================================================

// Helper: add point from wNAF table
static inline void s_add_from_wnaf_table(ecdsa_gej_t *r, const ecdsa_wnaf_table_t *table, int digit) {
    if (digit == 0) return;
    
    int idx = (digit > 0 ? digit : -digit) >> 1;
    
    if (ecdsa_gej_is_infinity(r)) {
        ecdsa_gej_set_ge(r, &table->table[idx]);
        if (digit < 0) {
            ecdsa_gej_neg(r, r);
        }
    } else {
        if (digit > 0) {
            ecdsa_gej_add_ge(r, r, &table->table[idx]);
        } else {
            ecdsa_ge_t neg;
            ecdsa_ge_neg(&neg, &table->table[idx]);
            ecdsa_gej_add_ge(r, r, &neg);
        }
    }
}

// Helper: add point from static precomputed table (for G)
static inline void s_add_from_pre_g(ecdsa_gej_t *r, int digit) {
    if (digit == 0) return;
    
    ecdsa_ge_t point;
    ecdsa_ecmult_table_get_ge(&point, digit);
    
    if (ecdsa_gej_is_infinity(r)) {
        ecdsa_gej_set_ge(r, &point);
    } else {
        ecdsa_gej_add_ge(r, r, &point);
    }
}

// Helper: add point from static precomputed table (for G*2^128)
static inline void s_add_from_pre_g_128(ecdsa_gej_t *r, int digit) {
    if (digit == 0) return;
    
    ecdsa_ge_t point;
    ecdsa_ecmult_table_get_ge_128(&point, digit);
    
    if (ecdsa_gej_is_infinity(r)) {
        ecdsa_gej_set_ge(r, &point);
    } else {
        ecdsa_gej_add_ge(r, r, &point);
    }
}

/**
 * @brief Simultaneous scalar multiplication using Strauss/Shamir algorithm
 * @param[out] a_result Result: na * a + ng * G
 * @param[in] a_point Point A in Jacobian coordinates (can be NULL if na is zero)
 * @param[in] a_scalar_a Scalar multiplier for A
 * @param[in] a_scalar_g Scalar multiplier for generator G
 * 
 * OPTIMIZATIONS (like bitcoin-core):
 * 1. Split ng into ng_1 + ng_128 * 2^128 (reduces iterations from 256 to 129)
 * 2. Use WINDOW_G=15 precomputed tables (8192 points instead of 16)
 * 3. Use static precomputed tables (no runtime computation)
 */
void ecdsa_ecmult_strauss(ecdsa_gej_t *a_result, const ecdsa_gej_t *a_point, 
                          const ecdsa_scalar_t *a_scalar_a, const ecdsa_scalar_t *a_scalar_g) {
    dap_return_if_fail(a_result);
    
    // =========================================================================
    // Split ng into ng_1 + ng_128 * 2^128 (each ~128 bits)
    // This halves the number of loop iterations!
    // =========================================================================
    ecdsa_scalar_t ng_1, ng_128;
    int wnaf_ng_1[129] = {0}, wnaf_ng_128[129] = {0};
    int bits_ng_1 = 0, bits_ng_128 = 0;
    
    if (a_scalar_g && !ecdsa_scalar_is_zero(a_scalar_g)) {
        ecdsa_scalar_split_128(&ng_1, &ng_128, a_scalar_g);
        
        // Build wNAF for both halves (using WINDOW_G for large precomputed tables)
        bits_ng_1 = ecdsa_scalar_to_wnaf((ecdsa_wnaf_t*)wnaf_ng_1, &ng_1, ECDSA_WINDOW_G);
        bits_ng_128 = ecdsa_scalar_to_wnaf((ecdsa_wnaf_t*)wnaf_ng_128, &ng_128, ECDSA_WINDOW_G);
    }
    
    // =========================================================================
    // Build wNAF for arbitrary point A (uses small runtime table, WINDOW=5)
    // =========================================================================
    ecdsa_wnaf_t wnaf_a[ECDSA_WNAF_MAX_LEN] = {0};
    int bits_a = 0;
    ecdsa_wnaf_table_t table_a;
    
    if (a_point && a_scalar_a && !ecdsa_scalar_is_zero(a_scalar_a)) {
        bits_a = ecdsa_scalar_to_wnaf(wnaf_a, a_scalar_a, ECDSA_WNAF_WINDOW);
        
        // Convert Jacobian to affine for table construction
        ecdsa_ge_t affine;
        ecdsa_ge_set_gej(&affine, a_point);
        ecdsa_wnaf_table_build(&table_a, &affine, ECDSA_WNAF_WINDOW);
    }
    
    // =========================================================================
    // Find maximum bit length to process
    // =========================================================================
    int bits = bits_a;
    if (bits_ng_1 > bits) bits = bits_ng_1;
    if (bits_ng_128 > bits) bits = bits_ng_128;
    
    // =========================================================================
    // Main loop: simultaneous double-and-add
    // Much faster because we only iterate ~129 times (not ~256)
    // =========================================================================
    ecdsa_gej_set_infinity(a_result);
    
    for (int i = bits - 1; i >= 0; i--) {
        // Double
        if (!ecdsa_gej_is_infinity(a_result)) {
            ecdsa_gej_double(a_result, a_result);
        }
        
        // Add from scalar_a (arbitrary point, small table)
        if (i < bits_a && wnaf_a[i] != 0) {
            s_add_from_wnaf_table(a_result, &table_a, wnaf_a[i]);
        }
        
        // Add from ng_1 (uses pre_g static table - huge, fast)
        if (i < bits_ng_1 && wnaf_ng_1[i] != 0) {
            s_add_from_pre_g(a_result, wnaf_ng_1[i]);
        }
        
        // Add from ng_128 (uses pre_g_128 static table - huge, fast)
        if (i < bits_ng_128 && wnaf_ng_128[i] != 0) {
            s_add_from_pre_g_128(a_result, wnaf_ng_128[i]);
        }
    }
}

// =============================================================================
// Batch Jacobian to Affine Conversion
// Uses ecdsa_field_inv_batch from ecdsa_field.c (Montgomery's trick)
// =============================================================================

void ecdsa_ge_set_gej_batch(ecdsa_ge_t *r, const ecdsa_gej_t *a, size_t n) {
    dap_return_if_fail(r && a);
    if (n == 0) return;
    
    // Collect all Z coordinates
    ecdsa_field_t *zs = DAP_NEW_Z_COUNT(ecdsa_field_t, n);
    ecdsa_field_t *zinvs = DAP_NEW_Z_COUNT(ecdsa_field_t, n);
    size_t *indices = DAP_NEW_Z_COUNT(size_t, n);
    
    if (!zs || !zinvs || !indices) {
        // Fallback to individual conversions
        for (size_t i = 0; i < n; i++) {
            ecdsa_ge_set_gej(&r[i], &a[i]);
        }
        DAP_DELETE(zs);
        DAP_DELETE(zinvs);
        DAP_DELETE(indices);
        return;
    }
    
    // Handle infinity points and collect Zs
    size_t count = 0;
    for (size_t i = 0; i < n; i++) {
        if (a[i].infinity) {
            ecdsa_ge_set_infinity(&r[i]);
        } else {
            zs[count] = a[i].z;
            indices[count] = i;
            count++;
        }
    }
    
    // Batch invert Z coordinates
    if (count > 0) {
        ecdsa_field_inv_batch(zinvs, zs, count);
        
        // Convert each point
        for (size_t j = 0; j < count; j++) {
            size_t i = indices[j];
            ecdsa_field_t z2, z3;
            
            // z2 = 1/Z^2
            ecdsa_field_sqr(&z2, &zinvs[j]);
            
            // z3 = 1/Z^3
            ecdsa_field_mul(&z3, &z2, &zinvs[j]);
            
            // x = X/Z^2
            ecdsa_field_mul(&r[i].x, &a[i].x, &z2);
            ecdsa_field_normalize(&r[i].x);
            
            // y = Y/Z^3
            ecdsa_field_mul(&r[i].y, &a[i].y, &z3);
            ecdsa_field_normalize(&r[i].y);
            
            r[i].infinity = false;
        }
    }
    
    DAP_DELETE(zs);
    DAP_DELETE(zinvs);
    DAP_DELETE(indices);
}
