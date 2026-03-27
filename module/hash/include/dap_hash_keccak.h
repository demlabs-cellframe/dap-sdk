/**
 * @file dap_hash_keccak.h
 * @brief DAP Keccak-p[1600] permutation API
 * @details Core Keccak permutation with runtime SIMD dispatch.
 *          Used internally by SHA3/SHAKE implementations.
 *
 * Keccak-p[1600] is the core permutation:
 *   - State: 1600 bits = 25 lanes × 64 bits = 5×5 array of uint64_t
 *   - 24 rounds of: θ → ρ → π → χ → ι
 *
 * Architecture dispatch (like dap_json):
 *   x86/x64: AVX-512 → AVX2 → SSE2 → Reference C
 *   ARM:     SVE2 → SVE → NEON → Reference C
 *
 * @author DAP SDK Team
 * @copyright DeM Labs Inc. 2026
 * @license GNU GPL v3
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "dap_cpu_arch.h"
#include "dap_arch_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Constants
// ============================================================================

#define DAP_KECCAK_STATE_SIZE     25      // 25 lanes (5×5)
#define DAP_KECCAK_STATE_BYTES    200     // 25 × 8 = 200 bytes = 1600 bits
#define DAP_KECCAK_ROUNDS         24      // Number of rounds for Keccak-p[1600]

// ============================================================================
// Types
// ============================================================================

/**
 * @brief Keccak state - 25 lanes of 64 bits each (1600 bits total)
 */
typedef struct dap_hash_keccak_state {
    uint64_t lanes[DAP_KECCAK_STATE_SIZE];
} dap_hash_keccak_state_t;

/**
 * @brief Keccak sponge context for streaming operations
 */
typedef struct dap_hash_keccak_ctx {
    dap_hash_keccak_state_t state;
    size_t rate;              // Rate in bytes
    size_t block_idx;         // Current position in rate block
    uint8_t suffix;           // Domain separation suffix
    uint8_t absorbing;        // 1 = absorbing phase, 0 = squeezing phase
} dap_hash_keccak_ctx_t;

// ============================================================================
// Core permutation functions - Reference implementation (always available)
// ============================================================================

void dap_hash_keccak_state_init_ref(dap_hash_keccak_state_t *state);
void dap_hash_keccak_permute_ref(dap_hash_keccak_state_t *state);
void dap_hash_keccak_permute_rounds_ref(dap_hash_keccak_state_t *state, unsigned rounds);
void dap_hash_keccak_xor_bytes(dap_hash_keccak_state_t *state, const uint8_t *data, size_t len);
void dap_hash_keccak_extract_bytes(const dap_hash_keccak_state_t *state, uint8_t *out, size_t len);

// ============================================================================
// SIMD implementation declarations (conditionally compiled)
// ============================================================================

#if DAP_PLATFORM_X86
void dap_hash_keccak_permute_sse2(dap_hash_keccak_state_t *state);
void dap_hash_keccak_permute_avx2(dap_hash_keccak_state_t *state);
void dap_hash_keccak_permute_avx512(dap_hash_keccak_state_t *state);
void dap_hash_keccak_permute_avx512vl_asm(dap_hash_keccak_state_t *state);
void dap_hash_keccak_permute_avx512vl_pt(dap_hash_keccak_state_t *state);

void dap_keccak_absorb_136_avx512vl_asm(uint64_t *state, const uint8_t *data, size_t len, uint8_t suffix);
void dap_keccak_absorb_168_avx512vl_asm(uint64_t *state, const uint8_t *data, size_t len, uint8_t suffix);
void dap_keccak_absorb_72_avx512vl_asm(uint64_t *state, const uint8_t *data, size_t len, uint8_t suffix);
void dap_keccak_squeeze_136_avx512vl_asm(uint64_t *state, uint8_t *out, size_t nblocks);
void dap_keccak_squeeze_168_avx512vl_asm(uint64_t *state, uint8_t *out, size_t nblocks);
void dap_keccak_squeeze_72_avx512vl_asm(uint64_t *state, uint8_t *out, size_t nblocks);

void dap_hash_keccak_permute_scalar_bmi2(dap_hash_keccak_state_t *state);
void dap_keccak_absorb_136_scalar_bmi2(uint64_t *state, const uint8_t *data, size_t len, uint8_t suffix);
void dap_keccak_absorb_168_scalar_bmi2(uint64_t *state, const uint8_t *data, size_t len, uint8_t suffix);
void dap_keccak_absorb_72_scalar_bmi2(uint64_t *state, const uint8_t *data, size_t len, uint8_t suffix);
void dap_keccak_squeeze_136_scalar_bmi2(uint64_t *state, uint8_t *out, size_t nblocks);
void dap_keccak_squeeze_168_scalar_bmi2(uint64_t *state, uint8_t *out, size_t nblocks);
void dap_keccak_squeeze_72_scalar_bmi2(uint64_t *state, uint8_t *out, size_t nblocks);
#endif

