/**
 * @file spx_fips202.h
 * @brief SPHINCS+ FIPS202 compatibility - redirects to native DAP Keccak
 */

#ifndef SPX_FIPS202_H
#define SPX_FIPS202_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "params.h"
#include "dap_hash_keccak.h"
#include "dap_hash_sha3.h"
#include "dap_hash_shake128.h"
#include "dap_hash_shake256.h"

// Rate constants
#define SPX_SHAKE128_RATE DAP_SHAKE128_RATE
#define SPX_SHAKE256_RATE DAP_SHAKE256_RATE
#define SPX_SHA3_256_RATE DAP_KECCAK_SHA3_256_RATE
#define SPX_SHA3_512_RATE DAP_KECCAK_SHA3_512_RATE

// =============================================================================
// SHAKE128 functions
// =============================================================================

#define shake128_absorb SPX_NAMESPACE(shake128_absorb)
static inline void shake128_absorb(uint64_t *s, const uint8_t *input, size_t inlen)
{
    dap_hash_shake128_absorb(s, input, inlen);
}

#define shake128_squeezeblocks SPX_NAMESPACE(shake128_squeezeblocks)
static inline void shake128_squeezeblocks(uint8_t *output, size_t nblocks, uint64_t *s)
{
    dap_hash_shake128_squeezeblocks(output, nblocks, s);
}

#define shake128_inc_init SPX_NAMESPACE(shake128_inc_init)
static inline void shake128_inc_init(uint64_t *s_inc)
{
    memset(s_inc, 0, 26 * sizeof(uint64_t));  // 25 state + 1 for position
}

#define shake128_inc_absorb SPX_NAMESPACE(shake128_inc_absorb)
void shake128_inc_absorb(uint64_t *s_inc, const uint8_t *input, size_t inlen);

#define shake128_inc_finalize SPX_NAMESPACE(shake128_inc_finalize)
void shake128_inc_finalize(uint64_t *s_inc);

#define shake128_inc_squeeze SPX_NAMESPACE(shake128_inc_squeeze)
void shake128_inc_squeeze(uint8_t *output, size_t outlen, uint64_t *s_inc);

#define shake128 SPX_NAMESPACE(shake128)
static inline void shake128(uint8_t *output, size_t outlen, const uint8_t *input, size_t inlen)
{
    dap_hash_shake128(output, outlen, input, inlen);
}

// =============================================================================
// SHAKE256 functions
// =============================================================================

#define shake256_absorb SPX_NAMESPACE(shake256_absorb)
static inline void shake256_absorb(uint64_t *s, const uint8_t *input, size_t inlen)
{
    dap_hash_shake256_absorb(s, input, inlen);
}

#define shake256_squeezeblocks SPX_NAMESPACE(shake256_squeezeblocks)
static inline void shake256_squeezeblocks(uint8_t *output, size_t nblocks, uint64_t *s)
{
    dap_hash_shake256_squeezeblocks(output, nblocks, s);
}

#define shake256_inc_init SPX_NAMESPACE(shake256_inc_init)
static inline void shake256_inc_init(uint64_t *s_inc)
{
    memset(s_inc, 0, 26 * sizeof(uint64_t));  // 25 state + 1 for position
}

#define shake256_inc_absorb SPX_NAMESPACE(shake256_inc_absorb)
void shake256_inc_absorb(uint64_t *s_inc, const uint8_t *input, size_t inlen);

#define shake256_inc_finalize SPX_NAMESPACE(shake256_inc_finalize)
void shake256_inc_finalize(uint64_t *s_inc);

#define shake256_inc_squeeze SPX_NAMESPACE(shake256_inc_squeeze)
void shake256_inc_squeeze(uint8_t *output, size_t outlen, uint64_t *s_inc);

#define shake256 SPX_NAMESPACE(shake256)
static inline void shake256(uint8_t *output, size_t outlen, const uint8_t *input, size_t inlen)
{
    dap_hash_shake256(output, outlen, input, inlen);
}

// =============================================================================
// SHA3-256 functions
// =============================================================================

#define sha3_256_inc_init SPX_NAMESPACE(sha3_256_inc_init)
static inline void sha3_256_inc_init(uint64_t *s_inc)
{
    memset(s_inc, 0, 26 * sizeof(uint64_t));
}

#define sha3_256_inc_absorb SPX_NAMESPACE(sha3_256_inc_absorb)
void sha3_256_inc_absorb(uint64_t *s_inc, const uint8_t *input, size_t inlen);

#define sha3_256_inc_finalize SPX_NAMESPACE(sha3_256_inc_finalize)
void sha3_256_inc_finalize(uint8_t *output, uint64_t *s_inc);

#define sha3_256 SPX_NAMESPACE(sha3_256)
static inline void sha3_256(uint8_t *output, const uint8_t *input, size_t inlen)
{
    dap_hash_sha3_256_raw(output, input, inlen);
}

// =============================================================================
// SHA3-512 functions
// =============================================================================

#define sha3_512_inc_init SPX_NAMESPACE(sha3_512_inc_init)
static inline void sha3_512_inc_init(uint64_t *s_inc)
{
    memset(s_inc, 0, 26 * sizeof(uint64_t));
}

#define sha3_512_inc_absorb SPX_NAMESPACE(sha3_512_inc_absorb)
void sha3_512_inc_absorb(uint64_t *s_inc, const uint8_t *input, size_t inlen);

#define sha3_512_inc_finalize SPX_NAMESPACE(sha3_512_inc_finalize)
void sha3_512_inc_finalize(uint8_t *output, uint64_t *s_inc);

#define sha3_512 SPX_NAMESPACE(sha3_512)
static inline void sha3_512(uint8_t *output, const uint8_t *input, size_t inlen)
{
    dap_hash_sha3_512(output, input, inlen);
}

#endif /* SPX_FIPS202_H */
