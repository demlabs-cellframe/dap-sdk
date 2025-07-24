/*
 * Python DAP SDK Bindings - Main Header
 * Includes all specialized module headers
 */

#ifndef PYTHON_DAP_H
#define PYTHON_DAP_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdint.h>
#include <stdbool.h>

// Include all module headers
#include "python_dap_common.h"
#include "python_dap_config.h"
#include "python_dap_system.h"
#include "python_dap_logging.h"
#include "python_dap_time.h"
#include "python_dap_server.h"
#include "python_dap_client.h"
#include "python_dap_events.h"
#include "python_dap_network.h"

// Module method array functions - each module provides its methods
PyMethodDef* py_dap_common_get_methods(void);
PyMethodDef* py_dap_config_get_methods(void);
PyMethodDef* py_dap_system_get_methods(void);
PyMethodDef* py_dap_logging_get_methods(void);
PyMethodDef* py_dap_time_get_methods(void);
PyMethodDef* py_dap_server_get_methods(void);
PyMethodDef* py_dap_client_get_methods(void);
PyMethodDef* py_dap_events_get_methods(void);
PyMethodDef* py_dap_network_get_methods(void);
PyMethodDef* py_dap_plugin_get_methods(void);

// Module initialization functions - each module contributes to PyInit
int py_dap_common_module_init(PyObject* module);
int py_dap_config_module_init(PyObject* module);
int py_dap_system_module_init(PyObject* module);
int py_dap_logging_module_init(PyObject* module);
int py_dap_time_module_init(PyObject* module);
int py_dap_server_module_init(PyObject* module);
int py_dap_client_module_init(PyObject* module);
int py_dap_server_module_init(PyObject* module);
int py_dap_events_module_init(PyObject* module);
int py_dap_network_module_init(PyObject* module);
int py_dap_stream_module_init(PyObject* module);
int py_dap_http_module_init(PyObject* module);

// Function declarations for getting methods arrays
PyMethodDef* py_dap_stream_get_methods(void);
PyMethodDef* py_dap_http_get_methods(void);

#endif // PYTHON_DAP_H 