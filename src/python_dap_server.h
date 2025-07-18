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

#pragma once

// Platform defines first
#ifdef __linux__
#define DAP_OS_LINUX
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#endif

#include <Python.h>
#include "dap_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations only - do not include full DAP SDK headers in header file
struct dap_server;
typedef struct dap_server dap_server_t;

// Server management functions
void* py_dap_server_new(const char* a_cfg_section);
void py_dap_server_delete(void* a_server);
int py_dap_server_listen_addr_add(void* a_server, const char* a_addr, uint16_t a_port);
int py_dap_server_start(void* a_server);
int py_dap_server_stop(void* a_server);
int py_dap_server_init(void);
void py_dap_server_deinit(void);
void** py_dap_server_get_all(size_t* a_count);

// Python wrapper functions
PyObject* py_dap_server_new_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_server_delete_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_server_listen_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_server_start_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_server_stop_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_server_init_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_server_deinit_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_server_get_all_wrapper(PyObject* self, PyObject* args);

// Module methods for Python integration
PyMethodDef* py_dap_server_get_methods(void);

#ifdef __cplusplus
}
#endif
