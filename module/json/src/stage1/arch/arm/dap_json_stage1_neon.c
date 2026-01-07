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
 * @file dap_json_stage1_neon.c
 * @brief Stage 1 JSON tokenization - ARM NEON implementation
 * @details TODO Phase 1.4: Full NEON implementation (16 bytes/iteration)
 * Currently: Stub that delegates to reference implementation
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#include "dap_common.h"
#include "internal/dap_json_stage1.h"
#include "internal/dap_json_stage1_ref.h"

#define LOG_TAG "dap_json_stage1_neon"

/**
 * @brief ARM NEON implementation of Stage 1 tokenization (STUB)
 * 
 * TODO Phase 1.4: Implement full NEON version similar to AVX2
 * - 16 bytes per iteration
 * - uint8x16_t / uint16x8_t / uint32x4_t types
 * - v* intrinsics (vcombine_*, vget_*, etc.)
 * 
 * For now, just delegate to reference implementation
 */
int dap_json_stage1_run_neon(dap_json_stage1_t *a_stage1)
{
    if (!a_stage1) {
        return STAGE1_ERROR_INVALID_INPUT;
    }
    
    // TODO Phase 1.4: Implement NEON SIMD version
    // For now, use reference implementation
    log_it(L_INFO, "NEON stub: delegating to reference implementation");
    return dap_json_stage1_run_ref(a_stage1);
}

