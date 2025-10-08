#pragma once

#include <stddef.h>
#include <stdint.h>
#include "dap_enc_key.h"
#include "chipmunk/chipmunk.h"

#ifdef __cplusplus
extern "C" {
#endif
// Chipmunk algorithm key type identifier
#define DAP_ENC_KEY_TYPE_SIG_CHIPMUNK 0x0108


// Function prototypes

/**
 * @brief Initialize Chipmunk algorithm module
 * @return Returns 0 on success, negative error code on failure
 */
int dap_enc_chipmunk_init(void);

/**
 * @brief Create new Chipmunk key pair
 * @param[out] a_public_key Output buffer for public key
 * @param[in] a_public_key_size Size of public key buffer
 * @param[out] a_private_key Output buffer for private key
 * @param[in] a_private_key_size Size of private key buffer
 * @return Returns 0 on success, negative error code on failure
 */
int chipmunk_keypair(uint8_t *a_public_key, size_t a_public_key_size,
                     uint8_t *a_private_key, size_t a_private_key_size);

/**
 * @brief Get size of Chipmunk signature
 * @return Size of the signature in bytes
 */
size_t dap_enc_chipmunk_calc_signature_size(void);

/**
 * @brief Get size of Chipmunk signature for deserialization callback
 * @param[in] a_key Key object (unused)
 * @return Size of the signature in bytes
 */
uint64_t dap_enc_chipmunk_deser_sig_size(const void *a_key);

/**
 * @brief Sign data with Chipmunk algorithm
 * @param[in] key Key object
 * @param[in] data Data to sign
 * @param[in] data_size Size of data
 * @param[out] signature Buffer for signature
 * @param[in] signature_size Size of signature buffer
 * @return 0 on success or negative error code
 */
int dap_enc_chipmunk_get_sign(dap_enc_key_t *key, const void *data, const size_t data_size, 
                           void *signature, const size_t signature_size);

/**
 * @brief Verify signature with Chipmunk algorithm
 * @param[in] key Key object
 * @param[in] data Original data
 * @param[in] data_size Size of data
 * @param[in] signature Signature to verify
 * @param[in] signature_size Size of signature
 * @return 0 if verification successful, negative error code otherwise
 */
int dap_enc_chipmunk_verify_sign(dap_enc_key_t *key, const void *data, const size_t data_size, 
                              void *signature, const size_t signature_size);


dap_enc_key_t *dap_enc_chipmunk_key_new();

// Create key from provided seed
dap_enc_key_t *dap_enc_chipmunk_key_generate(
        const void *kex_buf, size_t kex_size,
        const void *seed, size_t seed_size,
        const void *key_n, size_t key_n_size);

/**
 * @brief Delete Chipmunk key and free resources
 * @param[in] key Key object to delete
 */
void dap_enc_chipmunk_key_delete(dap_enc_key_t *key);

// Serialization functions
uint8_t* dap_enc_chipmunk_write_private_key(const void *a_key, size_t *a_buflen_out);
uint8_t* dap_enc_chipmunk_write_public_key(const void *a_key, size_t *a_buflen_out);
uint64_t dap_enc_chipmunk_ser_private_key_size(const void *a_key);
uint64_t dap_enc_chipmunk_ser_public_key_size(const void *a_key);

// Deserialization functions  
void* dap_enc_chipmunk_read_private_key(const uint8_t *a_buf, size_t a_buflen);
void* dap_enc_chipmunk_read_public_key(const uint8_t *a_buf, size_t a_buflen);
uint64_t dap_enc_chipmunk_deser_private_key_size(const void *unused);
uint64_t dap_enc_chipmunk_deser_public_key_size(const void *unused);

// Signature serialization/deserialization functions
uint8_t *dap_enc_chipmunk_write_signature(const void *a_sign, size_t *a_sign_len);
void *dap_enc_chipmunk_read_signature(const uint8_t *a_buf, size_t a_buflen);

// Delete functions for memory cleanup
void dap_enc_chipmunk_public_key_delete(void *a_pub_key);
void dap_enc_chipmunk_private_key_delete(void *a_priv_key);
void dap_enc_chipmunk_signature_delete(void *a_signature);

#ifdef __cplusplus
}
#endif 
