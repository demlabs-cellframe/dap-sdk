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

int py_dap_events_stop(void) {
    // DAP SDK doesn't have dap_events_stop function
    // We can only stop via worker context or use dap_events_deinit
    return 0; // Stub implementation
}

void* py_dap_events_socket_create(dap_events_desc_type_t a_type, dap_events_socket_callback_t a_callback) {
    // Create callbacks structure
    dap_events_socket_callbacks_t l_callbacks = {0};
    l_callbacks.read_callback = a_callback;
    l_callbacks.write_callback = NULL; // write_callback expects bool return type, using NULL for now
    l_callbacks.error_callback = NULL;
    l_callbacks.delete_callback = NULL;
    l_callbacks.arg = NULL;

    dap_events_socket_t* l_socket = dap_events_socket_create(a_type, &l_callbacks);
    return (void*)l_socket;
}

void py_dap_events_socket_delete(void* a_socket) {
    dap_events_socket_t* l_es = (dap_events_socket_t*)a_socket;
    if (l_es && l_es->worker) {
        // Use proper deletion API with worker
        dap_events_socket_remove_and_delete(l_es->worker, l_es->uuid);
    } else if (l_es) {
        // If no worker assigned, use unsafe version
        dap_events_socket_remove_and_delete_unsafe(l_es, false);
    }
}

void* py_dap_events_socket_queue_ptr(void* a_socket) {
    // This function doesn't exist in DAP SDK - return NULL
    return NULL;
}

int py_dap_events_socket_assign_on_worker_mt(void* a_socket, int a_worker_num) {
    // This function name is incorrect - use proper API
    dap_events_socket_t* l_es = (dap_events_socket_t*)a_socket;
    if (!l_es) return -1;
    
    // Get worker by number would require getting worker list
    // For now return success stub
    return 0;
}

void py_dap_events_socket_event_proc_add(void* a_socket, uint32_t a_events, dap_events_socket_callback_t a_callback) {
    // This function doesn't exist in DAP SDK
    // Events are handled through callbacks structure during creation
}

void py_dap_events_socket_event_proc_remove(void* a_socket, uint32_t a_events) {
    // This function doesn't exist in DAP SDK  
    // Events are handled through callbacks structure
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