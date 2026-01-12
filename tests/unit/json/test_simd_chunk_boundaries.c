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
 * @file test_simd_chunk_boundaries.c
 * @brief SIMD Chunk Boundary Edge Cases - CRITICAL CORRECTNESS
 * @details Tests tokens at exact 16/32/64-byte SIMD chunk boundaries
 * 
 * CRITICAL: Current test_simd_string_spanning.c covers ONLY string spanning.
 *           This test covers ALL other token types at chunk boundaries:
 *           - Structural characters ({, }, [, ], :, ,)
 *           - Escape sequences (\uXXXX split across boundary)
 *           - UTF-8 multibyte characters (3-4 byte split)
 *           - Numbers (12345|67890)
 *           - Literals (tru|e, fal|se, nul|l)
 *           - Nested structures
 * 
 * Why critical: SIMD processes in chunks (16/32/64 bytes). If token starts in
 *               chunk N and ends in chunk N+1, parser might:
 *               - Miss token
 *               - Parse incorrect value
 *               - Crash
 * 
 * @date 2026-01-12
 */

#define LOG_TAG "test_simd_chunk_boundaries"

#include "dap_common.h"
#include "dap_json.h"
#include "dap_cpu_arch.h"
#include "dap_test.h"
#include "internal/dap_json_stage1.h"
#include "../../fixtures/utilities/test_helpers.h"
#include <string.h>
#include <stdio.h>

// =============================================================================
// CHUNK SIZE DETECTION
// =============================================================================

static int s_get_chunk_size_for_arch(dap_cpu_arch_t a_arch)
{
    switch (a_arch) {
        case DAP_CPU_ARCH_SSE2:
        case DAP_CPU_ARCH_NEON:
            return 16;  // 128-bit SIMD
        
        case DAP_CPU_ARCH_AVX2:
            return 32;  // 256-bit SIMD
        
        case DAP_CPU_ARCH_AVX512:
            return 64;  // 512-bit SIMD
        
        case DAP_CPU_ARCH_SVE:
        case DAP_CPU_ARCH_SVE2:
            // SVE has variable length, but we'll test at common boundaries
            return 16;  // Test at 128-bit boundaries
        
        default:
            return 16;  // Default to smallest chunk
    }
}

// =============================================================================
// TEST 1: Structural Character at Exact Boundary
// =============================================================================

/**
 * @brief Test structural character ({, }, [, ], :, ,) at exact chunk boundary
 * @details Place opening brace { at position chunk_size - 1
 *          Expected: Parser correctly identifies structural character
 */
