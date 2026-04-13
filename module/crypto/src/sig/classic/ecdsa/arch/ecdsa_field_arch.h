/*
 * ECDSA Field Arithmetic - Architecture Dispatcher
 * 
 * This header provides runtime dispatch to the best available
 * implementation of field multiplication/squaring based on CPU features.
 */

#ifndef ECDSA_FIELD_ARCH_H
#define ECDSA_FIELD_ARCH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../ecdsa_field.h"
#include "dap_cpu_arch.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Implementation IDs
// ============================================================================

typedef enum {
    ECDSA_FIELD_IMPL_GENERIC = 0,      // Portable C with __uint128_t (interleaved reduction)
    ECDSA_FIELD_IMPL_X86_64_ASM,       // x86-64 inline assembly (MULQ)
    ECDSA_FIELD_IMPL_AVX2_BMI2,        // AVX2 + BMI2 (MULX/ADCX/ADOX)
    // NOTE: AVX-512 removed - IFMA doesn't help for interleaved reduction pattern
    ECDSA_FIELD_IMPL_ARM64_NEON,       // ARM64 NEON (MUL/UMULH)
    ECDSA_FIELD_IMPL_ARM64_SVE,        // ARM64 SVE (future)
    ECDSA_FIELD_IMPL_ARM32_NEON,       // ARM32 NEON (future)
    ECDSA_FIELD_IMPL_COUNT
} ecdsa_field_impl_t;

// ============================================================================
// Function Pointers
// ============================================================================

typedef void (*ecdsa_field_mul_fn)(ecdsa_field_t *r, const ecdsa_field_t *a, const ecdsa_field_t *b);
typedef void (*ecdsa_field_sqr_fn)(ecdsa_field_t *r, const ecdsa_field_t *a);

// ============================================================================
// Implementation Registry
// ============================================================================

typedef struct {
    const char *name;
    const char *description;
    ecdsa_field_impl_t id;
    bool available;  // Set at runtime based on CPU features
    
    // Function pointers
    ecdsa_field_mul_fn mul;
    ecdsa_field_sqr_fn sqr;
} ecdsa_field_impl_info_t;

// ============================================================================
// Dispatcher API
// ============================================================================

// Initialize dispatcher (detects CPU features, selects best implementation)
void ecdsa_field_dispatch_init(void);

// Get current active implementation
ecdsa_field_impl_t ecdsa_field_get_impl(void);

// Get implementation info
const ecdsa_field_impl_info_t* ecdsa_field_get_impl_info(ecdsa_field_impl_t impl);

// Get all implementations (for benchmarking)
const ecdsa_field_impl_info_t* ecdsa_field_get_all_impls(size_t *count);

// Force specific implementation (for testing/benchmarking)
bool ecdsa_field_set_impl(ecdsa_field_impl_t impl);

// ============================================================================
// Global dispatch function pointers (set by ecdsa_field_dispatch_init)
// ============================================================================

extern ecdsa_field_mul_fn ecdsa_field_mul_ptr;
extern ecdsa_field_sqr_fn ecdsa_field_sqr_ptr;

// ============================================================================
// Dispatched Functions (static inline for zero overhead after init)
// ============================================================================

static inline void ecdsa_field_mul_dispatch(ecdsa_field_t *r, const ecdsa_field_t *a, const ecdsa_field_t *b) {
    if (__builtin_expect(!ecdsa_field_mul_ptr, 0)) {
        ecdsa_field_dispatch_init();
    }
    ecdsa_field_mul_ptr(r, a, b);
}

static inline void ecdsa_field_sqr_dispatch(ecdsa_field_t *r, const ecdsa_field_t *a) {
    if (__builtin_expect(!ecdsa_field_sqr_ptr, 0)) {
        ecdsa_field_dispatch_init();
    }
    ecdsa_field_sqr_ptr(r, a);
}

// ============================================================================
// Individual Implementation Declarations
// ============================================================================

// Generic (always available)
void ecdsa_field_mul_generic(ecdsa_field_t *r, const ecdsa_field_t *a, const ecdsa_field_t *b);
void ecdsa_field_sqr_generic(ecdsa_field_t *r, const ecdsa_field_t *a);

#if DAP_PLATFORM_X86_64
void ecdsa_field_mul_x86_64_asm(ecdsa_field_t *r, const ecdsa_field_t *a, const ecdsa_field_t *b);
void ecdsa_field_sqr_x86_64_asm(ecdsa_field_t *r, const ecdsa_field_t *a);

void ecdsa_field_mul_avx2_bmi2(ecdsa_field_t *r, const ecdsa_field_t *a, const ecdsa_field_t *b);
void ecdsa_field_sqr_avx2_bmi2(ecdsa_field_t *r, const ecdsa_field_t *a);
#endif

#if DAP_PLATFORM_ARM64
void ecdsa_field_mul_neon(ecdsa_field_t *r, const ecdsa_field_t *a, const ecdsa_field_t *b);
void ecdsa_field_sqr_neon(ecdsa_field_t *r, const ecdsa_field_t *a);

#if !defined(__APPLE__)
void ecdsa_field_mul_sve(ecdsa_field_t *r, const ecdsa_field_t *a, const ecdsa_field_t *b);
void ecdsa_field_sqr_sve(ecdsa_field_t *r, const ecdsa_field_t *a);
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif // ECDSA_FIELD_ARCH_H
