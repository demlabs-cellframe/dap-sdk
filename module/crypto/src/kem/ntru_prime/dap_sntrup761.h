#pragma once

/*
 * Streamlined NTRU Prime 761 (sntrup761) KEM.
 * Parameters: p=761, q=4591, w=286.
 * Ring: Z_q[x] / (x^p - x - 1).
 */

#include <stddef.h>
#include <stdint.h>

#define SNTRUP761_P         761
#define SNTRUP761_Q         4591
#define SNTRUP761_W         286

#define SNTRUP761_PUBLICKEYBYTES  (SNTRUP761_P * 2)        /* 1522 */
#define SNTRUP761_SECRETKEYBYTES  (((SNTRUP761_P + 3) / 4) * 2 + SNTRUP761_PUBLICKEYBYTES + 32) /* 1936 */
#define SNTRUP761_CIPHERTEXTBYTES (SNTRUP761_P * 2)        /* 1522 */
#define SNTRUP761_BYTES           32

int sntrup761_keypair(uint8_t *pk, uint8_t *sk);
int sntrup761_enc(uint8_t *ct, uint8_t *ss, const uint8_t *pk);
int sntrup761_dec(uint8_t *ss, const uint8_t *ct, const uint8_t *sk);
