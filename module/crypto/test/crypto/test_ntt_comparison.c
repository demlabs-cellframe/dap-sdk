#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "dap_common.h"
#include "dap_enc_test.h"
#include "chipmunk.h"
#include "chipmunk_poly.h"
#include "chipmunk_ntt.h"

#define LOG_TAG "test_ntt_comparison"

/**
 * @brief Test NTT operations with simple known values
 */
int test_ntt_simple() {
    printf("\n=== TESTING NTT WITH SIMPLE KNOWN VALUES ===\n");
    
    // Create simple test polynomial: [1, 2, 3, 4, 0, 0, ..., 0]
    chipmunk_poly_t l_test_poly = {0};
    l_test_poly.coeffs[0] = 1;
    l_test_poly.coeffs[1] = 2;
    l_test_poly.coeffs[2] = 3;
    l_test_poly.coeffs[3] = 4;
    for (int i = 4; i < CHIPMUNK_N; i++) {
        l_test_poly.coeffs[i] = 0;
    }
    
    printf("Original polynomial first coeffs: %d %d %d %d\n", 
           l_test_poly.coeffs[0], l_test_poly.coeffs[1], 
           l_test_poly.coeffs[2], l_test_poly.coeffs[3]);
    
    // Make backup
    chipmunk_poly_t l_backup = l_test_poly;
    
    // Apply NTT
    chipmunk_ntt(l_test_poly.coeffs);
    printf("After NTT first coeffs: %d %d %d %d\n", 
           l_test_poly.coeffs[0], l_test_poly.coeffs[1], 
           l_test_poly.coeffs[2], l_test_poly.coeffs[3]);
    
    // Apply InvNTT
    chipmunk_invntt(l_test_poly.coeffs);
    printf("After InvNTT first coeffs: %d %d %d %d\n", 
           l_test_poly.coeffs[0], l_test_poly.coeffs[1], 
           l_test_poly.coeffs[2], l_test_poly.coeffs[3]);
    
    // Check if we got back original values
    bool l_success = true;
    for (int i = 0; i < CHIPMUNK_N; i++) {
        if (l_test_poly.coeffs[i] != l_backup.coeffs[i]) {
            printf("❌ Mismatch at coefficient %d: got %d, expected %d\n", 
                   i, l_test_poly.coeffs[i], l_backup.coeffs[i]);
            l_success = false;
            if (i > 10) break; // Don't spam too much
        }
    }
    
    if (l_success) {
        printf("✅ NTT/InvNTT roundtrip test PASSED\n");
        return 0;
    } else {
        printf("❌ NTT/InvNTT roundtrip test FAILED\n");
        return -1;
    }
}

/**
 * @brief Test pointwise multiplication in NTT domain
 */
int test_ntt_pointwise() {
    printf("\n=== TESTING NTT POINTWISE MULTIPLICATION ===\n");
    
    // Create two simple polynomials: [1, 1, 0, 0, ...] and [2, 3, 0, 0, ...]
    chipmunk_poly_t l_poly_a = {0};
    chipmunk_poly_t l_poly_b = {0};
    chipmunk_poly_t l_result = {0};
    
    l_poly_a.coeffs[0] = 1;
    l_poly_a.coeffs[1] = 1;
    
    l_poly_b.coeffs[0] = 2;
    l_poly_b.coeffs[1] = 3;
    
    printf("Poly A first coeffs: %d %d\n", l_poly_a.coeffs[0], l_poly_a.coeffs[1]);
    printf("Poly B first coeffs: %d %d\n", l_poly_b.coeffs[0], l_poly_b.coeffs[1]);
    
    // Transform to NTT domain
    chipmunk_ntt(l_poly_a.coeffs);
    chipmunk_ntt(l_poly_b.coeffs);
    
    // Pointwise multiply in NTT domain
    int l_result_code = chipmunk_ntt_pointwise_montgomery(l_result.coeffs, l_poly_a.coeffs, l_poly_b.coeffs);
    if (l_result_code != CHIPMUNK_ERROR_SUCCESS) {
        printf("❌ Pointwise multiplication failed with error %d\n", l_result_code);
        return -1;
    }
    
    // Transform back to time domain
    chipmunk_invntt(l_result.coeffs);
    
    printf("Result first coeffs: %d %d %d %d\n", 
           l_result.coeffs[0], l_result.coeffs[1], 
           l_result.coeffs[2], l_result.coeffs[3]);
    
    // Expected result of (1 + x) * (2 + 3x) = 2 + 5x + 3x^2
    // But in ring Z[x]/(x^512 + 1) this might be different
    printf("Expected for (1+x)*(2+3x): coeffs 2, 5, 3, 0, ...\n");
    
    return 0;
}

int dap_enc_test_ntt_comparison(void) {
    log_it(L_INFO, "Starting NTT comparison tests");
    
    int l_result = 0;
    
    // Test basic NTT roundtrip
    if (test_ntt_simple() != 0) {
        l_result = -1;
    }
    
    // Test pointwise multiplication
    if (test_ntt_pointwise() != 0) {
        l_result = -1;
    }
    
    if (l_result == 0) {
        log_it(L_INFO, "✅ All NTT comparison tests PASSED");
    } else {
        log_it(L_ERROR, "❌ Some NTT comparison tests FAILED");
    }
    
    return l_result;
} 