#include "dap_common.h"
#include "dap_memwipe.h"
#include "dap_enc_chipmunk.h"
#include "dap_enc_key.h"
#include "dap_sign.h"
#include "chipmunk/chipmunk.h"
#include "dap_hash.h"
#include "dap_rand.h"
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
    dap_enc_key_t *l_key = DAP_NEW_Z(dap_enc_key_t);
    if (!l_key) {
        log_it(L_ERROR, "Failed to allocate dap_enc_key_t structure");
        return NULL;
    }

    l_key->type = DAP_ENC_KEY_TYPE_SIG_CHIPMUNK;
    l_key->dec_na = 0;
    l_key->enc_na = 0;
    l_key->sign_get = dap_enc_chipmunk_get_sign;
    l_key->sign_verify = dap_enc_chipmunk_verify_sign;
    l_key->priv_key_data_size = CHIPMUNK_PRIVATE_KEY_SIZE;
    l_key->pub_key_data_size = CHIPMUNK_PUBLIC_KEY_SIZE;

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

    int ret = chipmunk_keypair(l_key->pub_key_data, l_key->pub_key_data_size,
                               l_key->priv_key_data, l_key->priv_key_data_size);
    if (ret != 0) {
        log_it(L_ERROR, "chipmunk_keypair failed with error %d", ret);
        DAP_DELETE(l_key->priv_key_data);
        DAP_DELETE(l_key->pub_key_data);
        DAP_DELETE(l_key);
        return NULL;
    }

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
    dap_return_if_pass(!a_key);
    DAP_DEL_Z(a_key->pub_key_data);
    a_key->pub_key_data_size = 0;
    DAP_WIPE_AND_FREE(a_key->priv_key_data, a_key->priv_key_data_size);
    a_key->priv_key_data_size = 0;
}

/*
 * CR-D20 fix (Round-3): framed wire format for private/public keys.
 *
 *  Offset  Size  Field
 *  ------  ----  -----
 *    0      4    magic    = "CHMP" (0x43, 0x48, 0x4D, 0x50)
 *    4      2    version  = little-endian uint16, 0x0001
 *    6      2    reserved = 0x0000 (future flags / parameter set id)
 *    8      4    payload_len = little-endian uint32
 *   12      N    payload bytes (canonical Chipmunk encoding)
 *
 * Rationale:
 *  * Previous implementation copied the raw in-memory struct via memcpy.
 *    That format was endian-sensitive, had no version/length prefix, and
 *    was indistinguishable from attacker-controlled buffers of the same
 *    size — a deliberately malformed blob could survive deserialization
 *    without any integrity check.
 *  * `priv_key_data` / `pub_key_data` already hold the canonical
 *    byte-serialised form produced by chipmunk_{private,public}_key_to_bytes,
 *    so this framing layer is purely a wire-level wrapper and introduces
 *    no new in-memory representation.
 */
#define DAP_ENC_CHIPMUNK_WIRE_MAGIC_0 0x43u /* 'C' */
#define DAP_ENC_CHIPMUNK_WIRE_MAGIC_1 0x48u /* 'H' */
#define DAP_ENC_CHIPMUNK_WIRE_MAGIC_2 0x4Du /* 'M' */
#define DAP_ENC_CHIPMUNK_WIRE_MAGIC_3 0x50u /* 'P' */
#define DAP_ENC_CHIPMUNK_WIRE_VERSION_V1 0x0001u
#define DAP_ENC_CHIPMUNK_WIRE_HEADER_SIZE 12u

static inline void s_chipmunk_wire_put_u16_le(uint8_t *a_out, uint16_t a_value)
{
    a_out[0] = (uint8_t)(a_value & 0xFFu);
    a_out[1] = (uint8_t)((a_value >> 8) & 0xFFu);
}

static inline void s_chipmunk_wire_put_u32_le(uint8_t *a_out, uint32_t a_value)
{
    a_out[0] = (uint8_t)(a_value & 0xFFu);
    a_out[1] = (uint8_t)((a_value >> 8) & 0xFFu);
    a_out[2] = (uint8_t)((a_value >> 16) & 0xFFu);
    a_out[3] = (uint8_t)((a_value >> 24) & 0xFFu);
}

