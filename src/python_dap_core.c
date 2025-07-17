/*
 * Python DAP Core Implementation
 * Wrapper functions around DAP SDK core functions
 */

#include "python_dap.h"
#include "dap_common.h"
#include <stdlib.h>
#include <time.h>
#include <stdio.h>

// Core wrapper implementations

int py_dap_common_init(const char *console_title, const char *a_log_file) {
    return dap_common_init(console_title, a_log_file);
}

void py_dap_common_deinit(void) {
    dap_common_deinit();
}

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

// Time wrapper functions - simplified

uint64_t py_dap_time_now(void) {
    return (uint64_t)time(NULL);
}

int py_dap_time_to_str_rfc822(char * out, size_t out_size_max, uint64_t timestamp) {
    if (!out || out_size_max == 0) {
        return -1;
    }
    
    time_t t = (time_t)timestamp;
    struct tm *tm_info = gmtime(&t);
    if (!tm_info) {
        return -1;
    }
    
    // RFC 822 format: "Tue, 15 Nov 1994 12:45:26 GMT"
    int result = strftime(out, out_size_max, "%a, %d %b %Y %H:%M:%S GMT", tm_info);
    return (result > 0) ? 0 : -1;
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