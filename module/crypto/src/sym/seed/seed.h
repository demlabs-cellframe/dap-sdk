/*
 * SEED block cipher — Korean Information Security Agency standard (KISA).
 *
 * Originally based on OpenSSL SEED implementation, adapted for DAP SDK.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DAP_SEED_BLOCK_SIZE 16
#define DAP_SEED_KEY_LENGTH 16

typedef struct dap_seed_key_schedule {
    uint32_t data[32];
} dap_seed_key_schedule_t;

void dap_seed_set_key(const unsigned char a_rawkey[DAP_SEED_KEY_LENGTH],
                      dap_seed_key_schedule_t *a_ks);

void dap_seed_encrypt(const unsigned char a_in[DAP_SEED_BLOCK_SIZE],
                      unsigned char a_out[DAP_SEED_BLOCK_SIZE],
                      const dap_seed_key_schedule_t *a_ks);

void dap_seed_decrypt(const unsigned char a_in[DAP_SEED_BLOCK_SIZE],
                      unsigned char a_out[DAP_SEED_BLOCK_SIZE],
                      const dap_seed_key_schedule_t *a_ks);

void dap_seed_ofb128_encrypt(const unsigned char *a_in, unsigned char *a_out,
                             size_t a_len, const dap_seed_key_schedule_t *a_ks,
                             unsigned char a_ivec[DAP_SEED_BLOCK_SIZE], int *a_num);

/* Backward compatibility aliases */
#define SEED_BLOCK_SIZE     DAP_SEED_BLOCK_SIZE
#define SEED_KEY_LENGTH     DAP_SEED_KEY_LENGTH
#define SEED_KEY_SCHEDULE   dap_seed_key_schedule_t
#define SEED_set_key        dap_seed_set_key
#define SEED_encrypt        dap_seed_encrypt
#define SEED_decrypt        dap_seed_decrypt
#define SEED_ofb128_encrypt dap_seed_ofb128_encrypt

#ifdef __cplusplus
}
#endif
