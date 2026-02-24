/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * This file is part of DAP SDK the open source project
 *
 *    DAP SDK is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    DAP SDK is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file dap_hash_compat.h
 * @brief Compatibility layer: old hash type names → dap_hash_sha3_256_t
 *
 * Maps legacy dap_hash_fast_t / dap_chain_hash_fast_t names and their
 * associated helpers to the canonical dap_hash_sha3_256 API so that
 * higher-level modules compile without mass-renaming.
 *
 * All aliases resolve to SHA3-256 (the default hash algorithm).
 */

#pragma once

#include "dap_hash_sha3.h"

// =============================================================================
// Type aliases
// =============================================================================

typedef dap_hash_sha3_256_t     dap_chain_hash_fast_t;
typedef dap_hash_sha3_256_t     dap_hash_fast_t;
typedef dap_hash_sha3_256_t     dap_hash_t;

// =============================================================================
// Size constants
// =============================================================================

#define DAP_HASH_FAST_SIZE              DAP_HASH_SHA3_256_SIZE
#define DAP_CHAIN_HASH_FAST_SIZE        DAP_HASH_SHA3_256_SIZE
#define DAP_HASH_FAST_STR_SIZE          DAP_HASH_SHA3_256_STR_SIZE
#define DAP_CHAIN_HASH_FAST_STR_SIZE    DAP_HASH_SHA3_256_STR_SIZE

// =============================================================================
// Core hashing function
// =============================================================================

#define dap_hash_fast(data, size, out) \
    dap_hash_sha3_256((data), (size), (dap_hash_sha3_256_t *)(out))

// =============================================================================
// dap_hash_fast_* helpers
// =============================================================================

#define dap_hash_fast_compare(a, b)         dap_hash_sha3_256_compare((a), (b))
#define dap_hash_fast_is_blank(h)           dap_hash_sha3_256_is_blank(h)
#define dap_hash_fast_to_str(h, s, sz)      dap_hash_sha3_256_to_str((h), (s), (sz))
#define dap_hash_fast_to_str_new(h)         dap_hash_sha3_256_to_str_new(h)
#define dap_hash_fast_to_str_static(h)      dap_hash_sha3_256_to_str_static(h)
#define dap_hash_fast_str_new(data, size)   dap_hash_sha3_256_str_new((data), (size))
#define dap_hash_fast_from_str(s, h)        dap_hash_sha3_256_from_str((s), (h))

// =============================================================================
// dap_chain_hash_fast_* helpers
// =============================================================================

#define dap_chain_hash_fast_to_str(h, s, sz)        dap_hash_sha3_256_to_str((h), (s), (sz))
#define dap_chain_hash_fast_to_str_do(h, s)         dap_hash_sha3_256_to_str_do((h), (s))
#define dap_chain_hash_fast_to_str_new(h)           dap_hash_sha3_256_to_str_new(h)
#define dap_chain_hash_fast_to_str_static(h)        dap_hash_sha3_256_to_str_static(h)
#define dap_chain_hash_fast_from_str(s, h)          dap_hash_sha3_256_from_str((s), (h))
#define dap_chain_hash_fast_from_hex_str(s, h)      dap_hash_sha3_256_from_hex_str((s), (h))
#define dap_chain_hash_fast_from_base58_str(s, h)   dap_hash_sha3_256_from_base58_str((s), (h))
