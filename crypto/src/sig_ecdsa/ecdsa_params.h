#ifndef __ECDSA_PARAMS__
#define __ECDSA_PARAMS__

#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "dap_crypto_common.h"
#include <secp256k1.h>


#define ECDSA_SIG_SIZE 64
#define ECDSA_PRIVATE_KEY_SIZE 32
#define ECDSA_PUBLIC_KEY_SIZE 64


///==========================================================================================


typedef struct {
    unsigned char data[ECDSA_PRIVATE_KEY_SIZE];
} ecdsa_private_key_t;

typedef secp256k1_pubkey ecdsa_public_key_t;
typedef secp256k1_ecdsa_signature ecdsa_signature_t;
typedef secp256k1_context ecdsa_context_t;

///==========================================================================================

//int ecdsa_crypto_sign_keypair(ecdsa_public_key_t *public_key, ecdsa_private_key_t *private_key,
//        ecdsa_kind_t kind, const void * seed, size_t seed_size);

//int ecdsa_crypto_sign(ecdsa_signature_t *, const unsigned char *, unsigned long long, const ecdsa_private_key_t *);

//int ecdsa_crypto_sign_open( unsigned char *, unsigned long long, ecdsa_signature_t *, const ecdsa_public_key_t *);

///==========================================================================================

#endif


