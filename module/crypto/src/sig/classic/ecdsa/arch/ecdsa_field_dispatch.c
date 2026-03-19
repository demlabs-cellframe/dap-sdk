/*
 * ECDSA Field Arithmetic - Architecture Dispatcher Implementation
 * 
 * Runtime detection and dispatch to optimal field mul/sqr implementation.
 */

#include <string.h>
#include <pthread.h>
#include "ecdsa_field_arch.h"
#include "dap_cpu_arch.h"

#if defined(__x86_64__) || defined(_M_X64)
#include <cpuid.h>
#endif

// ============================================================================
// Global Function Pointers
// ============================================================================

ecdsa_field_mul_fn ecdsa_field_mul_ptr = NULL;
ecdsa_field_sqr_fn ecdsa_field_sqr_ptr = NULL;

// ============================================================================
// Implementation Registry
// ============================================================================

static ecdsa_field_impl_info_t s_impls[ECDSA_FIELD_IMPL_COUNT] = {
    [ECDSA_FIELD_IMPL_GENERIC] = {
        .name = "generic",
        .description = "Portable C with uint128 (interleaved reduction)",
        .id = ECDSA_FIELD_IMPL_GENERIC,
        .available = true,  // Always available
        .mul = ecdsa_field_mul_generic,
        .sqr = ecdsa_field_sqr_generic
    },
#if defined(__x86_64__) || defined(_M_X64)
    [ECDSA_FIELD_IMPL_X86_64_ASM] = {
        .name = "x86_64_asm",
        .description = "x86-64 inline assembly (MULQ)",
        .id = ECDSA_FIELD_IMPL_X86_64_ASM,
        .available = false,  // Set at runtime
        .mul = ecdsa_field_mul_x86_64_asm,
        .sqr = ecdsa_field_sqr_x86_64_asm
    },
    [ECDSA_FIELD_IMPL_AVX2_BMI2] = {
        .name = "avx2_bmi2",
        .description = "AVX2 + BMI2 (MULX with uint128 accum)",
        .id = ECDSA_FIELD_IMPL_AVX2_BMI2,
        .available = false,  // Set at runtime
        .mul = ecdsa_field_mul_avx2_bmi2,
        .sqr = ecdsa_field_sqr_avx2_bmi2
    },
#endif
#if defined(__aarch64__)
    [ECDSA_FIELD_IMPL_ARM64_NEON] = {
        .name = "neon",
        .description = "ARM64 NEON",
        .id = ECDSA_FIELD_IMPL_ARM64_NEON,
        .available = false,  // Set at runtime
        .mul = ecdsa_field_mul_neon,
        .sqr = ecdsa_field_sqr_neon
    },
    #if !defined(__APPLE__)
    [ECDSA_FIELD_IMPL_ARM64_SVE] = {
        .name = "sve",
        .description = "ARM64 SVE (scalable vectors)",
        .id = ECDSA_FIELD_IMPL_ARM64_SVE,
        .available = false,  // Set at runtime
        .mul = ecdsa_field_mul_sve,
        .sqr = ecdsa_field_sqr_sve
    },
    #endif
#endif
};

static ecdsa_field_impl_t s_current_impl = ECDSA_FIELD_IMPL_GENERIC;
static bool s_initialized = false;
static pthread_once_t s_field_once = PTHREAD_ONCE_INIT;

// ============================================================================
// CPU Feature Detection
// ============================================================================

#if defined(__x86_64__) || defined(_M_X64)
static void detect_x86_features(void) {
    unsigned int eax, ebx, ecx, edx;
    
    // Basic CPUID info
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        // Check for SSE2 (always present on x86-64)
        s_impls[ECDSA_FIELD_IMPL_X86_64_ASM].available = true;
    }
    
    // Extended features (leaf 7)
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        // AVX2: bit 5 of EBX
        // BMI2: bit 8 of EBX  
        // ADX:  bit 19 of EBX
        bool has_avx2 = (ebx >> 5) & 1;
        bool has_bmi2 = (ebx >> 8) & 1;
        
        // AVX2+BMI2 version uses MULX with __uint128_t accumulation
        // ADX not required since we don't use ADCX/ADOX anymore
        s_impls[ECDSA_FIELD_IMPL_AVX2_BMI2].available = has_avx2 && has_bmi2;
    }
}
#endif

