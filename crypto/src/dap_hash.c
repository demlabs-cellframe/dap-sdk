/*
 * Authors:
 * Dmitriy A. Gearasimov <kahovski@gmail.com>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://github.com/demlabsinc
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
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "dap_strfuncs.h"
#include "dap_common.h"
#include "dap_hash.h"
#include "dap_enc_base58.h"
// XKCP includes moved here from header for encapsulation
#include "KeccakHash.h"
#include "SimpleFIPS202.h"
#include "hash/sha2-256/dap_sha2_256.h"

#define LOG_TAG "dap_hash"

static bool s_debug_more = false;
/**
 * @brief dap_chain_str_to_hash_fast_to_str
 * @param a_hash_str
 * @param a_hash
 * @return
 */
int dap_chain_hash_fast_from_hex_str( const char *a_hex_str, dap_chain_hash_fast_t *a_hash)
{
    if (!a_hex_str || !a_hash)
        return -1;
    size_t l_hash_str_len = strlen(a_hex_str);
    if ( l_hash_str_len == DAP_CHAIN_HASH_FAST_STR_LEN && !dap_strncmp(a_hex_str, "0x", 2) && !dap_is_hex_string(a_hex_str + 2, l_hash_str_len - 2)) {
        dap_hex2bin(a_hash->raw, a_hex_str + 2, l_hash_str_len - 2);
        return  0;
    } else  { // Wrong string
        return -1;
    }
}


/**
 * @brief dap_chain_hash_fast_from_base58_str
 * @param a_base58_str
 * @param a_datum_hash
 * @return
 */
int dap_chain_hash_fast_from_base58_str(const char *a_base58_str,  dap_chain_hash_fast_t *a_hash)
{
    if (!a_hash)
        return -1;
    if (!a_base58_str)
        return -2;
    size_t l_hash_len = dap_strlen(a_base58_str);
    if (l_hash_len > DAP_ENC_BASE58_ENCODE_SIZE(sizeof(dap_hash_fast_t)))
        return -3;
    // from base58 to binary
    byte_t l_out[DAP_ENC_BASE58_DECODE_SIZE(DAP_ENC_BASE58_ENCODE_SIZE(sizeof(dap_hash_fast_t)))];
    size_t l_out_size = dap_enc_base58_decode(a_base58_str, l_out);
    if (l_out_size != sizeof(dap_hash_fast_t))
        return -4;
    memcpy(a_hash, l_out, sizeof(dap_hash_fast_t));
    return 0;
}

int dap_chain_hash_fast_from_str( const char *a_hash_str, dap_chain_hash_fast_t *a_hash)
{
    return dap_chain_hash_fast_from_hex_str(a_hash_str, a_hash) && dap_chain_hash_fast_from_base58_str(a_hash_str, a_hash);
}

/**
 * @brief Compute SHA2-256 hash
 * @param[out] a_output Output buffer (must be 32 bytes)
 * @param[in] a_input Input data
 * @param[in] a_inlen Input length
 * @return Returns 0 on success, negative error code on failure
 */
int dap_hash_sha2_256(uint8_t a_output[32], const uint8_t *a_input, size_t a_inlen) {
    return dap_sha2_256(a_output, a_input, a_inlen);
}

/**
 * @brief dap_hash_fast
 * get SHA3_256 hash for specific data (moved from inline to proper encapsulation)
 * @param a_data_in input data
 * @param a_data_in_size size of input data
 * @param a_hash_out returned hash
 * @return true on success, false on failure
 */
bool dap_hash_fast( const void *a_data_in, size_t a_data_in_size, dap_hash_fast_t *a_hash_out )
{
    // Allow empty input (a_data_in_size == 0) - SHA3 can hash empty data
    // For empty input, a_data_in can be NULL, but a_hash_out must be valid
    if (a_hash_out == NULL)
        return false;
    
    // For non-empty input, a_data_in must not be NULL
    if (a_data_in_size > 0 && a_data_in == NULL)
        return false;

    // Handle empty input case in order with each other cases
    SHA3_256( (unsigned char *)a_hash_out, (const unsigned char *)a_data_in, a_data_in_size );

    return true;
}

/**
 * @brief Configurable hash function with arbitrary output size
 */
