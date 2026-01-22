/**
 * @file test_stage1_run_64.c
 * @brief Test full Stage 1 run with 64-byte JSON
 */

#define LOG_TAG "test_stage1_run_64"

#include <stdio.h>
#include <string.h>
#include "dap_common.h"

// Forward declarations
typedef struct dap_json_stage1 dap_json_stage1_t;
extern dap_json_stage1_t* dap_json_stage1_create(const uint8_t *input, size_t len);
extern void dap_json_stage1_free(dap_json_stage1_t *stage1);
extern int dap_json_stage1_run(dap_json_stage1_t *stage1);

int main(void)
{
    printf("Testing full Stage 1 run with 64-byte JSON\n");
    
    // Exact 64-byte JSON that fails on ARM64
    const char *json = "{\"aaaaaaaaaaa\":\"1\\n2\",\"bbbbbbbbbbb\":\"3\\t4\",\"ccccccccccc\":\"5\\r6\"}";
    size_t len = strlen(json);
    
    log_it(L_INFO, "JSON length: %zu", len);
    log_it(L_INFO, "JSON: %s", json);
    
    // Show byte breakdown
    log_it(L_INFO, "Chunk breakdown (16-byte chunks):");
    for (size_t i = 0; i < len; i += 16) {
        printf("  [%2zu-%2zu]: ", i, (i+15 < len) ? i+15 : len-1);
        for (size_t j = i; j < i+16 && j < len; j++) {
            if (json[j] == '"') printf("<%c>", json[j]);
            else if (json[j] == '\\') printf("<\\>");
            else printf("%c", json[j]);
        }
        printf("\n");
    }
    
    // Create stage1 structure
    dap_json_stage1_t *stage1 = dap_json_stage1_create((const uint8_t *)json, len);
    if (!stage1) {
        log_it(L_ERROR, "Failed to create stage1");
        return 1;
    }
    
    // Run full Stage 1
    log_it(L_INFO, "Running Stage 1...");
    int result = dap_json_stage1_run(stage1);
    
    log_it(L_INFO, "Stage 1 result: %d", result);
    
    if (result != 0) {
        log_it(L_ERROR, "✗ Stage 1 FAILED with code: %d", result);
        dap_json_stage1_free(stage1);
        return 1;
    }
    
    log_it(L_INFO, "✓ Stage 1 PASSED");
    dap_json_stage1_free(stage1);
    
    printf("✓ Stage 1 run test PASSED\n");
    return 0;
}
