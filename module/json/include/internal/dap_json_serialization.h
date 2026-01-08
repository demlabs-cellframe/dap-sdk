/*
 * Authors:
 * Dmitry Gerasimov <naeper@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * DAP SDK  https://gitlab.demlabs.net/dap/dap-sdk
 * Copyright  (c) 2024-2025
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

#include "dap_json_type.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                          JSON SERIALIZATION API                            */
/* ========================================================================== */

/**
 * @brief Serialize JSON value to string (compact format)
 * @param a_value JSON value to serialize
 * @return Dynamically allocated string (caller must free with DAP_DELETE)
 */
char* dap_json_value_serialize(dap_json_value_t *a_value);

/**
 * @brief Serialize JSON value to string (pretty-printed format)
 * @param a_value JSON value to serialize
 * @return Dynamically allocated string (caller must free with DAP_DELETE)
 */
char* dap_json_value_serialize_pretty(dap_json_value_t *a_value);

#ifdef __cplusplus
}
#endif

