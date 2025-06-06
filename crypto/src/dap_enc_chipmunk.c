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
static bool s_debug_more = false;

// Initialize the Chipmunk module
int dap_enc_chipmunk_init(void)
{
    log_it(L_NOTICE, "Chipmunk algorithm initialized");
    return chipmunk_init();
}

// Allocate and initialize new private key
dap_enc_key_t *dap_enc_chipmunk_key_new(void)
{
    debug_if(s_debug_more, L_DEBUG, "dap_enc_chipmunk_key_new: Creating new Chipmunk key");
    
    // Allocate the key structure
    dap_enc_key_t *l_key = DAP_NEW_Z(dap_enc_key_t);
    if (!l_key) {
        log_it(L_ERROR, "Failed to allocate dap_enc_key_t structure");
        return NULL;
    }
    
    debug_if(s_debug_more, L_DEBUG, "✅ DAP_NEW_Z(dap_enc_key_t) successful, l_key = %p", (void*)l_key);
    
    // Basic corruption check - verify memory is properly zeroed
    if (l_key->priv_key_data_size != 0 || l_key->pub_key_data_size != 0 ||
        l_key->priv_key_data != NULL || l_key->pub_key_data != NULL) {
        log_it(L_ERROR, "❌ CORRUPTION DETECTED IMMEDIATELY AFTER DAP_NEW_Z!");
        DAP_DELETE(l_key);
        return NULL;
    }

    // Set key type and management functions
    l_key->type = DAP_ENC_KEY_TYPE_SIG_CHIPMUNK;
    l_key->dec_na = 0;
    l_key->enc_na = 0;
    l_key->sign_get = dap_enc_chipmunk_get_sign;
    l_key->sign_verify = dap_enc_chipmunk_verify_sign;
    
    l_key->priv_key_data_size = CHIPMUNK_PRIVATE_KEY_SIZE;
    l_key->pub_key_data_size = CHIPMUNK_PUBLIC_KEY_SIZE;
    
    // Quick check after setting sizes
    if (l_key->priv_key_data_size != CHIPMUNK_PRIVATE_KEY_SIZE || 
        l_key->pub_key_data_size != CHIPMUNK_PUBLIC_KEY_SIZE) {
        log_it(L_ERROR, "❌ CORRUPTION DETECTED AFTER SIZE ASSIGNMENT!");
        DAP_DELETE(l_key);
        return NULL;
    }
    
    // Allocate memory for keys
    l_key->priv_key_data = DAP_NEW_Z_SIZE(uint8_t, l_key->priv_key_data_size);
    if (!l_key->priv_key_data) {
        log_it(L_ERROR, "Failed to allocate memory for private key");
        DAP_DELETE(l_key);
        return NULL;
    }
    
    l_key->pub_key_data = DAP_NEW_Z_SIZE(uint8_t, l_key->pub_key_data_size);
    if (!l_key->pub_key_data) {
        log_it(L_ERROR, "Failed to allocate memory for public key");
        DAP_DELETE(l_key->priv_key_data);
        DAP_DELETE(l_key);
        return NULL;
    }
    
    // Final check before chipmunk_keypair
    if (l_key->priv_key_data_size != CHIPMUNK_PRIVATE_KEY_SIZE || 
        l_key->pub_key_data_size != CHIPMUNK_PUBLIC_KEY_SIZE) {
        log_it(L_ERROR, "❌ CORRUPTION after key allocation!");
        DAP_DELETE(l_key->priv_key_data);
        DAP_DELETE(l_key->pub_key_data);
        DAP_DELETE(l_key);
        return NULL;
    }
    
    // Generate Chipmunk keypair
    debug_if(s_debug_more, L_DEBUG, "Calling chipmunk_keypair");

    
    int ret = chipmunk_keypair(l_key->pub_key_data, l_key->pub_key_data_size,
                              l_key->priv_key_data, l_key->priv_key_data_size);
    

    
    // Check if chipmunk_keypair succeeded
    if (ret != 0) {
        log_it(L_ERROR, "chipmunk_keypair failed with error %d", ret);
        DAP_DELETE(l_key->priv_key_data);
        DAP_DELETE(l_key->pub_key_data);
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
    
    debug_if(s_debug_more, L_DEBUG, "dap_enc_chipmunk_key_generate: seed=%p, seed_size=%zu", seed, seed_size);
    
    // Если seed не предоставлен или имеет неправильный размер, используем случайную генерацию
    if (!seed || seed_size < 32) {
        debug_if(s_debug_more, L_DEBUG, "No valid seed provided, using random key generation");
        return dap_enc_chipmunk_key_new();
    }
    
    // Используем детерминированную генерацию с предоставленным seed
    debug_if(s_debug_more, L_DEBUG, "Using deterministic key generation with provided seed");
    
    // Создаем структуру ключа
    dap_enc_key_t *l_key = DAP_NEW_Z(dap_enc_key_t);
    if (!l_key) {
        log_it(L_ERROR, "Failed to allocate dap_enc_key_t structure");
        return NULL;
    }
    
    // Настраиваем ключ
    l_key->type = DAP_ENC_KEY_TYPE_SIG_CHIPMUNK;
    l_key->dec_na = 0;
    l_key->enc_na = 0;
    l_key->sign_get = dap_enc_chipmunk_get_sign;
    l_key->sign_verify = dap_enc_chipmunk_verify_sign;
    l_key->priv_key_data_size = CHIPMUNK_PRIVATE_KEY_SIZE;
    l_key->pub_key_data_size = CHIPMUNK_PUBLIC_KEY_SIZE;
    
    // Выделяем память для ключей
    l_key->priv_key_data = DAP_NEW_Z_SIZE(uint8_t, l_key->priv_key_data_size);
    l_key->pub_key_data = DAP_NEW_Z_SIZE(uint8_t, l_key->pub_key_data_size);
    
    if (!l_key->priv_key_data || !l_key->pub_key_data) {
        log_it(L_ERROR, "Failed to allocate memory for key data");
        if (l_key->priv_key_data) DAP_DELETE(l_key->priv_key_data);
        if (l_key->pub_key_data) DAP_DELETE(l_key->pub_key_data);
        DAP_DELETE(l_key);
        return NULL;
    }
    
    // Используем первые 32 байта seed'а для детерминированной генерации
    uint8_t l_key_seed[32];
    memcpy(l_key_seed, seed, 32);
    
    debug_if(s_debug_more, L_DEBUG, "Calling chipmunk_keypair_from_seed with seed %02x%02x%02x%02x...", 
             l_key_seed[0], l_key_seed[1], l_key_seed[2], l_key_seed[3]);
    
    // Генерируем ключи детерминированно
    int ret = chipmunk_keypair_from_seed(l_key_seed,
                                         l_key->pub_key_data, l_key->pub_key_data_size,
                                         l_key->priv_key_data, l_key->priv_key_data_size);
    
    if (ret != 0) {
        log_it(L_ERROR, "chipmunk_keypair_from_seed failed with error %d", ret);
        DAP_DELETE(l_key->priv_key_data);
        DAP_DELETE(l_key->pub_key_data);
        DAP_DELETE(l_key);
        return NULL;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Successfully generated deterministic Chipmunk keypair");
    return l_key;
}

// Get signature size
size_t dap_enc_chipmunk_calc_signature_size(void)
{
    return CHIPMUNK_SIGNATURE_SIZE;
}

// Get signature size for callback (accepts key parameter)
uint64_t dap_enc_chipmunk_deser_sig_size(const void *a_key)
{
    (void)a_key; // Unused parameter
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
        log_it(L_ERROR, "Invalid parameters in dap_enc_chipmunk_get_sign");
        return -1;
    }

    if (!a_key->priv_key_data) {
        log_it(L_ERROR, "No private key data in dap_enc_chipmunk_get_sign");
        return -1;
    }

    debug_if(s_debug_more, L_DEBUG, "Calling chipmunk_sign");
    int result = chipmunk_sign(a_key->priv_key_data, a_data, a_data_size, a_signature);
    
    if (result != 0) {
        log_it(L_ERROR, "Chipmunk signature creation failed with code %d", result);
        return -2;  // Consistent error code for sign failures
    }
    
    debug_if(s_debug_more, L_DEBUG, "Chipmunk signature created successfully");
    return 0;
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
        log_it(L_ERROR, "Invalid parameters in dap_enc_chipmunk_verify_sign");
        return -1;
    }

    debug_if(s_debug_more, L_DEBUG, "Calling chipmunk_verify");
    int result = chipmunk_verify(key->pub_key_data, data, data_size, signature);
    
    if (result != 0) {
        debug_if(s_debug_more, L_DEBUG, "Signature verification failed with code %d", result);
        return -2;  // Consistent error code for verify failures
    }
    
    debug_if(s_debug_more, L_DEBUG, "Chipmunk signature verified successfully");
    return 0;
}

