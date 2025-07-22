/*
 * Python DAP HTTP Module Header  
 * HTTP client function wrappers around DAP SDK
 */

#ifndef PYTHON_DAP_HTTP_H
#define PYTHON_DAP_HTTP_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>

// HTTP client management functions
PyObject* dap_http_client_init_wrapper(PyObject* self, PyObject* args);
PyObject* dap_http_client_deinit_wrapper(PyObject* self, PyObject* args);
PyObject* dap_http_client_new_wrapper(PyObject* self, PyObject* args);
PyObject* dap_http_client_delete_wrapper(PyObject* self, PyObject* args);

// HTTP client request functions
PyObject* dap_http_client_request_wrapper(PyObject* self, PyObject* args);
PyObject* dap_http_client_get_response_code_wrapper(PyObject* self, PyObject* args);
PyObject* dap_http_client_get_response_data_wrapper(PyObject* self, PyObject* args);

// Method definitions array for Python module
extern PyMethodDef dap_http_methods[];

#endif // PYTHON_DAP_HTTP_H
