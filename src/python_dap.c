/*
 * Python DAP Extension Module
 * Main module initialization with dynamic method collection from submodules
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdlib.h>
#include <string.h>
#include "python_dap.h"

// Helper function to count methods in a PyMethodDef array
static int count_methods(PyMethodDef* methods) {
    if (!methods) return 0;
    
    int count = 0;
    while (methods[count].ml_name != NULL) {
        count++;
    }
    return count;
}

// Helper function to concatenate method arrays
static PyMethodDef* concatenate_methods(void) {
    // Get method arrays from all modules
    PyMethodDef* common_methods = py_dap_common_get_methods();
    PyMethodDef* config_methods = py_dap_config_get_methods();
    PyMethodDef* system_methods = py_dap_system_get_methods();
    PyMethodDef* logging_methods = py_dap_logging_get_methods();
    PyMethodDef* time_methods = py_dap_time_get_methods();
    PyMethodDef* server_methods = py_dap_server_get_methods();
    PyMethodDef* client_methods = py_dap_client_get_methods();
    PyMethodDef* events_methods = py_dap_events_get_methods();
    
    // Count total methods
    int total_count = 0;
    total_count += count_methods(common_methods);
    total_count += count_methods(config_methods);
    total_count += count_methods(system_methods);
    total_count += count_methods(logging_methods);
    total_count += count_methods(time_methods);
    total_count += count_methods(server_methods);
    total_count += count_methods(client_methods);
    total_count += count_methods(events_methods);
    
    // Allocate memory for concatenated array (+1 for sentinel)
    PyMethodDef* all_methods = (PyMethodDef*)malloc((total_count + 1) * sizeof(PyMethodDef));
    if (!all_methods) {
        return NULL;
    }
    
    // Concatenate all method arrays
    int current_index = 0;
    
    // Helper macro to copy methods
    #define COPY_METHODS(methods) \
        if (methods) { \
            int count = count_methods(methods); \
            memcpy(&all_methods[current_index], methods, count * sizeof(PyMethodDef)); \
            current_index += count; \
        }
    
    COPY_METHODS(common_methods);
    COPY_METHODS(config_methods);
    COPY_METHODS(system_methods);
    COPY_METHODS(logging_methods);
    COPY_METHODS(time_methods);
    COPY_METHODS(server_methods);
    COPY_METHODS(client_methods);
    COPY_METHODS(events_methods);
    
    // Add sentinel
    all_methods[current_index].ml_name = NULL;
    all_methods[current_index].ml_meth = NULL;
    all_methods[current_index].ml_flags = 0;
    all_methods[current_index].ml_doc = NULL;
    
    return all_methods;
}

// Module definition
static struct PyModuleDef python_dap_module = {
    PyModuleDef_HEAD_INIT,
    "python_dap",                                       // Module name
    "Python DAP SDK bindings - MODULAR VERSION",       // Module documentation
    -1,                                                 // Size of per-interpreter state
    NULL                                                // Method table (will be set dynamically)
};

// Module initialization function
PyMODINIT_FUNC PyInit_python_dap(void) {
    // Dynamically build method table
    PyMethodDef* all_methods = concatenate_methods();
    if (!all_methods) {
        PyErr_SetString(PyExc_MemoryError, "Failed to allocate method table");
        return NULL;
    }
    
    // Set method table
    python_dap_module.m_methods = all_methods;
    
    // Create module
    PyObject *module = PyModule_Create(&python_dap_module);
    if (!module) {
        free(all_methods);
        return NULL;
    }

    // Let each module contribute to initialization
    if (py_dap_common_module_init(module) < 0) goto error;
    if (py_dap_config_module_init(module) < 0) goto error;
    if (py_dap_system_module_init(module) < 0) goto error;
    if (py_dap_logging_module_init(module) < 0) goto error;
    if (py_dap_time_module_init(module) < 0) goto error;
    if (py_dap_server_module_init(module) < 0) goto error;
    if (py_dap_client_module_init(module) < 0) goto error;
    if (py_dap_events_module_init(module) < 0) goto error;
    
    return module;

error:
    free(all_methods);
    Py_DECREF(module);
    return NULL;
} 