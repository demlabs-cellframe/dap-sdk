/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Python DAP SDK https://gitlab.demlabs.net/dap/python-dap
 * Copyright  (c) 2025
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

// Platform specific includes first
#ifdef __linux__
#define DAP_OS_LINUX
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#include "python_dap_server.h"
#include "dap_common.h"
#include "dap_server.h"
#include "dap_events_socket.h"

#undef LOG_TAG
#define LOG_TAG "python_dap_server"

// Server wrapper implementations using DAP SDK functions

void* py_dap_server_new(const char* a_cfg_section) {
    dap_return_val_if_fail(a_cfg_section, NULL);
    
    // Create new server with null callbacks - to be set later
    dap_server_t* l_server = dap_server_new(a_cfg_section, NULL, NULL);
    if (!l_server) {
        log_it(L_ERROR, "Failed to create DAP server for section: %s", a_cfg_section);
        return NULL;
    }
    
    log_it(L_DEBUG, "Created DAP server for section: %s", a_cfg_section);
    return (void*)l_server;
}

void py_dap_server_delete(void* a_server) {
    dap_return_if_fail(a_server);
    
    dap_server_t* l_server = (dap_server_t*)a_server;
    dap_server_delete(l_server);
    log_it(L_DEBUG, "Deleted DAP server");
}

int py_dap_server_listen_addr_add(void* a_server, const char* a_addr, uint16_t a_port) {
    dap_return_val_if_fail(a_server && a_addr, -1);
    
    dap_server_t* l_server = (dap_server_t*)a_server;
    
    // Add listening address with TCP client type and null callbacks
    int l_result = dap_server_listen_addr_add(l_server, a_addr, a_port, 
                                              DESCRIPTOR_TYPE_SOCKET_CLIENT, NULL);
    if (l_result != 0) {
        log_it(L_ERROR, "Failed to add listen address %s:%u", a_addr, a_port);
        return -1;
    }
    
    log_it(L_DEBUG, "Added listen address %s:%u", a_addr, a_port);
    return 0;
}

int py_dap_server_start(void* a_server) {
    dap_return_val_if_fail(a_server, -1);
    
    // DAP server starts automatically after listen_addr_add
    // This is a placeholder for any additional start logic
    log_it(L_DEBUG, "DAP server started");
    return 0;
}

int py_dap_server_stop(void* a_server) {
    dap_return_val_if_fail(a_server, -1);
    
    // For stopping we would need to implement server shutdown
    // This is a placeholder for future implementation
    log_it(L_DEBUG, "DAP server stop requested");
    return 0;
}

int py_dap_server_init(void) {
    int l_result = dap_server_init();
    if (l_result != 0) {
        log_it(L_ERROR, "Failed to initialize DAP server subsystem");
        return -1;
    }
    
    log_it(L_DEBUG, "DAP server subsystem initialized");
    return 0;
}

void py_dap_server_deinit(void) {
    dap_server_deinit();
    log_it(L_DEBUG, "DAP server subsystem deinitialized");
}

void** py_dap_server_get_all(size_t* a_count) {
    dap_return_val_if_fail(a_count, NULL);
    
    // DAP SDK doesn't have a direct function to get all servers
    // This would need to be implemented using internal structures
    *a_count = 0;
    log_it(L_DEBUG, "Get all servers not implemented in DAP SDK");
    return NULL;
}

// Python wrapper functions

PyObject* py_dap_server_new_wrapper(PyObject* self, PyObject* args) {
    const char* l_cfg_section;
    
    if (!PyArg_ParseTuple(args, "s", &l_cfg_section)) {
        return NULL;
    }
    
    void* l_server = py_dap_server_new(l_cfg_section);
    if (!l_server) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create DAP server");
        return NULL;
    }
    
    return PyLong_FromVoidPtr(l_server);
}

PyObject* py_dap_server_delete_wrapper(PyObject* self, PyObject* args) {
    void* l_server;
    
    if (!PyArg_ParseTuple(args, "k", &l_server)) {
        return NULL;
    }
    
    py_dap_server_delete(l_server);
    Py_RETURN_NONE;
}

