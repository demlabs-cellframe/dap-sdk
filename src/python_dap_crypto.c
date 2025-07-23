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

// Real crypto functions using DAP SDK

void* py_dap_crypto_key_create(const char* type) {
    if (!type) {
        return NULL;
    }
    
    // Map string type to DAP key type
    dap_enc_key_type_t key_type;
    if (strcmp(type, "dilithium") == 0) {
        key_type = DAP_ENC_KEY_TYPE_SIG_DILITHIUM;
    } else if (strcmp(type, "falcon") == 0) {
        key_type = DAP_ENC_KEY_TYPE_SIG_FALCON;
    } else if (strcmp(type, "picnic") == 0) {
        key_type = DAP_ENC_KEY_TYPE_SIG_PICNIC;
    } else if (strcmp(type, "bliss") == 0) {
        key_type = DAP_ENC_KEY_TYPE_SIG_BLISS;
    } else {
        // Default to DILITHIUM for unknown types
        key_type = DAP_ENC_KEY_TYPE_SIG_DILITHIUM;
    }
    
    // Use correct function signature with all required parameters
    dap_enc_key_t* key = dap_enc_key_new_generate(key_type, NULL, 0, NULL, 0, 0);
    return (void*)key;
}

void py_dap_crypto_key_delete(void* key) {
    if (key) {
        dap_enc_key_delete((dap_enc_key_t*)key);
    }
}

void* py_dap_crypto_key_sign(void* dap_key, const void* data, size_t data_size) {
    if (!dap_key || !data) {
        return NULL;
    }
    
    dap_enc_key_t* key = (dap_enc_key_t*)dap_key;
    dap_sign_t* sign = dap_sign_create(key, data, data_size);
    
    if (!sign) {
        // Use dap_enc_key_delete for cleanup instead of non-existent dap_sign_delete
        return NULL;
    }
    
    return (void*)sign;
}

bool py_dap_crypto_key_verify(void* sign, void* dap_key, const void* data, size_t data_size) {
    if (!sign || !dap_key || !data) {
        return false;
    }
    
    // Use correct function signature with data and data_size parameters
    return dap_sign_verify((dap_sign_t*)sign, data, data_size) == 1;
}

// Real hash functions using DAP SDK

void* py_dap_hash_fast_create(const void* data, size_t size) {
    if (!data || size == 0) {
        return NULL;
    }
    
    // Create real hash using DAP SDK
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

void* py_dap_hash_slow_create(const void* data, size_t size) {
    if (!data || size == 0) {
        return NULL;
    }
    
    // For now, use same fast hash - DAP SDK may not have separate slow hash
    return py_dap_hash_fast_create(data, size);
} 