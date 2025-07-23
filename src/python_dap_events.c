/*
 * Python DAP Events Implementation
 * Real bindings to DAP SDK Events functions
 */

#include "python_dap.h"
#include "dap_events.h"
#include "dap_events_socket.h"
#include "dap_worker.h"
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
    // Use DAP SDK function to stop all events
    dap_events_stop_all();
    return 0;
}

void* py_dap_events_socket_create(dap_events_desc_type_t a_type, dap_events_socket_callback_t a_callback) {
    // Create callbacks structure
    dap_events_socket_callbacks_t l_callbacks = {0};
    l_callbacks.read_callback = a_callback;
    l_callbacks.write_callback = NULL; 
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
    dap_events_socket_t* l_es = (dap_events_socket_t*)a_socket;
    if (!l_es) return NULL;
    
    // Return pointer to the queue or socket itself for queue operations
    // This is used for queue_ptr_send operations
    return (void*)l_es;
}

int py_dap_events_socket_assign_on_worker_mt(void* a_socket, int a_worker_num) {
    dap_events_socket_t* l_es = (dap_events_socket_t*)a_socket;
    if (!l_es) return -1;
    
    // Get worker by index
    dap_worker_t* l_worker = dap_events_worker_get((uint8_t)a_worker_num);
    if (!l_worker) return -1;
    
    // Assign socket to worker
    dap_events_socket_assign_on_worker(l_es, l_worker);
    return 0;
}

void py_dap_events_socket_event_proc_add(void* a_socket, uint32_t a_events, dap_events_socket_callback_t a_callback) {
    dap_events_socket_t* l_es = (dap_events_socket_t*)a_socket;
    if (!l_es) return;
    
    // Set appropriate callback based on event type
    // This is a simplified implementation - in real DAP SDK events are set during socket creation
    if (a_events & POLLIN) {
        l_es->callbacks.read_callback = a_callback;
    }
    // Add other event types as needed
}

void py_dap_events_socket_event_proc_remove(void* a_socket, uint32_t a_events) {
    dap_events_socket_t* l_es = (dap_events_socket_t*)a_socket;
    if (!l_es) return;
    
    // Remove callbacks based on event type
    if (a_events & POLLIN) {
        l_es->callbacks.read_callback = NULL;
    }
    // Remove other event types as needed
}

// Python wrapper functions

PyObject* py_dap_events_init_wrapper(PyObject* self, PyObject* args) {
    int workers;
    int queue_size;
    
    if (!PyArg_ParseTuple(args, "ii", &workers, &queue_size)) {
        return NULL;
    }
    
    int result = py_dap_events_init((uint32_t)workers, (uint32_t)queue_size);
    return PyLong_FromLong(result);
}

PyObject* py_dap_events_deinit_wrapper(PyObject* self, PyObject* args) {
    py_dap_events_deinit();
    Py_RETURN_NONE;
}

PyObject* py_dap_events_start_wrapper(PyObject* self, PyObject* args) {
    int result = py_dap_events_start();
    return PyLong_FromLong(result);
}

PyObject* py_dap_events_stop_wrapper(PyObject* self, PyObject* args) {
    int result = py_dap_events_stop();
    return PyLong_FromLong(result);
}

PyObject* py_dap_events_socket_create_wrapper(PyObject* self, PyObject* args) {
    int type;
    PyObject* callback_obj = NULL;
    
    if (!PyArg_ParseTuple(args, "i|O", &type, &callback_obj)) {
        return NULL;
    }
    
    // For now, create socket without callback (can be set later)
    void* socket = py_dap_events_socket_create((dap_events_desc_type_t)type, NULL);
    
    return PyLong_FromVoidPtr(socket);
}

PyObject* py_dap_events_socket_delete_wrapper(PyObject* self, PyObject* args) {
    PyObject* socket_obj;
    
    if (!PyArg_ParseTuple(args, "O", &socket_obj)) {
        return NULL;
    }
    
    void* socket = PyLong_AsVoidPtr(socket_obj);
    if (!socket) {
        PyErr_SetString(PyExc_ValueError, "Invalid socket pointer");
        return NULL;
    }
    
    py_dap_events_socket_delete(socket);
    Py_RETURN_NONE;
}

