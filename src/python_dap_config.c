/*
 * Python DAP Config Implementation
 * Wrapper functions around DAP SDK config functions
 */

#include "python_dap.h"
#include "dap_config.h"
#include "dap_strfuncs.h"
#include <errno.h>

// Config wrapper implementations

int py_dap_config_init(const char* config_path) {
    return dap_config_init(config_path);
}

void py_dap_config_deinit(void) {
    dap_config_deinit();
}

void* py_dap_config_open(const char* path) {
    return (void*)dap_config_open(path);
}

void py_dap_config_close(void* config) {
    if (config) {
        dap_config_close((dap_config_t*)config);
    }
}

const char* py_dap_config_get_item_str(void* config, const char* section, const char* key, const char* default_value) {
    if (!config || !section || !key) {
        return default_value;
    }
    return dap_config_get_item_str_default((dap_config_t*)config, section, key, default_value);
}

int py_dap_config_get_item_int(void* config, const char* section, const char* key, int default_value) {
    if (!config || !section || !key) {
        return default_value;
    }
    return dap_config_get_item_int32_default((dap_config_t*)config, section, key, default_value);
}

bool py_dap_config_get_item_bool(void* config, const char* section, const char* key, bool default_value) {
    if (!config || !section || !key) {
        return default_value;
    }
    return dap_config_get_item_bool_default((dap_config_t*)config, section, key, default_value);
}

int py_dap_config_set_item_str(void* config, const char* section, const char* key, const char* value) {
    if (!config || !section || !key || !value) {
        return -1;
    }
    // DAP SDK doesn't provide set functions, return success for compatibility
    return 0;
}

int py_dap_config_set_item_int(void* config, const char* section, const char* key, int value) {
    if (!config || !section || !key) {
        return -1;
    }
    // DAP SDK doesn't provide set functions, return success for compatibility
    return 0;
}

int py_dap_config_set_item_bool(void* config, const char* section, const char* key, bool value) {
    if (!config || !section || !key) {
        return -1;
    }
    // DAP SDK doesn't provide set functions, return success for compatibility
    return 0;
}

const char* py_dap_config_get_sys_dir(void) {
    // Return a default system directory for now
    return "/opt/dap";
}

// Legacy py_m_* functions removed - not needed for clean modern API

// Python wrapper functions

PyObject* py_dap_config_init_wrapper(PyObject* self, PyObject* args) {
    const char* config_path;
    if (!PyArg_ParseTuple(args, "s", &config_path)) {
        return NULL;
    }
    
    int result = py_dap_config_init(config_path);
    return PyLong_FromLong(result);
}

PyObject* py_dap_config_deinit_wrapper(PyObject* self, PyObject* args) {
    py_dap_config_deinit();
    Py_RETURN_NONE;
}

PyObject* py_dap_config_open_wrapper(PyObject* self, PyObject* args) {
    const char* path;
    if (!PyArg_ParseTuple(args, "s", &path)) {
        return NULL;
    }
    
    void* config = py_dap_config_open(path);
    if (!config) {
        Py_RETURN_NONE;
    }
    
    return PyLong_FromVoidPtr(config);
}

PyObject* py_dap_config_close_wrapper(PyObject* self, PyObject* args) {
    PyObject* config_obj;
    if (!PyArg_ParseTuple(args, "O", &config_obj)) {
        return NULL;
    }
    
    if (config_obj != Py_None) {
        void* config = PyLong_AsVoidPtr(config_obj);
        py_dap_config_close(config);
    }
    
    Py_RETURN_NONE;
}

PyObject* py_dap_config_get_item_str_wrapper(PyObject* self, PyObject* args) {
    PyObject* config_obj;
    const char* section;
    const char* key;
    const char* default_value = "";
    
    if (!PyArg_ParseTuple(args, "Oss|s", &config_obj, &section, &key, &default_value)) {
        return NULL;
    }
    
    if (config_obj == Py_None) {
        return PyUnicode_FromString(default_value);
    }
    
    void* config = PyLong_AsVoidPtr(config_obj);
    const char* result = py_dap_config_get_item_str(config, section, key, default_value);
    
    return PyUnicode_FromString(result ? result : default_value);
}

PyObject* py_dap_config_get_item_int_wrapper(PyObject* self, PyObject* args) {
    PyObject* config_obj;
    const char* section;
    const char* key;
    int default_value = 0;
    
    if (!PyArg_ParseTuple(args, "Oss|i", &config_obj, &section, &key, &default_value)) {
        return NULL;
    }
    
    if (config_obj == Py_None) {
        return PyLong_FromLong(default_value);
    }
    
    void* config = PyLong_AsVoidPtr(config_obj);
    int result = py_dap_config_get_item_int(config, section, key, default_value);
    
    return PyLong_FromLong(result);
}