#if DAP_PLATFORM_ARM
void dap_hash_keccak_permute_neon(dap_hash_keccak_state_t *state);
#if defined(__aarch64__)
void dap_hash_keccak_permute_neon_sha3_asm(dap_hash_keccak_state_t *state);
#if !defined(__APPLE__)
void dap_hash_keccak_permute_sve(dap_hash_keccak_state_t *state);
void dap_hash_keccak_permute_sve2(dap_hash_keccak_state_t *state);
#endif
#endif
#endif

// ============================================================================
// Dispatch API — uses standard DAP dispatch infrastructure
// ============================================================================

/**
 * @brief Initialize Keccak state to zero
 */
static inline void dap_hash_keccak_state_init(dap_hash_keccak_state_t *state)
{
    dap_hash_keccak_state_init_ref(state);
}

DAP_DISPATCH_DECLARE_RESOLVE(dap_hash_keccak_permute, void, dap_hash_keccak_state_t *);

/**
 * @brief Resolve best Keccak permutation for the current CPU.
 *
 * Uses KECCAK algo class for vendor-aware dispatch via tune rules.
 * Highest matching arch wins (>= semantics).
 */
static inline dap_hash_keccak_permute_fn_t dap_hash_keccak_permute_resolve(void)
{
    dap_algo_class_t l_class = dap_algo_class_register("KECCAK");
    dap_cpu_arch_t arch = dap_cpu_arch_get_best_for(l_class);

    DAP_DISPATCH_RESOLVE_X86(DAP_CPU_ARCH_AVX512, dap_hash_keccak_permute_avx512vl_asm);
    DAP_DISPATCH_RESOLVE_X86(DAP_CPU_ARCH_AVX2,   dap_hash_keccak_permute_scalar_bmi2);
    DAP_DISPATCH_RESOLVE_X86(DAP_CPU_ARCH_SSE2,   dap_hash_keccak_permute_sse2);

#if DAP_PLATFORM_ARM && defined(__aarch64__)
#if !defined(__APPLE__)
    DAP_DISPATCH_RESOLVE_ARM(DAP_CPU_ARCH_SVE2,   dap_hash_keccak_permute_sve2);
    DAP_DISPATCH_RESOLVE_ARM(DAP_CPU_ARCH_SVE,    dap_hash_keccak_permute_sve);
#endif
    DAP_DISPATCH_RESOLVE_ARM(DAP_CPU_ARCH_NEON,   dap_hash_keccak_permute_neon_sha3_asm);
#elif DAP_PLATFORM_ARM
    DAP_DISPATCH_RESOLVE_ARM(DAP_CPU_ARCH_NEON,   dap_hash_keccak_permute_neon);
#endif

    return dap_hash_keccak_permute_ref;
}

/**
 * @brief Apply Keccak-p[1600] permutation with cached SIMD dispatch.
 *
 * First call: resolve + cache. Subsequent: one predicted indirect call.
 */
static inline void dap_hash_keccak_permute(dap_hash_keccak_state_t *state)
{
    DAP_DISPATCH_INLINE_CALL(dap_hash_keccak_permute, state);
}

/**
 * @brief Get current implementation name (for debugging/benchmarks)
 */
static inline const char *dap_hash_keccak_get_impl_name(void)
{
    dap_cpu_arch_t arch = dap_cpu_arch_get();
    return dap_cpu_arch_get_name(arch);
}

// ============================================================================
// SHA3/SHAKE rate and suffix constants
// ============================================================================

#define DAP_KECCAK_SHA3_224_RATE  144   // (1600 - 2×224) / 8
#define DAP_KECCAK_SHA3_256_RATE  136   // (1600 - 2×256) / 8
#define DAP_KECCAK_SHA3_384_RATE  104   // (1600 - 2×384) / 8
#define DAP_KECCAK_SHA3_512_RATE  72    // (1600 - 2×512) / 8
#define DAP_KECCAK_SHAKE128_RATE  168   // (1600 - 2×128) / 8
#define DAP_KECCAK_SHAKE256_RATE  136   // (1600 - 2×256) / 8

#define DAP_KECCAK_SHA3_SUFFIX    0x06  // SHA-3: 01|1
#define DAP_KECCAK_SHAKE_SUFFIX   0x1F  // SHAKE: 1111|1

// ============================================================================
// Sponge Construction API
// ============================================================================

/**
 * @brief Initialize sponge context
 * @param ctx Context to initialize
 * @param rate Rate in bytes (determines security level)
 * @param suffix Domain separation suffix (SHA3 or SHAKE)
 */
