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

// Server type constants
typedef enum {
    DAP_SERVER_TYPE_HTTP = 0,
    DAP_SERVER_TYPE_JSON_RPC = 1,
    DAP_SERVER_TYPE_TCP = 2,
    DAP_SERVER_TYPE_WEBSOCKET = 3
} dap_server_type_t;

// Client stage constants
typedef enum {
    DAP_CLIENT_STAGE_BEGIN = 0,
    DAP_CLIENT_STAGE_ENC_INIT = 1,
    DAP_CLIENT_STAGE_STREAM_CTL = 2,
    DAP_CLIENT_STAGE_STREAM_SESSION = 3,
    DAP_CLIENT_STAGE_STREAM_STREAMING = 4,
    DAP_CLIENT_STAGE_DISCONNECTED = 5,
    DAP_CLIENT_STAGE_ERROR = 6,
    DAP_CLIENT_STAGE_ESTABLISHED = 7
} dap_client_stage_t;

// Module method array functions - each module provides its methods
PyMethodDef* py_dap_common_get_methods(void);
PyMethodDef* py_dap_config_get_methods(void);
PyMethodDef* py_dap_system_get_methods(void);
PyMethodDef* py_dap_logging_get_methods(void);
PyMethodDef* py_dap_time_get_methods(void);
PyMethodDef* py_dap_server_get_methods(void);
PyMethodDef* py_dap_client_get_methods(void);
PyMethodDef* py_dap_events_get_methods(void);

// Module initialization functions - each module contributes to PyInit
int py_dap_common_module_init(PyObject* module);
int py_dap_config_module_init(PyObject* module);
int py_dap_system_module_init(PyObject* module);
int py_dap_logging_module_init(PyObject* module);
int py_dap_time_module_init(PyObject* module);
int py_dap_server_module_init(PyObject* module);
int py_dap_client_module_init(PyObject* module);
int py_dap_events_module_init(PyObject* module);

#endif // PYTHON_DAP_H 