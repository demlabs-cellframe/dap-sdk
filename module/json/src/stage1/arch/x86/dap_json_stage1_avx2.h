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
 * @file dap_json_stage1_avx2.h
 * @brief AVX2-optimized Stage 1 JSON tokenization header
 * @details Entry point for AVX2 SIMD implementation (32 bytes/iteration)
 */

#pragma once

#include "internal/dap_json_stage1.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief AVX2-optimized Stage 1 tokenization (32 bytes per iteration)
 * @param[in,out] a_stage1 Stage 1 parser state
 * @return STAGE1_SUCCESS or error code
 */
extern int dap_json_stage1_run_avx2(dap_json_stage1_t *a_stage1);

#ifdef __cplusplus
}
#endif

