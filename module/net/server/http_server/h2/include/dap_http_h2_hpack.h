/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2025
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file dap_http_h2_hpack.h
 * @brief HPACK header field representation (RFC 7541) for HTTP/2.
 */

/**
 * @defgroup DAP_HPACK_CONST Constants
 * @{
 */
#define DAP_HPACK_STATIC_TABLE_SIZE 61
#define DAP_HPACK_DEFAULT_TABLE_SIZE 4096
#define DAP_HPACK_ENTRY_OVERHEAD 32
/** @} */

/**
 * @defgroup DAP_HPACK_TYPES Types
 * @{
 */

/** Single logical header (name/value and lengths). */
typedef struct dap_hpack_header {
    char *name;
    char *value;
    size_t name_len;
    size_t value_len;
} dap_hpack_header_t;

/** One entry in the encoder dynamic table (ring buffer slot). */
typedef struct dap_hpack_dyn_entry {
    char *name;
    char *value;
    size_t name_len;
    size_t value_len;
    size_t entry_size; /* name_len + value_len + 32 */
} dap_hpack_dyn_entry_t;

/** Dynamic table state for encode/decode sessions. */
typedef struct dap_hpack_context {
    dap_hpack_dyn_entry_t *entries; /* ring buffer */
    size_t capacity;                /* max entries in buffer */
    size_t count;                   /* current entries */
    size_t head;                    /* insertion point (newest) */
    size_t current_size;            /* sum of entry sizes */
    size_t max_size;                /* maximum allowed size */
} dap_hpack_context_t;

/** @} */

/**
 * @defgroup DAP_HPACK_CB Callbacks
 * @{
 */

/** Invoked for each decoded header field. */
typedef void (*dap_hpack_header_cb_t)(const char *name, size_t name_len,
                                      const char *value, size_t value_len,
                                      void *userdata);

/** @} */

/**
 * @defgroup DAP_HPACK_CTX Context lifecycle
 * @{
 */
int dap_hpack_context_init(dap_hpack_context_t *ctx, size_t max_size);
void dap_hpack_context_deinit(dap_hpack_context_t *ctx);
void dap_hpack_context_resize(dap_hpack_context_t *ctx, size_t new_max_size);
/** @} */

/**
 * @defgroup DAP_HPACK_INT Integer encoding (RFC 7541 section 5.1)
 * @{
 */
int dap_hpack_int_encode(uint64_t value, uint8_t prefix_bits, uint8_t prefix_mask,
                         uint8_t *dst, size_t dst_cap, size_t *out_len);
int dap_hpack_int_decode(const uint8_t *src, size_t src_len, uint8_t prefix_bits,
                         uint64_t *out_value, size_t *out_consumed);
/** @} */

/**
 * @defgroup DAP_HPACK_BLOCK Header block encode/decode
 * @{
 */
int dap_hpack_decode(dap_hpack_context_t *ctx, const uint8_t *src, size_t src_len,
                     dap_hpack_header_cb_t cb, void *userdata);
int dap_hpack_encode(dap_hpack_context_t *ctx, const dap_hpack_header_t *headers, size_t count,
                     uint8_t *dst, size_t dst_cap, size_t *out_len);
/** @} */

/**
 * @defgroup DAP_HPACK_STATIC Static table
 * @{
 */
const dap_hpack_header_t *dap_hpack_static_find(const char *name, size_t name_len,
                                                const char *value, size_t value_len,
                                                size_t *out_index);
/** @} */

#ifdef __cplusplus
}
#endif
