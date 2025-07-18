#ifndef PYTHON_DAP_SERVER_H
#define PYTHON_DAP_SERVER_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>

// Server management functions
void* py_dap_server_new(const char* a_name, int a_type);
void py_dap_server_delete(void* a_server);
int py_dap_server_listen(void* a_server, const char* a_addr, uint16_t a_port);
int py_dap_server_start(void* a_server);
int py_dap_server_stop(void* a_server);

// Python wrapper functions
PyObject* py_dap_server_new_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_server_delete_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_server_listen_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_server_start_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_server_stop_wrapper(PyObject* self, PyObject* args);

#endif // PYTHON_DAP_SERVER_H 