// ============================================================================
// Dispatcher Implementation
// ============================================================================

static void s_field_dispatch_impl(void) {
#if defined(__x86_64__) || defined(_M_X64)
    detect_x86_features();
#endif

#if defined(__aarch64__)
    s_impls[ECDSA_FIELD_IMPL_ARM64_NEON].available = dap_cpu_arch_is_available(DAP_CPU_ARCH_NEON);
    #if !defined(__APPLE__)
    s_impls[ECDSA_FIELD_IMPL_ARM64_SVE].available = dap_cpu_arch_is_available(DAP_CPU_ARCH_SVE);
    #endif
#endif

    s_current_impl = ECDSA_FIELD_IMPL_GENERIC;

    dap_cpu_arch_t best = dap_cpu_arch_get_best();
    switch (best) {
#if defined(__x86_64__) || defined(_M_X64)
        case DAP_CPU_ARCH_AVX2:
            if (s_impls[ECDSA_FIELD_IMPL_AVX2_BMI2].available)
                s_current_impl = ECDSA_FIELD_IMPL_AVX2_BMI2;
            break;
#endif
#if defined(__aarch64__)
    #if !defined(__APPLE__)
        case DAP_CPU_ARCH_SVE2:
        case DAP_CPU_ARCH_SVE:
            if (s_impls[ECDSA_FIELD_IMPL_ARM64_SVE].available) {
                s_current_impl = ECDSA_FIELD_IMPL_ARM64_SVE;
                break;
            }
            /* fallthrough */
    #endif
        case DAP_CPU_ARCH_NEON:
            if (s_impls[ECDSA_FIELD_IMPL_ARM64_NEON].available)
                s_current_impl = ECDSA_FIELD_IMPL_ARM64_NEON;
            break;
#endif
        default:
            break;
    }

#if defined(__x86_64__) || defined(_M_X64)
    if (s_current_impl == ECDSA_FIELD_IMPL_GENERIC &&
        s_impls[ECDSA_FIELD_IMPL_X86_64_ASM].available) {
        s_current_impl = ECDSA_FIELD_IMPL_X86_64_ASM;
    }
#endif
    
    ecdsa_field_mul_ptr = s_impls[s_current_impl].mul;
    ecdsa_field_sqr_ptr = s_impls[s_current_impl].sqr;
    
    s_initialized = true;
}

void ecdsa_field_dispatch_init(void) {
    pthread_once(&s_field_once, s_field_dispatch_impl);
}

ecdsa_field_impl_t ecdsa_field_get_impl(void) {
    ecdsa_field_dispatch_init();
    return s_current_impl;
}

const ecdsa_field_impl_info_t* ecdsa_field_get_impl_info(ecdsa_field_impl_t impl) {
    if (impl >= ECDSA_FIELD_IMPL_COUNT) return NULL;
    return &s_impls[impl];
}

const ecdsa_field_impl_info_t* ecdsa_field_get_all_impls(size_t *count) {
    if (count) *count = ECDSA_FIELD_IMPL_COUNT;
    return s_impls;
}

bool ecdsa_field_set_impl(ecdsa_field_impl_t impl) {
    ecdsa_field_dispatch_init();
    if (impl >= ECDSA_FIELD_IMPL_COUNT) return false;
    if (!s_impls[impl].available) return false;
    
    s_current_impl = impl;
    ecdsa_field_mul_ptr = s_impls[impl].mul;
    ecdsa_field_sqr_ptr = s_impls[impl].sqr;
    return true;
}

// ============================================================================
// Public API - dispatched field multiplication and squaring
// ============================================================================

void ecdsa_field_mul(ecdsa_field_t *r, const ecdsa_field_t *a, const ecdsa_field_t *b) {
    ecdsa_field_mul_dispatch(r, a, b);
}

void ecdsa_field_sqr(ecdsa_field_t *r, const ecdsa_field_t *a) {
    ecdsa_field_sqr_dispatch(r, a);
}
