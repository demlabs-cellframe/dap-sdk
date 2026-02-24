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
    (int a_level, const char *a_tag, const char *a_fmt, ...))
    UNUSED(a_level);
    UNUSED(a_tag);
    UNUSED(a_fmt);
}

/**

// ============================================================================
// Configuration Mocks
// ============================================================================

/**
 * @brief Mock for dap_config_get_item_bool_default
 * 
 * By default, returns the default value (pass-through behavior).
 */
DAP_MOCK_CUSTOM(bool, dap_config_get_item_bool_default,
    (const dap_config_t *a_config, const char *a_section, const char *a_key, bool a_default))
    UNUSED(a_config);
    UNUSED(a_section);
    UNUSED(a_key);
    return a_default;
}

/**
 * @brief Mock for dap_config_get_item_str_default
 * 
 * By default, returns the default value.
 */
DAP_MOCK_CUSTOM(const char *, dap_config_get_item_str_default,
    (const dap_config_t *a_config, const char *a_section, const char *a_key, const char *a_default))
    UNUSED(a_config);
    UNUSED(a_section);
    UNUSED(a_key);
    return a_default;
}

/**
 * @brief Mock for dap_config_get_item_int32_default
 * 
 * By default, returns the default value.
 */
DAP_MOCK_CUSTOM(int32_t, dap_config_get_item_int32_default,
    (const dap_config_t *a_config, const char *a_section, const char *a_key, int32_t a_default))
    UNUSED(a_config);
    UNUSED(a_section);
    UNUSED(a_key);
    return a_default;
}

/**
 * @brief Mock for dap_config_get_item_uint32_default
 * 
 * By default, returns the default value.
 */
DAP_MOCK_CUSTOM(uint32_t, dap_config_get_item_uint32_default,
    (const dap_config_t *a_config, const char *a_section, const char *a_key, uint32_t a_default))
    UNUSED(a_config);
    UNUSED(a_section);
    UNUSED(a_key);
    return a_default;
}


#ifdef __cplusplus
}
#endif

