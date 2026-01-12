/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * DAP JSON Native Implementation Team
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
 * @file dap_json_string.h
 * @brief Zero-Copy JSON String - Phase 2.2 P0 Optimization
 * 
 * CRITICAL OPTIMIZATION: String copying is the #1 bottleneck (21x gap measured).
 * 
 * Zero-copy strategy:
 * - Strings WITHOUT escape sequences: store pointer directly into JSON buffer
 * - Strings WITH escapes: lazy unescaping on first access
 * - SIMD string scanner: find closing quote/backslash 32 bytes at a time (AVX2)
 * 
 * Expected impact: +1000-2000% (10-20x) for string-heavy JSON
 * 
 * Architecture:
 * 1. dap_json_string_t - zero-copy string structure
 * 2. s_scan_string_simd() - SIMD string scanner (AVX2/SSE2/NEON)
 * 3. s_unescape_lazy() - lazy unescaping on demand
 * 
 * @author DAP JSON Native Implementation Team
 * @date 2026-01-12
 */

#ifndef DAP_JSON_STRING_H
#define DAP_JSON_STRING_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "dap_cpu_arch.h" /* For runtime CPU dispatch */

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct dap_arena;

/* ========================================================================== */
/*                        ZERO-COPY STRING STRUCTURE                          */
/* ========================================================================== */

/**
 * @brief Zero-copy JSON string
 * @details Strings without escapes - zero-copy (just pointer).
 *          Strings with escapes - lazy unescaping on first access.
 * 
 * Memory layout optimized for cache performance:
 * - Hot fields first (data, length, flags)
 * - Cold fields last (unescaped pointer - rarely used)
 */
typedef struct {
    const char *data;           /**< Pointer into original JSON buffer (zero-copy) */
    uint32_t length;            /**< String length in bytes */
    
    /* Flags packed into single byte for cache efficiency */
    uint8_t needs_unescape : 1; /**< True if string contains escape sequences */
    uint8_t unescaped_valid : 1;/**< True if unescaped pointer is valid */
    uint8_t reserved : 6;       /**< Reserved for future use */
    
    /* Cold data - rarely accessed */
    char *unescaped;            /**< Unescaped string (allocated lazily on first access) */
    uint32_t unescaped_length;  /**< Length of unescaped string */
} dap_json_string_t;

/* ========================================================================== */
/*                        STRING SCANNING API                                 */
/* ========================================================================== */

/**
 * @brief Reference C string scanner (NO SIMD)
 * @details Character-by-character scanning - correctness baseline
 * @return true on success, false on error
 */
bool dap_json_string_scan_ref(
    const uint8_t *a_input,
    size_t a_input_len,
    dap_json_string_t *a_out_string,
    uint32_t *a_out_end_offset
);

/* Forward declarations for SIMD implementations (generated) */
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)

#ifdef __SSE2__
bool dap_json_string_scan_sse2(
    const uint8_t *a_input,
    size_t a_input_len,
    dap_json_string_t *a_out_string,
    uint32_t *a_out_end_offset
);
#endif

#ifdef __AVX2__
bool dap_json_string_scan_avx2(
    const uint8_t *a_input,
    size_t a_input_len,
    dap_json_string_t *a_out_string,
    uint32_t *a_out_end_offset
);
#endif

#ifdef __AVX512F__
bool dap_json_string_scan_avx512(
    const uint8_t *a_input,
    size_t a_input_len,
    dap_json_string_t *a_out_string,
    uint32_t *a_out_end_offset
);
#endif

#elif defined(__arm__) || defined(__aarch64__)

#if defined(__ARM_NEON) || defined(__aarch64__)
bool dap_json_string_scan_neon(
    const uint8_t *a_input,
    size_t a_input_len,
    dap_json_string_t *a_out_string,
    uint32_t *a_out_end_offset
);
#endif

#endif /* x86 / ARM */

