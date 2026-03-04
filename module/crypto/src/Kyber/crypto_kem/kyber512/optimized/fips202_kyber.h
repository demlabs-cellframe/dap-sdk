/**
 * @file fips202_kyber.h
 * @brief Kyber512 FIPS202 compatibility - redirects to native DAP Keccak
 * 
 * This header replaces the original Kyber FIPS202 implementation with calls
 * to the unified DAP hash functions.
 */

#ifndef FIPS202_H
#define FIPS202_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "dap_hash_keccak.h"
#include "dap_hash_sha3.h"
#include "dap_hash_shake128.h"
#include "dap_hash_shake256.h"

// Rate constants
#define SHAKE128_RATE DAP_SHAKE128_RATE
#define SHAKE256_RATE DAP_SHAKE256_RATE
#define SHA3_256_RATE DAP_KECCAK_SHA3_256_RATE
#define SHA3_512_RATE DAP_KECCAK_SHA3_512_RATE

// Disable namespace macros - use inline functions directly
#define FIPS202_NAMESPACE(s) pqcrystals_kyber512_ref##s

// Keccak state type compatible with Kyber
typedef struct {
    uint64_t s[25];
} keccak_state;

// SHAKE128 functions
static inline void pqcrystals_kyber512_ref_shake128_absorb(keccak_state *state, const uint8_t *in, size_t inlen)
{
    dap_hash_shake128_absorb(state->s, in, inlen);
}

static inline void pqcrystals_kyber512_ref_shake128_squeezeblocks(uint8_t *out, size_t nblocks, keccak_state *state)
{
    dap_hash_shake128_squeezeblocks(out, nblocks, state->s);
}

static inline void pqcrystals_kyber512_ref_shake128(uint8_t *out, size_t outlen, const uint8_t *in, size_t inlen)
{
    dap_hash_shake128(out, outlen, in, inlen);
}

// SHAKE256 functions  
static inline void pqcrystals_kyber512_ref_shake256_absorb(keccak_state *state, const uint8_t *in, size_t inlen)
{
    dap_hash_shake256_absorb(state->s, in, inlen);
}

static inline void pqcrystals_kyber512_ref_shake256_squeezeblocks(uint8_t *out, size_t nblocks, keccak_state *state)
{
    dap_hash_shake256_squeezeblocks(out, nblocks, state->s);
}

static inline void pqcrystals_kyber512_ref_shake256(uint8_t *out, size_t outlen, const uint8_t *in, size_t inlen)
{
    dap_hash_shake256(out, outlen, in, inlen);
}

// SHA3 functions
static inline void pqcrystals_kyber512_ref_sha3_256(uint8_t h[32], const uint8_t *in, size_t inlen)
{
    dap_hash_sha3_256_raw(h, in, inlen);
}

static inline void pqcrystals_kyber512_ref_sha3_512(uint8_t h[64], const uint8_t *in, size_t inlen)
{
    dap_hash_sha3_512(h, in, inlen);
}

// Function name mappings
#define shake128_absorb pqcrystals_kyber512_ref_shake128_absorb
#define shake128_squeezeblocks pqcrystals_kyber512_ref_shake128_squeezeblocks
#define shake128 pqcrystals_kyber512_ref_shake128
#define shake256_absorb pqcrystals_kyber512_ref_shake256_absorb
#define shake256_squeezeblocks pqcrystals_kyber512_ref_shake256_squeezeblocks
#define shake256 pqcrystals_kyber512_ref_shake256
#define sha3_256 pqcrystals_kyber512_ref_sha3_256
#define sha3_512 pqcrystals_kyber512_ref_sha3_512

#endif /* FIPS202_H */
