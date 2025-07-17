/*
 * Python DAP Core Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/python_cellframe_common.h"

// Core initialization functions
int dap_common_init(void) {
    return 0;  // Success
}

void dap_common_deinit(void) {
    // Cleanup
}

// Memory management functions
void* dap_malloc(size_t size) {
    return malloc(size);
}

void dap_free(void* ptr) {
    if (ptr) {
        free(ptr);
    }
}

void* dap_calloc(size_t num, size_t size) {
    return calloc(num, size);
}

void* dap_realloc(void* ptr, size_t size) {
    return realloc(ptr, size);
}

// System functions
const char* exec_with_ret_multistring(const char* command) {
    if (!command) {
        return NULL;
    }
    
    FILE* fp = popen(command, "r");
    if (!fp) {
        return NULL;
    }
    
    static char buffer[4096];
    size_t pos = 0;
    int c;
    
    while ((c = fgetc(fp)) != EOF && pos < sizeof(buffer) - 1) {
        buffer[pos++] = c;
    }
    buffer[pos] = '\0';
    
    pclose(fp);
    return buffer;
}
