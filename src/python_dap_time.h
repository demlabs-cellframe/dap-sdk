#ifndef PYTHON_DAP_TIME_H
#define PYTHON_DAP_TIME_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>

// Time functions
uint64_t py_dap_time_now(void);
char* py_dap_time_to_str_rfc822(uint64_t a_timestamp);

// Python wrapper functions
PyObject* py_dap_time_now_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_time_to_str_rfc822_wrapper(PyObject* self, PyObject* args);

#endif // PYTHON_DAP_TIME_H 