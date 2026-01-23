/*
 * Quick test for zero-copy string scanning
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "dap_common.h"
#include "internal/dap_json_string.h"

#define LOG_TAG "test_zero_copy"

int main() {
    printf("=== Testing Zero-Copy String Scanner ===\n\n");
    
    // Test 1: Simple string without escapes
    {
        const char *json = "\"hello world\"";
        dap_json_string_zc_t result;
        uint32_t end_offset = 0;
        
        bool ok = dap_json_string_scan_ref(
            (const uint8_t*)json,
            strlen(json),
            &result,
            &end_offset
        );
        
        assert(ok);
        assert(result.length == 11); // "hello world"
        assert(result.needs_unescape == 0);
        assert(end_offset == 13); // after closing quote
        assert(strncmp(result.data, "hello world", 11) == 0);
        
        printf("✅ Test 1: Simple string (no escapes)\n");
    }
    
    // Test 2: String with escape sequences
    {
        const char *json = "\"hello\\nworld\"";
        dap_json_string_zc_t result;
        uint32_t end_offset = 0;
        
        bool ok = dap_json_string_scan_ref(
            (const uint8_t*)json,
            strlen(json),
            &result,
            &end_offset
        );
        
        assert(ok);
        assert(result.needs_unescape == 1);
        assert(result.length == 12); // "hello\nworld" (raw)
        
        printf("✅ Test 2: String with escapes\n");
    }
    
    // Test 3: Empty string
    {
        const char *json = "\"\"";
        dap_json_string_zc_t result;
        uint32_t end_offset = 0;
        
        bool ok = dap_json_string_scan_ref(
            (const uint8_t*)json,
            strlen(json),
            &result,
            &end_offset
        );
        
        assert(ok);
        assert(result.length == 0);
        assert(end_offset == 2);
        
        printf("✅ Test 3: Empty string\n");
    }
    
    // Test 4: Unicode escape
    {
        const char *json = "\"hello\\u0041world\""; // \u0041 = 'A'
        dap_json_string_zc_t result;
        uint32_t end_offset = 0;
        
        bool ok = dap_json_string_scan_ref(
            (const uint8_t*)json,
            strlen(json),
            &result,
            &end_offset
        );
        
        assert(ok);
        assert(result.needs_unescape == 1);
        
        printf("✅ Test 4: Unicode escape\n");
    }
    
    printf("\n🎉 ALL TESTS PASSED!\n");
    return 0;
}
