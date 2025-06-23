/*
 * Post-Quantum Cryptography Common NTT Module
 * 
 * Unified Number Theoretic Transform interface for post-quantum cryptographic algorithms.
 * Provides common NTT operations across Kyber, Falcon, Chipmunk, and Dilithium.
 * 
 * This module eliminates code duplication by providing a unified API while
 * maintaining algorithm-specific optimizations and parameter sets.
 */

#ifndef PQ_COMMON_NTT_H
#define PQ_COMMON_NTT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../poly/pq_common_poly.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief NTT domain types
 */
typedef enum {
    PQ_NTT_TIME_DOMAIN = 0,    // Polynomial in time domain
    PQ_NTT_FREQ_DOMAIN = 1     // Polynomial in frequency domain (NTT domain)
} pq_ntt_domain_t;

/**
 * @brief NTT context structure
 * 
 * Contains algorithm-specific NTT parameters and precomputed values
 */
typedef struct {
    pq_algorithm_t alg;        // Algorithm identifier
    uint16_t n;                // Polynomial degree
    uint32_t q;                // Modulus
    pq_coeff_type_t coeff_type; // Coefficient type
    
    // Precomputed values
    union {
        int16_t *zetas_16;     // Kyber zetas
        int32_t *zetas_32;     // Chipmunk/Dilithium zetas
        double *zetas_fpr;     // Falcon zetas
    } zetas;
    
    union {
        int16_t *zetas_inv_16; // Kyber inverse zetas
        int32_t *zetas_inv_32; // Chipmunk/Dilithium inverse zetas
        double *zetas_inv_fpr; // Falcon inverse zetas
    } zetas_inv;
    
    // Montgomery parameters (for algorithms that use them)
    uint32_t mont_r;           // Montgomery R
    uint32_t mont_r_inv;       // Montgomery R^(-1)
    uint32_t q_inv;            // -q^(-1) mod R
    
    // Barrett reduction parameters
    uint32_t barrett_v;        // Barrett reduction constant
    
    size_t zetas_size;         // Size of zetas array
    size_t zetas_inv_size;     // Size of inverse zetas array
} pq_ntt_ctx_t;

/**
 * @brief Initialize NTT context
 * 
 * @param ctx NTT context to initialize
 * @param alg Algorithm identifier
 * @return 0 on success, negative on error
 */
int pq_ntt_init(pq_ntt_ctx_t *ctx, pq_algorithm_t alg);

/**
 * @brief Free NTT context resources
 * 
 * @param ctx NTT context to free
 */
void pq_ntt_free(pq_ntt_ctx_t *ctx);

/**
 * @brief Transform polynomial to NTT domain
 * 
 * @param ctx NTT context
 * @param poly Polynomial to transform
 * @return 0 on success, negative on error
 */
int pq_ntt_forward(pq_ntt_ctx_t *ctx, pq_poly_t *poly);

/**
 * @brief Transform polynomial from NTT domain
 * 
 * @param ctx NTT context
 * @param poly Polynomial to transform
 * @return 0 on success, negative on error
 */
int pq_ntt_inverse(pq_ntt_ctx_t *ctx, pq_poly_t *poly);

/**
 * @brief Pointwise multiplication in NTT domain
 * 
 * @param ctx NTT context
 * @param result Result polynomial
 * @param a First polynomial (must be in NTT domain)
 * @param b Second polynomial (must be in NTT domain)
 * @return 0 on success, negative on error
 */
int pq_ntt_pointwise_mul(pq_ntt_ctx_t *ctx, pq_poly_t *result, 
                        const pq_poly_t *a, const pq_poly_t *b);

/**
 * @brief Add polynomials in NTT domain
 * 
 * @param ctx NTT context
 * @param result Result polynomial
 * @param a First polynomial (must be in NTT domain)
 * @param b Second polynomial (must be in NTT domain)
 * @return 0 on success, negative on error
 */
int pq_ntt_add(pq_ntt_ctx_t *ctx, pq_poly_t *result,
               const pq_poly_t *a, const pq_poly_t *b);

/**
 * @brief Subtract polynomials in NTT domain
 * 
 * @param ctx NTT context
 * @param result Result polynomial
 * @param a First polynomial (must be in NTT domain)
 * @param b Second polynomial (must be in NTT domain)
 * @return 0 on success, negative on error
 */
int pq_ntt_sub(pq_ntt_ctx_t *ctx, pq_poly_t *result,
               const pq_poly_t *a, const pq_poly_t *b);

