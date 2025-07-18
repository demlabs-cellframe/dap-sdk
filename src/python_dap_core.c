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

// Memory management wrappers - use standard C functions

void* py_dap_malloc(size_t size) {
    return malloc(size);
}

void py_dap_free(void* ptr) {
    free(ptr);
}

void* py_dap_calloc(size_t num, size_t size) {
    return calloc(num, size);
}

void* py_dap_realloc(void* ptr, size_t size) {
    return realloc(ptr, size);
}

// Time wrapper functions

uint64_t py_dap_time_now(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    }
    return 0;
}

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

// Logging wrapper functions - simplified

void py_dap_log_level_set(int level) {
    dap_log_level_set((enum dap_log_level)level);
}

void py_dap_log_set_external_output(int output_type, void* callback) {
    dap_log_set_external_output((LOGGER_EXTERNAL_OUTPUT)output_type, callback);
}

void py_dap_log_set_format(int format) {
    dap_log_set_format((dap_log_format_t)format);
}

// System wrapper functions

char* py_exec_with_ret_multistring(const char* command) {
    return exec_with_ret_multistring(command);
} 