void dap_hash_keccak_sponge_init(dap_hash_keccak_ctx_t *ctx, size_t rate, uint8_t suffix);

/**
 * @brief Absorb data into sponge
 */
void dap_hash_keccak_sponge_absorb(dap_hash_keccak_ctx_t *ctx, const uint8_t *data, size_t len);

/**
 * @brief Finalize absorbing and switch to squeezing
 */
void dap_hash_keccak_sponge_finalize(dap_hash_keccak_ctx_t *ctx);

/**
 * @brief Squeeze output from sponge (can be called multiple times)
 */
void dap_hash_keccak_sponge_squeeze(dap_hash_keccak_ctx_t *ctx, uint8_t *out, size_t len);

// ============================================================================
// Fused sponge dispatch — absorb/squeeze for specific rates
// ============================================================================

typedef void (*dap_keccak_absorb_fn_t)(uint64_t *, const uint8_t *, size_t, uint8_t);
typedef void (*dap_keccak_squeeze_fn_t)(uint64_t *, uint8_t *, size_t);

void dap_keccak_absorb_136_ref(uint64_t *state, const uint8_t *data, size_t len, uint8_t suffix);
void dap_keccak_absorb_168_ref(uint64_t *state, const uint8_t *data, size_t len, uint8_t suffix);
void dap_keccak_absorb_72_ref(uint64_t *state, const uint8_t *data, size_t len, uint8_t suffix);
void dap_keccak_squeeze_136_ref(uint64_t *state, uint8_t *out, size_t nblocks);
void dap_keccak_squeeze_168_ref(uint64_t *state, uint8_t *out, size_t nblocks);
void dap_keccak_squeeze_72_ref(uint64_t *state, uint8_t *out, size_t nblocks);

typedef struct {
    dap_keccak_absorb_fn_t  absorb_136, absorb_168, absorb_72;
    dap_keccak_squeeze_fn_t squeeze_136, squeeze_168, squeeze_72;
} dap_keccak_sponge_ops_t;

static inline dap_keccak_sponge_ops_t dap_keccak_sponge_resolve(void)
{
    dap_algo_class_t l_class = dap_algo_class_register("KECCAK_SPONGE");
    dap_cpu_arch_t arch = dap_cpu_arch_get_best_for(l_class);

    dap_keccak_sponge_ops_t ops = {
        .absorb_136  = dap_keccak_absorb_136_ref,
        .absorb_168  = dap_keccak_absorb_168_ref,
        .absorb_72   = dap_keccak_absorb_72_ref,
        .squeeze_136 = dap_keccak_squeeze_136_ref,
        .squeeze_168 = dap_keccak_squeeze_168_ref,
        .squeeze_72  = dap_keccak_squeeze_72_ref,
    };

#if DAP_PLATFORM_X86
    if (__builtin_expect(arch >= DAP_CPU_ARCH_AVX2, 1)) {
        ops.absorb_136  = dap_keccak_absorb_136_scalar_bmi2;
        ops.absorb_168  = dap_keccak_absorb_168_scalar_bmi2;
        ops.absorb_72   = dap_keccak_absorb_72_scalar_bmi2;
        ops.squeeze_136 = dap_keccak_squeeze_136_scalar_bmi2;
        ops.squeeze_168 = dap_keccak_squeeze_168_scalar_bmi2;
        ops.squeeze_72  = dap_keccak_squeeze_72_scalar_bmi2;
    }
    if (__builtin_expect(arch >= DAP_CPU_ARCH_AVX512, 1)) {
        ops.absorb_136  = dap_keccak_absorb_136_avx512vl_asm;
        ops.absorb_168  = dap_keccak_absorb_168_avx512vl_asm;
        ops.absorb_72   = dap_keccak_absorb_72_avx512vl_asm;
        ops.squeeze_136 = dap_keccak_squeeze_136_avx512vl_asm;
        ops.squeeze_168 = dap_keccak_squeeze_168_avx512vl_asm;
        ops.squeeze_72  = dap_keccak_squeeze_72_avx512vl_asm;
    }
#endif

    return ops;
}

static dap_keccak_sponge_ops_t s_keccak_sponge_ops;
static int s_keccak_sponge_resolved = 0;

static inline const dap_keccak_sponge_ops_t *dap_keccak_sponge_get_ops(void)
{
    if (__builtin_expect(!s_keccak_sponge_resolved, 0)) {
        s_keccak_sponge_ops = dap_keccak_sponge_resolve();
        s_keccak_sponge_resolved = 1;
    }
    return &s_keccak_sponge_ops;
}

#ifdef __cplusplus
}
#endif
