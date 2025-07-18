#ifndef PYTHON_DAP_CLIENT_H
#define PYTHON_DAP_CLIENT_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>

// Client management functions
void* py_dap_client_new(void);
void py_dap_client_delete(void* a_client);
int py_dap_client_connect(void* a_client, const char* a_addr, uint16_t a_port);
int py_dap_client_disconnect(void* a_client);

// Python wrapper functions
PyObject* py_dap_client_new_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_client_delete_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_client_connect_to_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_client_disconnect_wrapper(PyObject* self, PyObject* args);

#endif // PYTHON_DAP_CLIENT_H 