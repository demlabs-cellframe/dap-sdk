/*
 * ECDSA Field Arithmetic - Architecture Dispatcher Implementation
 * 
 * Runtime detection and dispatch to optimal field mul/sqr implementation.
 */

#include <string.h>
#include <pthread.h>
#include "ecdsa_field_arch.h"
#include "dap_cpu_arch.h"
#include "dap_cpu_detect.h"
#include "dap_arch_dispatch.h"

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
#if DAP_PLATFORM_X86_64
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
#if DAP_PLATFORM_ARM64
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
// Dispatcher Implementation
// ============================================================================

static void s_field_dispatch_impl(void) {
#if DAP_PLATFORM_X86_64
    dap_cpu_features_t l_feat = dap_cpu_detect_features();
    s_impls[ECDSA_FIELD_IMPL_X86_64_ASM].available = true;
    s_impls[ECDSA_FIELD_IMPL_AVX2_BMI2].available = l_feat.has_avx2 && l_feat.has_bmi2;
#endif

#if DAP_PLATFORM_ARM64
    s_impls[ECDSA_FIELD_IMPL_ARM64_NEON].available = dap_cpu_arch_is_available(DAP_CPU_ARCH_NEON);
    #if !defined(__APPLE__)
    s_impls[ECDSA_FIELD_IMPL_ARM64_SVE].available = dap_cpu_arch_is_available(DAP_CPU_ARCH_SVE);
    #endif
#endif

    DAP_DISPATCH_DEFAULT(ecdsa_field_mul, ecdsa_field_mul_generic);
    DAP_DISPATCH_DEFAULT(ecdsa_field_sqr, ecdsa_field_sqr_generic);
    DAP_DISPATCH_ARCH_SELECT;

    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2, ecdsa_field_mul, ecdsa_field_mul_avx2_bmi2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2, ecdsa_field_sqr, ecdsa_field_sqr_avx2_bmi2);
#if DAP_PLATFORM_X86_64
    if (ecdsa_field_mul_ptr == ecdsa_field_mul_generic) {
        ecdsa_field_mul_ptr = ecdsa_field_mul_x86_64_asm;
        ecdsa_field_sqr_ptr = ecdsa_field_sqr_x86_64_asm;
    }
#endif

    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON, ecdsa_field_mul, ecdsa_field_mul_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON, ecdsa_field_sqr, ecdsa_field_sqr_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_SVE, ecdsa_field_mul, ecdsa_field_mul_sve);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_SVE, ecdsa_field_sqr, ecdsa_field_sqr_sve);

    s_current_impl = ECDSA_FIELD_IMPL_GENERIC;
#if DAP_PLATFORM_X86_64
    if (ecdsa_field_mul_ptr == ecdsa_field_mul_avx2_bmi2)
        s_current_impl = ECDSA_FIELD_IMPL_AVX2_BMI2;
    else if (ecdsa_field_mul_ptr == ecdsa_field_mul_x86_64_asm)
        s_current_impl = ECDSA_FIELD_IMPL_X86_64_ASM;
#endif
#if DAP_PLATFORM_ARM64
    if (ecdsa_field_mul_ptr == ecdsa_field_mul_neon)
        s_current_impl = ECDSA_FIELD_IMPL_ARM64_NEON;
#if !defined(__APPLE__)
    else if (ecdsa_field_mul_ptr == ecdsa_field_mul_sve)
        s_current_impl = ECDSA_FIELD_IMPL_ARM64_SVE;
#endif
#endif

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