PyObject* py_dap_events_socket_queue_ptr_wrapper(PyObject* self, PyObject* args) {
    PyObject* socket_obj;
    
    if (!PyArg_ParseTuple(args, "O", &socket_obj)) {
        return NULL;
    }
    
    void* socket = PyLong_AsVoidPtr(socket_obj);
    if (!socket) {
        PyErr_SetString(PyExc_ValueError, "Invalid socket pointer");
        return NULL;
    }
    
    void* queue_ptr = py_dap_events_socket_queue_ptr(socket);
    return PyLong_FromVoidPtr(queue_ptr);
}

PyObject* py_dap_events_socket_assign_on_worker_mt_wrapper(PyObject* self, PyObject* args) {
    PyObject* socket_obj;
    int worker_num;
    
    if (!PyArg_ParseTuple(args, "Oi", &socket_obj, &worker_num)) {
        return NULL;
    }
    
    void* socket = PyLong_AsVoidPtr(socket_obj);
    if (!socket) {
        PyErr_SetString(PyExc_ValueError, "Invalid socket pointer");
        return NULL;
    }
    
    int result = py_dap_events_socket_assign_on_worker_mt(socket, worker_num);
    return PyLong_FromLong(result);
}

PyObject* py_dap_events_socket_event_proc_add_wrapper(PyObject* self, PyObject* args) {
    PyObject* socket_obj;
    int events;
    PyObject* callback_obj = NULL;
    
    if (!PyArg_ParseTuple(args, "Oi|O", &socket_obj, &events, &callback_obj)) {
        return NULL;
    }
    
    void* socket = PyLong_AsVoidPtr(socket_obj);
    if (!socket) {
        PyErr_SetString(PyExc_ValueError, "Invalid socket pointer");
        return NULL;
    }
    
    // For now, add without callback
    py_dap_events_socket_event_proc_add(socket, (uint32_t)events, NULL);
    Py_RETURN_NONE;
}

PyObject* py_dap_events_socket_event_proc_remove_wrapper(PyObject* self, PyObject* args) {
    PyObject* socket_obj;
    int events;
    
    if (!PyArg_ParseTuple(args, "Oi", &socket_obj, &events)) {
        return NULL;
    }
    
    void* socket = PyLong_AsVoidPtr(socket_obj);
    if (!socket) {
        PyErr_SetString(PyExc_ValueError, "Invalid socket pointer");
        return NULL;
    }
    
    py_dap_events_socket_event_proc_remove(socket, (uint32_t)events);
    Py_RETURN_NONE;
}

// Module method array with ALL needed functions
static PyMethodDef events_methods[] = {
    {"dap_events_init", py_dap_events_init_wrapper, METH_VARARGS, "Initialize DAP events"},
    {"dap_events_deinit", py_dap_events_deinit_wrapper, METH_NOARGS, "Deinitialize DAP events"},
    {"dap_events_start", py_dap_events_start_wrapper, METH_NOARGS, "Start DAP events"},
    {"dap_events_stop", py_dap_events_stop_wrapper, METH_NOARGS, "Stop DAP events"},
    {"dap_events_socket_create", py_dap_events_socket_create_wrapper, METH_VARARGS, "Create event socket"},
    {"dap_events_socket_delete", py_dap_events_socket_delete_wrapper, METH_VARARGS, "Delete event socket"},
    {"dap_events_socket_queue_ptr", py_dap_events_socket_queue_ptr_wrapper, METH_VARARGS, "Get socket queue pointer"},
    {"dap_events_socket_assign_on_worker_mt", py_dap_events_socket_assign_on_worker_mt_wrapper, METH_VARARGS, "Assign socket to worker"},
    {"dap_events_socket_event_proc_add", py_dap_events_socket_event_proc_add_wrapper, METH_VARARGS, "Add event processor"},
    {"dap_events_socket_event_proc_remove", py_dap_events_socket_event_proc_remove_wrapper, METH_VARARGS, "Remove event processor"},
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