/*
 * Python DAP Network Coordinator
 * Manages initialization and coordination of DAP SDK network subsystems
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "python_dap.h"
#include "python_dap_network.h"
#include "python_dap_stream.h"
#include "python_dap_http.h"
#include "dap_common.h"
#include "dap_stream.h"
#include "dap_stream_ctl.h"
#include "dap_stream_worker.h"
#include "dap_client.h"
#include "dap_client_http.h"

// External config reference
extern dap_config_t *g_config;

// Network subsystem states
static bool s_network_initialized = false;
static bool s_stream_initialized = false;
static bool s_client_initialized = false;
static bool s_http_initialized = false;

// Network coordinator functions

PyObject* dap_network_init_wrapper(PyObject* self, PyObject* args) {
    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }
    
    if (s_network_initialized) {
        return PyLong_FromLong(0); // Already initialized
    }
    
    int result = 0;
    
    // Initialize stream subsystem
    if (!s_stream_initialized && g_config) {
        result = dap_stream_init(g_config);
        if (result == 0) {
            result = dap_stream_ctl_init();
            if (result == 0) {
                result = dap_stream_worker_init();
                s_stream_initialized = (result == 0);
            }
        }
    }
    
    // Initialize client subsystem
    if (!s_client_initialized && result == 0) {
        result = dap_client_init();
        s_client_initialized = (result == 0);
    }
    
    // Initialize HTTP subsystem
    if (!s_http_initialized && result == 0) {
        result = dap_http_init();
        s_http_initialized = (result == 0);
    }
    
    s_network_initialized = (result == 0);
    return PyLong_FromLong(result);
}

PyObject* dap_network_deinit_wrapper(PyObject* self, PyObject* args) {
    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }
    
    // Deinitialize in reverse order
    if (s_http_initialized) {
        dap_client_http_deinit();
        s_http_initialized = false;
    }
    
    if (s_client_initialized) {
        dap_client_deinit();
        s_client_initialized = false;
    }
    
    if (s_stream_initialized) {
        // Note: dap_stream_worker_deinit() doesn't exist in current DAP SDK
        dap_stream_ctl_deinit();
        dap_stream_deinit();
        s_stream_initialized = false;
    }
    
    s_network_initialized = false;
    return PyLong_FromLong(0);
}

PyObject* dap_network_get_status_wrapper(PyObject* self, PyObject* args) {
    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }
    
    PyObject* status_dict = PyDict_New();
    if (!status_dict) {
        return NULL;
    }
    
    PyDict_SetItemString(status_dict, "network_initialized", 
                        s_network_initialized ? Py_True : Py_False);
    PyDict_SetItemString(status_dict, "stream_initialized", 
                        s_stream_initialized ? Py_True : Py_False);
    PyDict_SetItemString(status_dict, "client_initialized", 
                        s_client_initialized ? Py_True : Py_False);
    PyDict_SetItemString(status_dict, "http_initialized", 
                        s_http_initialized ? Py_True : Py_False);
    
    return status_dict;
}

PyObject* dap_network_reinit_stream_wrapper(PyObject* self, PyObject* args) {
    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }
    
    if (s_stream_initialized) {
        // Note: dap_stream_worker_deinit() doesn't exist in current DAP SDK
        dap_stream_ctl_deinit();
        dap_stream_deinit();
        s_stream_initialized = false;
    }
    
    int result = 0;
    if (g_config) {
        result = dap_stream_init(g_config);
        if (result == 0) {
            result = dap_stream_ctl_init();
            if (result == 0) {
                result = dap_stream_worker_init();
                s_stream_initialized = (result == 0);
            }
        }
    }
    
    return PyLong_FromLong(result);
}

PyObject* dap_network_reinit_http_wrapper(PyObject* self, PyObject* args) {
    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }
    
    if (s_http_initialized) {
        dap_http_deinit();
        s_http_initialized = false;
    }
    
    int result = dap_http_init();
    s_http_initialized = (result == 0);
    
    return PyLong_FromLong(result);
}

PyObject* dap_network_reinit_client_wrapper(PyObject* self, PyObject* args) {
    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }
    
    if (s_client_initialized) {
        dap_client_deinit();
        s_client_initialized = false;
    }
    
    int result = dap_client_init();
    s_client_initialized = (result == 0);
    
    return PyLong_FromLong(result);
}

// Network coordinator method definitions
static PyMethodDef DapNetworkMethods[] = {
    // Network coordination functions
    {"dap_network_init", dap_network_init_wrapper, METH_VARARGS, "Initialize network subsystems"},
    {"dap_network_deinit", dap_network_deinit_wrapper, METH_VARARGS, "Deinitialize network subsystems"},
    {"dap_network_get_status", dap_network_get_status_wrapper, METH_VARARGS, "Get network subsystems status"},
    {"dap_network_reinit_stream", dap_network_reinit_stream_wrapper, METH_VARARGS, "Reinitialize stream subsystem"},
    {"dap_network_reinit_http", dap_network_reinit_http_wrapper, METH_VARARGS, "Reinitialize HTTP subsystem"},
    {"dap_network_reinit_client", dap_network_reinit_client_wrapper, METH_VARARGS, "Reinitialize client subsystem"},
    
    {NULL, NULL, 0, NULL}
};

// Get methods array for module registration
PyMethodDef* py_dap_network_get_methods(void) {
    return DapNetworkMethods;
} 

// Module initialization function
int py_dap_network_module_init(PyObject* module) {
    // Add network subsystem status constants
    PyModule_AddIntConstant(module, "DAP_NETWORK_STATUS_NONE", 0);
    PyModule_AddIntConstant(module, "DAP_NETWORK_STATUS_STREAM", 1);
    PyModule_AddIntConstant(module, "DAP_NETWORK_STATUS_CLIENT", 2);
    PyModule_AddIntConstant(module, "DAP_NETWORK_STATUS_HTTP", 4);
    PyModule_AddIntConstant(module, "DAP_NETWORK_STATUS_ALL", 7);
    
    // Network initialization success codes
    PyModule_AddIntConstant(module, "DAP_NETWORK_INIT_SUCCESS", 0);
    PyModule_AddIntConstant(module, "DAP_NETWORK_INIT_ERROR", -1);
    
    return 0;
} 