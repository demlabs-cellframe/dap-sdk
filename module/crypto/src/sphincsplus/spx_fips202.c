/**
 * @file spx_fips202.c
 * @brief SPHINCS+ FIPS202 - incremental functions implementation
 * 
 * Uses native DAP Keccak for core permutation.
 */

#include <string.h>
#include "spx_fips202.h"

// State layout: s_inc[0..24] = state, s_inc[25] = absorbed byte count

// =============================================================================
// Internal helpers
// =============================================================================

static void keccak_inc_absorb(uint64_t *s_inc, size_t rate, const uint8_t *input, size_t inlen)
{
    size_t pos = (size_t)s_inc[25];
    uint8_t *state_bytes = (uint8_t *)s_inc;
    
    // XOR input into state
    while (inlen > 0) {
        size_t to_absorb = rate - pos;
        if (to_absorb > inlen) to_absorb = inlen;
        
        for (size_t i = 0; i < to_absorb; i++) {
            state_bytes[pos + i] ^= input[i];
        }
        
        pos += to_absorb;
        input += to_absorb;
        inlen -= to_absorb;
        
        if (pos == rate) {
            dap_hash_keccak_permute((dap_hash_keccak_state_t *)s_inc);
            pos = 0;
        }
    }
    
    s_inc[25] = (uint64_t)pos;
}

static void keccak_inc_finalize(uint64_t *s_inc, size_t rate, uint8_t suffix)
{
    size_t pos = (size_t)s_inc[25];
    uint8_t *state_bytes = (uint8_t *)s_inc;
    
    // Apply suffix
    state_bytes[pos] ^= suffix;
    state_bytes[rate - 1] ^= 0x80;
    
    dap_hash_keccak_permute((dap_hash_keccak_state_t *)s_inc);
    s_inc[25] = 0;  // Reset position for squeeze
}

static void keccak_inc_squeeze(uint8_t *output, size_t outlen, uint64_t *s_inc, size_t rate)
{
    size_t pos = (size_t)s_inc[25];
    
    while (outlen > 0) {
        if (pos == 0 && s_inc[25] != 0) {
            // Need new permutation (not first squeeze after finalize)
            dap_hash_keccak_permute((dap_hash_keccak_state_t *)s_inc);
        }
        
        size_t to_squeeze = rate - pos;
        if (to_squeeze > outlen) to_squeeze = outlen;
        
        dap_hash_keccak_extract_bytes((dap_hash_keccak_state_t *)s_inc, output, to_squeeze);
        
        output += to_squeeze;
        outlen -= to_squeeze;
        pos += to_squeeze;
        
        if (pos == rate) {
            dap_hash_keccak_permute((dap_hash_keccak_state_t *)s_inc);
            pos = 0;
        }
    }
    
    s_inc[25] = (uint64_t)pos;
}

// =============================================================================
// SHAKE128 incremental
// =============================================================================

void SPX_NAMESPACE(shake128_inc_absorb)(uint64_t *s_inc, const uint8_t *input, size_t inlen)
{
    keccak_inc_absorb(s_inc, DAP_SHAKE128_RATE, input, inlen);
}

void SPX_NAMESPACE(shake128_inc_finalize)(uint64_t *s_inc)
{
    keccak_inc_finalize(s_inc, DAP_SHAKE128_RATE, 0x1F);
}

void SPX_NAMESPACE(shake128_inc_squeeze)(uint8_t *output, size_t outlen, uint64_t *s_inc)
{
    keccak_inc_squeeze(output, outlen, s_inc, DAP_SHAKE128_RATE);
}

// =============================================================================
// SHAKE256 incremental
// =============================================================================

void SPX_NAMESPACE(shake256_inc_absorb)(uint64_t *s_inc, const uint8_t *input, size_t inlen)
{
    keccak_inc_absorb(s_inc, DAP_SHAKE256_RATE, input, inlen);
}

void SPX_NAMESPACE(shake256_inc_finalize)(uint64_t *s_inc)
{
    keccak_inc_finalize(s_inc, DAP_SHAKE256_RATE, 0x1F);
}

void SPX_NAMESPACE(shake256_inc_squeeze)(uint8_t *output, size_t outlen, uint64_t *s_inc)
{
    keccak_inc_squeeze(output, outlen, s_inc, DAP_SHAKE256_RATE);
}

// =============================================================================
// SHA3-256 incremental
// =============================================================================

void SPX_NAMESPACE(sha3_256_inc_absorb)(uint64_t *s_inc, const uint8_t *input, size_t inlen)
{
    keccak_inc_absorb(s_inc, DAP_KECCAK_SHA3_256_RATE, input, inlen);
}

void SPX_NAMESPACE(sha3_256_inc_finalize)(uint8_t *output, uint64_t *s_inc)
{
    keccak_inc_finalize(s_inc, DAP_KECCAK_SHA3_256_RATE, 0x06);
    dap_hash_keccak_extract_bytes((dap_hash_keccak_state_t *)s_inc, output, 32);
}

// =============================================================================
// SHA3-512 incremental
// =============================================================================

void SPX_NAMESPACE(sha3_512_inc_absorb)(uint64_t *s_inc, const uint8_t *input, size_t inlen)
{
    keccak_inc_absorb(s_inc, DAP_KECCAK_SHA3_512_RATE, input, inlen);
}

void SPX_NAMESPACE(sha3_512_inc_finalize)(uint8_t *output, uint64_t *s_inc)
{
    keccak_inc_finalize(s_inc, DAP_KECCAK_SHA3_512_RATE, 0x06);
    dap_hash_keccak_extract_bytes((dap_hash_keccak_state_t *)s_inc, output, 64);
}
