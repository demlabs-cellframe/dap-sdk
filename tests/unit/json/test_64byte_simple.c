/**
 * @file test_64byte_simple.c
 * @brief Minimal test for 64-byte JSON bug
 */

#define LOG_TAG "test_64byte"

#include <stdio.h>
#include <string.h>
#include "dap_common.h"
#include "dap_json.h"

int main(void)
{
    printf("Testing 64-byte JSON parsing\n");
    
    // Exact 64-byte JSON
    const char *json = "{\"aaaaaaaaaaa\":\"1\\n2\",\"bbbbbbbbbbb\":\"3\\t4\",\"ccccccccccc\":\"5\\r6\"}";
    
    printf("JSON length: %zu\n", strlen(json));
    printf("JSON: %s\n", json);
    
    // Enable debug AFTER module might be auto-initialized
    dap_json_set_debug(true);
    
    dap_json_t *result = dap_json_parse_string(json);
    
    if (result) {
        printf("✓ SUCCESS - JSON parsed!\n");
        dap_json_object_free(result);
        return 0;
    } else {
        printf("✗ FAILED - JSON parsing failed\n");
        return 1;
    }
}
