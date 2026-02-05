/*
 * ECDSA Scalar Multiplication - Architecture Dispatcher
 * 
 * This header provides runtime dispatch to the best available
 * implementation based on detected CPU features.
 */

#ifndef ECDSA_SCALAR_MUL_ARCH_H
#define ECDSA_SCALAR_MUL_ARCH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "ecdsa_scalar.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Implementation IDs
// ============================================================================

typedef enum {
    ECDSA_SCALAR_IMPL_GENERIC = 0,     // Portable C with __uint128_t
    ECDSA_SCALAR_IMPL_X86_64_ASM,      // x86-64 inline assembly
    ECDSA_SCALAR_IMPL_AVX2_BMI2,       // AVX2 + BMI2 (MULX/ADCX/ADOX)
    ECDSA_SCALAR_IMPL_AVX512,          // AVX-512 (future)
    ECDSA_SCALAR_IMPL_ARM64_NEON,      // ARM64 NEON
    ECDSA_SCALAR_IMPL_ARM64_SVE,       // ARM64 SVE (future)
    ECDSA_SCALAR_IMPL_ARM32_NEON,      // ARM32 NEON (future)
    ECDSA_SCALAR_IMPL_COUNT
} ecdsa_scalar_impl_t;

// ============================================================================
// Function Pointers (for benchmarking individual implementations)
// ============================================================================

typedef void (*ecdsa_scalar_mul_512_fn)(uint64_t l[8], const ecdsa_scalar_t *a, const ecdsa_scalar_t *b);
typedef void (*ecdsa_scalar_mul_shift_384_fn)(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b);
typedef void (*ecdsa_scalar_reduce_512_fn)(ecdsa_scalar_t *r, const uint64_t l[8]);
typedef void (*ecdsa_scalar_mul_fn)(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b);

// ============================================================================
// Implementation Registry
// ============================================================================

typedef struct {
    const char *name;
    const char *description;
    ecdsa_scalar_impl_t id;
    bool available;  // Set at runtime based on CPU features
    
    // Function pointers
    ecdsa_scalar_mul_512_fn mul_512;
    ecdsa_scalar_mul_shift_384_fn mul_shift_384;
    ecdsa_scalar_reduce_512_fn reduce_512;
    ecdsa_scalar_mul_fn mul;
} ecdsa_scalar_impl_info_t;

// ============================================================================
// Dispatcher API
// ============================================================================

// Initialize dispatcher (detects CPU features, selects best implementation)
void ecdsa_scalar_dispatch_init(void);

// Get current active implementation
ecdsa_scalar_impl_t ecdsa_scalar_get_impl(void);

// Get implementation info
const ecdsa_scalar_impl_info_t* ecdsa_scalar_get_impl_info(ecdsa_scalar_impl_t impl);

// Get all implementations (for benchmarking)
const ecdsa_scalar_impl_info_t* ecdsa_scalar_get_all_impls(size_t *count);

// Force specific implementation (for testing/benchmarking)
bool ecdsa_scalar_set_impl(ecdsa_scalar_impl_t impl);

// ============================================================================
// Dispatched Functions (call these - they use best available implementation)
// ============================================================================

void ecdsa_scalar_mul_512_dispatch(uint64_t l[8], const ecdsa_scalar_t *a, const ecdsa_scalar_t *b);
void ecdsa_scalar_mul_shift_384_dispatch(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b);

// ============================================================================
// Individual Implementation Declarations
// ============================================================================

// Generic (always available)
void ecdsa_scalar_mul_512_generic(uint64_t l[8], const ecdsa_scalar_t *a, const ecdsa_scalar_t *b);
void ecdsa_scalar_mul_shift_384_generic(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b);
void ecdsa_scalar_reduce_512_generic(ecdsa_scalar_t *r, const uint64_t l[8]);
void ecdsa_scalar_mul_generic(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b);

#if defined(__x86_64__) || defined(_M_X64)
// x86-64 ASM
void ecdsa_scalar_mul_512_x86_64_asm(uint64_t l[8], const ecdsa_scalar_t *a, const ecdsa_scalar_t *b);
void ecdsa_scalar_mul_shift_384_x86_64_asm(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b);
void ecdsa_scalar_reduce_512_x86_64_asm(ecdsa_scalar_t *r, const uint64_t l[8]);
void ecdsa_scalar_mul_x86_64_asm(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b);

// AVX2 + BMI2
void ecdsa_scalar_mul_512_avx2_bmi2(uint64_t l[8], const ecdsa_scalar_t *a, const ecdsa_scalar_t *b);
void ecdsa_scalar_mul_shift_384_avx2_bmi2(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b);
void ecdsa_scalar_reduce_512_avx2_bmi2(ecdsa_scalar_t *r, const uint64_t l[8]);
void ecdsa_scalar_mul_avx2_bmi2(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b);
#endif

#if defined(__aarch64__)
// ARM64 NEON
void ecdsa_scalar_mul_512_neon(uint64_t l[8], const ecdsa_scalar_t *a, const ecdsa_scalar_t *b);
void ecdsa_scalar_mul_shift_384_neon(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b);
void ecdsa_scalar_reduce_512_neon(ecdsa_scalar_t *r, const uint64_t l[8]);
void ecdsa_scalar_mul_neon(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b);
#endif

#ifdef __cplusplus
}
#endif

#endif // ECDSA_SCALAR_MUL_ARCH_H
