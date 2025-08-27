/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net    https:/gitlab.com/demlabs
 * Kelvin Project https://github.com/kelvinblockchain
 * Copyright  (c) 2017-2018
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <stdint.h>
#include "dap_common.h"
#include "dap_enc_ca.h"
#include "dap_enc_key.h"
#include "dap_hash.h"
#include "dap_string.h"
#include "dap_json.h"

enum dap_sign_type_enum {
    SIG_TYPE_NULL = 0x0000,
    SIG_TYPE_BLISS = 0x0001,
    SIG_TYPE_TESLA = 0x0003, /// @brief
    SIG_TYPE_PICNIC = 0x0101, /// @brief
    SIG_TYPE_DILITHIUM = 0x0102, /// @brief
    SIG_TYPE_FALCON = 0x0103, /// @brief Falcon signature
    SIG_TYPE_SPHINCSPLUS = 0x0104, /// @brief Sphincs+ signature
    SIG_TYPE_ECDSA = 0x105,
    SIG_TYPE_SHIPOVNIK = 0x0106,
    SIG_TYPE_CHIPMUNK = 0x0107, /// @brief Chipmunk signature
#ifdef DAP_PQLR
    SIG_TYPE_PQLR_DILITHIUM = 0x1102,
    SIG_TYPE_PQLR_FALCON = 0x1103,
    SIG_TYPE_PQLR_SPHINCS = 0x1104,
#endif
    SIG_TYPE_MULTI_ECDSA_DILITHIUM = 0x0eff,
    SIG_TYPE_MULTI_CHAINED = 0x0f00, ///  @brief Has inside subset of different signatures and sign composed with all of them
    SIG_TYPE_MULTI_COMBINED = 0x0f01 ///  @brief Has inside subset of different public keys and sign composed with all of appropriate private keys
};
typedef uint32_t dap_sign_type_enum_t;

#define DAP_SIGN_HASH_TYPE_NONE      0x00
#define DAP_SIGN_HASH_TYPE_SHA3      0x01
#define DAP_SIGN_HASH_TYPE_STREEBOG  0x02
#define DAP_SIGN_HASH_TYPE_SIGN      0x0e
#define DAP_SIGN_HASH_TYPE_DEFAULT   0x0f  // not transferred in network, first try use sign hash, if false, use s_sign_hash_type_default

#define DAP_SIGN_PKEY_HASHING_FLAG BIT(7)
#define DAP_SIGN_ADD_PKEY_HASHING_FLAG(a) ((a) | DAP_SIGN_PKEY_HASHING_FLAG)
#define DAP_SIGN_REMOVE_PKEY_HASHING_FLAG(a) ((a) & ~DAP_SIGN_PKEY_HASHING_FLAG)
#define DAP_SIGN_GET_PKEY_HASHING_FLAG(a) ((a) & DAP_SIGN_PKEY_HASHING_FLAG)

typedef union dap_sign_type {
    dap_sign_type_enum_t type;
    uint32_t raw;
} DAP_ALIGN_PACKED dap_sign_type_t;

typedef struct dap_sign_hdr {
        dap_sign_type_t type; /// Signature type
        uint8_t hash_type;
        uint8_t padding;
        uint32_t sign_size; /// Signature size
        uint32_t sign_pkey_size; /// Signature serialized public key size
} DAP_ALIGN_PACKED dap_sign_hdr_t;

/**
  * @struct dap_sign
  * @brief Chain storage format for digital signature
  */
typedef struct dap_sign
{
    dap_sign_hdr_t header; /// Only header's hash is used for verification
    uint8_t pkey_n_sign[]; /// @param sig @brief raw signature data
} DAP_ALIGN_PACKED dap_sign_t;
typedef struct dap_pkey dap_pkey_t;
typedef dap_pkey_t *(*dap_sign_callback_t)(const uint8_t *);

