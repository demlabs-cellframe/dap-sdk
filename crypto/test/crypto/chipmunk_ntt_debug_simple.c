/*
 * Simple diagnostic test for NTT/invNTT debugging
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "dap_common.h"
#include "chipmunk/chipmunk.h"
#include "chipmunk/chipmunk_ntt.h"

// Test with very simple cases to debug NTT/invNTT step by step
int main() {
    printf("=== SIMPLE NTT DEBUG TEST ===\n\n");
    
    dap_common_init("chipmunk-simple-ntt-debug", NULL);
    
    // Test 1: All zeros (should stay zeros)
    int32_t poly_zeros[CHIPMUNK_N];
    memset(poly_zeros, 0, sizeof(poly_zeros));
    
    printf("🔬 Test 1: All zeros polynomial\n");
    printf("Before NTT: [%d, %d, %d, %d]\n", 
           poly_zeros[0], poly_zeros[1], poly_zeros[2], poly_zeros[3]);
    
    chipmunk_ntt(poly_zeros);
    printf("After NTT: [%d, %d, %d, %d]\n", 
           poly_zeros[0], poly_zeros[1], poly_zeros[2], poly_zeros[3]);
    
    chipmunk_invntt(poly_zeros);
    printf("After invNTT: [%d, %d, %d, %d]\n", 
           poly_zeros[0], poly_zeros[1], poly_zeros[2], poly_zeros[3]);
    
    // Check if all are still zeros
    int zeros_ok = 1;
    for (int i = 0; i < CHIPMUNK_N; i++) {
        if (poly_zeros[i] != 0) {
            zeros_ok = 0;
            break;
        }
    }
    printf("Result: %s\n\n", zeros_ok ? "✅ PASS" : "❌ FAIL");
    
    // Test 2: Single coefficient = 1 at position 0
    int32_t poly_single[CHIPMUNK_N];
    memset(poly_single, 0, sizeof(poly_single));
    poly_single[0] = 1;
    
    printf("🔬 Test 2: Single coefficient [1, 0, 0, ...]\n");
    printf("Before NTT: [%d, %d, %d, %d]\n", 
           poly_single[0], poly_single[1], poly_single[2], poly_single[3]);
    
    chipmunk_ntt(poly_single);
    printf("After NTT: [%d, %d, %d, %d]\n", 
           poly_single[0], poly_single[1], poly_single[2], poly_single[3]);
    
    // For [1,0,0,...] the NTT should be [1,1,1,1,...] (all ones)
    int ntt_ok = 1;
    for (int i = 0; i < 8; i++) { // Check first 8
        if (poly_single[i] != 1) {
            ntt_ok = 0;
            printf("NTT[%d] = %d (expected 1)\n", i, poly_single[i]);
        }
    }
    printf("NTT result: %s\n", ntt_ok ? "✅ Correct (all ones)" : "❌ Wrong");
    
    chipmunk_invntt(poly_single);
    printf("After invNTT: [%d, %d, %d, %d]\n", 
           poly_single[0], poly_single[1], poly_single[2], poly_single[3]);
    
    // Should be back to [1,0,0,...]
    int invntt_ok = (poly_single[0] == 1);
    for (int i = 1; i < 8; i++) { // Check first 8 zeros
        if (poly_single[i] != 0) {
            invntt_ok = 0;
            printf("invNTT[%d] = %d (expected 0)\n", i, poly_single[i]);
        }
    }
    printf("invNTT result: %s\n", invntt_ok ? "✅ Correct [1,0,0,...]" : "❌ Wrong");
    
    // Detailed analysis of what went wrong
    if (!invntt_ok) {
        printf("\n🔍 DETAILED ANALYSIS:\n");
        printf("poly_single[0] = %d (should be 1)\n", poly_single[0]);
        printf("Modulo check: %d %% %d = %d\n", 
               poly_single[0], CHIPMUNK_Q, poly_single[0] % CHIPMUNK_Q);
        
        // Check if it's a simple scaling factor
        if (poly_single[0] % CHIPMUNK_Q != 0) {
            int32_t mod_result = poly_single[0] % CHIPMUNK_Q;
            if (mod_result < 0) mod_result += CHIPMUNK_Q;
            printf("Positive modulo: %d\n", mod_result);
            
            // Check common scaling factors
            if (mod_result % 2 == 0) {
                printf("💡 Factor of 2 detected: %d = 2 * %d\n", mod_result, mod_result/2);
            }
            if (mod_result == 2) {
                printf("💡 This is exactly factor 2! Problem is likely in scaling.\n");
            }
        }
    }
    
    return (zeros_ok && ntt_ok && invntt_ok) ? 0 : 1;
} 