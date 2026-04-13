/**
 * @file dap_base64.h
 * @brief Base64 encoding/decoding
 *
 * Copyright (c) 2017-2026 Demlabs
 * License: GNU GPL v3
 */

#pragma once

#include <stddef.h>
#include "dap_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Calculate encoded size from input size
 */
#define DAP_BASE64_ENCODE_SIZE(in_size) ((size_t)(((4 * (in_size) / 3) + 3) & ~3))

/**
 * @brief Calculate decoded size from input size
 */
#define DAP_BASE64_DECODE_SIZE(in_size) ((size_t)((in_size) * 3 / 4 + (in_size) % 4))

/**
 * @brief Encode data to base64
 * @param a_in Input data
 * @param a_in_size Input size
 * @param a_out Output buffer
 * @param a_type Encoding type (B64 or B64_URLSAFE)
 * @return Output size
 */
size_t dap_base64_encode(const void *a_in, size_t a_in_size, char *a_out, dap_data_type_t a_type);

/**
 * @brief Decode base64 string to data
 * @param a_in Input base64 string
 * @param a_in_size Input string size
 * @param a_out Output buffer
 * @param a_type Encoding type (B64 or B64_URLSAFE)
 * @return Output size
 */
size_t dap_base64_decode(const char *a_in, size_t a_in_size, void *a_out, dap_data_type_t a_type);

/**
 * @brief Encode string to base64 and return newly allocated result
 * @param a_string Input string
 * @return Newly allocated base64 string or NULL on error
 */
char *dap_strdup_to_base64(const char *a_string);

/**
 * @brief Decode base64 string and return newly allocated result
 * @param a_string_base64 Input base64 string
 * @return Newly allocated decoded string or NULL on error
 */
char *dap_strdup_from_base64(const char *a_string_base64);

// Legacy names for compatibility
#define DAP_ENC_BASE64_ENCODE_SIZE  DAP_BASE64_ENCODE_SIZE
#define DAP_ENC_BASE64_DECODE_SIZE  DAP_BASE64_DECODE_SIZE
#define dap_enc_base64_encode       dap_base64_encode
#define dap_enc_base64_decode       dap_base64_decode
#define dap_enc_strdup_to_base64    dap_strdup_to_base64
#define dap_enc_strdup_from_base64  dap_strdup_from_base64

#ifdef __cplusplus
}
#endif