int dap_hash(dap_hash_type_t a_hash_type,
            const void *a_input, size_t a_input_size,
            uint8_t *a_output, size_t a_output_size,
            dap_hash_flags_t a_flags,
            const dap_hash_params_t *a_params) {
    if (!a_output || a_output_size == 0) {
        return -EINVAL;
    }
    
    // Allow empty input but validate non-NULL for non-zero size
    if (a_input_size > 0 && !a_input) {
        return -EINVAL;
    }
    
    // Prepare input with optional domain separation and salt
    const uint8_t *effective_input = (const uint8_t*)a_input;
    size_t effective_input_size = a_input_size;
    uint8_t *prepared_input = NULL;
    
    if (a_flags != DAP_HASH_FLAG_NONE) {
        // Calculate required size for prepared input
        const char *domain = (a_params && a_params->domain_separator) ? 
                            a_params->domain_separator : "[DapHashSeparator]";
        size_t domain_sep_size = (a_flags & DAP_HASH_FLAG_DOMAIN_SEPARATION) ? strlen(domain) + 1 : 0;
        size_t salt_size = (a_flags & DAP_HASH_FLAG_SALT && a_params && a_params->salt) ? 
                          a_params->salt_size : 0;
        size_t total_size = domain_sep_size + a_input_size + salt_size;
        
        prepared_input = DAP_NEW_SIZE(uint8_t, total_size);
        if (!prepared_input) {
            return -ENOMEM;
        }
        
        size_t offset = 0;
        
        // Add domain separation
        if (a_flags & DAP_HASH_FLAG_DOMAIN_SEPARATION) {
            memcpy(prepared_input + offset, domain, strlen(domain) + 1);
            offset += strlen(domain) + 1;
        }
        
        // Add original input
        if (a_input_size > 0) {
            memcpy(prepared_input + offset, a_input, a_input_size);
            offset += a_input_size;
        }
        
        // Add salt
        if (a_flags & DAP_HASH_FLAG_SALT && a_params && a_params->salt) {
            memcpy(prepared_input + offset, a_params->salt, a_params->salt_size);
            offset += a_params->salt_size;
        }
        
        effective_input = prepared_input;
        effective_input_size = total_size;
    }
    
    int result = 0;
    
    // Execute hash based on type
    switch (a_hash_type) {
        case DAP_HASH_TYPE_SHA3_256:
            if (a_output_size < 32) {
                result = -EINVAL;
                break;
            }
            SHA3_256(a_output, effective_input, effective_input_size);
            break;
            
        case DAP_HASH_TYPE_SHA3_384:
            if (a_output_size < 48) {
                result = -EINVAL;
                break;
            }
            SHA3_384(a_output, effective_input, effective_input_size);
            break;
            
        case DAP_HASH_TYPE_SHA3_512:
            if (a_output_size < 64) {
                result = -EINVAL;
                break;
            }
            SHA3_512(a_output, effective_input, effective_input_size);
            break;
            
        case DAP_HASH_TYPE_SHAKE128:
            SHAKE128(a_output, a_output_size, effective_input, effective_input_size);
            break;
            
        case DAP_HASH_TYPE_SHAKE256:
            SHAKE256(a_output, a_output_size, effective_input, effective_input_size);
            break;
            
        case DAP_HASH_TYPE_KECCAK:
        default:
            // Fallback to standard hash
            if (a_output_size < 32) {
                result = -EINVAL;
                break;
            }
            SHA3_256(a_output, effective_input, effective_input_size);
            break;
    }
    
    // Apply iterative hashing if requested
    if (result == 0 && (a_flags & DAP_HASH_FLAG_ITERATIVE)) {
        uint32_t iterations = (a_params && a_params->iterations > 0) ? 
                             a_params->iterations : 1000; // Default 1000 iterations
        
        uint8_t temp_output[128]; // Temporary buffer for iterations
        size_t temp_size = (a_output_size < 128) ? a_output_size : 128;
        
        memcpy(temp_output, a_output, temp_size);
        
        debug_if(s_debug_more, L_DEBUG, "Applying %u hash iterations for enhanced security", iterations);
        
        for (uint32_t i = 0; i < iterations; i++) {
            switch (a_hash_type) {
                case DAP_HASH_TYPE_SHAKE128:
                    SHAKE128(temp_output, temp_size, temp_output, temp_size);
                    break;
                case DAP_HASH_TYPE_SHAKE256:
                    SHAKE256(temp_output, temp_size, temp_output, temp_size);
                    break;
                default:
                    SHA3_256(temp_output, temp_output, temp_size);
                    break;
            }
        }
        
        memcpy(a_output, temp_output, a_output_size);
        memset(temp_output, 0, sizeof(temp_output)); // Clear sensitive data
    }
    
    // Cleanup
    if (prepared_input) {
        DAP_DELETE(prepared_input);
    }
    
    return result;
}
