/*
 * Internal ECDSA algorithm implementation
 * 
 * Sign and verify operations using the native field/scalar/group implementations.
 * 
 * NOTE: Internal API, not exposed outside sig_ecdsa/
 */

#ifndef ECDSA_IMPL_H
#define ECDSA_IMPL_H

#include "ecdsa_field.h"
#include "ecdsa_scalar.h"
#include "ecdsa_group.h"

#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// ECDSA Signature
// =============================================================================

typedef struct {
    ecdsa_scalar_t r;
    ecdsa_scalar_t s;
} ecdsa_sig_t;

// =============================================================================
// RFC6979 Deterministic Nonce Generation
// =============================================================================

/**
 * Generate deterministic nonce k per RFC6979
 * 
 * @param k         Output nonce
 * @param msg32     32-byte message hash
 * @param seckey32  32-byte private key
 * @param algo16    Optional 16-byte algorithm identifier (can be NULL)
 * @param data      Optional additional data (can be NULL)
 * @param datalen   Length of additional data
 * @param counter   Attempt counter (0 for first try)
 * @return          true if valid nonce generated
 */
bool ecdsa_nonce_rfc6979(
    ecdsa_scalar_t *k,
    const uint8_t *msg32,
    const uint8_t *seckey32,
    const uint8_t *algo16,
    const void *data,
    size_t datalen,
    unsigned int counter
);

// =============================================================================
// ECDSA Operations
// =============================================================================

/**
 * Sign a 32-byte message hash
 * 
 * @param sig       Output signature
 * @param msg32     32-byte message hash
 * @param seckey    Private key scalar
 * @param nonce     Nonce k (must be random or RFC6979)
 * @return          true on success
 */
bool ecdsa_sign_inner(
    ecdsa_sig_t *sig,
    const uint8_t *msg32,
    const ecdsa_scalar_t *seckey,
    const ecdsa_scalar_t *nonce
);

/**
 * Verify an ECDSA signature
 * 
 * @param sig       Signature to verify
 * @param msg32     32-byte message hash
 * @param pubkey    Public key point
 * @return          true if signature is valid
 */
bool ecdsa_verify_inner(
    const ecdsa_sig_t *sig,
    const uint8_t *msg32,
    const ecdsa_ge_t *pubkey
);

/**
 * Serialize signature to compact 64-byte format (r || s)
 */
void ecdsa_sig_serialize(uint8_t *output64, const ecdsa_sig_t *sig);

/**
 * Parse signature from compact 64-byte format
 */
bool ecdsa_sig_parse(ecdsa_sig_t *sig, const uint8_t *input64);

/**
 * Normalize signature to low-S form (s <= n/2)
 * @return true if s was negated
 */
bool ecdsa_sig_normalize(ecdsa_sig_t *sig);

// =============================================================================
// Public Key Operations
// =============================================================================

/**
 * Compute public key from private key: P = d*G
 */
bool ecdsa_pubkey_create(ecdsa_ge_t *pubkey, const ecdsa_scalar_t *seckey);

#endif // ECDSA_IMPL_H
