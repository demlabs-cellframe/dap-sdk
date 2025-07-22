#ifndef PYTHON_DAP_EVENTS_H
#define PYTHON_DAP_EVENTS_H

#include "Python.h"
#include "dap_events.h"
#include "dap_events_socket.h"

// Core events functions
int py_dap_events_init(uint32_t a_worker_threads_count, uint32_t a_connections_max);
void py_dap_events_deinit(void);
int py_dap_events_start(void);
int py_dap_events_stop(void);

// Socket management functions - ИСПРАВЛЕНЫ ТИПЫ
void* py_dap_events_socket_create(dap_events_desc_type_t a_type, dap_events_socket_callback_t a_callback);
void py_dap_events_socket_delete(void* a_socket);
void* py_dap_events_socket_queue_ptr(void* a_socket);
int py_dap_events_socket_assign_on_worker_mt(void* a_socket, int a_worker_num);
void py_dap_events_socket_event_proc_add(void* a_socket, uint32_t a_events, dap_events_socket_callback_t a_callback);
void py_dap_events_socket_event_proc_remove(void* a_socket, uint32_t a_events);

// Python wrapper functions
PyObject* py_dap_events_init_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_events_deinit_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_events_start_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_events_stop_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_events_socket_create_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_events_socket_delete_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_events_socket_queue_ptr_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_events_socket_assign_on_worker_mt_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_events_socket_event_proc_add_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_events_socket_event_proc_remove_wrapper(PyObject* self, PyObject* args);

// Module functions
PyMethodDef* py_dap_events_get_methods(void);
int py_dap_events_module_init(PyObject* module);

#endif // PYTHON_DAP_EVENTS_H 