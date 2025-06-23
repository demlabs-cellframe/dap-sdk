/*
 * Post-Quantum Cryptography Common Module - Main Header
 * 
 * Unified interface for post-quantum cryptographic algorithms.
 * Provides common operations across Kyber, Falcon, Chipmunk, and Dilithium.
 * 
 * This module eliminates code duplication by providing a unified API while
 * maintaining algorithm-specific optimizations and parameter sets.
 * 
 * Usage:
 *   #include "common/pq_common.h"
 *   
 *   // Initialize contexts
 *   pq_poly_t poly;
 *   pq_ntt_ctx_t ntt_ctx;
 *   pq_reduce_ctx_t reduce_ctx;
 *   pq_hash_ctx_t hash_ctx;
 *   
 *   pq_poly_init(&poly, PQ_ALG_KYBER_768);
 *   pq_ntt_init(&ntt_ctx, PQ_ALG_KYBER_768);
 *   pq_reduce_init(&reduce_ctx, PQ_ALG_KYBER_768);
 *   pq_hash_init(&hash_ctx, PQ_ALG_KYBER_768, PQ_HASH_SHAKE256, 32);
 *   
 *   // Use unified operations
 *   pq_poly_uniform(&poly, seed, seed_len, nonce);
 *   pq_ntt_forward(&ntt_ctx, &poly);
 *   pq_reduce_poly(&reduce_ctx, &poly, PQ_REDUCE_BARRETT);
 *   
 *   // Clean up
 *   pq_poly_free(&poly);
 *   pq_ntt_free(&ntt_ctx);
 *   pq_reduce_free(&reduce_ctx);
 *   pq_hash_free(&hash_ctx);
 */

#ifndef PQ_COMMON_H
#define PQ_COMMON_H

// Include all common module headers
#include "poly/pq_common_poly.h"
#include "ntt/pq_common_ntt.h"
#include "reduce/pq_common_reduce.h"
#include "hash/pq_common_hash.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Common module version information
 */
#define PQ_COMMON_VERSION_MAJOR 1
#define PQ_COMMON_VERSION_MINOR 0
#define PQ_COMMON_VERSION_PATCH 0

/**
 * @brief Get common module version string
 * 
 * @return Version string in format "major.minor.patch"
 */
const char* pq_common_get_version(void);

/**
 * @brief Get common module version components
 * 
 * @param major Output: major version
 * @param minor Output: minor version
 * @param patch Output: patch version
 */
void pq_common_get_version_components(int *major, int *minor, int *patch);

/**
 * @brief Initialize all common modules for an algorithm
 * 
 * @param alg Algorithm identifier
 * @param poly Output: initialized polynomial
 * @param ntt_ctx Output: initialized NTT context
 * @param reduce_ctx Output: initialized reduction context
 * @param hash_ctx Output: initialized hash context
 * @param hash_type Hash function type to use
 * @param hash_output_len Hash output length
 * @return 0 on success, negative on error
 */
int pq_common_init_all(pq_algorithm_t alg,
                      pq_poly_t *poly,
                      pq_ntt_ctx_t *ntt_ctx,
                      pq_reduce_ctx_t *reduce_ctx,
                      pq_hash_ctx_t *hash_ctx,
                      pq_hash_type_t hash_type,
                      uint32_t hash_output_len);

/**
 * @brief Free all common module resources
 * 
 * @param poly Polynomial to free
 * @param ntt_ctx NTT context to free
 * @param reduce_ctx Reduction context to free
 * @param hash_ctx Hash context to free
 */
void pq_common_free_all(pq_poly_t *poly,
                       pq_ntt_ctx_t *ntt_ctx,
                       pq_reduce_ctx_t *reduce_ctx,
                       pq_hash_ctx_t *hash_ctx);

/**
 * @brief Validate all common module contexts
 * 
 * @param poly Polynomial to validate
 * @param ntt_ctx NTT context to validate
 * @param reduce_ctx Reduction context to validate
 * @param hash_ctx Hash context to validate
 * @return 0 if all valid, negative on error
 */
int pq_common_validate_all(const pq_poly_t *poly,
                          const pq_ntt_ctx_t *ntt_ctx,
                          const pq_reduce_ctx_t *reduce_ctx,
                          const pq_hash_ctx_t *hash_ctx);

/**
 * @brief Get algorithm information
 * 
 * @param alg Algorithm identifier
 * @param name Output: algorithm name
 * @param n Output: polynomial degree
 * @param q Output: modulus
 * @param coeff_type Output: coefficient type
 * @param security_level Output: security level in bits
 * @return 0 on success, negative on error
 */
int pq_common_get_algorithm_info(pq_algorithm_t alg,
                                const char **name,
                                uint16_t *n,
                                uint32_t *q,
                                pq_coeff_type_t *coeff_type,
                                uint32_t *security_level);

/**
 * @brief Check if algorithm supports specific features
 * 
 * @param alg Algorithm identifier
 * @param feature Feature to check (e.g., "ntt", "montgomery", "vectorized")
 * @return true if supported, false otherwise
 */
bool pq_common_supports_feature(pq_algorithm_t alg, const char *feature);

/**
 * @brief Get recommended hash function for algorithm
 * 
 * @param alg Algorithm identifier
 * @return Recommended hash function type
 */
pq_hash_type_t pq_common_get_recommended_hash(pq_algorithm_t alg);

