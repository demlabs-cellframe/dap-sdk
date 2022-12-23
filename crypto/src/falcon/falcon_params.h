//
// Created by Евгений Крамсаков on 21.12.2022.
//

#ifndef _FALCON_PARAMS_H
#define _FALCON_PARAMS_H

#include "dap_crypto_common.h"


typedef enum { FALCON_COMPRESSED, FALCON_PADDED, FALCON_CT } falcon_kind_t;

typedef struct {
    falcon_kind_t kind;
} falcon_param_t;

typedef struct {
    falcon_kind_t kind;
    unsigned char* data;
} falcon_private_key_t;

typedef struct {
    falcon_kind_t kind;
    unsigned char* data;
} falcon_public_key_t;

typedef struct {
    falcon_kind_t kind;
    unsigned char* sig_data;
    uint64_t sig_len;
} falcon_signature_t;



bool falcon_params_init(falcon_param_t *, falcon_kind_t);

int falcon_crypto_sign_keypair(falcon_public_key_t* public_key, falcon_private_key_t* private_key,
                               falcon_kind_t kind, const void* seed, size_t seed_size);

int falcon_crypto_sign(falcon_signature_t*, const unsigned char*, unsigned long long, const falcon_private_key_t*);

int falcon_crypto_sign_open(unsigned char*, unsigned long long, falcon_signature_t*, const falcon_public_key_t*);

void falcon_private_key_delete(falcon_private_key_t* private_key);
void falcon_public_key_delete(falcon_public_key_t* public_key);
void falcon_private_and_public_keys_delete(falcon_private_key_t* private_key, falcon_public_key_t* public_key);

void falcon_signature_delete(falcon_signature_t* sig);

#endif //CELLFRAME_SDK_FALCON_PARAMS_H