/**
 * @brief Base multiplication for NTT
 * 
 * @param ctx NTT context
 * @param r Output array of 2 coefficients
 * @param a Input array of 2 coefficients
 * @param b Input array of 2 coefficients
 * @param zeta Zeta value for this multiplication
 * @return 0 on success, negative on error
 */
int pq_ntt_basemul(pq_ntt_ctx_t *ctx, void *r, const void *a, const void *b, int zeta);

/**
 * @brief Montgomery reduction
 * 
 * @param ctx NTT context
 * @param value Value to reduce
 * @return Reduced value
 */
int32_t pq_ntt_montgomery_reduce(pq_ntt_ctx_t *ctx, int64_t value);

/**
 * @brief Barrett reduction
 * 
 * @param ctx NTT context
 * @param value Value to reduce
 * @return Reduced value
 */
int32_t pq_ntt_barrett_reduce(pq_ntt_ctx_t *ctx, int32_t value);

/**
 * @brief Convert to Montgomery domain
 * 
 * @param ctx NTT context
 * @param poly Polynomial to convert
 * @return 0 on success, negative on error
 */
int pq_ntt_to_montgomery(pq_ntt_ctx_t *ctx, pq_poly_t *poly);

/**
 * @brief Convert from Montgomery domain
 * 
 * @param ctx NTT context
 * @param poly Polynomial to convert
 * @return 0 on success, negative on error
 */
int pq_ntt_from_montgomery(pq_ntt_ctx_t *ctx, pq_poly_t *poly);

/**
 * @brief Bit-reverse for NTT
 * 
 * @param x Input value
 * @param bits Number of bits
 * @return Bit-reversed value
 */
int pq_ntt_bit_reverse(int x, int bits);

/**
 * @brief Check if polynomial is in NTT domain
 * 
 * @param poly Polynomial to check
 * @return true if in NTT domain, false otherwise
 */
bool pq_ntt_is_freq_domain(const pq_poly_t *poly);

/**
 * @brief Set polynomial domain flag
 * 
 * @param poly Polynomial to modify
 * @param domain Domain to set
 * @return 0 on success, negative on error
 */
int pq_ntt_set_domain(pq_poly_t *poly, pq_ntt_domain_t domain);

/**
 * @brief Get polynomial domain
 * 
 * @param poly Polynomial to check
 * @return Current domain
 */
pq_ntt_domain_t pq_ntt_get_domain(const pq_poly_t *poly);

/**
 * @brief Optimized NTT for specific platforms
 * 
 * @param ctx NTT context
 * @param poly Polynomial to transform
 * @param forward true for forward transform, false for inverse
 * @return 0 on success, negative on error
 */
int pq_ntt_optimized(pq_ntt_ctx_t *ctx, pq_poly_t *poly, bool forward);

/**
 * @brief Vectorized NTT operations (if available)
 * 
 * @param ctx NTT context
 * @param polys Array of polynomials
 * @param count Number of polynomials
 * @param forward true for forward transform, false for inverse
 * @return 0 on success, negative on error
 */
int pq_ntt_vectorized(pq_ntt_ctx_t *ctx, pq_poly_t *polys, size_t count, bool forward);

/**
 * @brief Get NTT parameters for algorithm
 * 
 * @param alg Algorithm identifier
 * @param n Output: polynomial degree
 * @param q Output: modulus
 * @param coeff_type Output: coefficient type
 * @param mont_r Output: Montgomery R (if applicable)
 * @return 0 on success, negative on error
 */
int pq_ntt_get_params(pq_algorithm_t alg, uint16_t *n, uint32_t *q, 
                     pq_coeff_type_t *coeff_type, uint32_t *mont_r);

/**
 * @brief Validate NTT context
 * 
 * @param ctx NTT context to validate
 * @return 0 if valid, negative on error
 */
int pq_ntt_validate_ctx(const pq_ntt_ctx_t *ctx);

/**
 * @brief Get NTT performance statistics
 * 
 * @param ctx NTT context
 * @param forward_time Output: average forward transform time (microseconds)
 * @param inverse_time Output: average inverse transform time (microseconds)
 * @param mul_time Output: average pointwise multiplication time (microseconds)
 * @return 0 on success, negative on error
 */
int pq_ntt_get_stats(pq_ntt_ctx_t *ctx, double *forward_time, 
                    double *inverse_time, double *mul_time);

#ifdef __cplusplus
}
#endif

#endif // PQ_COMMON_NTT_H 