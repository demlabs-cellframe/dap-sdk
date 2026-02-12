/**
 * @file dap_base58.h
 * @brief Base58 encoding/decoding
 *
 * Copyright (c) 2017-2026 Demlabs
 * License: GNU GPL v3
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Calculate encoded size from input size
 */
#define DAP_BASE58_ENCODE_SIZE(a_in_size) ((size_t)((137 * (a_in_size) / 100) + 2))

/**
 * @brief Calculate decoded size from input size
 */
#define DAP_BASE58_DECODE_SIZE(a_in_size) ((size_t)(2 * (a_in_size) + 1))

/**
 * @brief Encode data to base58
 * @param a_in Input data
 * @param a_in_size Input size
 * @param a_out Output buffer (must be at least DAP_BASE58_ENCODE_SIZE bytes)
 * @return Output size
 */
size_t dap_base58_encode(const void *a_in, size_t a_in_size, char *a_out);

/**
 * @brief Decode base58 string to data
 * @param a_in Input base58 string
 * @param a_out Output buffer (must be at least DAP_BASE58_DECODE_SIZE bytes)
 * @return Output size, 0 on error
 */
size_t dap_base58_decode(const char *a_in, void *a_out);

/**
 * @brief Encode data to base58 and return newly allocated string
 * @param a_in Input data
 * @param a_in_size Input size
 * @return Newly allocated string or NULL on error
 */
char *dap_base58_encode_to_str(const void *a_in, size_t a_in_size);

/**
 * @brief Convert hex string to base58 string
 * @param a_in_str Hex string (e.g. "0xA21F...")
 * @return Newly allocated base58 string or NULL on error
 */
char *dap_base58_from_hex_str(const char *a_in_str);

/**
 * @brief Convert base58 string to hex string
 * @param a_in_str Base58 string
 * @return Newly allocated hex string (e.g. "0xA21F...") or NULL on error
 */
char *dap_base58_to_hex_str(const char *a_in_str);

// Legacy names for compatibility
#define DAP_ENC_BASE58_ENCODE_SIZE  DAP_BASE58_ENCODE_SIZE
#define DAP_ENC_BASE58_DECODE_SIZE  DAP_BASE58_DECODE_SIZE
#define dap_enc_base58_encode       dap_base58_encode
#define dap_enc_base58_decode       dap_base58_decode
#define dap_enc_base58_encode_to_str dap_base58_encode_to_str
#define dap_enc_base58_from_hex_str_to_str dap_base58_from_hex_str
#define dap_enc_base58_to_hex_str_from_str dap_base58_to_hex_str

#ifdef __cplusplus
}
#endif
