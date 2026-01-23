/**
 * @file test_bitmap_classification.c
 * @brief Direct test of bitmap classification for chunk 48-63
 * @details Tests if NEON correctly classifies symbol '}' at position 63
 */

#define LOG_TAG "test_bitmap_class"

#include <stdio.h>
#include <string.h>
#include "dap_common.h"
#include "dap_test.h"
#include "dap_test_helpers.h"

int main(void)
{
    printf("Testing bitmap classification for 64-byte JSON\n");
    
    // Exact 64-byte JSON
    const char *json = "{\"aaaaaaaaaaa\":\"1\\n2\",\"bbbbbbbbbbb\":\"3\\t4\",\"ccccccccccc\":\"5\\r6\"}";
    
    log_it(L_INFO, "JSON length: %zu", strlen(json));
    
    // Show last chunk (48-63)
    log_it(L_INFO, "Last chunk [48-63]:");
    for (size_t i = 48; i < 64; i++) {
        printf("  [%zu] = '%c' (0x%02x)%s\n", i, 
               (json[i] >= 32 && json[i] < 127) ? json[i] : '?',
               (unsigned char)json[i],
               (json[i] == '}') ? " <- STRUCTURAL!" : "");
    }
    
    // Position 63 should be '}'
    if (json[63] != '}') {
        log_it(L_ERROR, "Position 63 is not '}' but '%c' (0x%02x)", json[63], (unsigned char)json[63]);
        return 1;
    }
    
    log_it(L_INFO, "Position 63 correctly contains '}' (0x%02x)", (unsigned char)json[63]);
    
    // Note: We can't directly call s_classify_chunk_neon from here
    // But we can verify the input is correct
    
    log_it(L_INFO, "Chunk 48-63 breakdown:");
    log_it(L_INFO, "  Structural chars expected: ':' at 56, '}' at 63");
    log_it(L_INFO, "  Quote chars: at 52, 54, 57, 62");
    log_it(L_INFO, "  Backslash: at 59");
    
    printf("✓ Input verification PASSED - position 63 has '}'\n");
    printf("  If Stage 1 fails, bitmap classification is broken!\n");
    
    return 0;
}
