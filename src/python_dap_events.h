#ifndef PYTHON_DAP_EVENTS_H
#define PYTHON_DAP_EVENTS_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>

// Events management functions
int py_dap_events_init(uint32_t a_workers, uint32_t a_queue_size);
void py_dap_events_deinit(void);
int py_dap_events_start(void);
int py_dap_events_stop(void);

// Python wrapper functions
PyObject* py_dap_events_init_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_events_deinit_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_events_start_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_events_stop_wrapper(PyObject* self, PyObject* args);

#endif // PYTHON_DAP_EVENTS_H 