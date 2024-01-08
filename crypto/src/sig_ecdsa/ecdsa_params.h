#ifndef __ECDSA_PARAMS__
#define __ECDSA_PARAMS__

#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "dap_crypto_common.h"





#define DAP_ENC_ECDSA_SKEY_LEN 32



///========================================================================


///==========================================================================================
typedef struct secp256k1_context ecdsa_context_t;

typedef struct {
	unsigned char data[DAP_ENC_ECDSA_SKEY_LEN];
} ecdsa_private_key_t;

typedef struct secp256k1_pubkey ecdsa_public_key_t;

typedef struct secp256k1_ecdsa_signature ecdsa_signature_t;

typedef struct secp256k1_ecdsa_signature ecdsa_signature_t;

///==========================================================================================

int ecdsa_crypto_sign_keypair(ecdsa_public_key_t *public_key, ecdsa_private_key_t *private_key,
        ecdsa_kind_t kind, const void * seed, size_t seed_size);

int ecdsa_crypto_sign(ecdsa_signature_t *, const unsigned char *, unsigned long long, const ecdsa_private_key_t *);

int ecdsa_crypto_sign_open( unsigned char *, unsigned long long, ecdsa_signature_t *, const ecdsa_public_key_t *);

void ecdsa_private_key_delete(void *private_key);
void ecdsa_public_key_delete(void *public_key);
void ecdsa_private_and_public_keys_delete(void *private_key, void *public_key);

void ecdsa_signature_delete(void *sig);

///==========================================================================================

#endif


