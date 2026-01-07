/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.
 *
 * This file is part of DAP (Distributed Applications Platform) the open source project
 *
 *    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    DAP is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file dap_json_stage1_avx2.c
 * @brief AVX2-optimized Stage 1 JSON tokenization
 * @details Processes 32 bytes per iteration using AVX2 SIMD instructions
 * 
 * Performance target: 3-4 GB/s (single-core)
 * 
 * Key optimizations:
 * - Parallel character classification (structural, whitespace, quotes)
 * - SIMD string handling with escape detection
 * - Batch processing of structural characters
 * 
 * Architecture: Intel/AMD x86-64 with AVX2 support
 */

#ifdef __AVX2__

#include <immintrin.h>
#include <stdlib.h>
#include <string.h>

#include "dap_common.h"
#include "internal/dap_json_stage1.h"

#define LOG_TAG "dap_json_stage1_avx2"

// AVX2 processes 32 bytes per chunk
#define AVX2_CHUNK_SIZE 32

/* ========================================================================== */
/*                         SIMD PRIMITIVES                                    */
/* ========================================================================== */

/**
 * @brief Find structural characters in AVX2 chunk
 * @details Identifies {, }, [, ], :, , in parallel
 * @param[in] a_chunk 32-byte AVX2 vector
 * @return Bitmask where bit i = 1 if byte i is structural
 */
static inline uint32_t s_simd_find_structural_avx2(__m256i a_chunk)
{
    // Create comparison vectors for each structural character
    const __m256i l_brace_open  = _mm256_set1_epi8('{');
    const __m256i l_brace_close = _mm256_set1_epi8('}');
    const __m256i l_bracket_open  = _mm256_set1_epi8('[');
    const __m256i l_bracket_close = _mm256_set1_epi8(']');
    const __m256i l_colon = _mm256_set1_epi8(':');
    const __m256i l_comma = _mm256_set1_epi8(',');
    
    // Compare chunk against each structural char (parallel)
    __m256i l_cmp1 = _mm256_cmpeq_epi8(a_chunk, l_brace_open);
    __m256i l_cmp2 = _mm256_cmpeq_epi8(a_chunk, l_brace_close);
    __m256i l_cmp3 = _mm256_cmpeq_epi8(a_chunk, l_bracket_open);
    __m256i l_cmp4 = _mm256_cmpeq_epi8(a_chunk, l_bracket_close);
    __m256i l_cmp5 = _mm256_cmpeq_epi8(a_chunk, l_colon);
    __m256i l_cmp6 = _mm256_cmpeq_epi8(a_chunk, l_comma);
    
    // OR all comparisons together
    __m256i l_structural = _mm256_or_si256(l_cmp1, l_cmp2);
    l_structural = _mm256_or_si256(l_structural, l_cmp3);
    l_structural = _mm256_or_si256(l_structural, l_cmp4);
    l_structural = _mm256_or_si256(l_structural, l_cmp5);
    l_structural = _mm256_or_si256(l_structural, l_cmp6);
    
    // Convert to bitmask (1 bit per byte)
    return (uint32_t)_mm256_movemask_epi8(l_structural);
}

/**
 * @brief Find whitespace characters in AVX2 chunk
 * @details Identifies space, tab, newline, carriage return
 * @param[in] a_chunk 32-byte AVX2 vector
 * @return Bitmask where bit i = 1 if byte i is whitespace
 */
static inline uint32_t s_simd_find_whitespace_avx2(__m256i a_chunk)
{
    const __m256i l_space = _mm256_set1_epi8(' ');
    const __m256i l_tab   = _mm256_set1_epi8('\t');
    const __m256i l_nl    = _mm256_set1_epi8('\n');
    const __m256i l_cr    = _mm256_set1_epi8('\r');
    
    __m256i l_ws1 = _mm256_cmpeq_epi8(a_chunk, l_space);
    __m256i l_ws2 = _mm256_cmpeq_epi8(a_chunk, l_tab);
    __m256i l_ws3 = _mm256_cmpeq_epi8(a_chunk, l_nl);
    __m256i l_ws4 = _mm256_cmpeq_epi8(a_chunk, l_cr);
    
    __m256i l_whitespace = _mm256_or_si256(l_ws1, l_ws2);
    l_whitespace = _mm256_or_si256(l_whitespace, l_ws3);
    l_whitespace = _mm256_or_si256(l_whitespace, l_ws4);
    
    return (uint32_t)_mm256_movemask_epi8(l_whitespace);
}

/**
 * @brief Find quote characters in AVX2 chunk
 * @param[in] a_chunk 32-byte AVX2 vector
 * @return Bitmask where bit i = 1 if byte i is "
 */
static inline uint32_t s_simd_find_quotes_avx2(__m256i a_chunk)
{
    const __m256i l_quote = _mm256_set1_epi8('"');
    __m256i l_quotes = _mm256_cmpeq_epi8(a_chunk, l_quote);
    return (uint32_t)_mm256_movemask_epi8(l_quotes);
}

