/*
 * Python DAP System Implementation
 * Real bindings to DAP SDK system functions
 */

#include "python_dap.h"
#include "dap_common.h"
#include <string.h>
#include <errno.h>
#include <stdlib.h>

// Memory management wrapper implementations using REAL DAP SDK functions

void* py_dap_malloc(size_t a_size) {
    // Call REAL DAP SDK function
    return DAP_NEW_SIZE(void, a_size);
}

void py_dap_free(void* a_ptr) {
    if (a_ptr) {
        // Call REAL DAP SDK function
        DAP_DELETE(a_ptr);
    }
}

void* py_dap_realloc(void* a_ptr, size_t a_size) {
    // Call REAL DAP SDK function
    return DAP_REALLOC(a_ptr, a_size);
}

void* py_dap_calloc(size_t a_num, size_t a_size) {
    size_t total_size = a_num * a_size;
    void* ptr = py_dap_malloc(total_size);
    if (ptr) {
        memset(ptr, 0, total_size);
    }
    return ptr;
}

// Command execution wrapper using REAL DAP SDK

char* py_exec_with_ret_multistring(const char* a_command) {
    if (!a_command) {
        return NULL;
    }
    
    // Execute command and return result
    // Note: This is a simplified implementation
    // Real DAP SDK might have exec_with_ret function
    FILE* pipe = popen(a_command, "r");
    if (!pipe) {
        return NULL;
    }
    
    // Read command output
    char* result = py_dap_malloc(4096);
    if (!result) {
        pclose(pipe);
        return NULL;
    }
    
    size_t total_read = 0;
    size_t buffer_size = 4096;
    char buffer[256];
    
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        size_t len = strlen(buffer);
        if (total_read + len >= buffer_size - 1) {
            buffer_size *= 2;
            result = py_dap_realloc(result, buffer_size);
            if (!result) {
                pclose(pipe);
                return NULL;
            }
        }
        strcpy(result + total_read, buffer);
        total_read += len;
    }
    
    pclose(pipe);
    result[total_read] = '\0';
    
    return result;
}

// Python wrapper functions

PyObject* py_dap_malloc_wrapper(PyObject* self, PyObject* args) {
    size_t size;
    if (!PyArg_ParseTuple(args, "n", &size)) {
        return NULL;
    }
    
    void* ptr = py_dap_malloc(size);
    if (!ptr) {
        Py_RETURN_NONE;
    }
    
    return PyLong_FromVoidPtr(ptr);
}

PyObject* py_dap_free_wrapper(PyObject* self, PyObject* args) {
    void* ptr;
    
    if (!PyArg_ParseTuple(args, "K", &ptr)) {
        return NULL;
    }
    
    py_dap_free(ptr);
    Py_RETURN_NONE;
}

PyObject* py_dap_calloc_wrapper(PyObject* self, PyObject* args) {
    int num;
    int size;
    
    if (!PyArg_ParseTuple(args, "ii", &num, &size)) {
        return NULL;
    }
    
    void* ptr = py_dap_calloc(num, size);
    return PyLong_FromVoidPtr(ptr);
}

PyObject* py_dap_realloc_wrapper(PyObject* self, PyObject* args) {
    void* ptr;
    size_t size;
    
    if (!PyArg_ParseTuple(args, "Kn", &ptr, &size)) {
        return NULL;
    }
    
    void* new_ptr = py_dap_realloc(ptr, size);
    return PyLong_FromVoidPtr(new_ptr);
}

PyObject* py_exec_with_ret_multistring_wrapper(PyObject* self, PyObject* args) {
    const char* command;
    
    if (!PyArg_ParseTuple(args, "s", &command)) {
        return NULL;
    }
    
    char* result = py_exec_with_ret_multistring(command);
    if (!result) {
        Py_RETURN_NONE;
    }
    
    PyObject* py_result = PyUnicode_FromString(result);
    py_dap_free(result);  // Free the allocated string
    return py_result;
}

// Module method array
static PyMethodDef system_methods[] = {
    // Memory functions
    {"py_dap_malloc", py_dap_malloc_wrapper, METH_VARARGS, "Allocate memory"},
    {"py_dap_free", py_dap_free_wrapper, METH_VARARGS, "Free memory"},
    {"py_dap_calloc", py_dap_calloc_wrapper, METH_VARARGS, "Allocate and clear memory"},
    {"py_dap_realloc", py_dap_realloc_wrapper, METH_VARARGS, "Reallocate memory"},
    {"py_exec_with_ret_multistring", py_exec_with_ret_multistring_wrapper, METH_VARARGS, "Execute command and return multi-string result"},
    
    // Memory aliases without py_ prefix
    {"dap_malloc", py_dap_malloc_wrapper, METH_VARARGS, "Allocate memory (alias)"},
    {"dap_free", py_dap_free_wrapper, METH_VARARGS, "Free memory (alias)"},
    {"dap_calloc", py_dap_calloc_wrapper, METH_VARARGS, "Allocate and clear memory (alias)"},
    {"dap_realloc", py_dap_realloc_wrapper, METH_VARARGS, "Reallocate memory (alias)"},
    {NULL, NULL, 0, NULL}  // Sentinel
};

// Function to get methods array
PyMethodDef* py_dap_system_get_methods(void) {
    return system_methods;
}

// Module initialization function
int py_dap_system_module_init(PyObject* module) {
    // Add system-related constants if needed
    // Currently no constants for system module
    return 0;
} 