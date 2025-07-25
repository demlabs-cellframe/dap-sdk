#ifndef PYTHON_DAP_CRYPTO_H
#define PYTHON_DAP_CRYPTO_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdbool.h>
#include <stddef.h>

// Key management functions
void* py_dap_crypto_key_create(const char* type);
void* py_dap_crypto_key_create_from_seed(const char* type, const void* seed, size_t seed_size);
void py_dap_crypto_key_delete(void* key);

// Signature operations
void* py_dap_crypto_key_sign(void* dap_key, const void* data, size_t data_size);
bool py_dap_crypto_key_verify(void* sign, void* dap_key, const void* data, size_t data_size);

// Multi-signature operations
void* py_dap_crypto_multi_sign_create(void);
bool py_dap_crypto_multi_sign_add(void* multi_sign, void* sign);
void* py_dap_crypto_multi_sign_combine(void* multi_sign);
bool py_dap_crypto_multi_sign_verify(void* combined_sign, void** keys, size_t keys_count, const void* data, size_t data_size);
void py_dap_crypto_multi_sign_delete(void* multi_sign);

// Aggregated signature operations
void* py_dap_crypto_aggregated_sign_create(void);
bool py_dap_crypto_aggregated_sign_add(void* agg_sign, void* sign, void* key);
void* py_dap_crypto_aggregated_sign_combine(void* agg_sign);
bool py_dap_crypto_aggregated_sign_verify(void* combined_sign, const void* data, size_t data_size);
void py_dap_crypto_aggregated_sign_delete(void* agg_sign);

// Hash functions
void* py_dap_hash_fast_create(const void* data, size_t size);
void py_dap_hash_fast_delete(void* hash);

// Certificate operations
void* py_dap_cert_create(const char* name);
void py_dap_cert_delete(void* cert);
void* py_dap_cert_sign(void* cert, const void* data, size_t data_size);
bool py_dap_cert_verify(void* cert, void* sign, const void* data, size_t data_size);

// Python wrapper functions
PyObject* py_dap_crypto_key_create_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_crypto_key_create_from_seed_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_crypto_key_delete_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_crypto_key_sign_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_crypto_key_verify_wrapper(PyObject* self, PyObject* args);

PyObject* py_dap_crypto_multi_sign_create_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_crypto_multi_sign_add_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_crypto_multi_sign_combine_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_crypto_multi_sign_verify_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_crypto_multi_sign_delete_wrapper(PyObject* self, PyObject* args);

PyObject* py_dap_crypto_aggregated_sign_create_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_crypto_aggregated_sign_add_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_crypto_aggregated_sign_combine_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_crypto_aggregated_sign_verify_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_crypto_aggregated_sign_delete_wrapper(PyObject* self, PyObject* args);

PyObject* py_dap_hash_fast_create_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_hash_fast_delete_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_cert_create_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_cert_delete_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_cert_sign_wrapper(PyObject* self, PyObject* args);
PyObject* py_dap_cert_verify_wrapper(PyObject* self, PyObject* args);

// Module interface functions
PyMethodDef* py_dap_crypto_get_methods(void);
int py_dap_crypto_module_init(PyObject* module);

#endif // PYTHON_DAP_CRYPTO_H 