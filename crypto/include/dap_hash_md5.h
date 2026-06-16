/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * MD5 hash implementation (RFC 1321)
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#define DAP_HASH_MD5_DIGEST_SIZE 16

typedef struct dap_hash_md5_ctx {
    uint32_t state[4];
    uint64_t count;
    uint8_t buffer[64];
} dap_hash_md5_ctx_t;

void dap_hash_md5_init(dap_hash_md5_ctx_t *a_ctx);
void dap_hash_md5_update(dap_hash_md5_ctx_t *a_ctx, const uint8_t *a_data, size_t a_len);
void dap_hash_md5_final(dap_hash_md5_ctx_t *a_ctx, uint8_t a_digest[DAP_HASH_MD5_DIGEST_SIZE]);

/**
 * @brief Compute MD5 hash of data
 * @param a_data Input data
 * @param a_data_len Input data length
 * @param a_digest Output digest (16 bytes)
 */
void dap_hash_md5(const void *a_data, size_t a_data_len, uint8_t a_digest[DAP_HASH_MD5_DIGEST_SIZE]);

/**
 * @brief Compute MD5 hash and return as hex string
 * @param a_data Input data
 * @param a_data_len Input data length
 * @param a_hash_hex Output hex string (33 bytes including null terminator)
 * @return 0 on success, -1 on error
 */
int dap_hash_md5_hex(const void *a_data, size_t a_data_len, char *a_hash_hex, size_t a_hash_hex_size);
