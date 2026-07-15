/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * JA3 fingerprint calculator from TLS ClientHello wire data.
 * Spec: https://github.com/salesforce/ja3
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DAP_TLS_JA3_HASH_HEX_SIZE  33u  /* 32 hex chars + NUL */
#define DAP_TLS_JA3_STRING_MAX     512u

typedef struct dap_tls_ja3_result {
    char ja3_string[DAP_TLS_JA3_STRING_MAX];
    char ja3_hash[DAP_TLS_JA3_HASH_HEX_SIZE];
} dap_tls_ja3_result_t;

/**
 * Parse TLS Application Data record containing Handshake/ClientHello.
 * @return 0 on success, -1 on parse error, 1 if more data required
 */
int dap_tls_ja3_from_tls_record(const uint8_t *a_data, size_t a_size,
                                dap_tls_ja3_result_t *a_out);

/**
 * Parse TLS Handshake message (type 0x01 + 3-byte length + ClientHello body).
 * @return 0 on success, -1 on parse error, 1 if more data required
 */
int dap_tls_ja3_from_handshake(const uint8_t *a_data, size_t a_size,
                               dap_tls_ja3_result_t *a_out);

/**
 * Parse ClientHello body (starts at legacy_version field).
 * @return 0 on success, -1 on parse error
 */
int dap_tls_ja3_from_client_hello_body(const uint8_t *a_body, size_t a_body_size,
                                       dap_tls_ja3_result_t *a_out);

/**
 * Compute MD5(a_ja3_string) as lowercase hex into a_hash_hex.
 * @return 0 on success, -1 on error
 */
int dap_tls_ja3_hash_string(const char *a_ja3_string, char *a_hash_hex, size_t a_hash_hex_size);

#ifdef __cplusplus
}
#endif
