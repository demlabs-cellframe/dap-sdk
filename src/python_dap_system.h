#ifndef PYTHON_DAP_SYSTEM_H
#define PYTHON_DAP_SYSTEM_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>

// Memory management functions
void* py_dap_malloc(size_t a_size);
void py_dap_free(void* a_ptr);
void* py_dap_realloc(void* a_ptr, size_t a_size);
char* py_exec_with_ret_multistring(const char* a_command);

// Python wrapper functions
PyObject* py_dap_malloc_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_free_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_calloc_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_realloc_wrapper(PyObject* self, PyObject* args);
PyObject* py_exec_with_ret_multistring_wrapper(PyObject* self, PyObject* args);

#endif // PYTHON_DAP_SYSTEM_H 