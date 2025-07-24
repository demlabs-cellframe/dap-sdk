/*
 * Python DAP Crypto Module Implementation
 * Real cryptographic functions using DAP SDK
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "python_dap.h"
#include "dap_common.h"
#include "dap_enc_key.h"
#include "dap_sign.h"
#include "dap_hash.h"
#include "dap_enc_base64.h"
#include "dap_enc_chipmunk.h"
#include "dap_enc_dilithium.h"
#include "dap_enc_falcon.h"
#include "dap_enc_picnic.h"
#include "dap_enc_bliss.h"
#include "dap_cert.h"
#include "dap_cert_file.h"

// Key type mapping
static dap_enc_key_type_t get_key_type(const char* type) {
    if (!type) {
        return DAP_ENC_KEY_TYPE_SIG_DILITHIUM; // Default to DILITHIUM
    }
    
    if (strcmp(type, "dilithium") == 0) {
        return DAP_ENC_KEY_TYPE_SIG_DILITHIUM;
    } else if (strcmp(type, "falcon") == 0) {
        return DAP_ENC_KEY_TYPE_SIG_FALCON;
    } else if (strcmp(type, "picnic") == 0) {
        return DAP_ENC_KEY_TYPE_SIG_PICNIC;
    } else if (strcmp(type, "bliss") == 0) {
        return DAP_ENC_KEY_TYPE_SIG_BLISS;
    } else if (strcmp(type, "chipmunk") == 0) {
        return DAP_ENC_KEY_TYPE_SIG_CHIPMUNK;
    }
    
    return DAP_ENC_KEY_TYPE_SIG_DILITHIUM; // Default to DILITHIUM
}

// Key generation and management
void* py_dap_crypto_key_create(const char* type) {
    dap_enc_key_type_t key_type = get_key_type(type);
    
    // Generate key with proper parameters
    size_t key_size = 0;
    void* seed = NULL;
    
    switch(key_type) {
        case DAP_ENC_KEY_TYPE_SIG_DILITHIUM:
            key_size = 32; // Standard seed size for Dilithium
            break;
        case DAP_ENC_KEY_TYPE_SIG_FALCON:
            key_size = 40; // Falcon seed size
            break;
        case DAP_ENC_KEY_TYPE_SIG_PICNIC:
            key_size = 32; // Picnic seed size
            break;
        case DAP_ENC_KEY_TYPE_SIG_CHIPMUNK:
            key_size = 32; // Chipmunk seed size
            break;
        default:
            key_size = 32;
    }
    
    // Generate random seed
    seed = DAP_NEW_SIZE(uint8_t, key_size);
    if (!seed) {
        return NULL;
    }
    dap_random_bytes(seed, key_size, false);
    
    // Create key with seed
    dap_enc_key_t* key = dap_enc_key_new_generate(key_type, seed, key_size, NULL, 0, 0);
    DAP_DELETE(seed);
    
    return (void*)key;
}

void* py_dap_crypto_key_create_from_seed(const char* type, const void* seed, size_t seed_size) {
    if (!seed || seed_size == 0) {
        return NULL;
    }
    
    dap_enc_key_type_t key_type = get_key_type(type);
    return (void*)dap_enc_key_new_generate(key_type, seed, seed_size, NULL, 0, 0);
}

void py_dap_crypto_key_delete(void* key) {
    if (key) {
        dap_enc_key_delete((dap_enc_key_t*)key);
    }
}

// Signature operations
void* py_dap_crypto_key_sign(void* dap_key, const void* data, size_t data_size) {
    if (!dap_key || !data || data_size == 0) {
        return NULL;
    }
    
    dap_enc_key_t* key = (dap_enc_key_t*)dap_key;
    return (void*)dap_sign_create(key, data, data_size);
}

bool py_dap_crypto_key_verify(void* sign, void* dap_key, const void* data, size_t data_size) {
    if (!sign || !dap_key || !data || data_size == 0) {
        return false;
    }
    
    return dap_sign_verify((dap_sign_t*)sign, data, data_size) == 1;
}

// Hash functions
void* py_dap_hash_fast_create(const void* data, size_t size) {
    if (!data || size == 0) {
        return NULL;
    }
    
    dap_hash_fast_t* hash = DAP_NEW(dap_hash_fast_t);
    if (!hash) {
        return NULL;
    }
    
    if (!dap_hash_fast(data, size, hash)) {
        DAP_DELETE(hash);
        return NULL;
    }
    
    return (void*)hash;
}

void py_dap_hash_fast_delete(void* hash) {
    if (hash) {
        DAP_DELETE(hash);
    }
}

// Certificate operations
void* py_dap_cert_create(const char* name) {
    if (!name) {
        return NULL;
    }
    return (void*)dap_cert_new(name);
}

void py_dap_cert_delete(void* cert) {
    if (cert) {
        dap_cert_delete((dap_cert_t*)cert);
    }
}

void* py_dap_cert_sign(void* cert, const void* data, size_t data_size) {
    if (!cert || !data || data_size == 0) {
        return NULL;
    }
    
    dap_cert_t* l_cert = (dap_cert_t*)cert;
    return (void*)dap_cert_sign(l_cert, data, data_size);
}

bool py_dap_cert_verify(void* cert, void* sign, const void* data, size_t data_size) {
    if (!cert || !sign || !data || data_size == 0) {
        return false;
    }
    
    dap_cert_t* l_cert = (dap_cert_t*)cert;
    return dap_cert_verify(l_cert, (dap_sign_t*)sign, data, data_size) == 1;
}

// Python wrapper functions
static PyObject* py_dap_crypto_key_create_wrapper(PyObject* self, PyObject* args) {
    const char* type;
    if (!PyArg_ParseTuple(args, "s", &type)) {
        return NULL;
    }
    
    void* key = py_dap_crypto_key_create(type);
    if (!key) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create crypto key");
        return NULL;
    }
    
    return PyLong_FromVoidPtr(key);
}

static PyObject* py_dap_crypto_key_create_from_seed_wrapper(PyObject* self, PyObject* args) {
    const char* type;
    const char* seed;
    Py_ssize_t seed_size;
    
    if (!PyArg_ParseTuple(args, "ss#", &type, &seed, &seed_size)) {
        return NULL;
    }
    
    void* key = py_dap_crypto_key_create_from_seed(type, seed, (size_t)seed_size);
    if (!key) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create crypto key from seed");
        return NULL;
    }
    
    return PyLong_FromVoidPtr(key);
}

static PyObject* py_dap_crypto_key_delete_wrapper(PyObject* self, PyObject* args) {
    void* key;
    if (!PyArg_ParseTuple(args, "K", &key)) {
        return NULL;
    }
    
    py_dap_crypto_key_delete(key);
    Py_RETURN_NONE;
}

static PyObject* py_dap_crypto_key_sign_wrapper(PyObject* self, PyObject* args) {
    void* key;
    const char* data;
    Py_ssize_t data_size;
    
    if (!PyArg_ParseTuple(args, "Ks#", &key, &data, &data_size)) {
        return NULL;
    }
    
    void* sign = py_dap_crypto_key_sign(key, data, (size_t)data_size);
    if (!sign) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create signature");
        return NULL;
    }
    
    return PyLong_FromVoidPtr(sign);
}

static PyObject* py_dap_crypto_key_verify_wrapper(PyObject* self, PyObject* args) {
    void* sign;
    void* key;
    const char* data;
    Py_ssize_t data_size;
    
    if (!PyArg_ParseTuple(args, "KKs#", &sign, &key, &data, &data_size)) {
        return NULL;
    }
    
    bool result = py_dap_crypto_key_verify(sign, key, data, (size_t)data_size);
    return PyBool_FromLong(result);
}

static PyObject* py_dap_hash_fast_create_wrapper(PyObject* self, PyObject* args) {
    const char* data;
    Py_ssize_t data_size;
    
    if (!PyArg_ParseTuple(args, "s#", &data, &data_size)) {
        return NULL;
    }
    
    void* hash = py_dap_hash_fast_create(data, (size_t)data_size);
    if (!hash) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create hash");
        return NULL;
    }
    
    return PyLong_FromVoidPtr(hash);
}

static PyObject* py_dap_hash_fast_delete_wrapper(PyObject* self, PyObject* args) {
    void* hash;
    if (!PyArg_ParseTuple(args, "K", &hash)) {
        return NULL;
    }
    
    py_dap_hash_fast_delete(hash);
    Py_RETURN_NONE;
}

static PyObject* py_dap_cert_create_wrapper(PyObject* self, PyObject* args) {
    const char* name;
    if (!PyArg_ParseTuple(args, "s", &name)) {
        return NULL;
    }
    
    void* cert = py_dap_cert_create(name);
    if (!cert) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create certificate");
        return NULL;
    }
    
    return PyLong_FromVoidPtr(cert);
}

static PyObject* py_dap_cert_delete_wrapper(PyObject* self, PyObject* args) {
    void* cert;
    if (!PyArg_ParseTuple(args, "K", &cert)) {
        return NULL;
    }
    
    py_dap_cert_delete(cert);
    Py_RETURN_NONE;
}

static PyObject* py_dap_cert_sign_wrapper(PyObject* self, PyObject* args) {
    void* cert;
    const char* data;
    Py_ssize_t data_size;
    
    if (!PyArg_ParseTuple(args, "Ks#", &cert, &data, &data_size)) {
        return NULL;
    }
    
    void* sign = py_dap_cert_sign(cert, data, (size_t)data_size);
    if (!sign) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create certificate signature");
        return NULL;
    }
    
    return PyLong_FromVoidPtr(sign);
}

static PyObject* py_dap_cert_verify_wrapper(PyObject* self, PyObject* args) {
    void* cert;
    void* sign;
    const char* data;
    Py_ssize_t data_size;
    
    if (!PyArg_ParseTuple(args, "KKs#", &cert, &sign, &data, &data_size)) {
        return NULL;
    }
    
    bool result = py_dap_cert_verify(cert, sign, data, (size_t)data_size);
    return PyBool_FromLong(result);
}

// Method definitions
static PyMethodDef crypto_methods[] = {
    // Key management
    {"py_dap_crypto_key_create", py_dap_crypto_key_create_wrapper, METH_VARARGS,
     "Create a new crypto key of specified type"},
    {"py_dap_crypto_key_create_from_seed", py_dap_crypto_key_create_from_seed_wrapper, METH_VARARGS,
     "Create a new crypto key from seed"},
    {"py_dap_crypto_key_delete", py_dap_crypto_key_delete_wrapper, METH_VARARGS,
     "Delete a crypto key"},
     
    // Signature operations
    {"py_dap_crypto_key_sign", py_dap_crypto_key_sign_wrapper, METH_VARARGS,
     "Create a signature using crypto key"},
    {"py_dap_crypto_key_verify", py_dap_crypto_key_verify_wrapper, METH_VARARGS,
     "Verify a signature using crypto key"},
     
    // Hash functions
    {"py_dap_hash_fast_create", py_dap_hash_fast_create_wrapper, METH_VARARGS,
     "Create a fast hash of data"},
    {"py_dap_hash_fast_delete", py_dap_hash_fast_delete_wrapper, METH_VARARGS,
     "Delete a hash object"},
     
    // Certificate operations
    {"py_dap_cert_create", py_dap_cert_create_wrapper, METH_VARARGS,
     "Create a new certificate"},
    {"py_dap_cert_delete", py_dap_cert_delete_wrapper, METH_VARARGS,
     "Delete a certificate"},
    {"py_dap_cert_sign", py_dap_cert_sign_wrapper, METH_VARARGS,
     "Create a signature using certificate"},
    {"py_dap_cert_verify", py_dap_cert_verify_wrapper, METH_VARARGS,
     "Verify a signature using certificate"},
     
    {NULL, NULL, 0, NULL}  // Sentinel
};

// Function to get methods array
PyMethodDef* py_dap_crypto_get_methods(void) {
    return crypto_methods;
}

// Module initialization function
int py_dap_crypto_module_init(PyObject* module) {
    // Add crypto-related constants
    PyModule_AddIntConstant(module, "DAP_ENC_KEY_TYPE_SIG_DILITHIUM", DAP_ENC_KEY_TYPE_SIG_DILITHIUM);
    PyModule_AddIntConstant(module, "DAP_ENC_KEY_TYPE_SIG_FALCON", DAP_ENC_KEY_TYPE_SIG_FALCON);
    PyModule_AddIntConstant(module, "DAP_ENC_KEY_TYPE_SIG_PICNIC", DAP_ENC_KEY_TYPE_SIG_PICNIC);
    PyModule_AddIntConstant(module, "DAP_ENC_KEY_TYPE_SIG_BLISS", DAP_ENC_KEY_TYPE_SIG_BLISS);
    PyModule_AddIntConstant(module, "DAP_ENC_KEY_TYPE_SIG_CHIPMUNK", DAP_ENC_KEY_TYPE_SIG_CHIPMUNK);
    
    return 0;
} 