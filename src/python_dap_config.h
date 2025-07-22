#ifndef PYTHON_DAP_CONFIG_H
#define PYTHON_DAP_CONFIG_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>

// Config management functions
int py_dap_config_init(const char* a_config_path);
void py_dap_config_deinit(void);
void* py_dap_config_open(const char* a_path);
void py_dap_config_close(void* a_config);
const char* py_dap_config_get_item_str(void* a_config, const char* a_section, const char* a_key, const char* a_default);
int py_dap_config_get_item_int(void* a_config, const char* a_section, const char* a_key, int a_default);
bool py_dap_config_get_item_bool(void* a_config, const char* a_section, const char* a_key, bool a_default);
int py_dap_config_set_item_str(void* a_config, const char* a_section, const char* a_key, const char* a_value);
int py_dap_config_set_item_int(void* a_config, const char* a_section, const char* a_key, int a_value);
int py_dap_config_set_item_bool(void* a_config, const char* a_section, const char* a_key, bool a_value);
const char* py_dap_config_get_sys_dir(void);

// Python wrapper functions
PyObject* py_dap_config_init_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_config_deinit_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_config_open_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_config_close_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_config_get_item_str_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_config_get_item_int_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_config_get_item_bool_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_config_set_item_str_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_config_set_item_int_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_config_set_item_bool_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_config_get_sys_dir_wrapper(PyObject* self, PyObject* args);

// Legacy py_m_* wrapper functions removed - use modern API instead

// Module functions
PyMethodDef* py_dap_config_get_methods(void);
int py_dap_config_module_init(PyObject* module);

#endif // PYTHON_DAP_CONFIG_H 