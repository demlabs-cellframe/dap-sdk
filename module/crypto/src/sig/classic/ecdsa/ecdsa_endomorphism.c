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
#include <pthread.h>

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
static pthread_once_t s_endo_once = PTHREAD_ONCE_INIT;

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
static void s_endo_init_impl(void) {
    ecdsa_field_set_b32(&s_beta, s_beta_bytes);
    ecdsa_scalar_set_b32(&s_lambda, s_lambda_bytes, NULL);
    s_endo_initialized = true;
}

static void s_endo_init(void) {
    pthread_once(&s_endo_once, s_endo_init_impl);
}

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
// GLV Decomposition Constants (from bitcoin-core/secp256k1)
// =============================================================================

// g1 and g2 are 256-bit constants for scalar decomposition
// g1 = round(2^384 * b2 / n) where b2 = a1
// g2 = round(2^384 * (-b1) / n)
//
// From bitcoin-core scalar_impl.h:
// g1 = 0x3086D221A7D46BCDE86C90E49284EB153DAA8A1471E8CA7FE893209A45DBB031
// g2 = 0xE4437ED6010E88286F547FA90ABFE4C4221208AC9DF506C61571B4AE8AC47F71

static const uint8_t GLV_G1_BYTES[32] = {
    0x30, 0x86, 0xD2, 0x21, 0xA7, 0xD4, 0x6B, 0xCD,
    0xE8, 0x6C, 0x90, 0xE4, 0x92, 0x84, 0xEB, 0x15,
    0x3D, 0xAA, 0x8A, 0x14, 0x71, 0xE8, 0xCA, 0x7F,
    0xE8, 0x93, 0x20, 0x9A, 0x45, 0xDB, 0xB0, 0x31
};

static const uint8_t GLV_G2_BYTES[32] = {
    0xE4, 0x43, 0x7E, 0xD6, 0x01, 0x0E, 0x88, 0x28,
    0x6F, 0x54, 0x7F, 0xA9, 0x0A, 0xBF, 0xE4, 0xC4,
    0x22, 0x12, 0x08, 0xAC, 0x9D, 0xF5, 0x06, 0xC6,
    0x15, 0x71, 0xB4, 0xAE, 0x8A, 0xC4, 0x7F, 0x71
};

// minus_b1 = 0x00000000000000000000000000000000E4437ED6010E88286F547FA90ABFE4C3
static const uint8_t GLV_MINUS_B1_BYTES[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xE4, 0x43, 0x7E, 0xD6, 0x01, 0x0E, 0x88, 0x28,
    0x6F, 0x54, 0x7F, 0xA9, 0x0A, 0xBF, 0xE4, 0xC3
};

// minus_b2 = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE8A280AC50774346DD765CDA83DB1562C
static const uint8_t GLV_MINUS_B2_BYTES[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
    0x8A, 0x28, 0x0A, 0xC5, 0x07, 0x74, 0x34, 0x6D,
    0xD7, 0x65, 0xCD, 0xA8, 0x3D, 0xB1, 0x56, 0x2C
};

// Lazy-initialized scalar versions
static ecdsa_scalar_t s_g1_scalar;
static ecdsa_scalar_t s_g2_scalar;
static ecdsa_scalar_t s_minus_b1_scalar;
static ecdsa_scalar_t s_minus_b2_scalar;
static bool s_glv_scalars_initialized = false;
static pthread_once_t s_glv_once = PTHREAD_ONCE_INIT;

static void s_init_glv_scalars_impl(void) {
    ecdsa_scalar_set_b32(&s_g1_scalar, GLV_G1_BYTES, NULL);
    ecdsa_scalar_set_b32(&s_g2_scalar, GLV_G2_BYTES, NULL);
    ecdsa_scalar_set_b32(&s_minus_b1_scalar, GLV_MINUS_B1_BYTES, NULL);
    ecdsa_scalar_set_b32(&s_minus_b2_scalar, GLV_MINUS_B2_BYTES, NULL);
    s_glv_scalars_initialized = true;
}

static void s_init_glv_scalars(void) {
    pthread_once(&s_glv_once, s_init_glv_scalars_impl);
}

// =============================================================================
// 256x256 -> high 128 bits multiplication (shift by 384)
// Direct limb access for maximum performance (no byte conversion)
// =============================================================================

#ifdef ECDSA_SCALAR_64BIT