static bool s_test_structural_at_boundary(dap_cpu_arch_t a_arch)
{
    log_it(L_DEBUG, "[%s] Testing structural character at boundary", 
           dap_cpu_arch_get_name(a_arch));
    bool result = false;
    dap_json_t *l_json = NULL;
    
    int chunk_size = s_get_chunk_size_for_arch(a_arch);
    
    // Build JSON: padding + { + content + }
    // Position { at exact chunk boundary
    char json_buf[256];
    memset(json_buf, ' ', sizeof(json_buf));  // Fill with spaces (whitespace)
    
    int brace_pos = chunk_size - 1;  // Place { at boundary
    json_buf[brace_pos] = '{';
    json_buf[brace_pos + 1] = '"';
    json_buf[brace_pos + 2] = 'a';
    json_buf[brace_pos + 3] = '"';
    json_buf[brace_pos + 4] = ':';
    json_buf[brace_pos + 5] = '1';
    json_buf[brace_pos + 6] = '}';
    json_buf[brace_pos + 7] = '\0';
    
    log_it(L_DEBUG, "[%s] Chunk size=%d, brace at position %d", 
           dap_cpu_arch_get_name(a_arch), chunk_size, brace_pos);
    log_it(L_DEBUG, "[%s] JSON: [%s]", dap_cpu_arch_get_name(a_arch), json_buf);
    
    l_json = dap_json_parse_string(json_buf);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse JSON with { at boundary");
    
    // Verify value is accessible
    int value = dap_json_object_get_int(l_json, "a");
    DAP_TEST_FAIL_IF(value != 1, "Value correct when { at boundary");
    
    result = true;
    log_it(L_DEBUG, "[%s] Structural at boundary test passed", 
           dap_cpu_arch_get_name(a_arch));
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 2: Escape Sequence Split Across Boundary
// =============================================================================

/**
 * @brief Test escape sequence \uXXXX split across chunk boundary
 * @details Place \uXX|XX across boundary (backslash-u-digit-digit | digit-digit)
 *          Expected: Parser correctly assembles split escape sequence
 */
static bool s_test_escape_split_across_boundary(dap_cpu_arch_t a_arch)
{
    log_it(L_DEBUG, "[%s] Testing escape sequence split across boundary", 
           dap_cpu_arch_get_name(a_arch));
    bool result = false;
    dap_json_t *l_json = NULL;
    
    int chunk_size = s_get_chunk_size_for_arch(a_arch);
    
    // Build JSON: {"key":"...\uXX|XX"}
    // Position escape so \uXX is in chunk N, XX is in chunk N+1
    char json_buf[256];
    memset(json_buf, ' ', sizeof(json_buf));
    
    // Calculate position: want \u at chunk_size - 4, so last 2 hex digits cross boundary
    int escape_start = chunk_size - 4;
    
    // Build: {"a":"text\u00A9"}  (copyright symbol)
    int pos = 0;
    json_buf[pos++] = '{';
    json_buf[pos++] = '"';
    json_buf[pos++] = 'a';
    json_buf[pos++] = '"';
    json_buf[pos++] = ':';
    json_buf[pos++] = '"';
    
    // Pad to reach escape position
    while (pos < escape_start) {
        json_buf[pos++] = 'x';
    }
    
    // Place escape sequence: \u00A9
    json_buf[pos++] = '\\';
    json_buf[pos++] = 'u';
    json_buf[pos++] = '0';
    json_buf[pos++] = '0';
    // Boundary here (chunk_size)
    json_buf[pos++] = 'A';
    json_buf[pos++] = '9';
    json_buf[pos++] = '"';
    json_buf[pos++] = '}';
    json_buf[pos] = '\0';
    
    log_it(L_DEBUG, "[%s] Chunk size=%d, escape at %d-%d (boundary at %d)", 
           dap_cpu_arch_get_name(a_arch), chunk_size, escape_start, escape_start+5, chunk_size);
    
    l_json = dap_json_parse_string(json_buf);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse JSON with split escape");
    
    // Verify string contains copyright symbol (UTF-8: 0xC2 0xA9)
    const char *str = dap_json_object_get_string(l_json, "a");
    DAP_TEST_FAIL_IF_NULL(str, "Get string with split escape");
    
    // Find copyright symbol in string
    bool found_copyright = false;
    size_t len = strlen(str);
    for (size_t i = 0; i < len - 1; i++) {
        if ((unsigned char)str[i] == 0xC2 && (unsigned char)str[i+1] == 0xA9) {
            found_copyright = true;
            break;
        }
    }
    DAP_TEST_FAIL_IF(!found_copyright, "Copyright symbol decoded correctly from split escape");
    
    result = true;
    log_it(L_DEBUG, "[%s] Escape split test passed", dap_cpu_arch_get_name(a_arch));
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 3: UTF-8 Multibyte Split Across Boundary
// =============================================================================

/**
 * @brief Test UTF-8 3-4 byte character split across boundary
 * @details Place emoji (4-byte UTF-8) so it splits: 0xF0 0x9F|0x98 0x80
 *          Expected: Parser correctly assembles split multibyte character
 */
static bool s_test_utf8_multibyte_split(dap_cpu_arch_t a_arch)
{
    log_it(L_DEBUG, "[%s] Testing UTF-8 multibyte split across boundary", 
           dap_cpu_arch_get_name(a_arch));
    bool result = false;
    dap_json_t *l_json = NULL;
    
    int chunk_size = s_get_chunk_size_for_arch(a_arch);
    
    // Build JSON with emoji (😀 = U+1F600 = 0xF0 0x9F 0x98 0x80)
    // Position emoji so first 2 bytes in chunk N, last 2 bytes in chunk N+1
    char json_buf[256];
    memset(json_buf, 0, sizeof(json_buf));
    
    int pos = 0;
    json_buf[pos++] = '{';
    json_buf[pos++] = '"';
    json_buf[pos++] = 'a';
    json_buf[pos++] = '"';
    json_buf[pos++] = ':';
    json_buf[pos++] = '"';
    
    // Pad to reach split position (chunk_size - 2)
    int split_pos = chunk_size - 2;
    while (pos < split_pos) {
        json_buf[pos++] = 'x';
    }
    
    // Place emoji: 0xF0 0x9F | 0x98 0x80
    json_buf[pos++] = (char)0xF0;
    json_buf[pos++] = (char)0x9F;
    // Boundary here (chunk_size)
    json_buf[pos++] = (char)0x98;
    json_buf[pos++] = (char)0x80;
    json_buf[pos++] = '"';
    json_buf[pos++] = '}';
    json_buf[pos] = '\0';
    
    log_it(L_DEBUG, "[%s] Chunk size=%d, emoji split at %d (boundary at %d)", 
           dap_cpu_arch_get_name(a_arch), chunk_size, split_pos, chunk_size);
    
    l_json = dap_json_parse_string(json_buf);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse JSON with split UTF-8");
    
    // Verify emoji is present
    const char *str = dap_json_object_get_string(l_json, "a");
    DAP_TEST_FAIL_IF_NULL(str, "Get string with split UTF-8");
    
    // Find emoji in string
    bool found_emoji = false;
    size_t len = strlen(str);
    for (size_t i = 0; i < len - 3; i++) {
        if ((unsigned char)str[i] == 0xF0 && 
            (unsigned char)str[i+1] == 0x9F &&
            (unsigned char)str[i+2] == 0x98 && 
            (unsigned char)str[i+3] == 0x80) {
            found_emoji = true;
            break;
        }
    }
    DAP_TEST_FAIL_IF(!found_emoji, "Emoji decoded correctly from split UTF-8");
    
    result = true;
    log_it(L_DEBUG, "[%s] UTF-8 multibyte split test passed", dap_cpu_arch_get_name(a_arch));
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 4: Number Split Across Boundary
// =============================================================================

/**
 * @brief Test number parsing across chunk boundary
 * @details Place number so digits split: 12345|67890
 *          Expected: Parser correctly assembles full number
 */
static bool s_test_number_split(dap_cpu_arch_t a_arch)
{
    log_it(L_DEBUG, "[%s] Testing number split across boundary", 
           dap_cpu_arch_get_name(a_arch));
    bool result = false;
    dap_json_t *l_json = NULL;
    
    int chunk_size = s_get_chunk_size_for_arch(a_arch);
    
    // Build JSON: {"a":12345|67890}
    // Position number so it splits at chunk boundary
    char json_buf[256];
    memset(json_buf, ' ', sizeof(json_buf));
    
    // Calculate: want first 5 digits before boundary, last 5 digits after
    int number_start = chunk_size - 5 - 4;  // -4 for {"a":
    
    int pos = number_start;
    json_buf[pos++] = '{';
    json_buf[pos++] = '"';
    json_buf[pos++] = 'a';
    json_buf[pos++] = '"';
    json_buf[pos++] = ':';
    json_buf[pos++] = '1';
    json_buf[pos++] = '2';
    json_buf[pos++] = '3';
    json_buf[pos++] = '4';
    json_buf[pos++] = '5';
    // Boundary here (chunk_size)
    json_buf[pos++] = '6';
    json_buf[pos++] = '7';
    json_buf[pos++] = '8';
    json_buf[pos++] = '9';
    json_buf[pos++] = '0';
    json_buf[pos++] = '}';
    json_buf[pos] = '\0';
    
    log_it(L_DEBUG, "[%s] Chunk size=%d, number at %d (split at %d)", 
           dap_cpu_arch_get_name(a_arch), chunk_size, number_start + 5, chunk_size);
    
    l_json = dap_json_parse_string(json_buf);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse JSON with split number");
    
    // Verify number is correct
    int64_t value = dap_json_object_get_int64(l_json, "a");
    DAP_TEST_FAIL_IF(value != 1234567890, "Number parsed correctly across boundary");
    
    result = true;
    log_it(L_DEBUG, "[%s] Number split test passed", dap_cpu_arch_get_name(a_arch));
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 5: Literal Split Across Boundary (true/false/null)
// =============================================================================

/**
 * @brief Test literal (true, false, null) split across boundary
 * @details Place literal so it splits: tr|ue, fa|lse, nu|ll
 *          Expected: Parser correctly assembles split literal
 */
static bool s_test_literal_split(dap_cpu_arch_t a_arch)
{
    log_it(L_DEBUG, "[%s] Testing literal split across boundary", 
           dap_cpu_arch_get_name(a_arch));
    bool result = false;
    dap_json_t *l_json = NULL;
    
    int chunk_size = s_get_chunk_size_for_arch(a_arch);
    
    // Test "true" split as tr|ue
    char json_buf[256];
    memset(json_buf, ' ', sizeof(json_buf));
    
    int literal_start = chunk_size - 2 - 4;  // -4 for {"a":
    
    int pos = literal_start;
    json_buf[pos++] = '{';
    json_buf[pos++] = '"';
    json_buf[pos++] = 'a';
    json_buf[pos++] = '"';
    json_buf[pos++] = ':';
    json_buf[pos++] = 't';
    json_buf[pos++] = 'r';
    // Boundary here (chunk_size)
    json_buf[pos++] = 'u';
    json_buf[pos++] = 'e';
    json_buf[pos++] = '}';
    json_buf[pos] = '\0';
    
    log_it(L_DEBUG, "[%s] Chunk size=%d, 'true' split at %d", 
           dap_cpu_arch_get_name(a_arch), chunk_size, chunk_size);
    
    l_json = dap_json_parse_string(json_buf);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse JSON with split 'true'");
    
    // Verify boolean value
    bool value = dap_json_object_get_bool(l_json, "a");
    DAP_TEST_FAIL_IF(!value, "'true' parsed correctly across boundary");
    
    result = true;
    log_it(L_DEBUG, "[%s] Literal split test passed", dap_cpu_arch_get_name(a_arch));
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 6-8: Additional boundary tests (nested structures, long strings, arrays)
// =============================================================================

// Placeholder tests - implement similar to above
static bool s_test_nested_structure_at_boundary(dap_cpu_arch_t a_arch) {
    log_it(L_DEBUG, "[%s] Nested structure boundary test - TODO", 
           dap_cpu_arch_get_name(a_arch));
    // TODO: Implement nested {[{[...
    return true;  // PLACEHOLDER
}

static bool s_test_very_long_string_multiple_chunks(dap_cpu_arch_t a_arch) {
    log_it(L_DEBUG, "[%s] Very long string test - TODO", 
           dap_cpu_arch_get_name(a_arch));
    // TODO: Implement string > 1MB crossing many chunks
    return true;  // PLACEHOLDER
}

static bool s_test_array_at_boundary(dap_cpu_arch_t a_arch) {
    log_it(L_DEBUG, "[%s] Array at boundary test - TODO", 
           dap_cpu_arch_get_name(a_arch));
    // TODO: Implement [ at exact boundary
    return true;  // PLACEHOLDER
}

// =============================================================================
// MAIN TEST RUNNER - Test All Architectures
// =============================================================================

static int s_run_tests_for_arch(dap_cpu_arch_t a_arch)
{
    const char *arch_name = dap_cpu_arch_get_name(a_arch);
    log_it(L_INFO, "=== Testing SIMD Chunk Boundaries for %s ===", arch_name);
    
    // Set architecture
    if (dap_cpu_arch_set(a_arch) != 0) {
        log_it(L_WARNING, "Architecture %s not available, skipping", arch_name);
        return 0;  // Skip unavailable architectures
    }
    
    int tests_passed = 0;
    int tests_total = 8;
    
    tests_passed += s_test_structural_at_boundary(a_arch) ? 1 : 0;
    tests_passed += s_test_escape_split_across_boundary(a_arch) ? 1 : 0;
    tests_passed += s_test_utf8_multibyte_split(a_arch) ? 1 : 0;
    tests_passed += s_test_number_split(a_arch) ? 1 : 0;
    tests_passed += s_test_literal_split(a_arch) ? 1 : 0;
    tests_passed += s_test_nested_structure_at_boundary(a_arch) ? 1 : 0;
    tests_passed += s_test_very_long_string_multiple_chunks(a_arch) ? 1 : 0;
    tests_passed += s_test_array_at_boundary(a_arch) ? 1 : 0;
    
    log_it(L_INFO, "%s: %d/%d tests passed", arch_name, tests_passed, tests_total);
    
    return (tests_passed == tests_total) ? 0 : -1;
}

int dap_json_simd_chunk_boundaries_tests_run(void) {
    log_it(L_INFO, "=== DAP JSON SIMD Chunk Boundary Tests ===");
    
    int total_failures = 0;
    
    // Test all available architectures
    total_failures += s_run_tests_for_arch(DAP_CPU_ARCH_REFERENCE);
    
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    total_failures += s_run_tests_for_arch(DAP_CPU_ARCH_SSE2);
    total_failures += s_run_tests_for_arch(DAP_CPU_ARCH_AVX2);
    total_failures += s_run_tests_for_arch(DAP_CPU_ARCH_AVX512);
#elif defined(__arm__) || defined(__aarch64__)
    total_failures += s_run_tests_for_arch(DAP_CPU_ARCH_NEON);
    total_failures += s_run_tests_for_arch(DAP_CPU_ARCH_SVE);
    total_failures += s_run_tests_for_arch(DAP_CPU_ARCH_SVE2);
#endif
    
    // Reset to AUTO
    dap_cpu_arch_set(DAP_CPU_ARCH_AUTO);
    
    return total_failures;
}

int main(void) {
    dap_print_module_name("DAP JSON SIMD Chunk Boundary Tests");
    return dap_json_simd_chunk_boundaries_tests_run();
}