static inline uint16_t s_chipmunk_wire_get_u16_le(const uint8_t *a_in)
{
    return (uint16_t)((uint16_t)a_in[0] | ((uint16_t)a_in[1] << 8));
}

static inline uint32_t s_chipmunk_wire_get_u32_le(const uint8_t *a_in)
{
    return (uint32_t)a_in[0] |
           ((uint32_t)a_in[1] << 8) |
           ((uint32_t)a_in[2] << 16) |
           ((uint32_t)a_in[3] << 24);
}

static uint8_t *s_chipmunk_wire_pack(const uint8_t *a_payload, size_t a_payload_size, size_t *a_buflen_out,
                                     bool a_wipe_payload_on_error)
{
    if (!a_payload || !a_buflen_out || a_payload_size == 0 || a_payload_size > UINT32_MAX) {
        if (a_buflen_out) *a_buflen_out = 0;
        return NULL;
    }
    size_t l_total = DAP_ENC_CHIPMUNK_WIRE_HEADER_SIZE + a_payload_size;
    uint8_t *l_buf = DAP_NEW_SIZE(uint8_t, l_total);
    if (!l_buf) {
        log_it(L_ERROR, "Failed to allocate %zu bytes for Chipmunk wire frame", l_total);
        *a_buflen_out = 0;
        return NULL;
    }

    l_buf[0] = DAP_ENC_CHIPMUNK_WIRE_MAGIC_0;
    l_buf[1] = DAP_ENC_CHIPMUNK_WIRE_MAGIC_1;
    l_buf[2] = DAP_ENC_CHIPMUNK_WIRE_MAGIC_2;
    l_buf[3] = DAP_ENC_CHIPMUNK_WIRE_MAGIC_3;
    s_chipmunk_wire_put_u16_le(l_buf + 4, DAP_ENC_CHIPMUNK_WIRE_VERSION_V1);
    s_chipmunk_wire_put_u16_le(l_buf + 6, 0x0000u);
    s_chipmunk_wire_put_u32_le(l_buf + 8, (uint32_t)a_payload_size);
    memcpy(l_buf + DAP_ENC_CHIPMUNK_WIRE_HEADER_SIZE, a_payload, a_payload_size);

    (void)a_wipe_payload_on_error; /* retained for future expansion */
    *a_buflen_out = l_total;
    return l_buf;
}

static int s_chipmunk_wire_parse(const uint8_t *a_buf, size_t a_buflen,
                                 size_t a_expected_payload_size,
                                 const uint8_t **a_payload_out)
{
    if (!a_buf || !a_payload_out) {
        return -1;
    }
    if (a_buflen < DAP_ENC_CHIPMUNK_WIRE_HEADER_SIZE) {
        log_it(L_ERROR, "Chipmunk wire frame truncated: got %zu bytes, need at least %u",
               a_buflen, DAP_ENC_CHIPMUNK_WIRE_HEADER_SIZE);
        return -1;
    }
    if (a_buf[0] != DAP_ENC_CHIPMUNK_WIRE_MAGIC_0 ||
        a_buf[1] != DAP_ENC_CHIPMUNK_WIRE_MAGIC_1 ||
        a_buf[2] != DAP_ENC_CHIPMUNK_WIRE_MAGIC_2 ||
        a_buf[3] != DAP_ENC_CHIPMUNK_WIRE_MAGIC_3) {
        log_it(L_ERROR, "Chipmunk wire frame: bad magic (expected 'CHMP'); legacy or corrupted key format");
        return -1;
    }
    uint16_t l_version = s_chipmunk_wire_get_u16_le(a_buf + 4);
    if (l_version != DAP_ENC_CHIPMUNK_WIRE_VERSION_V1) {
        log_it(L_ERROR, "Chipmunk wire frame: unsupported version 0x%04x (expected 0x%04x)",
               l_version, DAP_ENC_CHIPMUNK_WIRE_VERSION_V1);
        return -1;
    }
    uint16_t l_reserved = s_chipmunk_wire_get_u16_le(a_buf + 6);
    if (l_reserved != 0x0000u) {
        log_it(L_ERROR, "Chipmunk wire frame: reserved field must be zero (got 0x%04x)", l_reserved);
        return -1;
    }
    uint32_t l_payload_len = s_chipmunk_wire_get_u32_le(a_buf + 8);
    if (l_payload_len != a_expected_payload_size) {
        log_it(L_ERROR, "Chipmunk wire frame: payload length mismatch (got %u, expected %zu)",
               l_payload_len, a_expected_payload_size);
        return -1;
    }
    if (a_buflen != (size_t)DAP_ENC_CHIPMUNK_WIRE_HEADER_SIZE + (size_t)l_payload_len) {
        log_it(L_ERROR, "Chipmunk wire frame: total length mismatch (got %zu, expected %zu)",
               a_buflen, (size_t)DAP_ENC_CHIPMUNK_WIRE_HEADER_SIZE + (size_t)l_payload_len);
        return -1;
    }

    *a_payload_out = a_buf + DAP_ENC_CHIPMUNK_WIRE_HEADER_SIZE;
    return 0;
}