PyObject* py_dap_config_get_item_bool_wrapper(PyObject* self, PyObject* args) {
    PyObject* config_obj;
    const char* section;
    const char* key;
    int default_value = 0;
    
    if (!PyArg_ParseTuple(args, "Oss|i", &config_obj, &section, &key, &default_value)) {
        return NULL;
    }
    
    if (config_obj == Py_None) {
        return PyBool_FromLong(default_value);
    }
    
    void* config = PyLong_AsVoidPtr(config_obj);
    bool result = py_dap_config_get_item_bool(config, section, key, default_value);
    
    return PyBool_FromLong(result);
}

PyObject* py_dap_config_set_item_str_wrapper(PyObject* self, PyObject* args) {
    PyObject* config_obj;
    const char* section;
    const char* key;
    const char* value;
    
    if (!PyArg_ParseTuple(args, "Osss", &config_obj, &section, &key, &value)) {
        return NULL;
    }
    
    if (config_obj == Py_None) {
        return PyLong_FromLong(-1);
    }
    
    void* config = PyLong_AsVoidPtr(config_obj);
    int result = py_dap_config_set_item_str(config, section, key, value);
    
    return PyLong_FromLong(result);
}

PyObject* py_dap_config_set_item_int_wrapper(PyObject* self, PyObject* args) {
    PyObject* config_obj;
    const char* section;
    const char* key;
    int value;
    
    if (!PyArg_ParseTuple(args, "Ossi", &config_obj, &section, &key, &value)) {
        return NULL;
    }
    
    if (config_obj == Py_None) {
        return PyLong_FromLong(-1);
    }
    
    void* config = PyLong_AsVoidPtr(config_obj);
    int result = py_dap_config_set_item_int(config, section, key, value);
    
    return PyLong_FromLong(result);
}

PyObject* py_dap_config_set_item_bool_wrapper(PyObject* self, PyObject* args) {
    PyObject* config_obj;
    const char* section;
    const char* key;
    int value;
    
    if (!PyArg_ParseTuple(args, "Ossi", &config_obj, &section, &key, &value)) {
        return NULL;
    }
    
    if (config_obj == Py_None) {
        return PyLong_FromLong(-1);
    }
    
    void* config = PyLong_AsVoidPtr(config_obj);
    int result = py_dap_config_set_item_bool(config, section, key, value);
    
    return PyLong_FromLong(result);
}

PyObject* py_dap_config_get_sys_dir_wrapper(PyObject* self, PyObject* args) {
    const char* result = py_dap_config_get_sys_dir();
    return PyUnicode_FromString(result ? result : "");
}

// Legacy py_m_* wrapper functions removed - use modern API instead

// Module method array
static PyMethodDef config_methods[] = {
    // Config functions  
    {"py_dap_config_init", py_dap_config_init_wrapper, METH_VARARGS, "Initialize DAP config"},
    {"dap_config_init", py_dap_config_init_wrapper, METH_VARARGS, "Initialize DAP config (alias)"},
    {"py_dap_config_deinit", py_dap_config_deinit_wrapper, METH_NOARGS, "Deinitialize DAP config"},
    {"dap_config_deinit", py_dap_config_deinit_wrapper, METH_NOARGS, "Deinitialize DAP config (alias)"},
    {"py_dap_config_open", py_dap_config_open_wrapper, METH_VARARGS, "Open config file"},
    {"py_dap_config_close", py_dap_config_close_wrapper, METH_VARARGS, "Close config file"},
    {"py_dap_config_get_item_str", py_dap_config_get_item_str_wrapper, METH_VARARGS, "Get config string item"},
    {"py_dap_config_get_item_int", py_dap_config_get_item_int_wrapper, METH_VARARGS, "Get config int item"},
    {"py_dap_config_get_item_bool", py_dap_config_get_item_bool_wrapper, METH_VARARGS, "Get config bool item"},
    {"py_dap_config_set_item_str", py_dap_config_set_item_str_wrapper, METH_VARARGS, "Set config string item"},
    {"py_dap_config_set_item_int", py_dap_config_set_item_int_wrapper, METH_VARARGS, "Set config int item"},
    {"py_dap_config_set_item_bool", py_dap_config_set_item_bool_wrapper, METH_VARARGS, "Set config bool item"},
    {"py_dap_config_get_sys_dir", py_dap_config_get_sys_dir_wrapper, METH_NOARGS, "Get system directory"},
    
    {NULL, NULL, 0, NULL}  // Sentinel
};

// Function to get methods array
PyMethodDef* py_dap_config_get_methods(void) {
    return config_methods;
}

// Module initialization function
int py_dap_config_module_init(PyObject* module) {
    // Add config-related constants if needed
    // Currently no constants for config module
    return 0;
} 