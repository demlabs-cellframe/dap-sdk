/*
 * Python DAP Time Implementation
 * Real bindings to DAP SDK Time functions
 */

#include "python_dap.h"
#include "dap_time.h"
#include <errno.h>
#include <time.h>
#include <string.h>
#include <unistd.h>  // for sleep()

// Time wrapper implementations using REAL DAP SDK functions

uint64_t py_dap_time_now(void) {
    // Call REAL DAP SDK function
    return dap_time_now();
}

uint64_t py_dap_time_now_sec(void) {
    // Call REAL DAP SDK function
    return dap_time_now();  // dap_time_now() returns time in seconds
}

uint64_t py_dap_time_now_usec(void) {
    // Get current time in microseconds
    return dap_nanotime_now() / 1000;  // Convert nanoseconds to microseconds
}

char* py_dap_time_to_str_rfc822(uint64_t a_timestamp_sec) {
    // Call REAL DAP SDK function
    static char s_buf[128];
    int l_ret = dap_time_to_str_rfc822(s_buf, sizeof(s_buf), a_timestamp_sec);
    return (l_ret == 0) ? s_buf : NULL;
}

char* py_dap_time_to_str_rfc3339(uint64_t a_timestamp_sec) {
    // Format timestamp to RFC3339 format
    static char s_buf[128];
    time_t time_val = (time_t)a_timestamp_sec;
    struct tm* tm_info = gmtime(&time_val);
    
    if (tm_info) {
        strftime(s_buf, sizeof(s_buf), "%Y-%m-%dT%H:%M:%SZ", tm_info);
        return s_buf;
    }
    
    return NULL;
}

uint64_t py_dap_time_from_str_rfc822(const char* a_time_str) {
    if (!a_time_str) {
        return 0;
    }
    
    // Call REAL DAP SDK function - it returns dap_time_t directly
    return dap_time_from_str_rfc822(a_time_str);
}

void py_dap_usleep(uint64_t a_microseconds) {
    // Call REAL DAP SDK function
    dap_usleep(a_microseconds);
}

void py_dap_sleep(uint32_t a_seconds) {
    // Use standard sleep function since dap_sleep doesn't exist
    sleep(a_seconds);
}

uint64_t py_dap_gettimeofday(void) {
    // Get current time of day in microseconds
    struct timeval tv;
    if (gettimeofday(&tv, NULL) == 0) {
        return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
    }
    return 0;
}

// Python wrapper functions

PyObject* py_dap_time_now_wrapper(PyObject* self, PyObject* args) {
    uint64_t timestamp = py_dap_time_now();
    return PyLong_FromUnsignedLongLong(timestamp);
}

PyObject* py_dap_time_to_str_rfc822_wrapper(PyObject* self, PyObject* args) {
    unsigned long long timestamp;
    
    if (!PyArg_ParseTuple(args, "K", &timestamp)) {
        return NULL;
    }
    
    char* rfc822_str = py_dap_time_to_str_rfc822((uint64_t)timestamp);
    if (!rfc822_str) {
        Py_RETURN_NONE;
    }
    
    return PyUnicode_FromString(rfc822_str);
}

// Module method array
static PyMethodDef time_methods[] = {
    {"py_dap_time_now", py_dap_time_now_wrapper, METH_NOARGS, "Get current timestamp"},
    {"dap_time_now", py_dap_time_now_wrapper, METH_NOARGS, "Get current timestamp (alias)"},
    {"dap_time_to_str_rfc822", py_dap_time_to_str_rfc822_wrapper, METH_VARARGS, "Convert timestamp to RFC822"},
    {NULL, NULL, 0, NULL}  // Sentinel
};

// Function to get methods array
PyMethodDef* py_dap_time_get_methods(void) {
    return time_methods;
}

// Module initialization function
int py_dap_time_module_init(PyObject* module) {
    // Add time-related constants if needed
    // Currently no constants for time module
    return 0;
} 