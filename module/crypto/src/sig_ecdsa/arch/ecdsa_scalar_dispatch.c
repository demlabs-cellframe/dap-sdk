/*
 * ECDSA Scalar Arithmetic - Runtime Dispatcher
 * 
 * Selects the best available implementation based on CPU features.
 * Uses dap_cpu_arch for unified CPU feature detection.
 */

#include "ecdsa_scalar_mul_arch.h"
#include "dap_cpu_arch.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// Implementation Registry
// ============================================================================

static ecdsa_scalar_impl_info_t s_implementations[] = {
    // Generic (always available)
    {
        .name = "generic",
        .description = "Portable C with __uint128_t",
        .id = ECDSA_SCALAR_IMPL_GENERIC,
        .available = true,
        .mul_512 = ecdsa_scalar_mul_512_generic,
        .mul_shift_384 = ecdsa_scalar_mul_shift_384_generic,
        .reduce_512 = ecdsa_scalar_reduce_512_generic,
        .mul = ecdsa_scalar_mul_generic,
    },
    
#if defined(__x86_64__) || defined(_M_X64)
    // x86-64 ASM
    {
        .name = "x86_64_asm",
        .description = "x86-64 inline assembly (MULQ)",
        .id = ECDSA_SCALAR_IMPL_X86_64_ASM,
        .available = true,  // Always available on x86-64
        .mul_512 = ecdsa_scalar_mul_512_x86_64_asm,
        .mul_shift_384 = ecdsa_scalar_mul_shift_384_x86_64_asm,
        .reduce_512 = ecdsa_scalar_reduce_512_x86_64_asm,
        .mul = ecdsa_scalar_mul_x86_64_asm,
    },
    
    // AVX2 + BMI2
    {
        .name = "avx2_bmi2",
        .description = "AVX2 + BMI2 (MULX/ADCX/ADOX)",
        .id = ECDSA_SCALAR_IMPL_AVX2_BMI2,
        .available = false,  // Set at runtime
        .mul_512 = ecdsa_scalar_mul_512_avx2_bmi2,
        .mul_shift_384 = ecdsa_scalar_mul_shift_384_avx2_bmi2,
        .reduce_512 = ecdsa_scalar_reduce_512_avx2_bmi2,
        .mul = ecdsa_scalar_mul_avx2_bmi2,
    },
    
    // AVX-512 IFMA
    {
        .name = "avx512",
        .description = "AVX-512 IFMA",
        .id = ECDSA_SCALAR_IMPL_AVX512,
        .available = false,  // Set at runtime
        .mul_512 = ecdsa_scalar_mul_512_avx512,
        .mul_shift_384 = ecdsa_scalar_mul_shift_384_avx512,
        .reduce_512 = ecdsa_scalar_reduce_512_avx512,
        .mul = ecdsa_scalar_mul_avx512,
    },
#endif

#if defined(__aarch64__)
    // ARM64 NEON
    {
        .name = "arm64_neon",
        .description = "ARM64 NEON (MUL/UMULH)",
        .id = ECDSA_SCALAR_IMPL_ARM64_NEON,
        .available = true,  // Always on ARM64
        .mul_512 = ecdsa_scalar_mul_512_neon,
        .mul_shift_384 = ecdsa_scalar_mul_shift_384_neon,
        .reduce_512 = ecdsa_scalar_reduce_512_neon,
        .mul = ecdsa_scalar_mul_neon,
    },
    
    #if !defined(__APPLE__)
    // ARM64 SVE (servers: Graviton3, Neoverse, Ampere)
    {
        .name = "arm64_sve",
        .description = "ARM64 SVE (scalable vectors)",
        .id = ECDSA_SCALAR_IMPL_ARM64_SVE,
        .available = false,  // Set at runtime
        .mul_512 = ecdsa_scalar_mul_512_sve,
        .mul_shift_384 = ecdsa_scalar_mul_shift_384_sve,
        .reduce_512 = ecdsa_scalar_reduce_512_sve,
        .mul = ecdsa_scalar_mul_sve,
    },
    #endif
#endif
};

static const size_t s_num_implementations = sizeof(s_implementations) / sizeof(s_implementations[0]);

// Current active implementation
static ecdsa_scalar_impl_t s_current_impl = ECDSA_SCALAR_IMPL_GENERIC;
static bool s_initialized = false;

// Global dispatch function pointers (used by static inline dispatchers in header)
ecdsa_scalar_mul_512_fn ecdsa_scalar_mul_512_ptr = NULL;
ecdsa_scalar_mul_shift_384_fn ecdsa_scalar_mul_shift_384_ptr = NULL;
ecdsa_scalar_reduce_512_fn ecdsa_scalar_reduce_512_ptr = NULL;
ecdsa_scalar_mul_fn ecdsa_scalar_mul_arch_ptr = NULL;

