/**
 * @file dap_data.h
 * @brief Data encoding types and utilities
 *
 * Copyright (c) 2017-2026 Demlabs
 * License: GNU GPL v3
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Data encoding type
 */
typedef enum dap_data_type {
    DAP_DATA_TYPE_RAW,          ///< Raw binary data
    DAP_DATA_TYPE_B64,          ///< Base64 encoded
    DAP_DATA_TYPE_B64_URLSAFE,  ///< Base64 URL-safe encoded
} dap_data_type_t;

// Legacy names for compatibility
typedef dap_data_type_t dap_enc_data_type_t;
#define DAP_ENC_DATA_TYPE_RAW       DAP_DATA_TYPE_RAW
#define DAP_ENC_DATA_TYPE_B64       DAP_DATA_TYPE_B64
#define DAP_ENC_DATA_TYPE_B64_URLSAFE DAP_DATA_TYPE_B64_URLSAFE

#ifdef __cplusplus
}
#endif
