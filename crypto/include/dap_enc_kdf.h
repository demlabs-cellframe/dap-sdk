/*
 * Authors:
 * Cellframe Team <admin@cellframe.net>
 * 
 * Copyright  (c) 2017-2025 Cellframe Team
 * All rights reserved.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "dap_enc_key.h"

/**
 * @brief Universal Key Derivation Function (KDF) for post-quantum protection
 * 
 * Works with any dap_enc_key_t object (KEM algorithms: Kyber512, NewHope, MSRLN, etc.)
 * 
 * Uses SHAKE256 (XOF - eXtendable Output Function) to derive keys from:
 * - Shared secret from KEM (extracted from dap_enc_key_t->shared_key or priv_key_data)
 * - Context string (e.g., "session_key", "handshake_key")
 * - Ratchet counter (for forward secrecy)
 * 
 * This provides:
 * - Post-quantum security via any KEM + SHAKE256
 * - Forward secrecy via ratcheting (each message uses new key)
 * - Context separation (different keys for different purposes)
 * - Universal: works with Kyber512, NewHope, MSRLN, future algorithms
 */

/**
 * @brief Derive a key from dap_enc_key_t shared secret using SHAKE256 KDF
 * 
 * Universal function that works with ANY KEM algorithm:
 * - Extracts shared secret from a_kem_key (shared_key or priv_key_data)
 * - Derives new key: derived_key = SHAKE256(shared_secret || context || counter)
 * 
 * @param a_kem_key KEM key object (Kyber512, NewHope, MSRLN, etc.) with shared secret
 * @param a_context Context string for domain separation (e.g., "session_key", "handshake")
 * @param a_context_size Size of context string (use strlen(a_context) or 0 for no context)
 * @param a_counter Ratchet counter (0 for first key, increment for each ratchet)
 * @param a_derived_key Output buffer for derived key
 * @param a_derived_key_size Desired size of derived key (e.g., 32 for SALSA2012)
 * @return 0 on success, negative on error
 * 
 * @example
 * ```c
 * // After Kyber512 handshake:
 * dap_enc_key_t *alice_key = ...; // Has shared_key after gen_alice_shared_key
 * uint8_t session_seed[32];
 * dap_enc_kdf_derive_from_key(alice_key, "session", 7, 0, session_seed, 32);
 * 
 * // For next message (ratcheting):
 * uint8_t next_seed[32];
 * dap_enc_kdf_derive_from_key(alice_key, "session", 7, 1, next_seed, 32);
 * ```
 */
int dap_enc_kdf_derive_from_key(const dap_enc_key_t *a_kem_key,
                                 const char *a_context, size_t a_context_size,
                                 uint64_t a_counter,
                                 void *a_derived_key, size_t a_derived_key_size);

/**
 * @brief Derive a key using raw bytes (low-level, for compatibility)
 * 
 * Formula: derived_key = SHAKE256(base_secret || context || counter_be64)
 * 
 * @param a_base_secret Base secret material (e.g., raw shared secret bytes)
 * @param a_base_secret_size Size of base secret in bytes
 * @param a_context Context string for domain separation
 * @param a_context_size Size of context string
 * @param a_counter Ratchet counter
 * @param a_derived_key Output buffer for derived key
 * @param a_derived_key_size Desired size of derived key
 * @return 0 on success, negative on error
 */
int dap_enc_kdf_derive(const void *a_base_secret, size_t a_base_secret_size,
                       const char *a_context, size_t a_context_size,
                       uint64_t a_counter,
                       void *a_derived_key, size_t a_derived_key_size);

/**
 * @brief Create symmetric encryption key from KEM shared secret with ratcheting
 * 
 * High-level convenience function:
 * - Extracts shared secret from a_kem_key
 * - Derives seed using KDF with ratchet counter
 * - Creates dap_enc_key_t for symmetric cipher (SALSA2012, AES, etc.)
 * 
 * @param a_kem_key KEM key object with shared secret
 * @param a_cipher_type Target symmetric cipher type (DAP_ENC_KEY_TYPE_SALSA2012, etc.)
 * @param a_context Context string (e.g., "session_encryption")
 * @param a_context_size Size of context string
 * @param a_counter Ratchet counter (0 for first key, 1+ for ratcheting)
 * @param a_key_size Desired key size (usually 32 bytes)
 * @return New dap_enc_key_t for symmetric encryption, or NULL on error
 * 
 * @example
 * ```c
 * dap_enc_key_t *alice_key = ...; // After handshake
 * dap_enc_key_t *session_key = dap_enc_kdf_create_cipher_key(
 *     alice_key, DAP_ENC_KEY_TYPE_SALSA2012, "session", 7, 0, 32);
 * // Use session_key for encryption
 * // ...
 * // For next message (forward secrecy):
 * dap_enc_key_delete(session_key);
 * session_key = dap_enc_kdf_create_cipher_key(
 *     alice_key, DAP_ENC_KEY_TYPE_SALSA2012, "session", 7, 1, 32);
 * ```
 */
dap_enc_key_t* dap_enc_kdf_create_cipher_key(const dap_enc_key_t *a_kem_key,
                                              dap_enc_key_type_t a_cipher_type,
                                              const char *a_context, size_t a_context_size,
                                              uint64_t a_counter,
                                              size_t a_key_size);


/**
 * @brief Derive multiple keys at once (convenience function)
 * 
 * Derives N keys from base secret with different counters:
 * - a_derived_keys[0] = SHAKE256(base_secret || context || start_counter)
 * - a_derived_keys[1] = SHAKE256(base_secret || context || start_counter + 1)
 * - ...
 * 
 * @param a_base_secret Base secret material
 * @param a_base_secret_size Size of base secret
 * @param a_context Context string
 * @param a_context_size Size of context
 * @param a_start_counter Starting counter value
 * @param a_derived_keys Array of output buffers (each of size a_key_size)
 * @param a_num_keys Number of keys to derive
 * @param a_key_size Size of each derived key
 * @return 0 on success, negative on error
 */
int dap_enc_kdf_derive_multiple(const void *a_base_secret, size_t a_base_secret_size,
                                 const char *a_context, size_t a_context_size,
                                 uint64_t a_start_counter,
                                 void **a_derived_keys, size_t a_num_keys, size_t a_key_size);

/**
 * @brief HKDF-like: Extract + Expand pattern
 * 
 * Extract: PRK = SHAKE256(salt || IKM)
 * Expand: OKM = SHAKE256(PRK || info || counter)
 * 
 * @param a_salt Optional salt (can be NULL)
 * @param a_salt_size Size of salt
 * @param a_ikm Input key material (e.g., Kyber512 shared secret)
 * @param a_ikm_size Size of IKM
 * @param a_info Optional context info (can be NULL)
 * @param a_info_size Size of info
 * @param a_okm Output key material buffer
 * @param a_okm_size Desired size of OKM
 * @return 0 on success, negative on error
 */
int dap_enc_kdf_hkdf(const void *a_salt, size_t a_salt_size,
                     const void *a_ikm, size_t a_ikm_size,
                     const void *a_info, size_t a_info_size,
                     void *a_okm, size_t a_okm_size);

