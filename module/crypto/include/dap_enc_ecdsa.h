/*
 * Compatibility header - redirects to dap_sig_ecdsa.h
 * 
 * This file provides backward compatibility with old dap_enc_sig_ecdsa_* naming.
 * New code should use dap_sig_ecdsa.h directly.
 */

#ifndef _DAP_ENC_ECDSA_H_
#define _DAP_ENC_ECDSA_H_

#include "dap_sig_ecdsa.h"

// =============================================================================
// Backward compatibility aliases
// =============================================================================

// Types
typedef dap_sig_ecdsa_pubkey_t ecdsa_public_key_t;
typedef dap_sig_ecdsa_signature_t ecdsa_signature_t;
typedef dap_sig_ecdsa_context_t ecdsa_context_t;

typedef struct {
    unsigned char data[DAP_SIG_ECDSA_PRIVKEY_SIZE];
} ecdsa_private_key_t;

// Size constants
#define ECDSA_PRIVATE_KEY_SIZE      DAP_SIG_ECDSA_PRIVKEY_SIZE
#define ECDSA_SIG_SIZE              DAP_SIG_ECDSA_SIGNATURE_SIZE
#define ECDSA_PUBLIC_KEY_SIZE       DAP_SIG_ECDSA_PUBKEY_SIZE
#define ECDSA_PKEY_SERIALIZED_SIZE  DAP_SIG_ECDSA_PUBKEY_UNCOMPRESSED

// Old enum (deprecated)
enum DAP_ECDSA_SIGN_SECURITY {
    ECDSA_TOY = 0, ECDSA_MAX_SPEED, ECDSA_MIN_SIZE, ECDSA_MAX_SECURITY
};

// Function aliases (old -> new naming)
#define dap_enc_sig_ecdsa_key_new                   dap_sig_ecdsa_key_new
#define dap_enc_sig_ecdsa_key_new_generate          dap_sig_ecdsa_key_new_generate
#define dap_enc_sig_ecdsa_get_sign                  dap_sig_ecdsa_get_sign
#define dap_enc_sig_ecdsa_verify_sign               dap_sig_ecdsa_verify_sign
#define dap_enc_sig_ecdsa_write_signature           dap_sig_ecdsa_write_signature
#define dap_enc_sig_ecdsa_write_public_key          dap_sig_ecdsa_write_public_key
#define dap_enc_sig_ecdsa_read_signature            dap_sig_ecdsa_read_signature
#define dap_enc_sig_ecdsa_read_public_key           dap_sig_ecdsa_read_public_key
#define dap_enc_sig_ecdsa_signature_delete          dap_sig_ecdsa_signature_delete
#define dap_enc_sig_ecdsa_private_key_delete        dap_sig_ecdsa_private_key_delete
#define dap_enc_sig_ecdsa_public_key_delete         dap_sig_ecdsa_public_key_delete
#define dap_enc_sig_ecdsa_private_and_public_keys_delete dap_sig_ecdsa_private_and_public_keys_delete
#define dap_enc_sig_ecdsa_deinit                    dap_sig_ecdsa_deinit
#define dap_enc_sig_ecdsa_ser_key_size              dap_sig_ecdsa_ser_key_size
#define dap_enc_sig_ecdsa_ser_pkey_size             dap_sig_ecdsa_ser_pkey_size
#define dap_enc_sig_ecdsa_deser_key_size            dap_sig_ecdsa_deser_key_size
#define dap_enc_sig_ecdsa_deser_pkey_size           dap_sig_ecdsa_deser_pkey_size
#define dap_enc_sig_ecdsa_signature_size            dap_sig_ecdsa_signature_size

// Hash function - use native SHA3
#include "dap_hash_sha3.h"
DAP_STATIC_INLINE bool dap_enc_sig_ecdsa_hash_fast(const unsigned char *a_data, size_t a_data_size, dap_hash_sha3_256_t *a_out) {
    dap_hash_sha3_256_raw((uint8_t *)a_out, a_data, a_data_size);
    return true;
}

// Deprecated function (no-op)
DAP_STATIC_INLINE void dap_enc_sig_ecdsa_set_type(enum DAP_ECDSA_SIGN_SECURITY type) {
    (void)type;
}

#endif // _DAP_ENC_ECDSA_H_
