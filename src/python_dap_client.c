/*
 * Python DAP Client Implementation
 * Real bindings to DAP SDK Client functions
 */

#include "python_dap.h"
#include "dap_client.h"
#include "dap_client_pvt.h"
#include <errno.h>

// Client wrapper implementations using REAL DAP SDK functions

void* py_dap_client_new() {
    // Call REAL DAP SDK function
    dap_client_t* l_client = dap_client_new();
    return (void*)l_client;
}

void py_dap_client_delete(void* a_client) {
    if (!a_client) {
        return;
    }
    
    // Call REAL DAP SDK function
    dap_client_delete((dap_client_t*)a_client);
}

int py_dap_client_connect(void* a_client, const char* a_addr, uint16_t a_port) {
    if (!a_client || !a_addr) {
        return -EINVAL;
    }
    
    // Call REAL DAP SDK function
    return dap_client_connect((dap_client_t*)a_client, a_addr, a_port);
}

int py_dap_client_disconnect(void* a_client) {
    if (!a_client) {
        return -EINVAL;
    }
    
    // Call REAL DAP SDK function
    dap_client_disconnect((dap_client_t*)a_client);
    return 0;
}

int py_dap_client_go_stage(void* a_client, dap_client_stage_t a_stage, 
                          dap_client_callback_stage_t a_callback) {
    if (!a_client) {
        return -EINVAL;
    }
    
    // Call REAL DAP SDK function
    return dap_client_go_stage((dap_client_t*)a_client, a_stage, a_callback);
}

int py_dap_client_request(void* a_client, const char* a_path, 
                         const void* a_data, size_t a_data_size,
                         dap_client_callback_data_t a_callback) {
    if (!a_client || !a_path) {
        return -EINVAL;
    }
    
    // Call REAL DAP SDK function
    return dap_client_request((dap_client_t*)a_client, a_path, 
                             (void*)a_data, a_data_size, a_callback, NULL);
}

ssize_t py_dap_client_write(void* a_client, const void* a_data, size_t a_data_size) {
    if (!a_client || !a_data || a_data_size == 0) {
        return -EINVAL;
    }
    
    // Call REAL DAP SDK function
    return dap_client_write((dap_client_t*)a_client, (void*)a_data, a_data_size);
}

ssize_t py_dap_client_read(void* a_client, void* a_data, size_t a_data_size) {
    if (!a_client || !a_data || a_data_size == 0) {
        return -EINVAL;
    }
    
    // Call REAL DAP SDK function
    return dap_client_read((dap_client_t*)a_client, a_data, a_data_size);
}

dap_client_stage_t py_dap_client_get_stage(void* a_client) {
    if (!a_client) {
        return DAP_CLIENT_STAGE_ERROR;
    }
    
    // Call REAL DAP SDK function
    return dap_client_get_stage((dap_client_t*)a_client);
}

int py_dap_client_set_callbacks(void* a_client,
                               dap_client_callback_connected_t a_connected_cb,
                               dap_client_callback_error_t a_error_cb,
                               dap_client_callback_delete_t a_delete_cb) {
    if (!a_client) {
        return -EINVAL;
    }
    
    // Call REAL DAP SDK function
    dap_client_set_callbacks((dap_client_t*)a_client, 
                            a_connected_cb, a_error_cb, a_delete_cb);
    return 0;
}

int py_dap_client_set_auth_cert(void* a_client, void* a_cert) {
    if (!a_client || !a_cert) {
        return -EINVAL;
    }
    
    // Call REAL DAP SDK function
    dap_client_set_auth_cert((dap_client_t*)a_client, (dap_cert_t*)a_cert);
    return 0;
}

void* py_dap_client_get_stream(void* a_client) {
    if (!a_client) {
        return NULL;
    }
    
    // Call REAL DAP SDK function
    dap_stream_t* l_stream = dap_client_get_stream((dap_client_t*)a_client);
    return (void*)l_stream;
}

// Global client management functions
int py_dap_client_init() {
    // Call REAL DAP SDK function
    return dap_client_init();
}

void py_dap_client_deinit() {
    // Call REAL DAP SDK function
    dap_client_deinit();
} 

// Python wrapper functions

PyObject* py_dap_client_new_wrapper(PyObject* self, PyObject* args) {
    // Call REAL implementation
    void* client_handle = py_dap_client_new();
    if (!client_handle) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create DAP client");
        return NULL;
    }
    
    return PyLong_FromVoidPtr(client_handle);
}

PyObject* py_dap_client_delete_wrapper(PyObject* self, PyObject* args) {
    void* client;
    
    if (!PyArg_ParseTuple(args, "K", &client)) {
        return NULL;
    }
    
    // Call REAL implementation
    py_dap_client_delete(client);
    Py_RETURN_NONE;
}

PyObject* py_dap_client_connect_to_wrapper(PyObject* self, PyObject* args) {
    void* client;
    const char* addr;
    int port;
    
    if (!PyArg_ParseTuple(args, "Ksi", &client, &addr, &port)) {
        return NULL;
    }
    
    // Call REAL implementation
    int result = py_dap_client_connect(client, addr, (uint16_t)port);
    return PyLong_FromLong(result);
}

PyObject* py_dap_client_disconnect_wrapper(PyObject* self, PyObject* args) {
    void* client;
    if (!PyArg_ParseTuple(args, "K", &client)) {
        return NULL;
    }
    
    // Call REAL implementation
    int result = py_dap_client_disconnect(client);
    return PyLong_FromLong(result);
}

// Module method array
static PyMethodDef client_methods[] = {
    {"dap_client_new", py_dap_client_new_wrapper, METH_NOARGS, "Create new network client"},
    {"dap_client_delete", py_dap_client_delete_wrapper, METH_VARARGS, "Delete network client"},
    {"dap_client_connect_to", py_dap_client_connect_to_wrapper, METH_VARARGS, "Connect client to address"},
    {"dap_client_disconnect", py_dap_client_disconnect_wrapper, METH_VARARGS, "Disconnect client"},
    {NULL, NULL, 0, NULL}  // Sentinel
};

// Function to get methods array
PyMethodDef* py_dap_client_get_methods(void) {
    return client_methods;
}

// Module initialization function
int py_dap_client_module_init(PyObject* module) {
    // Add client stage constants
    PyModule_AddIntConstant(module, "DAP_CLIENT_STAGE_BEGIN", 0);
    PyModule_AddIntConstant(module, "DAP_CLIENT_STAGE_ENC_INIT", 1);
    PyModule_AddIntConstant(module, "DAP_CLIENT_STAGE_STREAM_CTL", 2);
    PyModule_AddIntConstant(module, "DAP_CLIENT_STAGE_STREAM_SESSION", 3);
    PyModule_AddIntConstant(module, "DAP_CLIENT_STAGE_STREAM_STREAMING", 4);
    PyModule_AddIntConstant(module, "DAP_CLIENT_STAGE_DISCONNECTED", 5);
    PyModule_AddIntConstant(module, "DAP_CLIENT_STAGE_ERROR", 6);
    PyModule_AddIntConstant(module, "DAP_CLIENT_STAGE_ESTABLISHED", 7);
    return 0;
} 