// ============================================================================
// Dispatcher Implementation
// ============================================================================

static void s_update_function_pointers(void) {
    const ecdsa_scalar_impl_info_t *impl = ecdsa_scalar_get_impl_info(s_current_impl);
    if (impl) {
        ecdsa_scalar_mul_512_ptr = impl->mul_512;
        ecdsa_scalar_mul_shift_384_ptr = impl->mul_shift_384;
        ecdsa_scalar_reduce_512_ptr = impl->reduce_512;
        ecdsa_scalar_mul_arch_ptr = impl->mul;
    } else {
        // Fallback to generic
        ecdsa_scalar_mul_512_ptr = ecdsa_scalar_mul_512_generic;
        ecdsa_scalar_mul_shift_384_ptr = ecdsa_scalar_mul_shift_384_generic;
        ecdsa_scalar_reduce_512_ptr = ecdsa_scalar_reduce_512_generic;
        ecdsa_scalar_mul_arch_ptr = ecdsa_scalar_mul_generic;
    }
}

void ecdsa_scalar_dispatch_init(void) {
    if (s_initialized) return;
    
    // Use dap_cpu_arch for unified platform/feature detection
    dap_cpu_arch_t best = dap_cpu_arch_get_best();
    
    // Update availability and select implementation based on dap_cpu_arch
    for (size_t i = 0; i < s_num_implementations; i++) {
        switch (s_implementations[i].id) {
#if defined(__x86_64__) || defined(_M_X64)
            case ECDSA_SCALAR_IMPL_AVX2_BMI2:
                s_implementations[i].available = dap_cpu_arch_is_available(DAP_CPU_ARCH_AVX2);
                break;
            case ECDSA_SCALAR_IMPL_AVX512:
                s_implementations[i].available = dap_cpu_arch_is_available(DAP_CPU_ARCH_AVX512);
                break;
#endif
#if defined(__aarch64__) && !defined(__APPLE__)
            case ECDSA_SCALAR_IMPL_ARM64_SVE:
                s_implementations[i].available = dap_cpu_arch_is_available(DAP_CPU_ARCH_SVE);
                break;
#endif
            default:
                break;  // Keep compile-time availability
        }
    }
    
    // Select best implementation based on detected architecture
    switch (best) {
#if defined(__x86_64__) || defined(_M_X64)
        case DAP_CPU_ARCH_AVX512:
            s_current_impl = ECDSA_SCALAR_IMPL_AVX512;
            break;
        case DAP_CPU_ARCH_AVX2:
            s_current_impl = ECDSA_SCALAR_IMPL_AVX2_BMI2;
            break;
        case DAP_CPU_ARCH_SSE2:
            s_current_impl = ECDSA_SCALAR_IMPL_X86_64_ASM;
            break;
#endif
#if defined(__aarch64__)
    #if !defined(__APPLE__)
        case DAP_CPU_ARCH_SVE2:
        case DAP_CPU_ARCH_SVE:
            s_current_impl = ECDSA_SCALAR_IMPL_ARM64_SVE;
            break;
    #endif
        case DAP_CPU_ARCH_NEON:
            s_current_impl = ECDSA_SCALAR_IMPL_ARM64_NEON;
            break;
#endif
        default:
            s_current_impl = ECDSA_SCALAR_IMPL_GENERIC;
            break;
    }
    
    s_update_function_pointers();
    s_initialized = true;
}

ecdsa_scalar_impl_t ecdsa_scalar_get_impl(void) {
    if (!s_initialized) ecdsa_scalar_dispatch_init();
    return s_current_impl;
}

const ecdsa_scalar_impl_info_t* ecdsa_scalar_get_impl_info(ecdsa_scalar_impl_t impl) {
    for (size_t i = 0; i < s_num_implementations; i++) {
        if (s_implementations[i].id == impl) {
            return &s_implementations[i];
        }
    }
    return NULL;
}

const ecdsa_scalar_impl_info_t* ecdsa_scalar_get_all_impls(size_t *count) {
    if (!s_initialized) ecdsa_scalar_dispatch_init();
    if (count) *count = s_num_implementations;
    return s_implementations;
}

bool ecdsa_scalar_set_impl(ecdsa_scalar_impl_t impl) {
    if (!s_initialized) ecdsa_scalar_dispatch_init();
    
    for (size_t i = 0; i < s_num_implementations; i++) {
        if (s_implementations[i].id == impl && s_implementations[i].available) {
            s_current_impl = impl;
            s_update_function_pointers();
            return true;
        }
    }
    return false;
}
