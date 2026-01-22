/**
 * @file test_reference_scanner_64.c
 * @brief Test reference scanner directly with 64-byte JSON
 */

#define LOG_TAG "test_ref_scanner_64"

#include <stdio.h>
#include <string.h>
#include "dap_common.h"

// Forward declarations
typedef struct dap_json_stage1 dap_json_stage1_t;
extern dap_json_stage1_t* dap_json_stage1_create(const uint8_t *input, size_t len);
extern void dap_json_stage1_free(dap_json_stage1_t *stage1);
extern size_t dap_json_stage1_scan_string_ref(dap_json_stage1_t *a_stage1, size_t a_start_pos);

int main(void)
{
    printf("Testing reference scanner with 64-byte JSON\n");
    
    // Exact 64-byte JSON that fails on ARM64
    const char *json = "{\"aaaaaaaaaaa\":\"1\\n2\",\"bbbbbbbbbbb\":\"3\\t4\",\"ccccccccccc\":\"5\\r6\"}";
    size_t len = strlen(json);
    
    log_it(L_INFO, "JSON length: %zu", len);
    log_it(L_INFO, "JSON: %s", json);
    
    // Create stage1 structure
    dap_json_stage1_t *stage1 = dap_json_stage1_create((const uint8_t *)json, len);
    if (!stage1) {
        log_it(L_ERROR, "Failed to create stage1");
        return 1;
    }
    
    // Manually test reference scanner for string at position 57
    log_it(L_INFO, "Testing string at position 57: \"5\\r6\"");
    
    // Position 57 should be opening quote
    if (json[57] != '"') {
        log_it(L_ERROR, "Position 57 is not quote! char='%c' (0x%02x)", json[57], (unsigned char)json[57]);
        return 1;
    }
    
    // Call reference scanner directly
    size_t end = dap_json_stage1_scan_string_ref(stage1, 57);
    
    log_it(L_INFO, "Reference scanner returned: %zu (expected: 63)", end);
    
    if (end == 57) {
        log_it(L_ERROR, "✗ Reference scanner FAILED to find string end");
        return 1;
    }
    
    if (end == 63) {
        log_it(L_INFO, "✓ Reference scanner correctly found string end at position 63");
    } else {
        log_it(L_WARNING, "Reference scanner returned unexpected position: %zu", end);
    }
    
    dap_json_stage1_free(stage1);
    
    printf("✓ Reference scanner test PASSED\n");
    return 0;
}
