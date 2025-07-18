/*
 * Python DAP Events Implementation
 * Real bindings to DAP SDK Events functions
 */

#include "python_dap.h"
#include "dap_events.h"
#include "dap_events_socket.h"
#include <errno.h>

// Events wrapper implementations using REAL DAP SDK functions

int py_dap_events_init(uint32_t a_worker_threads_count, uint32_t a_connections_max) {
    // Call REAL DAP SDK function
    return dap_events_init(a_worker_threads_count, a_connections_max);
}

void py_dap_events_deinit() {
    // Call REAL DAP SDK function
    dap_events_deinit();
}

int py_dap_events_start() {
    // Call REAL DAP SDK function
    return dap_events_start();
}

int py_dap_events_stop() {
    // Call REAL DAP SDK function
    return dap_events_stop();
}

void* py_dap_events_socket_create(dap_events_desc_type_t a_type, 
                                 dap_events_socket_callback_t a_callback) {
    // Call REAL DAP SDK function
    dap_events_socket_t* l_socket = dap_events_socket_create(a_type, a_callback);
    return (void*)l_socket;
}

void py_dap_events_socket_delete(void* a_socket) {
    if (!a_socket) {
        return;
    }
    
    // Call REAL DAP SDK function
    dap_events_socket_delete((dap_events_socket_t*)a_socket);
}

void* py_dap_events_socket_queue_ptr(void* a_socket) {
    if (!a_socket) {
        return NULL;
    }
    
    // Call REAL DAP SDK function
    return dap_events_socket_queue_ptr((dap_events_socket_t*)a_socket);
}

int py_dap_events_socket_assign_on_worker_mt(void* a_socket, uint32_t a_worker_num) {
    if (!a_socket) {
        return -EINVAL;
    }
    
    // Call REAL DAP SDK function
    return dap_events_socket_assign_on_worker_mt((dap_events_socket_t*)a_socket, a_worker_num);
}

int py_dap_events_socket_event_proc_add(void* a_socket, uint32_t a_events, 
                                       dap_events_socket_callback_event_t a_callback) {
    if (!a_socket || !a_callback) {
        return -EINVAL;
    }
    
    // Call REAL DAP SDK function
    dap_events_socket_event_proc_add((dap_events_socket_t*)a_socket, a_events, a_callback);
    return 0;
}

int py_dap_events_socket_event_proc_remove(void* a_socket, uint32_t a_events) {
    if (!a_socket) {
        return -EINVAL;
    }
    
    // Call REAL DAP SDK function
    dap_events_socket_event_proc_remove((dap_events_socket_t*)a_socket, a_events);
    return 0;
} 

// Python wrapper functions

PyObject* py_dap_events_init_wrapper(PyObject* self, PyObject* args) {
    int workers;
    int queue_size;
    
    if (!PyArg_ParseTuple(args, "ii", &workers, &queue_size)) {
        return NULL;
    }
    
    // Call REAL implementation
    int result = py_dap_events_init((uint32_t)workers, (uint32_t)queue_size);
    return PyLong_FromLong(result);
}

PyObject* py_dap_events_deinit_wrapper(PyObject* self, PyObject* args) {
    // Call REAL implementation
    py_dap_events_deinit();
    Py_RETURN_NONE;
}

PyObject* py_dap_events_start_wrapper(PyObject* self, PyObject* args) {
    // Call REAL implementation
    int result = py_dap_events_start();
    return PyLong_FromLong(result);
}

PyObject* py_dap_events_stop_wrapper(PyObject* self, PyObject* args) {
    // Call REAL implementation
    int result = py_dap_events_stop();
    return PyLong_FromLong(result);
}

// Module method array
static PyMethodDef events_methods[] = {
    {"dap_events_init", py_dap_events_init_wrapper, METH_VARARGS, "Initialize DAP events"},
    {"dap_events_deinit", py_dap_events_deinit_wrapper, METH_NOARGS, "Deinitialize DAP events"},
    {"dap_events_start", py_dap_events_start_wrapper, METH_NOARGS, "Start DAP events"},
    {"dap_events_stop", py_dap_events_stop_wrapper, METH_NOARGS, "Stop DAP events"},
    {NULL, NULL, 0, NULL}  // Sentinel
};

// Function to get methods array
PyMethodDef* py_dap_events_get_methods(void) {
    return events_methods;
}

// Module initialization function
int py_dap_events_module_init(PyObject* module) {
    // Add events-related constants if needed
    // Currently no constants for events module
    return 0;
} 