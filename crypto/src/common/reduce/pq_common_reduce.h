/*
 * Post-Quantum Cryptography Common Reduction Module
 * 
 * Unified modular reduction interface for post-quantum cryptographic algorithms.
 * Provides common reduction operations across Kyber, Falcon, Chipmunk, and Dilithium.
 * 
 * This module eliminates code duplication by providing a unified API while
 * maintaining algorithm-specific optimizations and parameter sets.
 */

#ifndef PQ_COMMON_REDUCE_H
#define PQ_COMMON_REDUCE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../poly/pq_common_poly.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reduction method types
 */
typedef enum {
    PQ_REDUCE_BARRETT = 0,     // Barrett reduction
    PQ_REDUCE_MONTGOMERY = 1,  // Montgomery reduction
    PQ_REDUCE_CENTER = 2,      // Center around 0
    PQ_REDUCE_FREEZE = 3       // Final reduction to canonical form
} pq_reduce_method_t;

/**
 * @brief Reduction context structure
 * 
 * Contains algorithm-specific reduction parameters
 */
typedef struct {
    pq_algorithm_t alg;        // Algorithm identifier
    uint32_t q;                // Modulus
    pq_coeff_type_t coeff_type; // Coefficient type
    
    // Barrett reduction parameters
    uint32_t barrett_v;        // Barrett reduction constant v = floor(2^k / q)
    uint32_t barrett_k;        // Barrett reduction bit width k
    
    // Montgomery reduction parameters
    uint32_t mont_r;           // Montgomery R
    uint32_t mont_r_inv;       // Montgomery R^(-1)
    uint32_t q_inv;            // -q^(-1) mod R
    
    // Center reduction parameters
    int32_t center_bound;      // Bound for centering
    int32_t center_offset;     // Offset for centering
    
    // Optimization flags
    bool use_optimized;        // Use optimized implementations
    bool use_vectorized;       // Use vectorized implementations
} pq_reduce_ctx_t;

/**
 * @brief Initialize reduction context
 * 
 * @param ctx Reduction context to initialize
 * @param alg Algorithm identifier
 * @return 0 on success, negative on error
 */
int pq_reduce_init(pq_reduce_ctx_t *ctx, pq_algorithm_t alg);

/**
 * @brief Free reduction context resources
 * 
 * @param ctx Reduction context to free
 */
void pq_reduce_free(pq_reduce_ctx_t *ctx);

/**
 * @brief Barrett reduction
 * 
 * @param ctx Reduction context
 * @param value Value to reduce
 * @return Reduced value
 */
int32_t pq_reduce_barrett(pq_reduce_ctx_t *ctx, int32_t value);

/**
 * @brief Montgomery reduction
 * 
 * @param ctx Reduction context
 * @param value Value to reduce
 * @return Reduced value
 */
int32_t pq_reduce_montgomery(pq_reduce_ctx_t *ctx, int64_t value);

/**
 * @brief Center reduction (bring to range [-q/2, q/2))
 * 
 * @param ctx Reduction context
 * @param value Value to center
 * @return Centered value
 */
int32_t pq_reduce_center(pq_reduce_ctx_t *ctx, int32_t value);

/**
 * @brief Freeze reduction (final reduction to canonical form)
 * 
 * @param ctx Reduction context
 * @param value Value to freeze
 * @return Frozen value
 */
int32_t pq_reduce_freeze(pq_reduce_ctx_t *ctx, int32_t value);

/**
 * @brief Generic reduction using specified method
 * 
 * @param ctx Reduction context
 * @param value Value to reduce
 * @param method Reduction method to use
 * @return Reduced value
 */
int32_t pq_reduce_generic(pq_reduce_ctx_t *ctx, int64_t value, pq_reduce_method_t method);

/**
 * @brief Reduce polynomial coefficients
 * 
 * @param ctx Reduction context
 * @param poly Polynomial to reduce
 * @param method Reduction method to use
 * @return 0 on success, negative on error
 */
int pq_reduce_poly(pq_reduce_ctx_t *ctx, pq_poly_t *poly, pq_reduce_method_t method);

/**
 * @brief Reduce array of values
 * 
 * @param ctx Reduction context
 * @param values Array of values to reduce
 * @param count Number of values
 * @param method Reduction method to use
 * @return 0 on success, negative on error
 */
