/*
 * Post-Quantum Cryptography Common Hash Module
 * 
 * Unified hash function interface for post-quantum cryptographic algorithms.
 * Provides common hash operations across Kyber, Falcon, Chipmunk, and Dilithium.
 * 
 * This module eliminates code duplication by providing a unified API while
 * maintaining algorithm-specific optimizations and parameter sets.
 */

#ifndef PQ_COMMON_HASH_H
#define PQ_COMMON_HASH_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../poly/pq_common_poly.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Hash function types
 */
typedef enum {
    PQ_HASH_SHAKE128 = 0,      // SHAKE128
    PQ_HASH_SHAKE256 = 1,      // SHAKE256
    PQ_HASH_SHA2_256 = 2,      // SHA2-256
    PQ_HASH_SHA2_512 = 3,      // SHA2-512
    PQ_HASH_SHA3_256 = 4,      // SHA3-256
    PQ_HASH_SHA3_512 = 5,      // SHA3-512
    PQ_HASH_BLAKE2B = 6,       // BLAKE2b
    PQ_HASH_BLAKE2S = 7        // BLAKE2s
} pq_hash_type_t;

/**
 * @brief Hash context structure
 * 
 * Contains algorithm-specific hash parameters and state
 */
typedef struct {
    pq_algorithm_t alg;        // Algorithm identifier
    pq_hash_type_t hash_type;  // Hash function type
    uint32_t output_len;       // Output length in bytes
    uint32_t block_size;       // Block size in bytes
    uint32_t state_size;       // State size in bytes
    
    // Hash function state
    union {
        // SHAKE/SHA3 state
        struct {
            uint64_t state[25]; // SHA3 state
            uint8_t buffer[200]; // Input buffer
            size_t buffer_len;   // Current buffer length
            size_t total_len;    // Total input length
        } shake;
        
        // SHA2 state
        struct {
            uint32_t state[8];   // SHA2 state
            uint8_t buffer[64];  // Input buffer
            size_t buffer_len;   // Current buffer length
            uint64_t total_len;  // Total input length
        } sha2;
        
        // BLAKE2 state
        struct {
            uint64_t state[8];   // BLAKE2 state
            uint8_t buffer[128]; // Input buffer
            size_t buffer_len;   // Current buffer length
            uint64_t total_len;  // Total input length
            uint8_t key_len;     // Key length
        } blake2;
    } state;
    
    // Optimization flags
    bool use_optimized;        // Use optimized implementations
    bool use_vectorized;       // Use vectorized implementations
} pq_hash_ctx_t;

/**
 * @brief Initialize hash context
 * 
 * @param ctx Hash context to initialize
 * @param alg Algorithm identifier
 * @param hash_type Hash function type
 * @param output_len Output length in bytes
 * @return 0 on success, negative on error
 */
int pq_hash_init(pq_hash_ctx_t *ctx, pq_algorithm_t alg, pq_hash_type_t hash_type, uint32_t output_len);

/**
 * @brief Free hash context resources
 * 
 * @param ctx Hash context to free
 */
void pq_hash_free(pq_hash_ctx_t *ctx);

/**
 * @brief Reset hash context for reuse
 * 
 * @param ctx Hash context to reset
 * @return 0 on success, negative on error
 */
int pq_hash_reset(pq_hash_ctx_t *ctx);

/**
 * @brief Update hash with data
 * 
 * @param ctx Hash context
 * @param data Data to hash
 * @param len Length of data
 * @return 0 on success, negative on error
 */
int pq_hash_update(pq_hash_ctx_t *ctx, const uint8_t *data, size_t len);

/**
 * @brief Finalize hash and get output
 * 
 * @param ctx Hash context
 * @param output Output buffer
 * @param output_len Output length
 * @return 0 on success, negative on error
 */
int pq_hash_final(pq_hash_ctx_t *ctx, uint8_t *output, size_t output_len);

/**
 * @brief One-shot hash function
 * 
 * @param ctx Hash context
 * @param output Output buffer
 * @param output_len Output length
 * @param data Input data
 * @param data_len Input data length
 * @return 0 on success, negative on error
 */
int pq_hash_digest(pq_hash_ctx_t *ctx, uint8_t *output, size_t output_len,
                   const uint8_t *data, size_t data_len);

/**
 * @brief SHAKE128 function
 * 
 * @param output Output buffer
 * @param output_len Output length
 * @param input Input data
 * @param input_len Input data length
 * @return 0 on success, negative on error
 */
int pq_hash_shake128(uint8_t *output, size_t output_len,
                     const uint8_t *input, size_t input_len);

/**
 * @brief SHAKE256 function
 * 
 * @param output Output buffer
 * @param output_len Output length
 * @param input Input data
 * @param input_len Input data length
 * @return 0 on success, negative on error
 */
int pq_hash_shake256(uint8_t *output, size_t output_len,
                     const uint8_t *input, size_t input_len);

/**
 * @brief SHA2-256 function
 * 
 * @param output Output buffer (32 bytes)
 * @param input Input data
 * @param input_len Input data length
 * @return 0 on success, negative on error
 */
int pq_hash_sha2_256(uint8_t output[32], const uint8_t *input, size_t input_len);

