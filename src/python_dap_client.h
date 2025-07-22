#ifndef PYTHON_DAP_CLIENT_H
#define PYTHON_DAP_CLIENT_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>

// Client management functions
void* py_dap_client_new(void);
void py_dap_client_delete(void* a_client);
int py_dap_client_connect(void* a_client, const char* a_addr, uint16_t a_port);
int py_dap_client_disconnect(void* a_client);

// Python wrapper functions - core
PyObject* py_dap_client_new_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_client_delete_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_client_connect_to_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_client_disconnect_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_client_go_stage_wrapper(PyObject* self, PyObject* args);

// Python wrapper functions - additional
PyObject* py_dap_client_request_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_client_write_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_client_read_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_client_get_stage_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_client_set_callbacks_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_client_set_auth_cert_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_client_get_stream_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_client_get_uplink_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_client_stage_next_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_client_stage_transaction_begin_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_client_stage_transaction_end_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_client_get_stage_str_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_client_init_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_client_deinit_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_client_get_all_wrapper(PyObject* self, PyObject* args);

// Module interface functions
PyMethodDef* py_dap_client_get_methods(void);
int py_dap_client_module_init(PyObject* module);

#endif // PYTHON_DAP_CLIENT_H 