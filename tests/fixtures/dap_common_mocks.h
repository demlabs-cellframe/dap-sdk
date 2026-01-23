/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2025
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

/**
 * @file dap_common_mocks.h
 * @brief Common mocks for DAP SDK functions used across tests
 * 
 * Provides standard mock declarations and wrappers for frequently mocked
 * DAP SDK functions like logging, memory allocation, config access, etc.
 * 
 * @date 2025-10-28
 */

#pragma once

#include "dap_mock.h"
#include "dap_mock_linker_wrapper.h"
#include "dap_common.h"
#include "dap_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Logging Mocks
// ============================================================================

/**
 * @brief Mock for log_it function
 * 
 * By default, passes through to real logging for visibility.
 * Can be customized in tests if needed.
 */
DAP_MOCK_CUSTOM(void, log_it,
    PARAM(int, a_level),
    PARAM(const char *, a_tag),
    PARAM(const char *, a_fmt),
    PARAM_VARARGS()
) {
    // Default: pass through to real logging
    __real_log_it(a_level, a_tag, a_fmt, DAP_MOCK_VARARGS);
}

// ============================================================================
// Memory Allocation Mocks
// ============================================================================

/**
 * @brief Mock for DAP_NEW_Z (zero-initialized allocation)
 * 
 * By default, passes through to real allocation.
 */
DAP_MOCK_CUSTOM(void *, DAP_NEW_Z_impl,
    PARAM(size_t, a_size)
) {
    return __real_DAP_NEW_Z_impl(a_size);
}

/**
 * @brief Mock for DAP_DELETE (deallocation)
 * 
 * By default, passes through to real deallocation.
 */
DAP_MOCK_CUSTOM(void, DAP_DELETE_impl,
    PARAM(void *, a_ptr)
) {
    __real_DAP_DELETE_impl(a_ptr);
}

// ============================================================================
// Configuration Mocks
// ============================================================================

/**
 * @brief Mock for dap_config_get_item_bool_default
 * 
 * By default, returns the default value (pass-through behavior).
 */
DAP_MOCK_CUSTOM(bool, dap_config_get_item_bool_default,
    PARAM(const dap_config_t *, a_config),
    PARAM(const char *, a_section),
    PARAM(const char *, a_key),
    PARAM(bool, a_default)
) {
    // Default: return the default value
    return a_default;
}

/**
 * @brief Mock for dap_config_get_item_str_default
 * 
 * By default, returns the default value.
 */
DAP_MOCK_CUSTOM(const char *, dap_config_get_item_str_default,
    PARAM(const dap_config_t *, a_config),
    PARAM(const char *, a_section),
    PARAM(const char *, a_key),
    PARAM(const char *, a_default)
) {
    return a_default;
}

/**
 * @brief Mock for dap_config_get_item_int32_default
 * 
 * By default, returns the default value.
 */
DAP_MOCK_CUSTOM(int32_t, dap_config_get_item_int32_default,
    PARAM(const dap_config_t *, a_config),
    PARAM(const char *, a_section),
    PARAM(const char *, a_key),
    PARAM(int32_t, a_default)
) {
    return a_default;
}

/**
 * @brief Mock for dap_config_get_item_uint32_default
 * 
 * By default, returns the default value.
 */
DAP_MOCK_CUSTOM(uint32_t, dap_config_get_item_uint32_default,
    PARAM(const dap_config_t *, a_config),
    PARAM(const char *, a_section),
    PARAM(const char *, a_key),
    PARAM(uint32_t, a_default)
) {
    return a_default;
}


#ifdef __cplusplus
}
#endif

