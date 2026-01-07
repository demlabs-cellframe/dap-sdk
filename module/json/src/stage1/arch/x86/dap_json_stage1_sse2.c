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
 * @file dap_json_stage1_sse2.c
 * @brief Stage 1 JSON tokenization - SSE2 (x86/x64) implementation
 * @details TODO Phase 1.4: Full SSE2 implementation (16 bytes/iteration)
 * Currently: Stub that delegates to reference implementation
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <immintrin.h>

#include "dap_common.h"
#include "internal/dap_json_stage1.h"
#include "internal/dap_json_stage1_ref.h"

#define LOG_TAG "dap_json_stage1_sse2"

/**
 * @brief SSE2 implementation of Stage 1 tokenization (STUB)
 * 
 * TODO Phase 1.4: Implement full SSE2 version similar to AVX2
 * - 16 bytes per iteration (vs 32 for AVX2)
 * - __m128i instead of __m256i
 * - _mm_ intrinsics instead of _mm256_
 * 
 * For now, just delegate to reference implementation
 */
int dap_json_stage1_run_sse2(dap_json_stage1_t *a_stage1)
{
    if (!a_stage1) {
        return STAGE1_ERROR_INVALID_INPUT;
    }
    
    // TODO Phase 1.4: Implement SSE2 SIMD version
    // For now, use reference implementation
    log_it(L_INFO, "SSE2 stub: delegating to reference implementation");
    return dap_json_stage1_run_ref(a_stage1);
}

