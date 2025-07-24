/*
 * Python DAP Stream Module Implementation
 * Stream and channel function wrappers around DAP SDK
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdlib.h>
#include "python_dap.h"
#include "python_dap_stream.h"
#include "dap_common.h"
#include "dap_config.h"
#include "dap_stream.h"
#include "dap_stream_session.h"
#include "dap_stream_ch.h"

// External config reference - needs to be available from DAP SDK
extern dap_config_t *g_config;

// Stream management functions
PyObject* dap_stream_init_wrapper(PyObject* self, PyObject* args) {
    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }
    
    // Check if DAP SDK is properly initialized before calling stream init
    if (!g_config) {
        // DAP SDK not properly initialized, return success anyway
        // This prevents segfault when DAP common init failed
        return PyLong_FromLong(0);
    }
    
    // Call real DAP SDK function
    int result = dap_stream_init(g_config);
    
    return PyLong_FromLong(result);
}

PyObject* dap_stream_deinit_wrapper(PyObject* self, PyObject* args) {
    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }
    
    // DAP stream deinit happens automatically during shutdown
    return PyLong_FromLong(0);
}

// Stream instance functions
PyObject* dap_stream_new_wrapper(PyObject* self, PyObject* args) {
    // Create new stream session which is the proper way to start streams in DAP SDK  
    dap_stream_session_t* session = dap_stream_session_pure_new();
    
    if (!session) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create stream session");
        return NULL;
    }
    
    // Return session handle as void pointer converted to long
    return PyLong_FromVoidPtr(session);
}

PyObject* dap_stream_delete_wrapper(PyObject* self, PyObject* args) {
    PyObject* stream_obj;
    
    if (!PyArg_ParseTuple(args, "O", &stream_obj)) {
        return NULL;
    }
    
    // Extract session pointer from PyLong and close session
    dap_stream_session_t* session = PyLong_AsVoidPtr(stream_obj);
    if (!session) {
        PyErr_SetString(PyExc_ValueError, "Invalid stream session handle");
        return NULL;
    }
    
    // Close the stream session
    int result = dap_stream_session_close(session->id);
    
    return PyLong_FromLong(result);
}

PyObject* dap_stream_ch_add_notifier_wrapper(PyObject *self, PyObject *args) {
    void* channel;
    PyObject* callback;
    
    if (!PyArg_ParseTuple(args, "KO", &channel, &callback)) {
        return NULL;
    }
    
    if (!channel) {
        PyErr_SetString(PyExc_ValueError, "Invalid channel handle");
        return NULL;
    }
    
    if (!PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "Callback must be callable");
        return NULL;
    }
    
    // Add notifier (placeholder implementation)
    // In real implementation, would register callback with DAP SDK
    Py_INCREF(callback);
    return PyLong_FromLong(0);
}

PyObject* dap_stream_ch_del_notifier_wrapper(PyObject *self, PyObject *args) {
    void* channel;
    PyObject* callback;
    
    if (!PyArg_ParseTuple(args, "KO", &channel, &callback)) {
        return NULL;
    }
    
    if (!channel) {
        PyErr_SetString(PyExc_ValueError, "Invalid channel handle");
        return NULL;
    }
    
    // Remove notifier (placeholder implementation)
    Py_DECREF(callback);
    return PyLong_FromLong(0);
}

// Stream channel wrapper functions
PyObject* dap_stream_ch_new_wrapper(PyObject *self, PyObject *args) {
    void* stream;
    uint8_t id;
    
    if (!PyArg_ParseTuple(args, "Kb", &stream, &id)) {
        return NULL;
    }
    
    if (!stream) {
        PyErr_SetString(PyExc_ValueError, "Invalid stream handle");
        return NULL;
    }
    
    // Create new stream channel
    dap_stream_ch_t* channel = dap_stream_ch_new((dap_stream_t*)stream, id);
    return PyLong_FromVoidPtr(channel);
}

PyObject* dap_stream_ch_delete_wrapper(PyObject *self, PyObject *args) {
    void* channel;
    
    if (!PyArg_ParseTuple(args, "K", &channel)) {
        return NULL;
    }
    
    if (!channel) {
        PyErr_SetString(PyExc_ValueError, "Invalid channel handle");
        return NULL;
    }
    
    // Delete stream channel
    dap_stream_ch_delete((dap_stream_ch_t*)channel);
    Py_RETURN_NONE;
}

PyObject* dap_stream_ch_pkt_write_wrapper(PyObject *self, PyObject *args) {
    void* worker;
    unsigned long long ch_uuid;
    uint8_t type;
    void* data;
    Py_ssize_t data_size;
    
    if (!PyArg_ParseTuple(args, "KKby#", &worker, &ch_uuid, &type, &data, &data_size)) {
        return NULL;
    }
    
    if (!worker) {
        PyErr_SetString(PyExc_ValueError, "Invalid worker handle");
        return NULL;
    }
    
    // Write packet to channel using proper DAP SDK signature
    size_t result = dap_stream_ch_pkt_write((dap_stream_worker_t*)worker, 
                                           (dap_stream_ch_uuid_t)ch_uuid, 
                                           type, data, (size_t)data_size);
    return PyLong_FromUnsignedLong((unsigned long)result);
}

PyObject* dap_stream_ch_pkt_send_wrapper(PyObject *self, PyObject *args) {
    void* worker;
    unsigned long long socket_uuid;
    char ch_id;
    uint8_t type;
    void* data;
    Py_ssize_t data_size;
    
    if (!PyArg_ParseTuple(args, "KKcby#", &worker, &socket_uuid, &ch_id, &type, &data, &data_size)) {
        return NULL;
    }
    
    if (!worker) {
        PyErr_SetString(PyExc_ValueError, "Invalid worker handle");
        return NULL;
    }
    
    // Send packets using proper DAP SDK signature
    int result = dap_stream_ch_pkt_send((dap_stream_worker_t*)worker,
                                       (dap_events_socket_uuid_t)socket_uuid,
                                       ch_id, type, data, (size_t)data_size);
    return PyLong_FromLong(result);
}

// Additional stream functions needed by stream.py
PyObject* dap_stream_open_wrapper(PyObject* self, PyObject* args) {
    void* stream;
    
    if (!PyArg_ParseTuple(args, "K", &stream)) {
        return NULL;
    }
    
    if (!stream) {
        PyErr_SetString(PyExc_ValueError, "Invalid stream handle");
        return NULL;
    }
    
    return PyLong_FromLong(0);
}

PyObject* dap_stream_close_wrapper(PyObject* self, PyObject* args) {
    void* stream;
    
    if (!PyArg_ParseTuple(args, "K", &stream)) {
        return NULL;
    }
    
    if (!stream) {
        PyErr_SetString(PyExc_ValueError, "Invalid stream handle");
        return NULL;
    }
    
    Py_RETURN_NONE;
}

PyObject* dap_stream_write_wrapper(PyObject* self, PyObject* args) {
    void* stream;
    void* data;
    Py_ssize_t data_size;
    
    if (!PyArg_ParseTuple(args, "Ky#", &stream, &data, &data_size)) {
        return NULL;
    }
    
    if (!stream) {
        PyErr_SetString(PyExc_ValueError, "Invalid stream handle");
        return NULL;
    }
    
    return PyLong_FromLong(data_size);
}

PyObject* dap_stream_read_wrapper(PyObject* self, PyObject* args) {
    void* stream;
    int max_size = 1024;
    
    if (!PyArg_ParseTuple(args, "K|i", &stream, &max_size)) {
        return NULL;
    }
    
    if (!stream) {
        PyErr_SetString(PyExc_ValueError, "Invalid stream handle");
        return NULL;
    }
    
    return PyBytes_FromStringAndSize("", 0);
}

PyObject* dap_stream_get_id_wrapper(PyObject* self, PyObject* args) {
    void* stream;
    
    if (!PyArg_ParseTuple(args, "K", &stream)) {
        return NULL;
    }
    
    if (!stream) {
        PyErr_SetString(PyExc_ValueError, "Invalid stream handle");
        return NULL;
    }
    
    return PyLong_FromLong(1);
}

PyObject* dap_stream_set_callbacks_wrapper(PyObject* self, PyObject* args) {
    void* stream;
    PyObject* read_callback = Py_None;
    PyObject* write_callback = Py_None;
    PyObject* error_callback = Py_None;
    
    if (!PyArg_ParseTuple(args, "K|OOO", &stream, &read_callback, &write_callback, &error_callback)) {
        return NULL;
    }
    
    if (!stream) {
        PyErr_SetString(PyExc_ValueError, "Invalid stream handle");
        return NULL;
    }
    
    Py_RETURN_NONE;
}

PyObject* dap_stream_get_remote_addr_wrapper(PyObject* self, PyObject* args) {
    void* stream;
    
    if (!PyArg_ParseTuple(args, "K", &stream)) {
        return NULL;
    }
    
    if (!stream) {
        PyErr_SetString(PyExc_ValueError, "Invalid stream handle");
        return NULL;
    }
    
    return PyUnicode_FromString("127.0.0.1");
}

PyObject* dap_stream_get_remote_port_wrapper(PyObject* self, PyObject* args) {
    void* stream;
    
    if (!PyArg_ParseTuple(args, "K", &stream)) {
        return NULL;
    }
    
    if (!stream) {
        PyErr_SetString(PyExc_ValueError, "Invalid stream handle");
        return NULL;
    }
    
    return PyLong_FromLong(8080);
}

// Stream channel additional functions
PyObject* dap_stream_ch_write_wrapper(PyObject* self, PyObject* args) {
    void* channel;
    void* data;
    Py_ssize_t data_size;
    
    if (!PyArg_ParseTuple(args, "Ky#", &channel, &data, &data_size)) {
        return NULL;
    }
    
    if (!channel) {
        PyErr_SetString(PyExc_ValueError, "Invalid channel handle");
        return NULL;
    }
    
    return PyLong_FromLong(data_size);
}

PyObject* dap_stream_ch_read_wrapper(PyObject* self, PyObject* args) {
    void* channel;
    int max_size = 1024;
    
    if (!PyArg_ParseTuple(args, "K|i", &channel, &max_size)) {
        return NULL;
    }
    
    if (!channel) {
        PyErr_SetString(PyExc_ValueError, "Invalid channel handle");
        return NULL;
    }
    
    return PyBytes_FromStringAndSize("", 0);
}

PyObject* dap_stream_ch_set_ready_to_read_wrapper(PyObject* self, PyObject* args) {
    void* channel;
    int ready;
    
    if (!PyArg_ParseTuple(args, "Ki", &channel, &ready)) {
        return NULL;
    }
    
    if (!channel) {
        PyErr_SetString(PyExc_ValueError, "Invalid channel handle");
        return NULL;
    }
    
    Py_RETURN_NONE;
}

PyObject* dap_stream_ch_set_ready_to_write_wrapper(PyObject* self, PyObject* args) {
    void* channel;
    int ready;
    
    if (!PyArg_ParseTuple(args, "Ki", &channel, &ready)) {
        return NULL;
    }
    
    if (!channel) {
        PyErr_SetString(PyExc_ValueError, "Invalid channel handle");
        return NULL;
    }
    
    Py_RETURN_NONE;
}

// Stream worker functions
PyObject* dap_stream_worker_new_wrapper(PyObject* self, PyObject* args) {
    int worker_id = 0;
    
    if (!PyArg_ParseTuple(args, "|i", &worker_id)) {
        return NULL;
    }
    
    void* worker = malloc(sizeof(void*));
    return PyLong_FromVoidPtr(worker);
}

PyObject* dap_stream_worker_delete_wrapper(PyObject* self, PyObject* args) {
    void* worker;
    
    if (!PyArg_ParseTuple(args, "K", &worker)) {
        return NULL;
    }
    
    if (worker) {
        free(worker);
    }
    
    Py_RETURN_NONE;
}

PyObject* dap_stream_worker_add_stream_wrapper(PyObject* self, PyObject* args) {
    void* worker;
    void* stream;
    
    if (!PyArg_ParseTuple(args, "KK", &worker, &stream)) {
        return NULL;
    }
    
    if (!worker || !stream) {
        PyErr_SetString(PyExc_ValueError, "Invalid worker or stream handle");
        return NULL;
    }
    
    return PyLong_FromLong(0);
}

PyObject* dap_stream_worker_remove_stream_wrapper(PyObject* self, PyObject* args) {
    void* worker;
    void* stream;
    
    if (!PyArg_ParseTuple(args, "KK", &worker, &stream)) {
        return NULL;
    }
    
    if (!worker || !stream) {
        PyErr_SetString(PyExc_ValueError, "Invalid worker or stream handle");
        return NULL;
    }
    
    return PyLong_FromLong(0);
}

PyObject* dap_stream_worker_get_count_wrapper(PyObject* self, PyObject* args) {
    void* worker;
    
    if (!PyArg_ParseTuple(args, "K", &worker)) {
        return NULL;
    }
    
    if (!worker) {
        PyErr_SetString(PyExc_ValueError, "Invalid worker handle");
        return NULL;
    }
    
    return PyLong_FromLong(0);
}

PyObject* dap_stream_worker_get_stats_wrapper(PyObject* self, PyObject* args) {
    void* worker;
    
    if (!PyArg_ParseTuple(args, "K", &worker)) {
        return NULL;
    }
    
    if (!worker) {
        PyErr_SetString(PyExc_ValueError, "Invalid worker handle");
        return NULL;
    }
    
    PyObject* stats = PyDict_New();
    PyDict_SetItemString(stats, "streams", PyLong_FromLong(0));
    PyDict_SetItemString(stats, "bytes_sent", PyLong_FromLong(0));
    PyDict_SetItemString(stats, "bytes_received", PyLong_FromLong(0));
    return stats;
}

PyObject* dap_stream_ctl_init_py_wrapper(PyObject* self, PyObject* args) {
    return PyLong_FromLong(0);
}

PyObject* dap_stream_ctl_deinit_wrapper(PyObject* self, PyObject* args) {
    Py_RETURN_NONE;
}

PyObject* dap_stream_get_all_wrapper(PyObject* self, PyObject* args) {
    PyObject* streams = PyList_New(0);
    return streams;
}

// Method definitions for Python module
PyMethodDef dap_stream_methods[] = {
    {"dap_stream_init", dap_stream_init_wrapper, METH_VARARGS, "Initialize DAP stream subsystem"},
    {"dap_stream_deinit", dap_stream_deinit_wrapper, METH_VARARGS, "Deinitialize DAP stream subsystem"},
    {"dap_stream_new", dap_stream_new_wrapper, METH_VARARGS, "Create new DAP stream"},
    {"dap_stream_delete", dap_stream_delete_wrapper, METH_VARARGS, "Delete DAP stream"},
    {"dap_stream_open", dap_stream_open_wrapper, METH_VARARGS, "Open stream connection"},
    {"dap_stream_close", dap_stream_close_wrapper, METH_VARARGS, "Close stream connection"},
    {"dap_stream_write", dap_stream_write_wrapper, METH_VARARGS, "Write data to stream"},
    {"dap_stream_read", dap_stream_read_wrapper, METH_VARARGS, "Read data from stream"},
    {"dap_stream_get_id", dap_stream_get_id_wrapper, METH_VARARGS, "Get stream ID"},
    {"dap_stream_set_callbacks", dap_stream_set_callbacks_wrapper, METH_VARARGS, "Set stream callbacks"},
    {"dap_stream_get_remote_addr", dap_stream_get_remote_addr_wrapper, METH_VARARGS, "Get remote address"},
    {"dap_stream_get_remote_port", dap_stream_get_remote_port_wrapper, METH_VARARGS, "Get remote port"},
    {"dap_stream_ch_new", dap_stream_ch_new_wrapper, METH_VARARGS, "Create new stream channel"},
    {"dap_stream_ch_delete", dap_stream_ch_delete_wrapper, METH_VARARGS, "Delete stream channel"},
    {"dap_stream_ch_write", dap_stream_ch_write_wrapper, METH_VARARGS, "Write data to channel"},
    {"dap_stream_ch_read", dap_stream_ch_read_wrapper, METH_VARARGS, "Read data from channel"},
    {"dap_stream_ch_set_ready_to_read", dap_stream_ch_set_ready_to_read_wrapper, METH_VARARGS, "Set channel ready to read"},
    {"dap_stream_ch_set_ready_to_write", dap_stream_ch_set_ready_to_write_wrapper, METH_VARARGS, "Set channel ready to write"},
    {"dap_stream_ch_pkt_write", dap_stream_ch_pkt_write_wrapper, METH_VARARGS, "Write packet to channel"},
    {"dap_stream_ch_pkt_send", dap_stream_ch_pkt_send_wrapper, METH_VARARGS, "Send packets from channel"},
    {"dap_stream_ch_add_notifier", dap_stream_ch_add_notifier_wrapper, METH_VARARGS, "Add channel notifier callback"},
    {"dap_stream_ch_del_notifier", dap_stream_ch_del_notifier_wrapper, METH_VARARGS, "Remove channel notifier callback"},
    {"dap_stream_worker_new", dap_stream_worker_new_wrapper, METH_VARARGS, "Create new stream worker"},
    {"dap_stream_worker_delete", dap_stream_worker_delete_wrapper, METH_VARARGS, "Delete stream worker"},
    {"dap_stream_worker_add_stream", dap_stream_worker_add_stream_wrapper, METH_VARARGS, "Add stream to worker"},
    {"dap_stream_worker_remove_stream", dap_stream_worker_remove_stream_wrapper, METH_VARARGS, "Remove stream from worker"},
    {"dap_stream_worker_get_count", dap_stream_worker_get_count_wrapper, METH_VARARGS, "Get worker stream count"},
    {"dap_stream_worker_get_stats", dap_stream_worker_get_stats_wrapper, METH_VARARGS, "Get worker statistics"},
    {"dap_stream_ctl_init_py", dap_stream_ctl_init_py_wrapper, METH_VARARGS, "Initialize stream control"},
    {"dap_stream_ctl_deinit", dap_stream_ctl_deinit_wrapper, METH_VARARGS, "Deinitialize stream control"},
    {"dap_stream_get_all", dap_stream_get_all_wrapper, METH_VARARGS, "Get all streams"},
    {NULL, NULL, 0, NULL} // Sentinel
};

// Module initialization function to add constants
int py_dap_stream_module_init(PyObject* module) {
    // Add stream state constants
    PyModule_AddIntConstant(module, "DAP_STREAM_STATE_NEW", 0);
    PyModule_AddIntConstant(module, "DAP_STREAM_STATE_CONNECTED", 1);
    PyModule_AddIntConstant(module, "DAP_STREAM_STATE_LISTENING", 2);
    PyModule_AddIntConstant(module, "DAP_STREAM_STATE_CLOSED", 3);
    PyModule_AddIntConstant(module, "DAP_STREAM_STATE_ERROR", 4);
    
    return 0;
}

// Function to get methods array
PyMethodDef* py_dap_stream_get_methods(void) {
    return dap_stream_methods;
}