PyObject* py_dap_server_listen_wrapper(PyObject* self, PyObject* args) {
    void* l_server;
    const char* l_addr;
    unsigned int l_port;
    
    if (!PyArg_ParseTuple(args, "ksI", &l_server, &l_addr, &l_port)) {
        return NULL;
    }
    
    int l_result = py_dap_server_listen_addr_add(l_server, l_addr, (uint16_t)l_port);
    if (l_result != 0) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to add listen address");
        return NULL;
    }
    
    return PyLong_FromLong(l_result);
}

PyObject* py_dap_server_start_wrapper(PyObject* self, PyObject* args) {
    void* l_server;
    
    if (!PyArg_ParseTuple(args, "k", &l_server)) {
        return NULL;
    }
    
    int l_result = py_dap_server_start(l_server);
    return PyLong_FromLong(l_result);
}

PyObject* py_dap_server_stop_wrapper(PyObject* self, PyObject* args) {
    void* l_server;
    
    if (!PyArg_ParseTuple(args, "k", &l_server)) {
        return NULL;
    }
    
    int l_result = py_dap_server_stop(l_server);
    return PyLong_FromLong(l_result);
}

PyObject* py_dap_server_init_wrapper(PyObject* self, PyObject* args) {
    int l_result = py_dap_server_init();
    if (l_result != 0) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to initialize DAP server");
        return NULL;
    }
    
    return PyLong_FromLong(l_result);
}

PyObject* py_dap_server_deinit_wrapper(PyObject* self, PyObject* args) {
    py_dap_server_deinit();
    Py_RETURN_NONE;
}

PyObject* py_dap_server_get_all_wrapper(PyObject* self, PyObject* args) {
    size_t l_count = 0;
    void** l_servers = py_dap_server_get_all(&l_count);
    
    PyObject* l_list = PyList_New(l_count);
    if (!l_list) {
        return NULL;
    }
    
    for (size_t i = 0; i < l_count; i++) {
        PyObject* l_server_ptr = PyLong_FromVoidPtr(l_servers[i]);
        if (!l_server_ptr) {
            Py_DECREF(l_list);
            return NULL;
        }
        PyList_SetItem(l_list, i, l_server_ptr);
    }
    
    return l_list;
}

// Python method definitions
static PyMethodDef s_python_dap_server_methods[] = {
    {"server_new", py_dap_server_new_wrapper, METH_VARARGS, "Create new DAP server"},
    {"server_delete", py_dap_server_delete_wrapper, METH_VARARGS, "Delete DAP server"},
    {"server_listen", py_dap_server_listen_wrapper, METH_VARARGS, "Add listen address to server"},
    {"server_start", py_dap_server_start_wrapper, METH_VARARGS, "Start DAP server"},
    {"server_stop", py_dap_server_stop_wrapper, METH_VARARGS, "Stop DAP server"},
    {"server_init", py_dap_server_init_wrapper, METH_VARARGS, "Initialize DAP server subsystem"},
    {"server_deinit", py_dap_server_deinit_wrapper, METH_VARARGS, "Deinitialize DAP server subsystem"},
    {"server_get_all", py_dap_server_get_all_wrapper, METH_VARARGS, "Get all DAP servers"},
    
    // DAP prefixed aliases
    {"dap_server_new", py_dap_server_new_wrapper, METH_VARARGS, "Create new DAP server (alias)"},
    {"dap_server_delete", py_dap_server_delete_wrapper, METH_VARARGS, "Delete DAP server (alias)"},
    {"dap_server_listen", py_dap_server_listen_wrapper, METH_VARARGS, "Add listen address to server (alias)"},
    {"dap_server_start", py_dap_server_start_wrapper, METH_VARARGS, "Start DAP server (alias)"},
    {"dap_server_stop", py_dap_server_stop_wrapper, METH_VARARGS, "Stop DAP server (alias)"},
    {NULL, NULL, 0, NULL}
};

PyMethodDef* py_dap_server_get_methods(void) {
    return s_python_dap_server_methods;
}

// Server module initialization
int py_dap_server_module_init(PyObject* module) {
    // Add server type constants
    PyModule_AddIntConstant(module, "DAP_SERVER_TYPE_HTTP", 0);
    PyModule_AddIntConstant(module, "DAP_SERVER_TYPE_JSON_RPC", 1);
    PyModule_AddIntConstant(module, "DAP_SERVER_TYPE_TCP", 2);
    PyModule_AddIntConstant(module, "DAP_SERVER_TYPE_WEBSOCKET", 3);
    return 0;
}
