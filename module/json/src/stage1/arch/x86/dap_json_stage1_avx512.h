/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
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
 * @file dap_json_stage1_avx512.h
 * @brief AVX-512-optimized Stage 1 JSON tokenization header
 * @details Entry point for AVX-512 SIMD implementation (64 bytes/iteration)
 */

#pragma once

#include "internal/dap_json_stage1.h"
#include "internal/dap_json_stage1_ref.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief AVX-512-optimized Stage 1 tokenization (64 bytes per iteration)
 * @param[in,out] a_stage1 Stage 1 parser state
 * @return STAGE1_SUCCESS or error code
 * 
 * @note This function will return an error if called on a CPU without AVX-512 support.
 *       Use dap_cpu_detect to check CPU features before calling.
 */
extern int dap_json_stage1_run_avx512(dap_json_stage1_t *a_stage1);

#ifdef __cplusplus
}
#endif