// Serialization functions for private and public keys
uint8_t *dap_enc_chipmunk_write_private_key(const void *a_key, size_t *a_buflen_out)
{
    if (!a_key || !a_buflen_out) {
        if (a_buflen_out) *a_buflen_out = 0;
        return NULL;
    }
    return s_chipmunk_wire_pack((const uint8_t *)a_key, CHIPMUNK_PRIVATE_KEY_SIZE, a_buflen_out, true);
}

uint8_t *dap_enc_chipmunk_write_public_key(const void *a_key, size_t *a_buflen_out)
{
    if (!a_key || !a_buflen_out) {
        if (a_buflen_out) *a_buflen_out = 0;
        return NULL;
    }
    return s_chipmunk_wire_pack((const uint8_t *)a_key, CHIPMUNK_PUBLIC_KEY_SIZE, a_buflen_out, false);
}

uint64_t dap_enc_chipmunk_ser_private_key_size(const void *a_key)
{
    (void)a_key;
    return (uint64_t)DAP_ENC_CHIPMUNK_WIRE_HEADER_SIZE + (uint64_t)CHIPMUNK_PRIVATE_KEY_SIZE;
}

uint64_t dap_enc_chipmunk_ser_public_key_size(const void *a_key)
{
    (void)a_key;
    return (uint64_t)DAP_ENC_CHIPMUNK_WIRE_HEADER_SIZE + (uint64_t)CHIPMUNK_PUBLIC_KEY_SIZE;
}

void* dap_enc_chipmunk_read_private_key(const uint8_t *a_buf, size_t a_buflen)
{
    const uint8_t *l_payload = NULL;
    if (s_chipmunk_wire_parse(a_buf, a_buflen, CHIPMUNK_PRIVATE_KEY_SIZE, &l_payload) != 0) {
        log_it(L_ERROR, "Invalid framed buffer for Chipmunk private key deserialization");
        return NULL;
    }

    uint8_t *l_key = DAP_NEW_SIZE(uint8_t, CHIPMUNK_PRIVATE_KEY_SIZE);
    if (!l_key) {
        log_it(L_ERROR, "Memory allocation failed for private key deserialization");
        return NULL;
    }

    memcpy(l_key, l_payload, CHIPMUNK_PRIVATE_KEY_SIZE);
    return l_key;
}

void* dap_enc_chipmunk_read_public_key(const uint8_t *a_buf, size_t a_buflen)
{
    const uint8_t *l_payload = NULL;
    if (s_chipmunk_wire_parse(a_buf, a_buflen, CHIPMUNK_PUBLIC_KEY_SIZE, &l_payload) != 0) {
        log_it(L_ERROR, "Invalid framed buffer for Chipmunk public key deserialization");
        return NULL;
    }

    uint8_t *l_key = DAP_NEW_SIZE(uint8_t, CHIPMUNK_PUBLIC_KEY_SIZE);
    if (!l_key) {
        log_it(L_ERROR, "Memory allocation failed for public key deserialization");
        return NULL;
    }

    memcpy(l_key, l_payload, CHIPMUNK_PUBLIC_KEY_SIZE);
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

void *dap_enc_chipmunk_read_signature(const uint8_t *a_buf, size_t a_buflen)
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
    return (void*)l_sign;
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
