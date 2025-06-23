/*
 * Post-Quantum Cryptography Common Polynomial Module
 * 
 * Unified polynomial arithmetic interface for post-quantum cryptographic algorithms.
 * Provides common operations across Kyber, Falcon, Chipmunk, and Dilithium.
 * 
 * This module eliminates code duplication by providing a unified API while
 * maintaining algorithm-specific optimizations and parameter sets.
 */

#ifndef PQ_COMMON_POLY_H
#define PQ_COMMON_POLY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Algorithm identifiers for parameter selection
 */
typedef enum {
    PQ_ALG_KYBER_512 = 0,
    PQ_ALG_KYBER_768 = 1,
    PQ_ALG_KYBER_1024 = 2,
    PQ_ALG_FALCON_512 = 3,
    PQ_ALG_FALCON_1024 = 4,
    PQ_ALG_CHIPMUNK = 5,
    PQ_ALG_DILITHIUM_2 = 6,
    PQ_ALG_DILITHIUM_3 = 7,
    PQ_ALG_DILITHIUM_5 = 8
} pq_algorithm_t;

/**
 * @brief Polynomial coefficient types
 */
typedef enum {
    PQ_COEFF_INT16 = 0,  // Kyber uses int16_t
    PQ_COEFF_INT32 = 1,  // Chipmunk, Dilithium use int32_t
    PQ_COEFF_FPR = 2     // Falcon uses floating point
} pq_coeff_type_t;

/**
 * @brief Common polynomial structure
 * 
 * This structure can represent polynomials from different algorithms
 * by using the appropriate coefficient type and size.
 */
typedef struct {
    pq_algorithm_t alg;      // Algorithm identifier
    pq_coeff_type_t type;    // Coefficient type
    uint16_t n;              // Polynomial degree (256, 512, 1024)
    uint32_t q;              // Modulus
    union {
        int16_t *coeffs_16;  // For Kyber
        int32_t *coeffs_32;  // For Chipmunk, Dilithium
        double *coeffs_fpr;  // For Falcon
    } coeffs;
    size_t coeffs_size;      // Size of coefficients array
} pq_poly_t;

/**
 * @brief Initialize polynomial structure
 * 
 * @param poly Polynomial to initialize
 * @param alg Algorithm identifier
 * @return 0 on success, negative on error
 */
int pq_poly_init(pq_poly_t *poly, pq_algorithm_t alg);

/**
 * @brief Free polynomial resources
 * 
 * @param poly Polynomial to free
 */
void pq_poly_free(pq_poly_t *poly);

/**
 * @brief Zero polynomial coefficients
 * 
 * @param poly Polynomial to zero
 * @return 0 on success, negative on error
 */
int pq_poly_zero(pq_poly_t *poly);

/**
 * @brief Copy polynomial
 * 
 * @param dst Destination polynomial
 * @param src Source polynomial
 * @return 0 on success, negative on error
 */
int pq_poly_copy(pq_poly_t *dst, const pq_poly_t *src);

/**
 * @brief Add two polynomials
 * 
 * @param result Result polynomial
 * @param a First polynomial
 * @param b Second polynomial
 * @return 0 on success, negative on error
 */
int pq_poly_add(pq_poly_t *result, const pq_poly_t *a, const pq_poly_t *b);

/**
 * @brief Subtract two polynomials
 * 
 * @param result Result polynomial
 * @param a First polynomial
 * @param b Second polynomial
 * @return 0 on success, negative on error
 */
int pq_poly_sub(pq_poly_t *result, const pq_poly_t *a, const pq_poly_t *b);

/**
 * @brief Multiply two polynomials (pointwise in NTT domain)
 * 
 * @param result Result polynomial
 * @param a First polynomial
 * @param b Second polynomial
 * @return 0 on success, negative on error
 */
int pq_poly_mul(pq_poly_t *result, const pq_poly_t *a, const pq_poly_t *b);

/**
 * @brief Reduce polynomial coefficients modulo q
 * 
 * @param poly Polynomial to reduce
 * @return 0 on success, negative on error
 */
int pq_poly_reduce(pq_poly_t *poly);

/**
 * @brief Center polynomial coefficients around 0
 * 
 * @param poly Polynomial to center
 * @return 0 on success, negative on error
 */
int pq_poly_center(pq_poly_t *poly);

/**
 * @brief Generate uniform polynomial from seed
 * 
 * @param poly Output polynomial
 * @param seed Seed for generation
 * @param seed_len Seed length
 * @param nonce Nonce value
 * @return 0 on success, negative on error
 */
int pq_poly_uniform(pq_poly_t *poly, const uint8_t *seed, size_t seed_len, uint16_t nonce);

