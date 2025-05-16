#include "dap_enc_chipmunk.h"
#include "dap_enc_key.h"
#include "dap_common.h"
#include "dap_crypto_common.h"
#include "chipmunk/chipmunk.h"
#include <string.h>

#define LOG_TAG "dap_enc_chipmunk"

static int s_chipmunk_initialized = 0;

// Algorithm initialization
static void _chipmunk_init(void) {
    if (!s_chipmunk_initialized) {
        chipmunk_init();
        s_chipmunk_initialized = 1;
        log_it(L_NOTICE, "Chipmunk module initialized");
    }
}

// Key initialization
int dap_enc_chipmunk_key_new(dap_enc_key_t *a_key) {
    _chipmunk_init();
    
    dap_enc_chipmunk_key_t *l_key = DAP_NEW_Z(dap_enc_chipmunk_key_t);
    if (!l_key) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -1;
    }

    l_key->private_key = NULL;
    l_key->private_key_size = 0;
    l_key->public_key = NULL;
    l_key->public_key_size = 0;

    a_key->priv_key_data = l_key;
    a_key->type = DAP_ENC_KEY_TYPE_SIG_CHIPMUNK;
    a_key->enc = NULL;
    a_key->dec = NULL;
    a_key->enc_na = NULL;
    a_key->dec_na = NULL;
    a_key->dec_na_ext = NULL;
    
    a_key->sign_get = dap_enc_chipmunk_sign;
    a_key->sign_verify = dap_enc_chipmunk_verify;
    
    a_key->gen_bob_shared_key = NULL;
    a_key->gen_alice_shared_key = NULL;
    
    a_key->_inheritor = NULL;
    a_key->_inheritor_size = 0;
    a_key->_pvt = NULL;

    return 0;
}

// Key pair generation
int dap_enc_chipmunk_key_generate(dap_enc_key_t *a_key, size_t a_size) {
    dap_enc_chipmunk_key_t *l_key = (dap_enc_chipmunk_key_t *)a_key->priv_key_data;
    
    // Free old keys if they exist
    if (l_key->private_key) {
        DAP_DELETE(l_key->private_key);
    }
    if (l_key->public_key) {
        DAP_DELETE(l_key->public_key);
    }

    // Allocate memory for keys
    l_key->private_key = DAP_NEW_Z_SIZE(uint8_t, CHIPMUNK_PRIVATE_KEY_SIZE);
    l_key->public_key = DAP_NEW_Z_SIZE(uint8_t, CHIPMUNK_PUBLIC_KEY_SIZE);
    
    if (!l_key->private_key || !l_key->public_key) {
        log_it(L_CRITICAL, "Memory allocation error for keys");
        dap_enc_chipmunk_key_delete(a_key);
        return -1;
    }

    l_key->private_key_size = CHIPMUNK_PRIVATE_KEY_SIZE;
    l_key->public_key_size = CHIPMUNK_PUBLIC_KEY_SIZE;

    // Generate key pair using Chipmunk algorithm
    if (chipmunk_keypair(l_key->public_key, l_key->private_key) != 0) {
        log_it(L_ERROR, "Chipmunk keypair generation failed");
        dap_enc_chipmunk_key_delete(a_key);
        return -2;
    }

    return 0;
}

// Message signing
int dap_enc_chipmunk_sign(dap_enc_key_t *a_key, const void *a_msg, size_t a_msg_size, void *a_sign, const size_t a_sign_size) {
    dap_enc_chipmunk_key_t *l_key = (dap_enc_chipmunk_key_t *)a_key->priv_key_data;
    
    if (!l_key->private_key || !a_msg || !a_sign) {
        log_it(L_ERROR, "Invalid parameters for Chipmunk signing");
        return -1;
    }

    if (a_sign_size < CHIPMUNK_SIGNATURE_SIZE) {
        log_it(L_ERROR, "Output buffer too small for Chipmunk signature");
        return -2;
    }

    // Sign message using Chipmunk algorithm
    int res = chipmunk_sign(l_key->private_key, a_msg, a_msg_size, a_sign);
    if (res != 0) {
        log_it(L_ERROR, "Chipmunk signature generation failed with code %d", res);
        return -3;
    }

    return 0;
}

// Signature verification
int dap_enc_chipmunk_verify(dap_enc_key_t *a_key, const void *a_msg, size_t a_msg_size, void *a_sign, const size_t a_sign_size) {
    dap_enc_chipmunk_key_t *l_key = (dap_enc_chipmunk_key_t *)a_key->priv_key_data;
    
    if (!l_key->public_key || !a_msg || !a_sign) {
        log_it(L_ERROR, "Invalid parameters for Chipmunk verification");
        return -1;
    }

    if (a_sign_size != CHIPMUNK_SIGNATURE_SIZE) {
        log_it(L_ERROR, "Invalid signature size for Chipmunk verification");
        return -2;
    }

    // Verify signature using Chipmunk algorithm
    int res = chipmunk_verify(l_key->public_key, a_msg, a_msg_size, a_sign);
    if (res != 0) {
        log_it(L_ERROR, "Chipmunk signature verification failed with code %d", res);
        return -3;
    }

    return 0;
}

// Calculate signature size
size_t dap_enc_chipmunk_calc_signature_size(void) {
    return CHIPMUNK_SIGNATURE_SIZE;
}

// Resource deallocation
void dap_enc_chipmunk_key_delete(dap_enc_key_t *a_key) {
    if (!a_key || !a_key->priv_key_data) {
        return;
    }

    dap_enc_chipmunk_key_t *l_key = (dap_enc_chipmunk_key_t *)a_key->priv_key_data;
    
    if (l_key->private_key) {
        DAP_DELETE(l_key->private_key);
    }
    if (l_key->public_key) {
        DAP_DELETE(l_key->public_key);
    }
    
    DAP_DELETE(l_key);
    a_key->priv_key_data = NULL;
} 