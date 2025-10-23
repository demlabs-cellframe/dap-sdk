#ifndef __ECDSA_PARAMS__
#define __ECDSA_PARAMS__

#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "dap_crypto_common.h"
#include "secp256k1.h"
#include "secp256k1_preallocated.h"

#define ECDSA_PRIVATE_KEY_SIZE  32
#define ECDSA_SIG_SIZE          sizeof(secp256k1_ecdsa_signature)
#define ECDSA_PUBLIC_KEY_SIZE   sizeof(secp256k1_pubkey)
#define ECDSA_PKEY_SERIALIZED_SIZE ECDSA_PUBLIC_KEY_SIZE + 1

typedef struct {
    unsigned char data[ECDSA_PRIVATE_KEY_SIZE];
} DAP_ALIGN_PACKED ecdsa_private_key_t;

typedef secp256k1_pubkey ecdsa_public_key_t;
typedef secp256k1_ecdsa_signature ecdsa_signature_t;
typedef secp256k1_context ecdsa_context_t;

#endif