#ifdef __cplusplus
extern "C" {
#endif


int dap_sign_init(uint8_t a_sign_hash_type_default);

uint64_t dap_sign_get_size(dap_sign_t * a_chain_sign);
int dap_sign_verify_by_pkey(dap_sign_t *a_chain_sign, const void *a_data, const size_t a_data_size, dap_pkey_t *a_pkey);
DAP_STATIC_INLINE int dap_sign_verify (dap_sign_t *a_chain_sign, const void *a_data, const size_t a_data_size)
{
    return dap_sign_verify_by_pkey(a_chain_sign, a_data, a_data_size, NULL);
}

/**
 * @brief verify, if a_sign->header.sign_pkey_size and a_sign->header.sign_size bigger, then a_max_key_size
 * @param a_sign signed data object 
 * @param a_max_sign_size max size of signature
 * @return 0 if pass, otjer if not 
 */
DAP_STATIC_INLINE int dap_sign_verify_size(dap_sign_t *a_sign, size_t a_max_sign_size)
{
    return !(a_sign && (a_max_sign_size > sizeof(dap_sign_t)) && (a_sign->header.sign_size) &&
           (a_sign->header.sign_pkey_size) && (a_sign->header.type.type != SIG_TYPE_NULL) &&
           ((uint64_t)a_sign->header.sign_size + a_sign->header.sign_pkey_size + sizeof(dap_sign_t) <= (uint64_t)a_max_sign_size));
}

/**
 * @brief dap_sign_verify_all
 * @param a_sign
 * @param a_sign_size_max
 * @param a_data
 * @param a_data_size
 * @return
 */
DAP_STATIC_INLINE int dap_sign_verify_all(dap_sign_t *a_sign, const size_t a_sign_size_max, const void * a_data, const size_t a_data_size)
{
    return dap_sign_verify_size(a_sign, a_sign_size_max) ? -2 : dap_sign_verify(a_sign, a_data, a_data_size) ? -1 : 0;
}

const char *dap_sign_get_str_recommended_types();

// Create sign of data hash with key provided algorythm of signing and hashing (independently)
dap_sign_t * dap_sign_create_with_hash_type(dap_enc_key_t *a_key, const void * a_data, const size_t a_data_size, uint32_t a_hash_type);

DAP_STATIC_INLINE dap_sign_t *dap_sign_create(dap_enc_key_t *a_key, const void *a_data, const size_t a_data_size)
{
    return dap_sign_create_with_hash_type(a_key, a_data, a_data_size, DAP_SIGN_HASH_TYPE_DEFAULT);
}
//Create sign on raw data without hashing. Singing algorythm is key provided
int dap_sign_create_output(dap_enc_key_t *a_key, const void * a_data, const size_t a_data_size, void * a_output, size_t *a_output_size);

size_t dap_sign_create_output_unserialized_calc_size(dap_enc_key_t *a_key);
//int dap_sign_create_output(dap_enc_key_t *a_key, const void * a_data, const size_t a_data_size
//                                 , void * a_output, size_t a_output_size );

dap_sign_type_t dap_sign_type_from_key_type( dap_enc_key_type_t a_key_type);
dap_enc_key_type_t  dap_sign_type_to_key_type(dap_sign_type_t  a_chain_sign_type);

uint8_t* dap_sign_get_sign(dap_sign_t *a_sign, size_t *a_sign_size);
uint8_t* dap_sign_get_pkey(dap_sign_t *a_sign, size_t *a_pub_key_size);
bool dap_sign_get_pkey_hash(dap_sign_t *a_sign, dap_chain_hash_fast_t *a_sign_hash);
bool dap_sign_compare_pkeys(dap_sign_t *a_sign1, dap_sign_t *a_sign2);

dap_enc_key_t *dap_sign_to_enc_key_by_pkey(dap_sign_t *a_chain_sign, dap_pkey_t *a_pkey);
DAP_STATIC_INLINE dap_enc_key_t *dap_sign_to_enc_key(dap_sign_t * a_chain_sign)
{  
    return dap_sign_to_enc_key_by_pkey(a_chain_sign, NULL);
}



const char * dap_sign_type_to_str(dap_sign_type_t a_chain_sign_type);
dap_sign_type_t dap_sign_type_from_str(const char * a_type_str);
bool dap_sign_type_is_depricated(dap_sign_type_t a_sign_type);
dap_sign_t **dap_sign_get_unique_signs(void *a_data, size_t a_data_size, size_t *a_signs_count);

void dap_sign_get_information(dap_sign_t *a_sign, dap_string_t *a_str_out, const char *a_hash_out_type);
int dap_sign_get_information_json(dap_sign_t* a_sign, dap_json_t *a_json_out, const char *a_hash_out_type, int a_version);

int dap_sign_set_pkey_by_hash_callback (dap_sign_callback_t a_callback);

/**
 * @brief get SHA3 hash of buffer (a_sign), storing in output buffer a_sign_hash
 * @param a_sign to check
 * @return true or false
 */
DAP_STATIC_INLINE bool dap_sign_is_use_pkey_hash(dap_sign_t *a_sign)
{
    return  a_sign && DAP_SIGN_GET_PKEY_HASHING_FLAG(a_sign->header.hash_type);
}

// === Extended Signature Operations API ===

/**
 * @brief Types of signature aggregation algorithms
 */
typedef enum dap_sign_aggregation_type {
    DAP_SIGN_AGGREGATION_TYPE_NONE = 0,
    DAP_SIGN_AGGREGATION_TYPE_TREE_BASED,     // Tree-based aggregation (Chipmunk, ring signatures)
    DAP_SIGN_AGGREGATION_TYPE_LINEAR,         // Linear aggregation (BLS-style)
    DAP_SIGN_AGGREGATION_TYPE_RING,          // Ring signatures
    DAP_SIGN_AGGREGATION_TYPE_THRESHOLD,     // Threshold signatures
    DAP_SIGN_AGGREGATION_TYPE_MULTI_SCHEME   // Multi-scheme aggregation
} dap_sign_aggregation_type_t;

/**
 * @brief Context for batch signature verification
 */
typedef struct dap_sign_batch_verify_ctx {
    dap_sign_type_t signature_type;               // Type of signatures in batch
    uint32_t max_signatures;                      // Maximum number of signatures
    uint32_t signatures_count;                    // Current number of signatures
    dap_sign_t **signatures;                      // Array of signatures
    void **messages;                              // Array of messages
    size_t *message_sizes;                        // Array of message sizes
    dap_pkey_t **public_keys;                     // Array of public keys (optional)
} dap_sign_batch_verify_ctx_t;

/**
 * @brief Aggregation parameters for different algorithms
 */
typedef struct dap_sign_aggregation_params {
    dap_sign_aggregation_type_t aggregation_type;
    union {
        struct {
            uint32_t *signer_indices;             // For tree-based aggregation
            uint32_t tree_depth;                  // Tree depth hint
        } tree_params;
        struct {
            uint32_t threshold;                   // For threshold signatures
            uint32_t total_participants;
        } threshold_params;
        struct {
            uint32_t ring_size;                   // For ring signatures
            bool hide_signer_identity;
        } ring_params;
    };
} dap_sign_aggregation_params_t;

/**
 * @brief Performance statistics for signature operations
 */
typedef struct dap_sign_performance_stats {
    double aggregation_time_ms;                   // Time for aggregation
    double verification_time_ms;                  // Time for verification
    double batch_verification_time_ms;            // Time for batch verification
    uint32_t signatures_processed;                // Number of signatures processed
    double throughput_sigs_per_sec;               // Signatures per second
    size_t memory_usage_bytes;                    // Memory usage
} dap_sign_performance_stats_t;

// === Core Extended Signature Functions ===

/**
 * @brief Aggregate multiple signatures into a single signature
 * @param a_signatures Array of signatures to aggregate
 * @param a_signatures_count Number of signatures
 * @param a_params Aggregation parameters (algorithm-specific)
 * @return Aggregated signature or NULL on error (works only for aggregation-capable signature types)
 */
dap_sign_t *dap_sign_aggregate_signatures(
    dap_sign_t **a_signatures,
    uint32_t a_signatures_count,
    const dap_sign_aggregation_params_t *a_params
);

/**
 * @brief Verify an aggregated signature against multiple messages
 * @param a_aggregated_sign Aggregated signature to verify
 * @param a_messages Array of messages that were signed
 * @param a_message_sizes Array of message sizes
 * @param a_public_keys Array of public keys for verification (optional)
 * @param a_signers_count Number of signers
 * @return 0 on success, negative on error (works only for aggregation-capable signature types)
 */
int dap_sign_verify_aggregated(
    dap_sign_t *a_aggregated_sign,
    const void **a_messages,
    const size_t *a_message_sizes,
    dap_pkey_t **a_public_keys,
    uint32_t a_signers_count
);

// === Batch Verification Functions ===

/**
 * @brief Create a new batch verification context
 * @param a_signature_type Type of signatures in the batch
 * @param a_max_signatures Maximum number of signatures
 * @return New context or NULL on error
 */
dap_sign_batch_verify_ctx_t *dap_sign_batch_verify_ctx_new(
    dap_sign_type_t a_signature_type,
    uint32_t a_max_signatures
);

/**
 * @brief Free a batch verification context
 * @param a_ctx Context to free
 */
void dap_sign_batch_verify_ctx_free(dap_sign_batch_verify_ctx_t *a_ctx);

/**
 * @brief Add a signature to the batch verification context
 * @param a_ctx Batch verification context
 * @param a_signature Signature to add
 * @param a_message Message that was signed
 * @param a_message_size Size of the message
 * @param a_public_key Public key for verification (optional)
 * @return 0 on success, negative on error
 */
int dap_sign_batch_verify_add_signature(
    dap_sign_batch_verify_ctx_t *a_ctx,
    dap_sign_t *a_signature,
    const void *a_message,
    size_t a_message_size,
    dap_pkey_t *a_public_key
);

/**
 * @brief Execute batch verification of all signatures in the context
 * @param a_ctx Batch verification context
 * @return 0 if all signatures valid, negative on error
 */
int dap_sign_batch_verify_execute(dap_sign_batch_verify_ctx_t *a_ctx);

// === Extended Signature Query Functions ===

/**
 * @brief Check if a signature type supports aggregation
 * @param a_signature_type Signature type to check
 * @return true if aggregation is supported
 */
bool dap_sign_type_supports_aggregation(dap_sign_type_t a_signature_type);

/**
 * @brief Check if a signature type supports batch verification
 * @param a_signature_type Signature type to check
 * @return true if batch verification is supported
 */
bool dap_sign_type_supports_batch_verification(dap_sign_type_t a_signature_type);

/**
 * @brief Get supported aggregation types for a signature algorithm
 * @param a_signature_type Signature type
 * @param a_aggregation_types Output array of supported aggregation types
 * @param a_max_types Maximum number of types to return
 * @return Number of supported aggregation types
 */
uint32_t dap_sign_get_supported_aggregation_types(
    dap_sign_type_t a_signature_type,
    dap_sign_aggregation_type_t *a_aggregation_types,
    uint32_t a_max_types
);

/**
 * @brief Check if a signature is aggregated (contains multiple signatures)
 * @param a_sign Signature to check
 * @return true if signature is aggregated
 */
bool dap_sign_is_aggregated(dap_sign_t *a_sign);

/**
 * @brief Get the number of signatures in an aggregated signature
 * @param a_sign Aggregated signature
 * @return Number of signatures or 1 for regular signatures, 0 on error
 */
uint32_t dap_sign_get_signers_count(dap_sign_t *a_sign);

// === Performance Benchmarking ===

/**
 * @brief Benchmark aggregation performance for a specific algorithm
 * @param a_signature_type Type of signatures to benchmark
 * @param a_aggregation_type Type of aggregation to benchmark
 * @param a_signatures_count Number of signatures to benchmark
 * @param a_stats Output statistics
 * @return 0 on success, negative on error
 */
int dap_sign_benchmark_aggregation(
    dap_sign_type_t a_signature_type,
    dap_sign_aggregation_type_t a_aggregation_type,
    uint32_t a_signatures_count,
    dap_sign_performance_stats_t *a_stats
);

/**
 * @brief Benchmark batch verification performance
 * @param a_signature_type Type of signatures to benchmark
 * @param a_signatures_count Number of signatures to benchmark
 * @param a_stats Output statistics
 * @return 0 on success, negative on error
 */
int dap_sign_benchmark_batch_verification(
    dap_sign_type_t a_signature_type,
    uint32_t a_signatures_count,
    dap_sign_performance_stats_t *a_stats
);

#ifdef __cplusplus
}
#endif
