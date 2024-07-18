#ifndef SPX_FIPS202_H
#define SPX_FIPS202_H

#include <stddef.h>
#include <stdint.h>
#include "params.h"

#define SPX_SHAKE128_RATE 168
#define SPX_SHAKE256_RATE 136
#define SPX_SHA3_256_RATE 136
#define SPX_SHA3_512_RATE 72
#define shake128_absorb SPX_NAMESPACE(shake128_absorb)
void shake128_absorb(uint64_t *s, const uint8_t *input, size_t inlen);
#define shake128_squeezeblocks SPX_NAMESPACE(shake128_squeezeblocks)
void shake128_squeezeblocks(uint8_t *output, size_t nblocks, uint64_t *s);
#define shake128_inc_init SPX_NAMESPACE(shake128_inc_init)
void shake128_inc_init(uint64_t *s_inc);
#define shake128_inc_absorb SPX_NAMESPACE(shake128_inc_absorb)
void shake128_inc_absorb(uint64_t *s_inc, const uint8_t *input, size_t inlen);
#define shake128_inc_finalize SPX_NAMESPACE(shake128_inc_finalize)
void shake128_inc_finalize(uint64_t *s_inc);
#define shake128_inc_squeeze SPX_NAMESPACE(shake128_inc_squeeze)
void shake128_inc_squeeze(uint8_t *output, size_t outlen, uint64_t *s_inc);

#define shake256_absorb SPX_NAMESPACE(shake256_absorb)
void shake256_absorb(uint64_t *s, const uint8_t *input, size_t inlen);
#define shake256_squeezeblocks SPX_NAMESPACE(shake256_squeezeblocks)
void shake256_squeezeblocks(uint8_t *output, size_t nblocks, uint64_t *s);

#define shake256_inc_init SPX_NAMESPACE(shake256_inc_init)
void shake256_inc_init(uint64_t *s_inc);
#define shake256_inc_absorb SPX_NAMESPACE(shake256_inc_absorb)
void shake256_inc_absorb(uint64_t *s_inc, const uint8_t *input, size_t inlen);
#define shake256_inc_finalize SPX_NAMESPACE(shake256_inc_finalize)
void shake256_inc_finalize(uint64_t *s_inc);
#define shake256_inc_squeeze SPX_NAMESPACE(shake256_inc_squeeze)
void shake256_inc_squeeze(uint8_t *output, size_t outlen, uint64_t *s_inc);
#define shake128 SPX_NAMESPACE(shake128)
void shake128(uint8_t *output, size_t outlen,
              const uint8_t *input, size_t inlen);
#define shake256 SPX_NAMESPACE(shake256)
void shake256(uint8_t *output, size_t outlen,
              const uint8_t *input, size_t inlen);
#define sha3_256_inc_init SPX_NAMESPACE(sha3_256_inc_init)
void sha3_256_inc_init(uint64_t *s_inc);
#define sha3_256_inc_absorb SPX_NAMESPACE(sha3_256_inc_absorb)
void sha3_256_inc_absorb(uint64_t *s_inc, const uint8_t *input, size_t inlen);
#define sha3_256_inc_finalize SPX_NAMESPACE(sha3_256_inc_finalize)
void sha3_256_inc_finalize(uint8_t *output, uint64_t *s_inc);
#define sha3_256 SPX_NAMESPACE(sha3_256)
void sha3_256(uint8_t *output, const uint8_t *input, size_t inlen);
#define sha3_512_inc_init SPX_NAMESPACE(sha3_512_inc_init)
void sha3_512_inc_init(uint64_t *s_inc);
#define sha3_512_inc_absorb SPX_NAMESPACE(sha3_512_inc_absorb)
void sha3_512_inc_absorb(uint64_t *s_inc, const uint8_t *input, size_t inlen);
#define sha3_512_inc_finalize SPX_NAMESPACE(sha3_512_inc_finalize)
void sha3_512_inc_finalize(uint8_t *output, uint64_t *s_inc);
#define sha3_512 SPX_NAMESPACE(sha3_512)
void sha3_512(uint8_t *output, const uint8_t *input, size_t inlen);

#endif