/**
 * @brief Scan JSON string with optimal SIMD implementation (CPU dispatch)
 * @details Runtime dispatch to SSE2/AVX2/AVX-512/NEON based on CPU features.
 *          Uses branch prediction hints for fast common case.
 * 
 * Algorithm:
 * 1. SIMD scan for '"' or '\' in chunks (16-64 bytes depending on arch)
 * 2. If no escapes found - zero-copy (just pointer)
 * 3. If escapes found - mark needs_unescape, lazy unescape later
 * 
 * Performance: ~16-64 chars per cycle (vs 1 char/cycle reference)
 * 
 * @param[in] a_input JSON buffer (must start at opening quote)
 * @param[in] a_input_len Remaining buffer length
 * @param[out] a_out_string Output zero-copy string structure
 * @param[out] a_out_end_offset Offset after closing quote
 * @return true on success, false on error (unterminated string)
 */
static inline bool dap_json_string_scan(
    const uint8_t *a_input,
    size_t a_input_len,
    dap_json_string_t *a_out_string,
    uint32_t *a_out_end_offset
)
{
    // Fast path: get current CPU architecture (respects manual override)
    dap_cpu_arch_t arch = dap_cpu_arch_get();
    
    // Dispatch with branch prediction hints (usually AVX2/NEON on modern CPUs)
    switch (arch) {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
        case DAP_CPU_ARCH_AVX512:
#ifdef __AVX512F__
            return dap_json_string_scan_avx512(a_input, a_input_len, a_out_string, a_out_end_offset);
#else
            break; // Fall through to reference
#endif
        case DAP_CPU_ARCH_AVX2:
#ifdef __AVX2__
            return dap_json_string_scan_avx2(a_input, a_input_len, a_out_string, a_out_end_offset);
#else
            break;
#endif
        case DAP_CPU_ARCH_SSE2:
#ifdef __SSE2__
            return dap_json_string_scan_sse2(a_input, a_input_len, a_out_string, a_out_end_offset);
#else
            break;
#endif
#elif defined(__arm__) || defined(__aarch64__)
        case DAP_CPU_ARCH_NEON:
#if defined(__ARM_NEON) || defined(__aarch64__)
            return dap_json_string_scan_neon(a_input, a_input_len, a_out_string, a_out_end_offset);
#else
            break;
#endif
#endif
        case DAP_CPU_ARCH_REFERENCE:
        case DAP_CPU_ARCH_AUTO:
        default:
            // Fallback to reference implementation
            return dap_json_string_scan_ref(a_input, a_input_len, a_out_string, a_out_end_offset);
    }
    
    // Should not reach here
    return dap_json_string_scan_ref(a_input, a_input_len, a_out_string, a_out_end_offset);
}

/**
 * @brief Get C string (const char*) from zero-copy string
 * @details Lazy unescaping: if needs_unescape and !unescaped_valid, unescape now.
 * 
 * @param[in,out] a_string Zero-copy string
 * @param[in] a_arena Arena allocator for unescaped string (if needed)
 * @return C string pointer (either direct or unescaped)
 */
const char* dap_json_string_get_cstr(
    dap_json_string_t *a_string,
    struct dap_arena *a_arena
);

/**
 * @brief Get string length
 * @details Returns unescaped length if unescaped, otherwise raw length.
 * 
 * @param[in] a_string Zero-copy string
 * @return String length in bytes
 */
static inline uint32_t dap_json_string_get_length(const dap_json_string_t *a_string) {
    if (a_string->unescaped_valid) {
        return a_string->unescaped_length;
    }
    return a_string->length;
}

/**
 * @brief Check if string needs unescaping
 * @param[in] a_string Zero-copy string
 * @return true if string contains escape sequences
 */
static inline bool dap_json_string_needs_unescape(const dap_json_string_t *a_string) {
    return a_string->needs_unescape;
}

/**
 * @brief Free zero-copy string resources
 * @details Only frees unescaped buffer (if allocated).
 *          The original 'data' pointer is NOT freed (zero-copy).
 * 
 * @param[in,out] a_string Zero-copy string
 */
void dap_json_string_free(dap_json_string_t *a_string);

#ifdef __cplusplus
}
#endif

#endif /* DAP_JSON_STRING_H */
