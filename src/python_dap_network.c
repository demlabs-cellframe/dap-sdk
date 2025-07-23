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
#include "dap_http.h"

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
    
    // Deinitialize subsystems in reverse order
    if (s_http_initialized) {
        dap_http_deinit();
        s_http_initialized = false;
    }
    
    if (s_client_initialized) {
        dap_client_deinit();
        s_client_initialized = false;
    }
    
    if (s_stream_initialized) {
        dap_stream_worker_deinit();
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
        dap_stream_worker_deinit();
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

// Method definitions - combine stream and HTTP module methods
static PyMethodDef network_methods[] = {
    // Network coordination
    {"dap_network_init", dap_network_init_wrapper, METH_VARARGS, "Initialize network subsystems"},
    {"dap_network_deinit", dap_network_deinit_wrapper, METH_VARARGS, "Deinitialize network subsystems"},
    {"dap_network_get_status", dap_network_get_status_wrapper, METH_VARARGS, "Get network subsystems status"},
    {"dap_network_reinit_stream", dap_network_reinit_stream_wrapper, METH_VARARGS, "Reinitialize stream subsystem"},
    {"dap_network_reinit_http", dap_network_reinit_http_wrapper, METH_VARARGS, "Reinitialize HTTP subsystem"},
    {"dap_network_reinit_client", dap_network_reinit_client_wrapper, METH_VARARGS, "Reinitialize client subsystem"},
    
    // Stream module methods (from python_dap_stream.h)
    {"dap_stream_session_new", dap_stream_session_new_wrapper, METH_VARARGS, "Create new stream session"},
    {"dap_stream_session_open", dap_stream_session_open_wrapper, METH_VARARGS, "Open stream session"},
    {"dap_stream_session_close", dap_stream_session_close_wrapper, METH_VARARGS, "Close stream session"},
    {"dap_stream_ch_new", dap_stream_ch_new_wrapper, METH_VARARGS, "Create new stream channel"},
    {"dap_stream_ch_delete", dap_stream_ch_delete_wrapper, METH_VARARGS, "Delete stream channel"},
    {"dap_stream_ch_pkt_write", dap_stream_ch_pkt_write_wrapper, METH_VARARGS, "Write packet to stream channel"},
    {"dap_stream_ch_pkt_send", dap_stream_ch_pkt_send_wrapper, METH_VARARGS, "Send packet over stream channel"},
    
    // HTTP module methods (from python_dap_http.h)
    {"dap_http_client_init", dap_http_client_init_wrapper, METH_VARARGS, "Initialize HTTP client"},
    {"dap_http_client_deinit", dap_http_client_deinit_wrapper, METH_VARARGS, "Deinitialize HTTP client"},
    {"dap_http_client_new", dap_http_client_new_wrapper, METH_VARARGS, "Create new HTTP client"},
    {"dap_http_client_delete", dap_http_client_delete_wrapper, METH_VARARGS, "Delete HTTP client"},
    {"dap_http_client_request", dap_http_client_request_wrapper, METH_VARARGS, "Make HTTP request"},
    {"dap_http_simple_request", dap_http_simple_request_wrapper, METH_VARARGS, "Make simple HTTP request"},
    {"dap_http_client_get_response_code", dap_http_client_get_response_code_wrapper, METH_VARARGS, "Get HTTP response code"},
    {"dap_http_client_get_response_data", dap_http_client_get_response_data_wrapper, METH_VARARGS, "Get HTTP response data"},
    
    {NULL, NULL, 0, NULL}
};

// Get methods array for module registration
PyMethodDef* get_network_methods(void) {
    return network_methods;
} 