// Compute (a * b) >> 384, where a and b are 256-bit scalars
// Result is ~128-bit (fits in scalar limbs d[0], d[1])
// Works directly with limbs for maximum performance
static void scalar_mul_shift_384(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b) {
#ifdef __SIZEOF_INT128__
    // 160-bit accumulator (c0, c1, c2)
    uint64_t c0 = 0, c1 = 0;
    uint32_t c2 = 0;
    uint64_t l[8];  // 512-bit product
    
    // Schoolbook multiplication into l[0..7]
    // Using accumulator technique from bitcoin-core
    
    // l[0] = a0*b0
    {
        __uint128_t t = (__uint128_t)a->d[0] * b->d[0];
        l[0] = (uint64_t)t;
        c0 = (uint64_t)(t >> 64);
    }
    
    // l[1] = a0*b1 + a1*b0
    {
        __uint128_t t = (__uint128_t)a->d[0] * b->d[1];
        uint64_t th = (uint64_t)(t >> 64);
        uint64_t tl = (uint64_t)t;
        c0 += tl; th += (c0 < tl);
        c1 = th;
        
        t = (__uint128_t)a->d[1] * b->d[0];
        th = (uint64_t)(t >> 64);
        tl = (uint64_t)t;
        c0 += tl; th += (c0 < tl);
        c1 += th; c2 = (c1 < th);
        
        l[1] = c0; c0 = c1; c1 = c2; c2 = 0;
    }
    
    // l[2] = a0*b2 + a1*b1 + a2*b0
    {
        __uint128_t t = (__uint128_t)a->d[0] * b->d[2];
        uint64_t th = (uint64_t)(t >> 64), tl = (uint64_t)t;
        c0 += tl; th += (c0 < tl); c1 += th; c2 += (c1 < th);
        
        t = (__uint128_t)a->d[1] * b->d[1];
        th = (uint64_t)(t >> 64); tl = (uint64_t)t;
        c0 += tl; th += (c0 < tl); c1 += th; c2 += (c1 < th);
        
        t = (__uint128_t)a->d[2] * b->d[0];
        th = (uint64_t)(t >> 64); tl = (uint64_t)t;
        c0 += tl; th += (c0 < tl); c1 += th; c2 += (c1 < th);
        
        l[2] = c0; c0 = c1; c1 = c2; c2 = 0;
    }
    
    // l[3] = a0*b3 + a1*b2 + a2*b1 + a3*b0
    {
        __uint128_t t = (__uint128_t)a->d[0] * b->d[3];
        uint64_t th = (uint64_t)(t >> 64), tl = (uint64_t)t;
        c0 += tl; th += (c0 < tl); c1 += th; c2 += (c1 < th);
        
        t = (__uint128_t)a->d[1] * b->d[2];
        th = (uint64_t)(t >> 64); tl = (uint64_t)t;
        c0 += tl; th += (c0 < tl); c1 += th; c2 += (c1 < th);
        
        t = (__uint128_t)a->d[2] * b->d[1];
        th = (uint64_t)(t >> 64); tl = (uint64_t)t;
        c0 += tl; th += (c0 < tl); c1 += th; c2 += (c1 < th);
        
        t = (__uint128_t)a->d[3] * b->d[0];
        th = (uint64_t)(t >> 64); tl = (uint64_t)t;
        c0 += tl; th += (c0 < tl); c1 += th; c2 += (c1 < th);
        
        l[3] = c0; c0 = c1; c1 = c2; c2 = 0;
    }
    
    // l[4] = a1*b3 + a2*b2 + a3*b1
    {
        __uint128_t t = (__uint128_t)a->d[1] * b->d[3];
        uint64_t th = (uint64_t)(t >> 64), tl = (uint64_t)t;
        c0 += tl; th += (c0 < tl); c1 += th; c2 += (c1 < th);
        
        t = (__uint128_t)a->d[2] * b->d[2];
        th = (uint64_t)(t >> 64); tl = (uint64_t)t;
        c0 += tl; th += (c0 < tl); c1 += th; c2 += (c1 < th);
        
        t = (__uint128_t)a->d[3] * b->d[1];
        th = (uint64_t)(t >> 64); tl = (uint64_t)t;
        c0 += tl; th += (c0 < tl); c1 += th; c2 += (c1 < th);
        
        l[4] = c0; c0 = c1; c1 = c2; c2 = 0;
    }
    
    // l[5] = a2*b3 + a3*b2
    {
        __uint128_t t = (__uint128_t)a->d[2] * b->d[3];
        uint64_t th = (uint64_t)(t >> 64), tl = (uint64_t)t;
        c0 += tl; th += (c0 < tl); c1 += th;
        
        t = (__uint128_t)a->d[3] * b->d[2];
        th = (uint64_t)(t >> 64); tl = (uint64_t)t;
        c0 += tl; th += (c0 < tl); c1 += th;
        
        l[5] = c0; c0 = c1; c1 = 0;
    }
    
    // l[6] = a3*b3
    {
        __uint128_t t = (__uint128_t)a->d[3] * b->d[3];
        c0 += (uint64_t)t;
        c1 = (uint64_t)(t >> 64) + (c0 < (uint64_t)t);
        l[6] = c0;
        l[7] = c1;
    }
    
    // Result is (l >> 384) with rounding
    // shift=384 means: shiftlimbs=6, shiftlow=0
    // r->d[0] = l[6], r->d[1] = l[7]
    // Add rounding bit from l[5] bit 63
    uint64_t round_bit = (l[5] >> 63) & 1;
    r->d[0] = l[6] + round_bit;
    r->d[1] = l[7] + (r->d[0] < round_bit);
    r->d[2] = 0;
    r->d[3] = 0;
    
#else
    // Fallback without __uint128_t - less precise but functional
    // Use 32-bit multiplication
    uint32_t a32[8], b32[8];
    for (int i = 0; i < 4; i++) {
        a32[2*i] = (uint32_t)a->d[i];
        a32[2*i+1] = (uint32_t)(a->d[i] >> 32);
        b32[2*i] = (uint32_t)b->d[i];
        b32[2*i+1] = (uint32_t)(b->d[i] >> 32);
    }
    
    uint64_t acc = 0;
    uint32_t l32[16] = {0};
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            if (i + j >= 12) {  // Only compute high part
                acc += (uint64_t)a32[i] * b32[j];
            }
        }
        if (i >= 4) {
            l32[i + 8 - 4] = (uint32_t)acc;
            acc >>= 32;
        }
    }
    
    r->d[0] = ((uint64_t)l32[13] << 32) | l32[12];
    r->d[1] = ((uint64_t)l32[15] << 32) | l32[14];
    r->d[2] = 0;
    r->d[3] = 0;
