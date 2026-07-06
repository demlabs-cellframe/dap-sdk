/**
 * @file spx_fips202_nist.h
 * @brief Canonical SHAKE/SHA-3 shim for NIST SLH-DSA (FIPS 205).
 *
 * Reserved for the upcoming NIST-track SPHINCS+ parameter family
 * (SLH-DSA, FIPS 205).  All squeeze primitives MUST follow FIPS 202
 * (extract → permute), unlike the legacy vanilla SPHINCS+ shim in
 * spx_fips202.h which preserves master byte-for-byte compatibility.
 *
 * Usage rule:
 *   - vanilla SPHINCS+ TUs include "spx_fips202.h" (legacy)
 *   - SLH-DSA TUs MUST include "spx_fips202_nist.h" (canonical)
 *
 * The two headers share the same symbol names via SPX_NAMESPACE() and
 * therefore MUST NOT be included in the same translation unit.  The
 * isolation between vanilla and NIST variants is enforced at the file
 * level by the build system (separate object trees / namespaces).
 *
 * Until FIPS 205 sources land in-tree this header only declares the
 * canonical variants and asserts via #error if both shims are pulled in
 * together.
 */

#ifndef SPX_FIPS202_NIST_H
#define SPX_FIPS202_NIST_H

#ifdef SPX_FIPS202_H
#  error "spx_fips202.h (legacy) and spx_fips202_nist.h are mutually exclusive — pick one per TU"
#endif

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "params.h"
#include "dap_hash_keccak.h"
#include "dap_hash_sha3.h"
#include "dap_hash_shake128.h"
#include "dap_hash_shake256.h"

#define SPX_SHAKE128_RATE DAP_SHAKE128_RATE
#define SPX_SHAKE256_RATE DAP_SHAKE256_RATE
#define SPX_SHA3_256_RATE DAP_KECCAK_SHA3_256_RATE
#define SPX_SHA3_512_RATE DAP_KECCAK_SHA3_512_RATE

/* SHAKE128 — canonical FIPS 202 squeeze */
#define shake128_absorb        SPX_NAMESPACE(shake128_absorb)
static inline void shake128_absorb(uint64_t *s, const uint8_t *input, size_t inlen)
{
    dap_hash_shake128_absorb(s, input, inlen);
}

#define shake128_squeezeblocks SPX_NAMESPACE(shake128_squeezeblocks)
static inline void shake128_squeezeblocks(uint8_t *output, size_t nblocks, uint64_t *s)
{
    dap_hash_shake128_squeezeblocks(output, nblocks, s);
}

#define shake128 SPX_NAMESPACE(shake128)
static inline void shake128(uint8_t *output, size_t outlen, const uint8_t *input, size_t inlen)
{
    dap_hash_shake128(output, outlen, input, inlen);
}

/* SHAKE256 — canonical FIPS 202 squeeze */
#define shake256_absorb        SPX_NAMESPACE(shake256_absorb)
static inline void shake256_absorb(uint64_t *s, const uint8_t *input, size_t inlen)
{
    dap_hash_shake256_absorb(s, input, inlen);
}

#define shake256_squeezeblocks SPX_NAMESPACE(shake256_squeezeblocks)
static inline void shake256_squeezeblocks(uint8_t *output, size_t nblocks, uint64_t *s)
{
    dap_hash_shake256_squeezeblocks(output, nblocks, s);
}

#define shake256 SPX_NAMESPACE(shake256)
static inline void shake256(uint8_t *output, size_t outlen, const uint8_t *input, size_t inlen)
{
    dap_hash_shake256(output, outlen, input, inlen);
}

/* SHA3 wrappers — same as vanilla shim (SHA-3 has no squeeze divergence) */
#define sha3_256 SPX_NAMESPACE(sha3_256)
static inline void sha3_256(uint8_t *output, const uint8_t *input, size_t inlen)
{
    dap_hash_sha3_256_raw(output, input, inlen);
}

#define sha3_512 SPX_NAMESPACE(sha3_512)
static inline void sha3_512(uint8_t *output, const uint8_t *input, size_t inlen)
{
    dap_hash_sha3_512(output, input, inlen);
}

/* TODO(FIPS 205): wire SLH-DSA-specific shake128_inc_*/shake256_inc_* once
 * the upstream sources are imported.  The incremental code path in
 * spx_fips202.c is FIPS-conformant and may be reused unchanged. */

#endif /* SPX_FIPS202_NIST_H */
