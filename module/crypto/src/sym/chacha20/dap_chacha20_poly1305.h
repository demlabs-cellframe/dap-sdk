#pragma once

#include <stddef.h>
#include <stdint.h>

#define DAP_CHACHA20_KEY_SIZE    32
#define DAP_CHACHA20_NONCE_SIZE  12
#define DAP_CHACHA20_BLOCK_SIZE  64

#define DAP_POLY1305_KEY_SIZE    32
#define DAP_POLY1305_TAG_SIZE    16

#define DAP_CHACHA20_POLY1305_TAG_SIZE  16

void dap_chacha20_block(uint32_t a_out[16], const uint32_t a_in[16]);

void dap_chacha20_encrypt(uint8_t *a_out, const uint8_t *a_in, size_t a_len,
        const uint8_t a_key[DAP_CHACHA20_KEY_SIZE],
        const uint8_t a_nonce[DAP_CHACHA20_NONCE_SIZE], uint32_t a_counter);

void dap_poly1305_mac(uint8_t a_tag[DAP_POLY1305_TAG_SIZE],
        const uint8_t *a_msg, size_t a_msg_len,
        const uint8_t a_key[DAP_POLY1305_KEY_SIZE]);

int dap_chacha20_poly1305_seal(uint8_t *a_ct, uint8_t a_tag[DAP_CHACHA20_POLY1305_TAG_SIZE],
        const uint8_t *a_pt, size_t a_pt_len,
        const uint8_t *a_aad, size_t a_aad_len,
        const uint8_t a_key[DAP_CHACHA20_KEY_SIZE],
        const uint8_t a_nonce[DAP_CHACHA20_NONCE_SIZE]);

int dap_chacha20_poly1305_open(uint8_t *a_pt,
        const uint8_t *a_ct, size_t a_ct_len,
        const uint8_t a_tag[DAP_CHACHA20_POLY1305_TAG_SIZE],
        const uint8_t *a_aad, size_t a_aad_len,
        const uint8_t a_key[DAP_CHACHA20_KEY_SIZE],
        const uint8_t a_nonce[DAP_CHACHA20_NONCE_SIZE]);
