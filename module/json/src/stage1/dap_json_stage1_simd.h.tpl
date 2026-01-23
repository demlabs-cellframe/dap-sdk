/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.
 *
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
 * @file dap_json_stage1_{{ARCH_LOWER}}.h
 * @brief {{ARCH_NAME}}-optimized Stage 1 JSON tokenization header
 * @details Entry point for {{ARCH_NAME}} SIMD implementation
 * @generated Auto-generated from template
 * @date 2026-01-11
 */

#pragma once

#include "internal/dap_json_stage1.h"
#include "internal/dap_json_stage1_ref.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief {{ARCH_NAME}}-optimized Stage 1 tokenization ({{CHUNK_SIZE}} bytes per iteration)
 * @param[in,out] a_stage1 Stage 1 parser state
 * @return STAGE1_SUCCESS or error code
 * 
 * Performance target: {{PERF_TARGET}}
 */
extern int dap_json_stage1_run_{{ARCH_LOWER}}(dap_json_stage1_t *a_stage1);

/**
 * @brief Enable/disable detailed debug logging for {{ARCH_NAME}} implementation
 * @param a_enable true to enable detailed logging
 */
extern void dap_json_stage1_{{ARCH_LOWER}}_set_debug(bool a_enable);

#ifdef __cplusplus
}
#endif