#endif
}

#else  // 32-bit platform

static void scalar_mul_shift_384(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b) {
    // 32-bit implementation using 8x32-bit limbs
    uint64_t acc[16] = {0};
    
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            acc[i+j] += (uint64_t)a->d[i] * b->d[j];
        }
    }
    
    // Propagate carries
    for (int i = 0; i < 15; i++) {
        acc[i+1] += acc[i] >> 32;
        acc[i] &= 0xFFFFFFFF;
    }
    
    // Result is bits 384-511 = acc[12..15]
    r->d[0] = (uint32_t)acc[12];
    r->d[1] = (uint32_t)acc[13];
    r->d[2] = (uint32_t)acc[14];
    r->d[3] = (uint32_t)acc[15];
    r->d[4] = 0;
    r->d[5] = 0;
    r->d[6] = 0;
    r->d[7] = 0;
}

#endif // ECDSA_SCALAR_64BIT

// =============================================================================
// GLV Scalar Decomposition (Bitcoin-core algorithm)
// =============================================================================

void ecdsa_scalar_split_lambda(
    ecdsa_scalar_t *a_k1, bool *a_k1_neg,
    ecdsa_scalar_t *a_k2, bool *a_k2_neg,
    const ecdsa_scalar_t *a_k)
{
    dap_return_if_fail(a_k1 && a_k1_neg && a_k2 && a_k2_neg && a_k);
    
    // Initialize GLV constants
    s_init_glv_scalars();
    
    // GLV decomposition: k = k1 + k2*λ (mod n)
    // Bitcoin-core algorithm:
    //   c1 = round(k * g1 / 2^384)
    //   c2 = round(k * g2 / 2^384)
    //   r2 = c1 * minus_b1 + c2 * minus_b2
    //   r1 = k - r2 * lambda
    
    // Step 1: c1 = (k * g1) >> 384 (with rounding)
    ecdsa_scalar_t c1, c2;
    scalar_mul_shift_384(&c1, a_k, &s_g1_scalar);
    scalar_mul_shift_384(&c2, a_k, &s_g2_scalar);
    
    // Step 2: r2 = c1 * minus_b1 + c2 * minus_b2
    ecdsa_scalar_t tmp1, tmp2;
    ecdsa_scalar_mul(&tmp1, &c1, &s_minus_b1_scalar);
    ecdsa_scalar_mul(&tmp2, &c2, &s_minus_b2_scalar);
    ecdsa_scalar_add(a_k2, &tmp1, &tmp2);
    
    // Step 3: r1 = k - r2 * lambda
    ecdsa_scalar_t r2_lambda;
    ecdsa_scalar_mul(&r2_lambda, a_k2, ecdsa_get_lambda());
    ecdsa_scalar_negate(&r2_lambda, &r2_lambda);
    ecdsa_scalar_add(a_k1, a_k, &r2_lambda);
    
    // Determine signs: if scalar > n/2, negate and set flag
    *a_k1_neg = ecdsa_scalar_is_high(a_k1);
    if (*a_k1_neg) {
        ecdsa_scalar_negate(a_k1, a_k1);
    }
    
    *a_k2_neg = ecdsa_scalar_is_high(a_k2);
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
static pthread_once_t s_phi_g_once = PTHREAD_ONCE_INIT;

static void s_init_phi_g_table(void) {
    ecdsa_ecmult_gen_ctx_t *ctx = ecdsa_ecmult_gen_ctx_get();
    ecdsa_ge_t phi_g;
    ecdsa_ge_mul_lambda(&phi_g, &ctx->wnaf_table[0]);
    ecdsa_wnaf_table_build(&s_table_phi_g, &phi_g, ECDSA_WNAF_WINDOW);
}

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
    
    pthread_once(&s_phi_g_once, s_init_phi_g_table);
    
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
