/*
 * Python DAP Network Module Header
 * Network and stream function declarations
 */

#ifndef PYTHON_DAP_NETWORK_H
#define PYTHON_DAP_NETWORK_H

#include <Python.h>

// Stream function wrappers
PyObject* dap_stream_new_wrapper(PyObject* self, PyObject* args);
PyObject* dap_stream_delete_wrapper(PyObject* self, PyObject* args);
PyObject* dap_stream_open_wrapper(PyObject* self, PyObject* args);
PyObject* dap_stream_close_wrapper(PyObject* self, PyObject* args);
PyObject* dap_stream_write_wrapper(PyObject* self, PyObject* args);
PyObject* dap_stream_read_wrapper(PyObject* self, PyObject* args);
PyObject* dap_stream_get_id_wrapper(PyObject* self, PyObject* args);
PyObject* dap_stream_set_callbacks_wrapper(PyObject* self, PyObject* args);
PyObject* dap_stream_get_remote_addr_wrapper(PyObject* self, PyObject* args);
PyObject* dap_stream_get_remote_port_wrapper(PyObject* self, PyObject* args);

// Network initialization functions
PyObject* py_dap_network_init_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_network_deinit_wrapper(PyObject* self, PyObject* args);

// Module interface functions
PyMethodDef* py_dap_network_get_methods(void);
int py_dap_network_module_init(PyObject* module);

#endif // PYTHON_DAP_NETWORK_H 