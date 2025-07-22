/*
 * Python DAP HTTP Module Implementation
 * HTTP client function wrappers around DAP SDK
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "python_dap.h"
#include "python_dap_http.h"
#include "dap_common.h"
#include "dap_client.h"

// HTTP client management functions
PyObject* dap_http_client_init_wrapper(PyObject* self, PyObject* args) {
    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }
    
    // HTTP client subsystem initialization managed internally
    return PyLong_FromLong(0);
}

PyObject* dap_http_client_deinit_wrapper(PyObject* self, PyObject* args) {
    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }
    
    // DAP client subsystem cleanup happens automatically
    return PyLong_FromLong(0);
}

PyObject* dap_http_client_new_wrapper(PyObject* self, PyObject* args) {
    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }
    
    // Create new DAP client (HTTP client in DAP context)
    dap_client_t* client = dap_client_new(NULL, NULL); // No callbacks for now
    
    if (!client) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create DAP client");
        return NULL;
    }
    
    // Return client handle as void pointer converted to long
    return PyLong_FromVoidPtr(client);
}

PyObject* dap_http_client_delete_wrapper(PyObject* self, PyObject* args) {
    PyObject* client_obj;
    
    if (!PyArg_ParseTuple(args, "O", &client_obj)) {
        return NULL;
    }
    
    // Extract client pointer from PyLong and delete it
    dap_client_t* client = PyLong_AsVoidPtr(client_obj);
    if (!client) {
        PyErr_SetString(PyExc_ValueError, "Invalid client handle");
        return NULL;
    }
    
    // Delete the DAP client
    dap_client_delete(client);
    
    return PyLong_FromLong(0);
}

// Method definitions for Python module
PyMethodDef dap_http_methods[] = {
    {"dap_http_client_init", dap_http_client_init_wrapper, METH_VARARGS, "Initialize HTTP client subsystem"},
    {"dap_http_client_deinit", dap_http_client_deinit_wrapper, METH_VARARGS, "Deinitialize HTTP client subsystem"},
    {"dap_http_client_new", dap_http_client_new_wrapper, METH_VARARGS, "Create new HTTP client"},
    {"dap_http_client_delete", dap_http_client_delete_wrapper, METH_VARARGS, "Delete HTTP client"},
    {NULL, NULL, 0, NULL} // Sentinel
};
