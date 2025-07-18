#ifndef PYTHON_DAP_COMMON_H
#define PYTHON_DAP_COMMON_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>

// Common DAP functions
int py_dap_common_init(const char* a_console_title, const char* a_log_file);
void py_dap_common_deinit(void);

// Python wrapper functions
PyObject* py_dap_common_init_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_common_deinit_wrapper(PyObject* self, PyObject* args);

#endif // PYTHON_DAP_COMMON_H 