/**
 * @brief Find backslash characters in AVX2 chunk
 * @param[in] a_chunk 32-byte AVX2 vector
 * @return Bitmask where bit i = 1 if byte i is \
 */
static inline uint32_t s_simd_find_backslash_avx2(__m256i a_chunk)
{
    const __m256i l_backslash = _mm256_set1_epi8('\\');
    __m256i l_escapes = _mm256_cmpeq_epi8(a_chunk, l_backslash);
    return (uint32_t)_mm256_movemask_epi8(l_escapes);
}

/* ========================================================================== */
/*                         STRING HANDLING                                    */
/* ========================================================================== */

/**
 * @brief Compute escaped quotes bitmask
 * @details A quote preceded by ODD number of backslashes is escaped
 * @param[in] a_backslash_mask Bitmask of backslash positions
 * @param[in] a_quote_mask Bitmask of quote positions
 * @param[in,out] a_prev_backslash_run Backslash run carry from previous chunk
 * @return Bitmask of escaped quotes
 */
static inline uint32_t s_compute_escaped_quotes(
    uint32_t a_backslash_mask,
    uint32_t a_quote_mask,
    uint32_t *a_prev_backslash_run
)
{
    uint32_t l_escaped = 0;
    uint32_t l_backslash_run = *a_prev_backslash_run;
    
    for (int l_bit = 0; l_bit < AVX2_CHUNK_SIZE; l_bit++) {
        if (a_backslash_mask & (1U << l_bit)) {
            l_backslash_run++;
        } else if (a_quote_mask & (1U << l_bit)) {
            // Quote found: check if escaped (odd number of backslashes)
            if (l_backslash_run % 2 == 1) {
                l_escaped |= (1U << l_bit);
            }
            l_backslash_run = 0;
        } else {
            l_backslash_run = 0;
        }
    }
    
    *a_prev_backslash_run = l_backslash_run;
    return l_escaped;
}

/* ========================================================================== */
/*                         MAIN AVX2 TOKENIZER                                */
/* ========================================================================== */

/**
 * @brief AVX2-optimized Stage 1 tokenization
 * @details Processes input in 32-byte chunks using AVX2 SIMD
 * @param[in,out] a_stage1 Stage 1 parser state
 * @return STAGE1_SUCCESS on success, error code on failure
 * 
 * @note Falls back to reference implementation for:
 *       - Tail bytes (< 32 bytes remaining)
 *       - Complex string parsing (after SIMD detection)
 *       - Number/literal validation
 */
int dap_json_stage1_run_avx2(dap_json_stage1_t *a_stage1)
{
    if (!a_stage1 || !a_stage1->input) {
        log_it(L_ERROR, "Invalid stage1 or input");
        return STAGE1_ERROR_INVALID_INPUT;
    }
    
    log_it(L_DEBUG, "Starting AVX2 Stage 1 tokenization (%zu bytes)", a_stage1->input_len);
    
    const uint8_t *l_input = a_stage1->input;
    const size_t l_input_len = a_stage1->input_len;
    
    // State
    bool l_in_string = false;
    uint32_t l_prev_backslash_run = 0;
    
    // Process full AVX2 chunks
    size_t l_pos = 0;
    const size_t l_chunk_end = (l_input_len / AVX2_CHUNK_SIZE) * AVX2_CHUNK_SIZE;
    
    for (; l_pos < l_chunk_end; l_pos += AVX2_CHUNK_SIZE) {
        // Load 32 bytes
        __m256i l_chunk = _mm256_loadu_si256((const __m256i*)(l_input + l_pos));
        
        // Classify characters (parallel)
        uint32_t l_structural_mask = s_simd_find_structural_avx2(l_chunk);
        uint32_t l_whitespace_mask = s_simd_find_whitespace_avx2(l_chunk);
        uint32_t l_quote_mask = s_simd_find_quotes_avx2(l_chunk);
        uint32_t l_backslash_mask = s_simd_find_backslash_avx2(l_chunk);
        
        // Compute escaped quotes
        uint32_t l_escaped_quotes = s_compute_escaped_quotes(
            l_backslash_mask,
            l_quote_mask,
            &l_prev_backslash_run
        );
        
        // Real quotes (not escaped)
        uint32_t l_real_quotes = l_quote_mask & ~l_escaped_quotes;
        
        // TODO: Process structural characters
        // TODO: Handle string state transitions
        // TODO: Update statistics
        
        (void)l_structural_mask;  // Suppress warning
        (void)l_whitespace_mask;
        (void)l_real_quotes;
    }
    
    // TODO: Handle tail bytes with reference implementation
    // TODO: Finalize statistics
    
    log_it(L_INFO, "AVX2 Stage 1 complete: %zu tokens (STUB)", a_stage1->indices_count);
    
    return STAGE1_SUCCESS;
}

#endif // __AVX2__

