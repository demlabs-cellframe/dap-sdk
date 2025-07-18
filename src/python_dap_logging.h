#ifndef PYTHON_DAP_LOGGING_H
#define PYTHON_DAP_LOGGING_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>

// Logging functions
void py_dap_log_level_set(int a_level);
int py_dap_log_level_get(void);
void py_dap_log_set_external_output(int a_output_type, void* a_callback);

// Python wrapper functions
PyObject* py_dap_log_level_set_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_log_level_get_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_log_set_external_output_wrapper(PyObject* self, PyObject* args);

#endif // PYTHON_DAP_LOGGING_H 