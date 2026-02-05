/*
 * ECDSA Scalar Arithmetic - Runtime Dispatcher
 * 
 * Selects the best available implementation based on CPU features.
 */

#include "ecdsa_scalar_mul_arch.h"
#include <string.h>
#include <stdio.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <cpuid.h>

static bool s_has_bmi2 = false;
static bool s_has_adx = false;
static bool s_has_avx2 = false;

static void detect_x86_features(void) {
    unsigned int eax, ebx, ecx, edx;
    
    // Check for BMI2 and AVX2 (function 7, ecx=0)
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        s_has_bmi2 = (ebx >> 8) & 1;   // BMI2 bit
        s_has_avx2 = (ebx >> 5) & 1;   // AVX2 bit
        s_has_adx = (ebx >> 19) & 1;   // ADX bit
    }
}
#endif

#if defined(__aarch64__)
// ARM64 always has NEON
static bool s_has_neon = true;
#endif

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
#endif
};

static const size_t s_num_implementations = sizeof(s_implementations) / sizeof(s_implementations[0]);

// Current active implementation
static ecdsa_scalar_impl_t s_current_impl = ECDSA_SCALAR_IMPL_GENERIC;
static bool s_initialized = false;

// ============================================================================
// Dispatcher Implementation
// ============================================================================

void ecdsa_scalar_dispatch_init(void) {
    if (s_initialized) return;
    
#if defined(__x86_64__) || defined(_M_X64)
    detect_x86_features();
    
    // Update availability
    for (size_t i = 0; i < s_num_implementations; i++) {
        if (s_implementations[i].id == ECDSA_SCALAR_IMPL_AVX2_BMI2) {
            s_implementations[i].available = s_has_bmi2 && s_has_adx && s_has_avx2;
        }
    }
    
    // Select best available (highest performance)
    if (s_has_bmi2 && s_has_adx && s_has_avx2) {
        s_current_impl = ECDSA_SCALAR_IMPL_AVX2_BMI2;
    } else {
        s_current_impl = ECDSA_SCALAR_IMPL_X86_64_ASM;
    }
#elif defined(__aarch64__)
    s_current_impl = ECDSA_SCALAR_IMPL_ARM64_NEON;
#else
    s_current_impl = ECDSA_SCALAR_IMPL_GENERIC;
#endif
    
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
            return true;
        }
    }
    return false;
}

// ============================================================================
// Dispatched Functions
// ============================================================================

void ecdsa_scalar_mul_512_dispatch(uint64_t l[8], const ecdsa_scalar_t *a, const ecdsa_scalar_t *b) {
    if (!s_initialized) ecdsa_scalar_dispatch_init();
    
    const ecdsa_scalar_impl_info_t *impl = ecdsa_scalar_get_impl_info(s_current_impl);
    if (impl && impl->mul_512) {
        impl->mul_512(l, a, b);
    } else {
        ecdsa_scalar_mul_512_generic(l, a, b);
    }
}

void ecdsa_scalar_mul_shift_384_dispatch(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b) {
    if (!s_initialized) ecdsa_scalar_dispatch_init();
    
    const ecdsa_scalar_impl_info_t *impl = ecdsa_scalar_get_impl_info(s_current_impl);
    if (impl && impl->mul_shift_384) {
        impl->mul_shift_384(r, a, b);
    } else {
        ecdsa_scalar_mul_shift_384_generic(r, a, b);
    }
}
