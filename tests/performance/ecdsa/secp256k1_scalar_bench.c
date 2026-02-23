/*
 * bitcoin-core/secp256k1 internal scalar benchmark wrapper
 * 
 * This file includes the internal scalar implementation to allow
 * direct benchmarking of scalar operations for comparison.
 */

#ifdef HAVE_SECP256K1_COMPETITOR

// Required defines for secp256k1 internals BEFORE including their headers
#define SECP256K1_WIDEMUL_INT128 1

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// Include secp256k1 util.h for proper macros
#include "secp256k1/src/util.h"

// No-op verify for scalar operations
#undef SECP256K1_SCALAR_VERIFY
#define SECP256K1_SCALAR_VERIFY(r) ((void)(r))

// Include int128 native implementation
#include "secp256k1/src/int128.h"
#include "secp256k1/src/int128_native_impl.h"

// secp256k1 scalar verify (no-op)
static void secp256k1_scalar_verify(const void *r) { (void)r; }

// Include scalar implementation
#include "secp256k1/src/scalar_4x64.h"
#include "secp256k1/src/scalar_4x64_impl.h"

// ============================================================================
// Exported benchmark functions
// ============================================================================

void secp256k1_bench_scalar_set_b32(void *r, const unsigned char *b32) {
    secp256k1_scalar_set_b32((secp256k1_scalar*)r, b32, NULL);
}

void secp256k1_bench_scalar_mul(void *r, const void *a, const void *b) {
    secp256k1_scalar_mul((secp256k1_scalar*)r, 
                         (const secp256k1_scalar*)a, 
                         (const secp256k1_scalar*)b);
}

void secp256k1_bench_scalar_mul_shift_var(void *r, const void *a, const void *b, unsigned int shift) {
    secp256k1_scalar_mul_shift_var((secp256k1_scalar*)r,
                                   (const secp256k1_scalar*)a,
                                   (const secp256k1_scalar*)b,
                                   shift);
}

// Get scalar struct for size verification
size_t secp256k1_bench_scalar_size(void) {
    return sizeof(secp256k1_scalar);
}

#endif // HAVE_SECP256K1_COMPETITOR