/**
 * @brief SHA2-512 function
 * 
 * @param output Output buffer (64 bytes)
 * @param input Input data
 * @param input_len Input data length
 * @return 0 on success, negative on error
 */
int pq_hash_sha2_512(uint8_t output[64], const uint8_t *input, size_t input_len);

/**
 * @brief Generate uniform polynomial from hash
 * 
 * @param poly Output polynomial
 * @param seed Seed for generation
 * @param seed_len Seed length
 * @param nonce Nonce value
 * @param hash_type Hash function to use
 * @return 0 on success, negative on error
 */
int pq_hash_uniform_poly(pq_poly_t *poly, const uint8_t *seed, size_t seed_len,
                        uint16_t nonce, pq_hash_type_t hash_type);

/**
 * @brief Generate noise polynomial from hash
 * 
 * @param poly Output polynomial
 * @param seed Seed for generation
 * @param seed_len Seed length
 * @param nonce Nonce value
 * @param eta Noise parameter
 * @param hash_type Hash function to use
 * @return 0 on success, negative on error
 */
int pq_hash_noise_poly(pq_poly_t *poly, const uint8_t *seed, size_t seed_len,
                      uint16_t nonce, int eta, pq_hash_type_t hash_type);

/**
 * @brief Generate challenge polynomial from hash
 * 
 * @param poly Output polynomial
 * @param data Input data
 * @param data_len Input data length
 * @param hash_type Hash function to use
 * @return 0 on success, negative on error
 */
int pq_hash_challenge_poly(pq_poly_t *poly, const uint8_t *data, size_t data_len,
                          pq_hash_type_t hash_type);

/**
 * @brief Hash-based random number generation
 * 
 * @param output Output buffer
 * @param output_len Output length
 * @param seed Seed for generation
 * @param seed_len Seed length
 * @param nonce Nonce value
 * @param hash_type Hash function to use
 * @return 0 on success, negative on error
 */
int pq_hash_random_bytes(uint8_t *output, size_t output_len,
                        const uint8_t *seed, size_t seed_len,
                        uint16_t nonce, pq_hash_type_t hash_type);

/**
 * @brief Keyed hash function (HMAC-like)
 * 
 * @param output Output buffer
 * @param output_len Output length
 * @param key Key
 * @param key_len Key length
 * @param data Input data
 * @param data_len Input data length
 * @param hash_type Hash function to use
 * @return 0 on success, negative on error
 */
int pq_hash_keyed(uint8_t *output, size_t output_len,
                  const uint8_t *key, size_t key_len,
                  const uint8_t *data, size_t data_len,
                  pq_hash_type_t hash_type);

/**
 * @brief Optimized hash for specific platforms
 * 
 * @param ctx Hash context
 * @param output Output buffer
 * @param output_len Output length
 * @param data Input data
 * @param data_len Input data length
 * @return 0 on success, negative on error
 */
int pq_hash_optimized(pq_hash_ctx_t *ctx, uint8_t *output, size_t output_len,
                     const uint8_t *data, size_t data_len);

/**
 * @brief Vectorized hash operations (if available)
 * 
 * @param ctx Hash context
 * @param outputs Array of output buffers
 * @param output_lens Array of output lengths
 * @param count Number of hash operations
 * @param data Input data
 * @param data_len Input data length
 * @return 0 on success, negative on error
 */
int pq_hash_vectorized(pq_hash_ctx_t *ctx, uint8_t **outputs, size_t *output_lens,
                      size_t count, const uint8_t *data, size_t data_len);

/**
 * @brief Get hash parameters for algorithm
 * 
 * @param alg Algorithm identifier
 * @param hash_type Output: recommended hash type
 * @param output_len Output: recommended output length
 * @param block_size Output: block size
 * @return 0 on success, negative on error
 */
int pq_hash_get_params(pq_algorithm_t alg, pq_hash_type_t *hash_type,
                      uint32_t *output_len, uint32_t *block_size);

/**
 * @brief Validate hash context
 * 
 * @param ctx Hash context to validate
 * @return 0 if valid, negative on error
 */
int pq_hash_validate_ctx(const pq_hash_ctx_t *ctx);

/**
 * @brief Get hash performance statistics
 * 
 * @param ctx Hash context
 * @param update_time Output: average update time (microseconds per byte)
 * @param final_time Output: average finalize time (microseconds)
 * @param digest_time Output: average one-shot digest time (microseconds per byte)
 * @return 0 on success, negative on error
 */
int pq_hash_get_stats(pq_hash_ctx_t *ctx, double *update_time,
                     double *final_time, double *digest_time);

/**
 * @brief Constant-time comparison for hash outputs
 * 
 * @param a First hash output
 * @param b Second hash output
 * @param len Length of outputs
 * @return 0 if equal, non-zero if different
 */
int pq_hash_constant_time_cmp(const uint8_t *a, const uint8_t *b, size_t len);

/**
 * @brief Constant-time conditional copy
 * 
 * @param dst Destination buffer
 * @param src Source buffer
 * @param len Length of buffers
 * @param copy true to copy src to dst, false to keep dst unchanged
 */
void pq_hash_constant_time_copy(uint8_t *dst, const uint8_t *src, size_t len, bool copy);

#ifdef __cplusplus
}
#endif

#endif // PQ_COMMON_HASH_H 
