#include "dap_common.h"
#include "dap_enc_chipmunk.h"
#include "dap_enc_key.h"
#include "dap_sign.h"
#include "chipmunk/chipmunk.h"
#include "dap_hash.h"
#include "rand/dap_rand.h"
#include <string.h>

#define LOG_TAG "dap_enc_chipmunk"

// Флаг для расширенного логирования
static bool s_debug_more = true;

// Initialize the Chipmunk module
int dap_enc_chipmunk_init(void)
{
    log_it(L_NOTICE, "Chipmunk algorithm initialized");
    return chipmunk_init();
}

// Allocate and initialize new private key
dap_enc_key_t *dap_enc_chipmunk_key_new(void)
{
    // Log debug message for key generation
    debug_if(s_debug_more, L_INFO, "dap_enc_chipmunk_key_new: Starting to generate Chipmunk key pair");

    // Create key instance
    dap_enc_key_t *l_key = DAP_NEW_Z(dap_enc_key_t);
    if (!l_key) {
        log_it(L_CRITICAL, "Memory allocation error for key structure!");
        return NULL;
    }

    debug_if(s_debug_more, L_DEBUG, "Created dap_enc_key_t structure at %p", (void*)l_key);
    debug_if(s_debug_more, L_DEBUG, "dap_enc_chipmunk_key_new: Allocated main key structure at %p", (void*)l_key);

    // Set key type and management functions
    l_key->type = DAP_ENC_KEY_TYPE_SIG_CHIPMUNK;
    l_key->dec_na = 0;
    l_key->enc_na = 0;
    l_key->sign_get = dap_enc_chipmunk_get_sign;
    l_key->sign_verify = dap_enc_chipmunk_verify_sign;


    // Generate a real keypair using Chipmunk algorithm
    debug_if(s_debug_more,L_DEBUG, "dap_enc_chipmunk_key_new: Calling chipmunk_keypair");

    // Create the public key separately
    l_key->key_pub = DAP_NEW_SIZE(byte_t, CHIPMUNK_PUBLIC_KEY_SIZE);
    if (!l_key->key_pub) {
        log_it(L_CRITICAL, "Memory allocation error for public key data!");
        DAP_DELETE(l_key);
        return NULL;
    }

    // Create the public key separately
    l_key->key_pvt = DAP_NEW_SIZE(byte_t, CHIPMUNK_PRIVATE_KEY_SIZE );
    if (!l_key->key_pvt) {
        log_it(L_CRITICAL, "Memory allocation error for public key data!");
        DAP_DELETE(l_key);
        return NULL;
    }

    
    int l_result = chipmunk_keypair(l_key->key_pub, CHIPMUNK_PUBLIC_KEY_SIZE,
                       l_key->key_pvt, CHIPMUNK_PRIVATE_KEY_SIZE);
                       
    if (l_result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to generate Chipmunk keypair, error code: %d", l_result);
        DAP_DELETE(l_key->key_pvt);
        DAP_DELETE(l_key);
        return NULL;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Successfully generated Chipmunk keypair");
    
    return l_key;
}

// Create key from provided seed
dap_enc_key_t *dap_enc_chipmunk_key_generate(
        const void *kex_buf, size_t kex_size,
        const void *seed, size_t seed_size,
        const void *key_n, size_t key_n_size)
{
    (void) kex_buf; (void) kex_size; // Unused
    (void) key_n; (void) key_n_size; // Unused
    (void) seed; (void) seed_size;   // Currently unused, could implement deterministic key generation

    // For now, just generate a new random key regardless of seed
    return dap_enc_chipmunk_key_new();
}

// Get signature size
size_t dap_enc_chipmunk_calc_signature_size(void)
{
    return CHIPMUNK_SIGNATURE_SIZE;
}

// Sign data using Chipmunk algorithm
int dap_enc_chipmunk_get_sign(dap_enc_key_t *a_key, const void *a_data, const size_t a_data_size, void *a_signature,
                           const size_t a_signature_size)
{
    if (a_signature_size < CHIPMUNK_SIGNATURE_SIZE) {
        log_it(L_ERROR, "Signature size too small (expected %d, provided %zu)", 
               CHIPMUNK_SIGNATURE_SIZE, a_signature_size);
        return -1;
    }

    if (!a_key || !a_data || !a_signature || !a_data_size) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }

    if (!a_key->key_pvt ) {
        log_it(L_ERROR, "No private key data");
        return -1;
    }

    // Use actual chipmunk signing algorithm
    int result = chipmunk_sign(a_key->key_pvt, a_data, a_data_size, a_signature);
    if (result != 0) {
        log_it(L_ERROR, "Chipmunk signature creation failed with code %d", result);
        return -1;
    }

    return CHIPMUNK_SIGNATURE_SIZE;
}

// Verify signature using Chipmunk algorithm
int dap_enc_chipmunk_verify_sign(dap_enc_key_t *key, const void *data, const size_t data_size, void *signature, 
                              const size_t signature_size)
{
    if (signature_size < CHIPMUNK_SIGNATURE_SIZE) {
        log_it(L_ERROR, "Signature size too small (expected %d, provided %zu)", 
               CHIPMUNK_SIGNATURE_SIZE, signature_size);
        return -1;
    }

    if (!key || !key->pub_key_data || !data || !signature || !data_size) {
        log_it(L_ERROR, "Invalid parameters");
        return -1;
    }

    // Use actual chipmunk verification algorithm
    int result = chipmunk_verify(key->pub_key_data, data, data_size, signature);
    if (result != 0) {
        log_it(L_ERROR, "Chipmunk signature verification failed with code %d", result);
        return -1;
    }

    return 0; // Success
}

// Delete the key
void dap_enc_chipmunk_key_delete(dap_enc_key_t *a_key)
{
    if (!a_key) {
        log_it(L_ERROR, "dap_enc_chipmunk_key_delete: Invalid key pointer provided (NULL)");
        return;
    }

    log_it(L_DEBUG, "dap_enc_chipmunk_key_delete: Deleting Chipmunk key at %p", (void*)a_key);
    debug_if(s_debug_more, L_DEBUG, "Deleting Chipmunk key at %p", (void*)a_key);

    // Cleanup public key
    if (a_key->key_pub) {
        DAP_DELETE(a_key->key_pub);
        a_key->key_pub = NULL;  // Устанавливаем NULL после удаления, чтобы предотвратить двойное освобождение
        log_it(L_DEBUG, "dap_enc_chipmunk_key_delete: Public key data freed");
    } else {
        log_it(L_DEBUG, "dap_enc_chipmunk_key_delete: Public key data was NULL, nothing to free");
    }

    // Cleanup private
    if (a_key->key_pvt) {
        DAP_DELETE(a_key->key_pvt);
        a_key->key_pvt = NULL;  // Устанавливаем NULL после удаления, чтобы предотвратить двойное освобождение
        log_it(L_DEBUG, "dap_enc_chipmunk_key_delete: private key data freed");
    } else {
        log_it(L_DEBUG, "dap_enc_chipmunk_key_delete: private key data was NULL, nothing to free");
    }


    log_it(L_DEBUG, "dap_enc_chipmunk_key_delete: Chipmunk key deletion completed");
    debug_if(s_debug_more, L_DEBUG, "Chipmunk key deletion completed");
} 