/**
 * @brief Get recommended reduction method for algorithm
 * 
 * @param alg Algorithm identifier
 * @return Recommended reduction method
 */
pq_reduce_method_t pq_common_get_recommended_reduction(pq_algorithm_t alg);

/**
 * @brief Common polynomial operations with automatic context management
 * 
 * These functions automatically handle NTT and reduction operations
 * based on the algorithm's requirements.
 */

/**
 * @brief Generate uniform polynomial with automatic operations
 * 
 * @param poly Output polynomial
 * @param ntt_ctx NTT context
 * @param hash_ctx Hash context
 * @param seed Seed for generation
 * @param seed_len Seed length
 * @param nonce Nonce value
 * @return 0 on success, negative on error
 */
int pq_common_uniform_poly(pq_poly_t *poly,
                          pq_ntt_ctx_t *ntt_ctx,
                          pq_hash_ctx_t *hash_ctx,
                          const uint8_t *seed,
                          size_t seed_len,
                          uint16_t nonce);

/**
 * @brief Generate noise polynomial with automatic operations
 * 
 * @param poly Output polynomial
 * @param ntt_ctx NTT context
 * @param hash_ctx Hash context
 * @param seed Seed for generation
 * @param seed_len Seed length
 * @param nonce Nonce value
 * @param eta Noise parameter
 * @return 0 on success, negative on error
 */
int pq_common_noise_poly(pq_poly_t *poly,
                        pq_ntt_ctx_t *ntt_ctx,
                        pq_hash_ctx_t *hash_ctx,
                        const uint8_t *seed,
                        size_t seed_len,
                        uint16_t nonce,
                        int eta);

/**
 * @brief Multiply polynomials with automatic NTT operations
 * 
 * @param result Result polynomial
 * @param a First polynomial
 * @param b Second polynomial
 * @param ntt_ctx NTT context
 * @param reduce_ctx Reduction context
 * @return 0 on success, negative on error
 */
int pq_common_poly_mul(pq_poly_t *result,
                      const pq_poly_t *a,
                      const pq_poly_t *b,
                      pq_ntt_ctx_t *ntt_ctx,
                      pq_reduce_ctx_t *reduce_ctx);

/**
 * @brief Add polynomials with automatic reduction
 * 
 * @param result Result polynomial
 * @param a First polynomial
 * @param b Second polynomial
 * @param reduce_ctx Reduction context
 * @return 0 on success, negative on error
 */
int pq_common_poly_add(pq_poly_t *result,
                      const pq_poly_t *a,
                      const pq_poly_t *b,
                      pq_reduce_ctx_t *reduce_ctx);

/**
 * @brief Subtract polynomials with automatic reduction
 * 
 * @param result Result polynomial
 * @param a First polynomial
 * @param b Second polynomial
 * @param reduce_ctx Reduction context
 * @return 0 on success, negative on error
 */
int pq_common_poly_sub(pq_poly_t *result,
                      const pq_poly_t *a,
                      const pq_poly_t *b,
                      pq_reduce_ctx_t *reduce_ctx);

/**
 * @brief Get performance statistics for all modules
 * 
 * @param ntt_ctx NTT context
 * @param reduce_ctx Reduction context
 * @param hash_ctx Hash context
 * @param ntt_forward_time Output: average NTT forward time
 * @param ntt_inverse_time Output: average NTT inverse time
 * @param reduce_time Output: average reduction time
 * @param hash_time Output: average hash time
 * @return 0 on success, negative on error
 */
int pq_common_get_performance_stats(pq_ntt_ctx_t *ntt_ctx,
                                   pq_reduce_ctx_t *reduce_ctx,
                                   pq_hash_ctx_t *hash_ctx,
                                   double *ntt_forward_time,
                                   double *ntt_inverse_time,
                                   double *reduce_time,
                                   double *hash_time);

/**
 * @brief Reset performance statistics for all modules
 * 
 * @param ntt_ctx NTT context
 * @param reduce_ctx Reduction context
 * @param hash_ctx Hash context
 * @return 0 on success, negative on error
 */
int pq_common_reset_performance_stats(pq_ntt_ctx_t *ntt_ctx,
                                     pq_reduce_ctx_t *reduce_ctx,
                                     pq_hash_ctx_t *hash_ctx);

/**
 * @brief Enable/disable optimizations for all modules
 * 
 * @param ntt_ctx NTT context
 * @param reduce_ctx Reduction context
 * @param hash_ctx Hash context
 * @param use_optimized true to enable optimizations, false to disable
 * @return 0 on success, negative on error
 */
int pq_common_set_optimizations(pq_ntt_ctx_t *ntt_ctx,
                               pq_reduce_ctx_t *reduce_ctx,
                               pq_hash_ctx_t *hash_ctx,
                               bool use_optimized);

/**
 * @brief Enable/disable vectorization for all modules
 * 
 * @param ntt_ctx NTT context
 * @param reduce_ctx Reduction context
 * @param hash_ctx Hash context
 * @param use_vectorized true to enable vectorization, false to disable
 * @return 0 on success, negative on error
 */
int pq_common_set_vectorization(pq_ntt_ctx_t *ntt_ctx,
                               pq_reduce_ctx_t *reduce_ctx,
                               pq_hash_ctx_t *hash_ctx,
                               bool use_vectorized);

#ifdef __cplusplus
}
#endif

#endif // PQ_COMMON_H 