/*
 * Python DAP Stream Module Header
 * Stream and channel function wrappers around DAP SDK
 */

#ifndef PYTHON_DAP_STREAM_H
#define PYTHON_DAP_STREAM_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>

// Stream management functions
PyObject* dap_stream_init_wrapper(PyObject* self, PyObject* args);
PyObject* dap_stream_deinit_wrapper(PyObject* self, PyObject* args);

// Stream instance functions  
PyObject* dap_stream_new_wrapper(PyObject* self, PyObject* args);
PyObject* dap_stream_delete_wrapper(PyObject* self, PyObject* args);
PyObject* dap_stream_open_wrapper(PyObject* self, PyObject* args);
PyObject* dap_stream_close_wrapper(PyObject* self, PyObject* args);
PyObject* dap_stream_write_wrapper(PyObject* self, PyObject* args);
PyObject* dap_stream_read_wrapper(PyObject* self, PyObject* args);
PyObject* dap_stream_get_id_wrapper(PyObject* self, PyObject* args);
PyObject* dap_stream_set_callback_wrapper(PyObject* self, PyObject* args);
PyObject* dap_stream_get_remote_addr_wrapper(PyObject* self, PyObject* args);
PyObject* dap_stream_get_remote_port_wrapper(PyObject* self, PyObject* args);

// Method definitions array for Python module
extern PyMethodDef dap_stream_methods[];

#endif // PYTHON_DAP_STREAM_H