/**
 * @brief Generate noise polynomial
 * 
 * @param poly Output polynomial
 * @param seed Seed for generation
 * @param seed_len Seed length
 * @param nonce Nonce value
 * @param eta Noise parameter
 * @return 0 on success, negative on error
 */
int pq_poly_noise(pq_poly_t *poly, const uint8_t *seed, size_t seed_len, uint16_t nonce, int eta);

/**
 * @brief Check polynomial norm
 * 
 * @param poly Polynomial to check
 * @param bound Maximum absolute value
 * @return 0 if within bound, 1 if exceeds bound, negative on error
 */
int pq_poly_chknorm(const pq_poly_t *poly, int32_t bound);

/**
 * @brief Decompose polynomial into high and low bits
 * 
 * @param r1 High bits polynomial
 * @param r0 Low bits polynomial
 * @param a Input polynomial
 * @return 0 on success, negative on error
 */
int pq_poly_decompose(pq_poly_t *r1, pq_poly_t *r0, const pq_poly_t *a);

/**
 * @brief Power of 2 rounding
 * 
 * @param r1 High bits polynomial
 * @param r0 Low bits polynomial
 * @param a Input polynomial
 * @return 0 on success, negative on error
 */
int pq_poly_power2round(pq_poly_t *r1, pq_poly_t *r0, const pq_poly_t *a);

/**
 * @brief Make hint bits
 * 
 * @param hint Output hint bits
 * @param a First polynomial
 * @param b Second polynomial
 * @return Number of hints, negative on error
 */
int pq_poly_make_hint(uint8_t *hint, const pq_poly_t *a, const pq_poly_t *b);

/**
 * @brief Use hint bits
 * 
 * @param out Output polynomial
 * @param in Input polynomial
 * @param hint Hint bits
 * @return 0 on success, negative on error
 */
int pq_poly_use_hint(pq_poly_t *out, const pq_poly_t *in, const uint8_t *hint);

/**
 * @brief Pack polynomial to bytes
 * 
 * @param r Output byte array
 * @param poly Input polynomial
 * @param pack_type Packing type (eta, t1, t0, z, w1)
 * @return 0 on success, negative on error
 */
int pq_poly_pack(uint8_t *r, const pq_poly_t *poly, int pack_type);

/**
 * @brief Unpack polynomial from bytes
 * 
 * @param poly Output polynomial
 * @param r Input byte array
 * @param pack_type Packing type (eta, t1, t0, z, w1)
 * @return 0 on success, negative on error
 */
int pq_poly_unpack(pq_poly_t *poly, const uint8_t *r, int pack_type);

/**
 * @brief Convert polynomial to bytes
 * 
 * @param r Output byte array
 * @param poly Input polynomial
 * @return 0 on success, negative on error
 */
int pq_poly_tobytes(uint8_t *r, const pq_poly_t *poly);

/**
 * @brief Convert polynomial from bytes
 * 
 * @param poly Output polynomial
 * @param r Input byte array
 * @return 0 on success, negative on error
 */
int pq_poly_frombytes(pq_poly_t *poly, const uint8_t *r);

/**
 * @brief Convert polynomial to message
 * 
 * @param msg Output message
 * @param poly Input polynomial
 * @return 0 on success, negative on error
 */
int pq_poly_tomsg(uint8_t *msg, const pq_poly_t *poly);

/**
 * @brief Convert polynomial from message
 * 
 * @param poly Output polynomial
 * @param msg Input message
 * @return 0 on success, negative on error
 */
int pq_poly_frommsg(pq_poly_t *poly, const uint8_t *msg);

/**
 * @brief Negate polynomial
 * 
 * @param poly Polynomial to negate
 * @return 0 on success, negative on error
 */
int pq_poly_neg(pq_poly_t *poly);

/**
 * @brief Shift polynomial left
 * 
 * @param poly Polynomial to shift
 * @param k Shift amount
 * @return 0 on success, negative on error
 */
int pq_poly_shiftl(pq_poly_t *poly, unsigned int k);

/**
 * @brief Check if two polynomials are equal
 * 
 * @param a First polynomial
 * @param b Second polynomial
 * @return true if equal, false otherwise
 */
bool pq_poly_equal(const pq_poly_t *a, const pq_poly_t *b);

/**
 * @brief Get algorithm parameters
 * 
 * @param alg Algorithm identifier
 * @param n Output: polynomial degree
 * @param q Output: modulus
 * @param coeff_type Output: coefficient type
 * @return 0 on success, negative on error
 */
int pq_poly_get_params(pq_algorithm_t alg, uint16_t *n, uint32_t *q, pq_coeff_type_t *coeff_type);

#ifdef __cplusplus
}
#endif

#endif // PQ_COMMON_POLY_H 