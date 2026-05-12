/*
 * Authors:
 * DAP SDK Team
 * Copyright (c) 2017-2026
 *
 * Per-kind SHAKE128 / SHAKE256 dispatcher for the legacy Dilithium and
 * (FIPS 204) ML-DSA branches that share this codebase.
 *
 * Background
 * ----------
 * Up to commit 796227b1 the shared keccak fast-path emitted output block
 * N+1 instead of block N for every SHAKE squeeze (a double-permute bug
 * confirmed against the FIPS 202 KAT vectors).  CRYSTALS-Dilithium round 3
 * (MODE_*) artefacts that exist on production deployments were derived
 * with that broken behaviour — we therefore preserve it bit-for-bit by
 * routing every Dilithium SHAKE call through the *_legacy* squeeze
 * helpers.
 *
 * The new FIPS 204 ML-DSA (MLDSA_44 / MLDSA_65 / MLDSA_87) parameter sets
 * have not yet shipped to production, so they switch to the
 * FIPS-202-conformant path.  The single decision point is
 * dilithium_param_t::is_fips204; this header centralises the four switch
 * helpers used across dilithium_sign.c, dilithium_poly.c and
 * dilithium_batch_verify.c.
 *
 * Streaming-safety: the keccak state lifetime is always confined to a
 * single helper or a single absorb→…→squeeze sequence; no caller mixes
 * the two conventions on the same state object.
 */

#pragma once

#include "dap_hash_shake128.h"
#include "dap_hash_shake256.h"
#include "dap_hash_shake_x4.h"
#include "dilithium_params.h"

DAP_STATIC_INLINE void dil_shake256(const dilithium_param_t *p,
                                    uint8_t *out, size_t outlen,
                                    const uint8_t *in, size_t inlen)
{
    if (p->is_fips204)
        dap_hash_shake256(out, outlen, in, inlen);
    else
        dap_hash_shake256_legacy(out, outlen, in, inlen);
}

DAP_STATIC_INLINE void dil_shake256_squeezeblocks(const dilithium_param_t *p,
                                                  uint8_t *out, size_t nblocks,
                                                  uint64_t *state)
{
    if (p->is_fips204)
        dap_hash_shake256_squeezeblocks(out, nblocks, state);
    else
        dap_hash_shake256_legacy_squeezeblocks(out, nblocks, state);
}

DAP_STATIC_INLINE void dil_shake128(const dilithium_param_t *p,
                                    uint8_t *out, size_t outlen,
                                    const uint8_t *in, size_t inlen)
{
    if (p->is_fips204)
        dap_hash_shake128(out, outlen, in, inlen);
    else
        dap_hash_shake128_legacy(out, outlen, in, inlen);
}

DAP_STATIC_INLINE void dil_shake256_x4_squeezeblocks(const dilithium_param_t *p,
                                                     uint8_t *o0, uint8_t *o1,
                                                     uint8_t *o2, uint8_t *o3,
                                                     size_t nblocks,
                                                     dap_keccak_x4_state_t *state)
{
    if (p->is_fips204)
        dap_hash_shake256_x4_squeezeblocks(o0, o1, o2, o3, nblocks, state);
    else
        dap_hash_shake256_x4_legacy_squeezeblocks(o0, o1, o2, o3, nblocks, state);
}

DAP_STATIC_INLINE void dil_shake128_x4_squeezeblocks(const dilithium_param_t *p,
                                                     uint8_t *o0, uint8_t *o1,
                                                     uint8_t *o2, uint8_t *o3,
                                                     size_t nblocks,
                                                     dap_keccak_x4_state_t *state)
{
    if (p->is_fips204)
        dap_hash_shake128_x4_squeezeblocks(o0, o1, o2, o3, nblocks, state);
    else
        dap_hash_shake128_x4_legacy_squeezeblocks(o0, o1, o2, o3, nblocks, state);
}
