/*
 * Python DAP Core Implementation
 * Wrapper functions around DAP SDK core functions
 */

#include "python_dap.h"
#include "dap_common.h"
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

// Core wrapper implementations (common init functions moved to python_dap_common.c)

// Memory management and time functions are implemented in:
// - python_dap_system.c (memory management using DAP SDK functions)  
// - python_dap_time.c (time functions)

// String utility wrappers

char* py_dap_strdup(const char* str) {
    if (!str) {
        return NULL;
    }
    
    size_t len = strlen(str) + 1;
    char* copy = py_dap_malloc(len);
    if (copy) {
        memcpy(copy, str, len);
    }
    return copy;
}

void py_dap_free_string(char* str) {
    if (str) {
        py_dap_free(str);
    }
}

// Generic utility functions

int py_dap_errno_get(void) {
    return errno;
}

const char* py_dap_strerror(int errnum) {
    return strerror(errnum);
}

void py_dap_zero_memory(void* ptr, size_t size) {
    if (ptr && size > 0) {
        memset(ptr, 0, size);
    }
}

int py_dap_rand_int(int min, int max) {
    if (min > max) {
        int temp = min;
        min = max;
        max = temp;
    }
    return min + (rand() % (max - min + 1));
}

// Logging functions implemented in python_dap_logging.c
// System functions implemented in python_dap_system.c 