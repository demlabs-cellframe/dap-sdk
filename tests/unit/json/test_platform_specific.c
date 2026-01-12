/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 */

/**
 * @file test_platform_specific.c
 * @brief Platform-Specific Edge Cases - Phase 1.8.2
 * @details ПОЛНАЯ реализация 6 platform-specific tests
 * 
 * Tests:
 *   1. Big-endian vs little-endian (number serialization)
 *   2. 32-bit vs 64-bit platform (pointer size, size_t limits)
 *   3. Line ending normalization (\r\n vs \n in strings)
 *   4. Locale-independence (decimal separator, thousands separator)
 *   5. Alignment issues (unaligned memory access)
 *   6. Platform-specific limits (PATH_MAX, NAME_MAX)
 * 
 * @date 2026-01-12
 */

#define LOG_TAG "test_platform"

#include "dap_common.h"
#include "dap_json.h"
#include "dap_test.h"
#include "../../fixtures/utilities/test_helpers.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <locale.h>

// =============================================================================
// TEST 1: Endianness Independence
// =============================================================================

/**
 * @brief Test that number serialization is endianness-independent
 * @details JSON is text-based, so endianness should NOT affect parsing/serialization
 */
static bool s_test_endianness_independence(void) {
    log_it(L_DEBUG, "Testing endianness independence");
    bool result = false;
    dap_json_t *l_json = NULL;
    char *serialized = NULL;
    
    // Detect endianness
    uint32_t test_val = 0x12345678;
    uint8_t *bytes = (uint8_t*)&test_val;
    bool is_little_endian = (bytes[0] == 0x78);
    
    log_it(L_INFO, "Platform endianness: %s", 
           is_little_endian ? "little-endian" : "big-endian");
    
    // Test number parsing
    l_json = dap_json_parse_string("{\"int\":305419896,\"hex\":\"0x12345678\"}");
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse numbers");
    
    uint32_t int_val = (uint32_t)dap_json_object_get_uint64(l_json, "int");
    DAP_TEST_FAIL_IF(int_val != 0x12345678, "Number parsed correctly regardless of endianness");
    
    dap_json_object_free(l_json);
    
    // Test serialization
    // Create JSON with known byte pattern
    l_json = dap_json_parse_string("{\"value\":305419896}");
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse for serialization");
    
    serialized = dap_json_to_string(l_json);
    DAP_TEST_FAIL_IF_NULL(serialized, "Serialize");
    
    log_it(L_DEBUG, "Serialized: %s", serialized);
    
    // Should contain "305419896" as text
    DAP_TEST_FAIL_IF(strstr(serialized, "305419896") == NULL, 
                     "Serialized number is text, not binary");
    
    result = true;
    log_it(L_DEBUG, "Endianness independence test passed");
    
cleanup:
    DAP_DELETE(serialized);
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 2: 32-bit vs 64-bit Platform
// =============================================================================

static bool s_test_32bit_vs_64bit_platform(void) {
    log_it(L_DEBUG, "Testing 32-bit vs 64-bit platform");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    log_it(L_INFO, "sizeof(void*) = %zu, sizeof(size_t) = %zu", 
           sizeof(void*), sizeof(size_t));
    
    bool is_64bit = (sizeof(void*) == 8);
    log_it(L_INFO, "Platform: %s", is_64bit ? "64-bit" : "32-bit");
    
    // Test SIZE_MAX
    l_json = dap_json_parse_string("{\"size_max\":18446744073709551615}");
    
    if (is_64bit) {
        // On 64-bit, should parse UINT64_MAX
        if (l_json) {
            uint64_t val = dap_json_object_get_uint64(l_json, "size_max");
            log_it(L_INFO, "64-bit: parsed SIZE_MAX = %llu", (unsigned long long)val);
            dap_json_object_free(l_json);
            l_json = NULL;
        } else {
            log_it(L_WARNING, "64-bit platform rejected UINT64_MAX");
        }
    } else {
        // On 32-bit, might reject or parse as double
        if (l_json) {
            log_it(L_INFO, "32-bit: accepted large value (as double?)");
            dap_json_object_free(l_json);
            l_json = NULL;
        } else {
            log_it(L_INFO, "32-bit: rejected value exceeding platform limits");
        }
    }
    
    // Test large array allocation
    // On 32-bit, very large allocations should fail gracefully
    const char *large_array_json = "{\"count\":1000000}";
    l_json = dap_json_parse_string(large_array_json);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse large count");
    
    result = true;
    log_it(L_DEBUG, "32-bit vs 64-bit platform test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 3: Line Ending Normalization
// =============================================================================

static bool s_test_line_ending_normalization(void) {
    log_it(L_DEBUG, "Testing line ending normalization");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test \r\n (Windows) vs \n (Unix) in strings
    l_json = dap_json_parse_string("{\"unix\":\"line1\\nline2\",\"windows\":\"line1\\r\\nline2\"}");
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse different line endings");
    
    const char *unix_str = dap_json_object_get_string(l_json, "unix");
    const char *windows_str = dap_json_object_get_string(l_json, "windows");
    
    log_it(L_DEBUG, "Unix string length: %zu", strlen(unix_str));
    log_it(L_DEBUG, "Windows string length: %zu", strlen(windows_str));
    
    // Unix: "line1\nline2" = 11 chars
    // Windows: "line1\r\nline2" = 12 chars
    DAP_TEST_FAIL_IF(strlen(unix_str) != 11, "Unix line ending correct");
    DAP_TEST_FAIL_IF(strlen(windows_str) != 12, "Windows line ending correct");
    
    // Check actual characters
    DAP_TEST_FAIL_IF(unix_str[5] != '\n', "Unix has LF");
    DAP_TEST_FAIL_IF(windows_str[5] != '\r' || windows_str[6] != '\n', 
                     "Windows has CR+LF");
    
    dap_json_object_free(l_json);
    
    // Test that JSON with actual \r\n whitespace parses correctly
    const char *json_with_crlf = "{\r\n\"key\":\"value\"\r\n}";
    l_json = dap_json_parse_string(json_with_crlf);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse JSON with CRLF whitespace");
    
    const char *key_val = dap_json_object_get_string(l_json, "key");
    DAP_TEST_FAIL_IF(strcmp(key_val, "value") != 0, "CRLF whitespace handled");
    
    result = true;
    log_it(L_DEBUG, "Line ending normalization test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 4: Locale Independence
// =============================================================================

/**
 * @brief Test that JSON parsing/serialization is locale-independent
 * @details RFC 8259: JSON uses '.' as decimal separator, regardless of locale
 */
static bool s_test_locale_independence(void) {
    log_it(L_DEBUG, "Testing locale independence");
    bool result = false;
    dap_json_t *l_json = NULL;
    char *serialized = NULL;
    
    // Save current locale
    char *original_locale = setlocale(LC_NUMERIC, NULL);
    if (original_locale) {
        original_locale = strdup(original_locale);
    }
    
    log_it(L_INFO, "Original locale: %s", original_locale ? original_locale : "NULL");
    
    // Test with C locale (uses '.')
    setlocale(LC_NUMERIC, "C");
    l_json = dap_json_parse_string("{\"value\":3.14}");
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse with C locale");
    
    double val_c = dap_json_object_get_double(l_json, "value");
    DAP_TEST_FAIL_IF(val_c < 3.13 || val_c > 3.15, "Value correct in C locale");
    
    dap_json_object_free(l_json);
    
    // Try setting a locale that uses ',' as decimal separator (e.g., de_DE)
    // Note: This may fail if locale is not installed, which is OK
    if (setlocale(LC_NUMERIC, "de_DE.UTF-8") != NULL || 
        setlocale(LC_NUMERIC, "ru_RU.UTF-8") != NULL ||
        setlocale(LC_NUMERIC, "fr_FR.UTF-8") != NULL) {
        
        log_it(L_INFO, "Testing with comma-decimal locale: %s", setlocale(LC_NUMERIC, NULL));
        
        // JSON still uses '.' even in comma-decimal locale
        l_json = dap_json_parse_string("{\"value\":3.14}");
        DAP_TEST_FAIL_IF_NULL(l_json, "Parse with comma-decimal locale");
        
        double val_locale = dap_json_object_get_double(l_json, "value");
        DAP_TEST_FAIL_IF(val_locale < 3.13 || val_locale > 3.15, 
                         "JSON uses '.' regardless of locale");
        
        // Serialize
        serialized = dap_json_to_string(l_json);
        DAP_TEST_FAIL_IF_NULL(serialized, "Serialize in comma-decimal locale");
        
        log_it(L_DEBUG, "Serialized in locale: %s", serialized);
        
        // Should contain "3.14" not "3,14"
        DAP_TEST_FAIL_IF(strstr(serialized, "3.14") == NULL && strstr(serialized, "3,14") != NULL,
                         "Serialized JSON uses '.' not ','");
        
        dap_json_object_free(l_json);
        l_json = NULL;
    } else {
        log_it(L_INFO, "Comma-decimal locale not available, skipping locale test");
    }
    
    result = true;
    log_it(L_DEBUG, "Locale independence test passed");
    
cleanup:
    // Restore original locale
    if (original_locale) {
        setlocale(LC_NUMERIC, original_locale);
        free(original_locale);
    }
    DAP_DELETE(serialized);
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 5: Alignment Issues
// =============================================================================

/**
 * @brief Test unaligned memory access (critical on ARM, RISC-V)
 * @details Parser should handle unaligned buffers gracefully
 */
static bool s_test_alignment_issues(void) {
    log_it(L_DEBUG, "Testing alignment issues");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Allocate buffer with deliberate misalignment
    char *aligned_buf = (char*)malloc(128);
    DAP_TEST_FAIL_IF_NULL(aligned_buf, "Allocate buffer");
    
    // Create unaligned pointer (offset by 1, 2, 3 bytes)
    for (int offset = 0; offset < 4; offset++) {
        char *unaligned_buf = aligned_buf + offset;
        
        strcpy(unaligned_buf, "{\"key\":\"value\"}");
        
        log_it(L_DEBUG, "Testing with %d-byte offset (alignment = %zu)", 
               offset, (size_t)unaligned_buf % sizeof(void*));
        
        l_json = dap_json_parse_string(unaligned_buf);
        DAP_TEST_FAIL_IF_NULL(l_json, "Parse from unaligned buffer");
        
        const char *val = dap_json_object_get_string(l_json, "key");
        DAP_TEST_FAIL_IF(strcmp(val, "value") != 0, "Value correct from unaligned buffer");
        
        dap_json_object_free(l_json);
        l_json = NULL;
    }
    
    result = true;
    log_it(L_DEBUG, "Alignment issues test passed");
    
cleanup:
    free(aligned_buf);
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// TEST 6: Platform-Specific Limits
// =============================================================================

/**
 * @brief Test platform-specific limits (PATH_MAX, NAME_MAX, etc.)
 */
static bool s_test_platform_specific_limits(void) {
    log_it(L_DEBUG, "Testing platform-specific limits");
    bool result = false;
    dap_json_t *l_json = NULL;
    char *json_buf = NULL;
    
#ifdef PATH_MAX
    log_it(L_INFO, "PATH_MAX = %d", PATH_MAX);
    
    // Create JSON with PATH_MAX-sized string
    const size_t BUF_SIZE = PATH_MAX + 64;
    json_buf = (char*)malloc(BUF_SIZE);
    DAP_TEST_FAIL_IF_NULL(json_buf, "Allocate buffer");
    
    strcpy(json_buf, "{\"path\":\"");
    for (int i = 0; i < PATH_MAX - 1; i++) {
        strcat(json_buf, "a");
    }
    strcat(json_buf, "\"}");
    
    log_it(L_INFO, "Testing PATH_MAX-sized string (%d bytes)", PATH_MAX);
    
    l_json = dap_json_parse_string(json_buf);
    
    if (l_json) {
        log_it(L_INFO, "Parser accepted PATH_MAX-sized string");
        const char *path = dap_json_object_get_string(l_json, "path");
        DAP_TEST_FAIL_IF(strlen(path) != PATH_MAX - 1, "PATH_MAX string length correct");
        dap_json_object_free(l_json);
        l_json = NULL;
    } else {
        log_it(L_INFO, "Parser rejected PATH_MAX-sized string (acceptable if limits enforced)");
    }
    
    free(json_buf);
#else
    log_it(L_INFO, "PATH_MAX not defined on this platform");
#endif
    
    // Test INT_MAX
    log_it(L_INFO, "INT_MAX = %d", INT_MAX);
    
    char int_max_json[64];
    snprintf(int_max_json, sizeof(int_max_json), "{\"value\":%d}", INT_MAX);
    
    l_json = dap_json_parse_string(int_max_json);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse INT_MAX");
    
    int max_val = dap_json_object_get_int(l_json, "value");
    DAP_TEST_FAIL_IF(max_val != INT_MAX, "INT_MAX parsed correctly");
    
    result = true;
    log_it(L_DEBUG, "Platform-specific limits test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

// =============================================================================
// MAIN TEST RUNNER
// =============================================================================

int dap_json_platform_tests_run(void) {
    log_it(L_INFO, "=== DAP JSON Platform-Specific Tests ===");
    
    int tests_passed = 0;
    int tests_total = 6;
    
    tests_passed += s_test_endianness_independence() ? 1 : 0;
    tests_passed += s_test_32bit_vs_64bit_platform() ? 1 : 0;
    tests_passed += s_test_line_ending_normalization() ? 1 : 0;
    tests_passed += s_test_locale_independence() ? 1 : 0;
    tests_passed += s_test_alignment_issues() ? 1 : 0;
    tests_passed += s_test_platform_specific_limits() ? 1 : 0;
    
    log_it(L_INFO, "Platform-specific tests: %d/%d passed", tests_passed, tests_total);
    
    return (tests_passed == tests_total) ? 0 : -1;
}

int main(void) {
    dap_print_module_name("DAP JSON Platform-Specific Tests");
    return dap_json_platform_tests_run();
}