int pq_reduce_array(pq_reduce_ctx_t *ctx, void *values, size_t count, pq_reduce_method_t method);

/**
 * @brief Montgomery multiplication
 * 
 * @param ctx Reduction context
 * @param a First value
 * @param b Second value
 * @return Result of Montgomery multiplication
 */
int32_t pq_reduce_montgomery_mul(pq_reduce_ctx_t *ctx, int32_t a, int32_t b);

/**
 * @brief Convert to Montgomery domain
 * 
 * @param ctx Reduction context
 * @param value Value to convert
 * @return Value in Montgomery domain
 */
int32_t pq_reduce_to_montgomery(pq_reduce_ctx_t *ctx, int32_t value);

/**
 * @brief Convert from Montgomery domain
 * 
 * @param ctx Reduction context
 * @param value Value to convert
 * @return Value in normal domain
 */
int32_t pq_reduce_from_montgomery(pq_reduce_ctx_t *ctx, int32_t value);

/**
 * @brief Optimized Barrett reduction for specific platforms
 * 
 * @param ctx Reduction context
 * @param value Value to reduce
 * @return Reduced value
 */
int32_t pq_reduce_barrett_optimized(pq_reduce_ctx_t *ctx, int32_t value);

/**
 * @brief Vectorized reduction operations (if available)
 * 
 * @param ctx Reduction context
 * @param values Array of values to reduce
 * @param count Number of values
 * @param method Reduction method to use
 * @return 0 on success, negative on error
 */
int pq_reduce_vectorized(pq_reduce_ctx_t *ctx, void *values, size_t count, pq_reduce_method_t method);

/**
 * @brief Check if value is in canonical form
 * 
 * @param ctx Reduction context
 * @param value Value to check
 * @return true if in canonical form, false otherwise
 */
bool pq_reduce_is_canonical(pq_reduce_ctx_t *ctx, int32_t value);

/**
 * @brief Get canonical range for algorithm
 * 
 * @param ctx Reduction context
 * @param min Output: minimum value
 * @param max Output: maximum value
 * @return 0 on success, negative on error
 */
int pq_reduce_get_canonical_range(pq_reduce_ctx_t *ctx, int32_t *min, int32_t *max);

/**
 * @brief Validate reduction context
 * 
 * @param ctx Reduction context to validate
 * @return 0 if valid, negative on error
 */
int pq_reduce_validate_ctx(const pq_reduce_ctx_t *ctx);

/**
 * @brief Get reduction parameters for algorithm
 * 
 * @param alg Algorithm identifier
 * @param q Output: modulus
 * @param coeff_type Output: coefficient type
 * @param mont_r Output: Montgomery R (if applicable)
 * @param barrett_v Output: Barrett constant (if applicable)
 * @return 0 on success, negative on error
 */
int pq_reduce_get_params(pq_algorithm_t alg, uint32_t *q, pq_coeff_type_t *coeff_type,
                        uint32_t *mont_r, uint32_t *barrett_v);

/**
 * @brief Get reduction performance statistics
 * 
 * @param ctx Reduction context
 * @param barrett_time Output: average Barrett reduction time (microseconds)
 * @param montgomery_time Output: average Montgomery reduction time (microseconds)
 * @param center_time Output: average center reduction time (microseconds)
 * @return 0 on success, negative on error
 */
int pq_reduce_get_stats(pq_reduce_ctx_t *ctx, double *barrett_time,
                       double *montgomery_time, double *center_time);

/**
 * @brief Constant-time comparison for reduction
 * 
 * @param a First value
 * @param b Second value
 * @return 0 if equal, negative if a < b, positive if a > b
 */
int pq_reduce_constant_time_cmp(int32_t a, int32_t b);

/**
 * @brief Constant-time conditional swap
 * 
 * @param a First value
 * @param b Second value
 * @param swap true to swap, false to keep as is
 */
void pq_reduce_constant_time_swap(int32_t *a, int32_t *b, bool swap);

/**
 * @brief Constant-time conditional move
 * 
 * @param dst Destination
 * @param src Source
 * @param move true to move src to dst, false to keep dst unchanged
 */
void pq_reduce_constant_time_cmove(int32_t *dst, int32_t src, bool move);

#ifdef __cplusplus
}
#endif

#endif // PQ_COMMON_REDUCE_H 