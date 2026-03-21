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

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
void dap_hash_keccak_permute_sse2(dap_hash_keccak_state_t *state);
void dap_hash_keccak_permute_avx2(dap_hash_keccak_state_t *state);
void dap_hash_keccak_permute_avx512(dap_hash_keccak_state_t *state);
void dap_hash_keccak_permute_avx512vl_asm(dap_hash_keccak_state_t *state);
#endif

#if defined(__arm__) || defined(__aarch64__)
void dap_hash_keccak_permute_neon(dap_hash_keccak_state_t *state);
#if defined(__aarch64__)
void dap_hash_keccak_permute_neon_sha3_asm(dap_hash_keccak_state_t *state);
// SVE/SVE2 are NOT supported on Apple Silicon
#if !defined(__APPLE__)
void dap_hash_keccak_permute_sve(dap_hash_keccak_state_t *state);
void dap_hash_keccak_permute_sve2(dap_hash_keccak_state_t *state);
#endif
#endif
#endif

// ============================================================================
// Dispatch API (static inline for zero-overhead dispatch)
// ============================================================================

/**
 * @brief Initialize Keccak state to zero
 */
static inline void dap_hash_keccak_state_init(dap_hash_keccak_state_t *state)
{
    dap_hash_keccak_state_init_ref(state);
}

typedef void (*dap_hash_keccak_permute_fn_t)(dap_hash_keccak_state_t *);

/**
 * @brief Resolve best Keccak permutation once, cache the function pointer.
 */
static inline dap_hash_keccak_permute_fn_t s_keccak_resolve_permute(void)
{
    dap_cpu_arch_t arch = dap_cpu_arch_get();
    switch (arch) {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
        case DAP_CPU_ARCH_AVX512: return dap_hash_keccak_permute_avx512vl_asm;
        case DAP_CPU_ARCH_AVX2:   return dap_hash_keccak_permute_avx2;
        case DAP_CPU_ARCH_SSE2:   return dap_hash_keccak_permute_sse2;
#elif defined(__arm__) || defined(__aarch64__)
#if defined(__aarch64__)
        case DAP_CPU_ARCH_NEON:   return dap_hash_keccak_permute_neon_sha3_asm;
#else
        case DAP_CPU_ARCH_NEON:   return dap_hash_keccak_permute_neon;
#endif
#if defined(__aarch64__) && !defined(__APPLE__)
        case DAP_CPU_ARCH_SVE:    return dap_hash_keccak_permute_sve;
        case DAP_CPU_ARCH_SVE2:   return dap_hash_keccak_permute_sve2;
#endif
#endif
        default: return dap_hash_keccak_permute_ref;
    }
}

/**
 * @brief Apply Keccak-p[1600] permutation with cached SIMD dispatch.
 *
 * The function pointer is resolved on first call and cached, avoiding
 * repeated dap_cpu_arch_get() → dap_cpu_detect_features() overhead.
 */
static inline void dap_hash_keccak_permute(dap_hash_keccak_state_t *state)
{
    static dap_hash_keccak_permute_fn_t s_fn = NULL;
    if (__builtin_expect(s_fn == NULL, 0))
        s_fn = s_keccak_resolve_permute();
    s_fn(state);
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

#ifdef __cplusplus
}
#endif
