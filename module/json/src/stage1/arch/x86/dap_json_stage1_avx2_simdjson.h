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
 * @file dap_json_stage1_avx2_simdjson.h
 * @brief SimdJSON-style Stage 1 tokenization using AVX2 (Phase 2.1)
 * @details Full simdjson algorithm: bitmaps + flatten + parallel UTF-8 validation
 * @date 2026-01-11
 */

#pragma once

#include "internal/dap_json_stage1.h"

/**
 * @brief Main SimdJSON-style Stage 1 tokenization (AVX2)
 * @details Full simdjson algorithm implementation:
 *          - Bitmap classification for ALL byte classes
 *          - Parallel UTF-8 validation
 *          - Flatten algorithm for sequential indices
 *          - Target: 4-5 GB/s throughput on AVX2
 * 
 * Key differences from Phase 1 HYBRID:
 *   Phase 1: Sequential processing, 0.06 GB/s
 *   Phase 2: Parallel bitmaps, 4-5 GB/s (40-80x faster!)
 * 
 * @param a_stage1 Initialized Stage 1 parser
 * @return STAGE1_SUCCESS on success, error code otherwise
 */
int dap_json_stage1_run_avx2_simdjson(dap_json_stage1_t *a_stage1);

