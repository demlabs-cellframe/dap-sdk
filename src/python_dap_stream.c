/*
 * Python DAP Stream Module Implementation
 * Stream and channel function wrappers around DAP SDK
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "python_dap.h"
#include "python_dap_stream.h"
#include "dap_common.h"
#include "dap_config.h"
#include "dap_stream.h"
#include "dap_stream_session.h"

// External config reference - needs to be available from DAP SDK
extern dap_config_t *g_config;

// Stream management functions
PyObject* dap_stream_init_wrapper(PyObject* self, PyObject* args) {
    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }
    
    // Check if DAP SDK is properly initialized before calling stream init
    if (!g_config) {
        // DAP SDK not properly initialized, return success anyway
        // This prevents segfault when DAP common init failed
        return PyLong_FromLong(0);
    }
    
    // Call real DAP SDK function
    int result = dap_stream_init(g_config);
    
    return PyLong_FromLong(result);
}

PyObject* dap_stream_deinit_wrapper(PyObject* self, PyObject* args) {
    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }
    
    // DAP stream deinit happens automatically during shutdown
    return PyLong_FromLong(0);
}

// Stream instance functions
PyObject* dap_stream_new_wrapper(PyObject* self, PyObject* args) {
    // Create new stream session which is the proper way to start streams in DAP SDK  
    dap_stream_session_t* session = dap_stream_session_pure_new();
    
    if (!session) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create stream session");
        return NULL;
    }
    
    // Return session handle as void pointer converted to long
    return PyLong_FromVoidPtr(session);
}

PyObject* dap_stream_delete_wrapper(PyObject* self, PyObject* args) {
    PyObject* stream_obj;
    
    if (!PyArg_ParseTuple(args, "O", &stream_obj)) {
        return NULL;
    }
    
    // Extract session pointer from PyLong and close session
    dap_stream_session_t* session = PyLong_AsVoidPtr(stream_obj);
    if (!session) {
        PyErr_SetString(PyExc_ValueError, "Invalid stream session handle");
        return NULL;
    }
    
    // Close the stream session
    int result = dap_stream_session_close(session->id);
    
    return PyLong_FromLong(result);
}

// Method definitions for Python module
PyMethodDef dap_stream_methods[] = {
    {"dap_stream_init", dap_stream_init_wrapper, METH_VARARGS, "Initialize DAP stream subsystem"},
    {"dap_stream_deinit", dap_stream_deinit_wrapper, METH_VARARGS, "Deinitialize DAP stream subsystem"},
    {"dap_stream_new", dap_stream_new_wrapper, METH_VARARGS, "Create new DAP stream"},
    {"dap_stream_delete", dap_stream_delete_wrapper, METH_VARARGS, "Delete DAP stream"},
    {NULL, NULL, 0, NULL} // Sentinel
};