// Clean up key data, remove key pair
void dap_enc_chipmunk_key_delete(dap_enc_key_t *a_key)
{
    debug_if(s_debug_more, L_DEBUG, "dap_enc_chipmunk_key_delete: Deleting Chipmunk key at %p", 
             (void*)a_key);
    
    if (!a_key) {
        log_it(L_ERROR, "dap_enc_chipmunk_key_delete: NULL key passed");
        return;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Deleting Chipmunk key at %p", (void*)a_key);
    
    // Освобождаем открытый ключ
    if (a_key->pub_key_data) {
        DAP_DELETE(a_key->pub_key_data);
        a_key->pub_key_data = NULL;
        a_key->pub_key_data_size = 0;
        debug_if(s_debug_more, L_DEBUG, "dap_enc_chipmunk_key_delete: Public key data freed");
    }
    
    // Освобождаем закрытый ключ
    if (a_key->priv_key_data) {
        DAP_DELETE(a_key->priv_key_data);
        a_key->priv_key_data = NULL;
        a_key->priv_key_data_size = 0;
        debug_if(s_debug_more, L_DEBUG, "dap_enc_chipmunk_key_delete: private key data freed");
    }
    
    debug_if(s_debug_more, L_DEBUG, "dap_enc_chipmunk_key_delete: Chipmunk key deletion completed");
}

// Serialization functions for private and public keys
uint8_t *dap_enc_chipmunk_write_private_key(const void *a_key, size_t *a_buflen_out)
{
    log_it(L_INFO, "=== CORRUPTION INVESTIGATION: dap_enc_chipmunk_write_private_key ===");
    log_it(L_INFO, "Function called with a_key = %p", a_key);
    
    if (!a_key) {
        log_it(L_ERROR, "❌ a_key is NULL!");
        return NULL;
    }
    
    if (!a_buflen_out) {
        log_it(L_ERROR, "❌ a_buflen_out is NULL!");
        return NULL;
    }
    
    // Log current state of a_buflen_out
    log_it(L_INFO, "Initial *a_buflen_out = %zu", *a_buflen_out);
    
    const chipmunk_private_key_t* l_private_key = (const chipmunk_private_key_t*) a_key;
    
    // Log address and first few bytes of the key structure
    log_it(L_INFO, "l_private_key address: %p", (void*)l_private_key);
    log_it(L_INFO, "chipmunk_private_key_t structure size: %zu", sizeof(chipmunk_private_key_t));
    
    // Check if the address is reasonable 
    if ((uintptr_t)l_private_key < 0x1000 || (uintptr_t)l_private_key > 0x7fffffffffff) {
        log_it(L_ERROR, "❌ Suspicious private key address: %p", (void*)l_private_key);
        log_it(L_ERROR, "This looks like corrupted memory or invalid pointer!");
        return NULL;
    }
    
    // Memory pattern check
    const uint8_t *check_ptr = (const uint8_t*)l_private_key;
    log_it(L_INFO, "First 16 bytes of private key structure: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
           check_ptr[0], check_ptr[1], check_ptr[2], check_ptr[3],
           check_ptr[4], check_ptr[5], check_ptr[6], check_ptr[7],
           check_ptr[8], check_ptr[9], check_ptr[10], check_ptr[11],
           check_ptr[12], check_ptr[13], check_ptr[14], check_ptr[15]);
    
    log_it(L_INFO, "About to access private key structure...");
    log_it(L_INFO, "=========================================");
    
    *a_buflen_out = sizeof(chipmunk_private_key_t);
    uint8_t *l_buf = DAP_NEW_SIZE(uint8_t, *a_buflen_out);
    if (!l_buf) {
        log_it(L_ERROR, "Failed to allocate memory for private key serialization");
        return NULL;
    }
    
    memcpy(l_buf, l_private_key, *a_buflen_out);
    return l_buf;
}

uint8_t *dap_enc_chipmunk_write_public_key(const void *a_key, size_t *a_buflen_out)
{
    log_it(L_INFO, "=== CORRUPTION INVESTIGATION: dap_enc_chipmunk_write_public_key ===");
    log_it(L_INFO, "Function called with a_key = %p", a_key);
    
    if (!a_key) {
        log_it(L_ERROR, "❌ a_key is NULL!");
        return NULL;
    }
    
    if (!a_buflen_out) {
        log_it(L_ERROR, "❌ a_buflen_out is NULL!");
        return NULL;
    }
    
    // Log current state of a_buflen_out
    log_it(L_INFO, "Initial *a_buflen_out = %zu", *a_buflen_out);
    
    const chipmunk_public_key_t* l_public_key = (const chipmunk_public_key_t*) a_key;
    
    // Log address and first few bytes of the key structure
    log_it(L_INFO, "l_public_key address: %p", (void*)l_public_key);
    log_it(L_INFO, "chipmunk_public_key_t structure size: %zu", sizeof(chipmunk_public_key_t));
    
    // Check if the address is reasonable 
    if ((uintptr_t)l_public_key < 0x1000 || (uintptr_t)l_public_key > 0x7fffffffffff) {
        log_it(L_ERROR, "❌ Suspicious public key address: %p", (void*)l_public_key);
        log_it(L_ERROR, "This looks like corrupted memory or invalid pointer!");
        return NULL;
    }
    
    // Memory pattern check
    const uint8_t *check_ptr = (const uint8_t*)l_public_key;
    log_it(L_INFO, "First 16 bytes of public key structure: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
           check_ptr[0], check_ptr[1], check_ptr[2], check_ptr[3],
           check_ptr[4], check_ptr[5], check_ptr[6], check_ptr[7],
           check_ptr[8], check_ptr[9], check_ptr[10], check_ptr[11],
           check_ptr[12], check_ptr[13], check_ptr[14], check_ptr[15]);
    
    log_it(L_INFO, "About to access public key structure...");
    log_it(L_INFO, "=========================================");
    *a_buflen_out = sizeof(chipmunk_public_key_t);
    uint8_t *l_buf = DAP_NEW_SIZE(uint8_t, *a_buflen_out);
    if (!l_buf) {
        log_it(L_ERROR, "Memory allocation failed for public key serialization, size = %zu", *a_buflen_out);
        return NULL;
    }
    
    memcpy(l_buf, l_public_key, *a_buflen_out);
    return l_buf;
}

uint64_t dap_enc_chipmunk_ser_private_key_size(const void *a_key)
{
    const dap_enc_key_t *l_key = (const dap_enc_key_t *)a_key;
    return l_key ? l_key->priv_key_data_size : 0;
}

uint64_t dap_enc_chipmunk_ser_public_key_size(const void *a_key)
{
    const dap_enc_key_t *l_key = (const dap_enc_key_t *)a_key;
    return l_key ? l_key->pub_key_data_size : 0;
}

void* dap_enc_chipmunk_read_private_key(const uint8_t *a_buf, size_t a_buflen)
{
    if (!a_buf || a_buflen != CHIPMUNK_PRIVATE_KEY_SIZE) {
        log_it(L_ERROR, "Invalid buffer for private key deserialization");
        return NULL;
    }
    
    uint8_t *l_key = DAP_NEW_SIZE(uint8_t, a_buflen);
    if (!l_key) {
        log_it(L_ERROR, "Memory allocation failed for private key deserialization");
        return NULL;
    }
    
    memcpy(l_key, a_buf, a_buflen);
    return l_key;
}

void* dap_enc_chipmunk_read_public_key(const uint8_t *a_buf, size_t a_buflen)
{
    if (!a_buf || a_buflen != CHIPMUNK_PUBLIC_KEY_SIZE) {
        log_it(L_ERROR, "Invalid buffer for public key deserialization");
        return NULL;
    }
    
    uint8_t *l_key = DAP_NEW_SIZE(uint8_t, a_buflen);
    if (!l_key) {
        log_it(L_ERROR, "Memory allocation failed for public key deserialization");
        return NULL;
    }
    
    memcpy(l_key, a_buf, a_buflen);
    return l_key;
}

uint64_t dap_enc_chipmunk_deser_private_key_size(const void *unused)
{
    (void)unused;
    return CHIPMUNK_PRIVATE_KEY_SIZE;
}

uint64_t dap_enc_chipmunk_deser_public_key_size(const void *unused)
{
    (void)unused;
    return CHIPMUNK_PUBLIC_KEY_SIZE;
}

// Signature serialization/deserialization functions
uint8_t *dap_enc_chipmunk_write_signature(const void *a_sign, size_t *a_sign_len)
{
    if (!a_sign || !a_sign_len) {
        log_it(L_ERROR, "Invalid parameters for signature serialization");
        return NULL;
    }
    
    *a_sign_len = CHIPMUNK_SIGNATURE_SIZE;
    uint8_t *l_buf = DAP_NEW_SIZE(uint8_t, *a_sign_len);
    if (!l_buf) {
        log_it(L_ERROR, "Memory allocation failed for signature serialization");
        return NULL;
    }
    
    memcpy(l_buf, a_sign, *a_sign_len);
    return l_buf;
}

uint8_t *dap_enc_chipmunk_read_signature(const uint8_t *a_buf, size_t a_buflen)
{
    if (!a_buf || a_buflen != CHIPMUNK_SIGNATURE_SIZE) {
        log_it(L_ERROR, "Invalid buffer for signature deserialization");
        return NULL;
    }
    
    uint8_t *l_sign = DAP_NEW_SIZE(uint8_t, a_buflen);
    if (!l_sign) {
        log_it(L_ERROR, "Memory allocation failed for signature deserialization");
        return NULL;
    }
    
    memcpy(l_sign, a_buf, a_buflen);
    return l_sign;
}

// Delete functions for memory cleanup
void dap_enc_chipmunk_public_key_delete(void *a_pub_key)
{
    if (a_pub_key) {
        DAP_DELETE(a_pub_key);
    }
}

void dap_enc_chipmunk_private_key_delete(void *a_priv_key)
{
    if (a_priv_key) {
        DAP_DELETE(a_priv_key);
    }
}

void dap_enc_chipmunk_signature_delete(void *a_signature)
{
    // Note: This callback should only clean up the CONTENTS of the signature,
    // not the signature buffer itself. The main dap_enc_key_signature_delete()
    // function will handle freeing the buffer with DAP_DEL_Z().
    // For Chipmunk, the signature is a simple binary blob, so no internal cleanup needed.
    (void)a_signature; // Suppress unused parameter warning
}
