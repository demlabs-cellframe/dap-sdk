/*
 * Simple HOTS mathematical verification test
 * Tests if: Σ(a_i * σ_i) == H(m) * v0 + v1 when σ[i] = s0[i] * H(m) + s1[i]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "dap_common.h"
#include "chipmunk.h"
#include "chipmunk_hots.h"
#include "chipmunk_ntt.h"

int main() {
    printf("=== SIMPLE HOTS VERIFICATION EQUATION TEST ===\n\n");
    
    dap_common_init("chipmunk-ntt-debug", NULL);
    
    // Use simple test data to check mathematical consistency
    const uint8_t test_message[] = "test";
    const size_t message_len = 4;
    
    // Step 1: Setup parameters
    chipmunk_hots_params_t params;
    if (chipmunk_hots_setup(&params) != 0) {
        printf("❌ Setup failed\n");
        return 1;
    }
    printf("✓ Setup completed\n");
    
    // Step 2: Generate keys
    uint8_t seed[32] = {0x01, 0x02, 0x03, 0x04}; // Simple seed
    chipmunk_hots_pk_t pk;
    chipmunk_hots_sk_t sk;
    
    if (chipmunk_hots_keygen(seed, 0, &params, &pk, &sk) != 0) {
        printf("❌ Keygen failed\n");
        return 1;
    }
    printf("✓ Keys generated\n");
    
    // Step 3: Create signature
    chipmunk_hots_signature_t signature;
    if (chipmunk_hots_sign(&sk, test_message, message_len, &signature) != 0) {
        printf("❌ Signing failed\n");
        return 1;
    }
    printf("✓ Signature created\n");
    
    // Step 4: Manual verification - reproduce the exact equation step by step
    printf("\n🔍 MANUAL VERIFICATION EQUATION CHECK:\n");
    
    // Generate H(m)
    chipmunk_poly_t hm;
    if (chipmunk_poly_from_hash(&hm, test_message, message_len) != 0) {
        printf("❌ Failed to generate H(m)\n");
        return 1;
    }
    
    printf("H(m) time domain first coeffs: %d %d %d %d\n",
           hm.coeffs[0], hm.coeffs[1], hm.coeffs[2], hm.coeffs[3]);
    
    // Transform H(m) to NTT domain for calculations
    chipmunk_poly_t hm_ntt = hm;
    chipmunk_ntt(hm_ntt.coeffs);
    printf("H(m) NTT domain first coeffs: %d %d %d %d\n",
           hm_ntt.coeffs[0], hm_ntt.coeffs[1], hm_ntt.coeffs[2], hm_ntt.coeffs[3]);
    
    // Check equation consistency manually:
    // We know that σ[i] = s0[i] * H(m) + s1[i] (in NTT domain)
    // So: Σ(a_i * σ_i) should equal Σ(a_i * (s0[i] * H(m) + s1[i])) 
    //                              = Σ(a_i * s0[i] * H(m)) + Σ(a_i * s1[i])
    //                              = H(m) * Σ(a_i * s0[i]) + Σ(a_i * s1[i])
    //                              = H(m) * v0 + v1
    
    printf("\n🧮 EQUATION COMPONENT ANALYSIS:\n");
    
    // Left side: Σ(a_i * σ_i)
    chipmunk_poly_t left_sum;
    memset(&left_sum, 0, sizeof(chipmunk_poly_t));
    
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        chipmunk_poly_t ai_ntt = params.a[i];  // a[i] already in NTT
        chipmunk_poly_t sigma_i_ntt = signature.sigma[i];
        chipmunk_ntt(sigma_i_ntt.coeffs);  // Transform σ[i] to NTT
        
        chipmunk_poly_t term;
        chipmunk_poly_mul_ntt(&term, &ai_ntt, &sigma_i_ntt);
        chipmunk_poly_add_ntt(&left_sum, &left_sum, &term);
    }
    chipmunk_invntt(left_sum.coeffs);  // Convert to time domain
    printf("Left side (Σ a_i * σ_i) first coeffs: %d %d %d %d\n",
           left_sum.coeffs[0], left_sum.coeffs[1], left_sum.coeffs[2], left_sum.coeffs[3]);
    
    // Right side method 1: H(m) * v0 + v1 (direct calculation)
    chipmunk_poly_t v0_ntt = pk.v0;
    chipmunk_poly_t v1_ntt = pk.v1;
    chipmunk_ntt(v0_ntt.coeffs);
    chipmunk_ntt(v1_ntt.coeffs);
    
    chipmunk_poly_t right_sum_method1;
    chipmunk_poly_mul_ntt(&right_sum_method1, &hm_ntt, &v0_ntt);
    chipmunk_poly_add_ntt(&right_sum_method1, &right_sum_method1, &v1_ntt);
    chipmunk_invntt(right_sum_method1.coeffs);
    printf("Right side method 1 (H(m)*v0 + v1) first coeffs: %d %d %d %d\n",
           right_sum_method1.coeffs[0], right_sum_method1.coeffs[1], 
           right_sum_method1.coeffs[2], right_sum_method1.coeffs[3]);
    
    // Right side method 2: Manual reconstruction from definition
    // v0 = Σ(a_i * s0_i), v1 = Σ(a_i * s1_i)
    // So H(m) * v0 + v1 = H(m) * Σ(a_i * s0_i) + Σ(a_i * s1_i)
    //                   = Σ(H(m) * a_i * s0_i) + Σ(a_i * s1_i)
    //                   = Σ(a_i * (H(m) * s0_i + s1_i))
    //                   = Σ(a_i * σ_i)   [by definition of σ_i]
    
    chipmunk_poly_t right_sum_method2;
    memset(&right_sum_method2, 0, sizeof(chipmunk_poly_t));
    
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        chipmunk_poly_t ai_ntt = params.a[i];  // a[i] already in NTT
        
        // Compute σ[i] manually: s0[i] * H(m) + s1[i]
        chipmunk_poly_t manual_sigma;
        chipmunk_poly_mul_ntt(&manual_sigma, &sk.s0[i], &hm_ntt);
        chipmunk_poly_add_ntt(&manual_sigma, &manual_sigma, &sk.s1[i]);
        
        // Add a[i] * σ[i] to sum
        chipmunk_poly_t term;
        chipmunk_poly_mul_ntt(&term, &ai_ntt, &manual_sigma);
        chipmunk_poly_add_ntt(&right_sum_method2, &right_sum_method2, &term);
    }
    chipmunk_invntt(right_sum_method2.coeffs);
    printf("Right side method 2 (manual Σ a_i * σ_i) first coeffs: %d %d %d %d\n",
           right_sum_method2.coeffs[0], right_sum_method2.coeffs[1], 
           right_sum_method2.coeffs[2], right_sum_method2.coeffs[3]);
    
    // Compare all three methods
    printf("\n📊 COMPARISON:\n");
    bool left_eq_right1 = true;
    bool left_eq_right2 = true;
    bool right1_eq_right2 = true;
    
    for (int i = 0; i < CHIPMUNK_N; i++) {
        if (left_sum.coeffs[i] != right_sum_method1.coeffs[i]) left_eq_right1 = false;
        if (left_sum.coeffs[i] != right_sum_method2.coeffs[i]) left_eq_right2 = false;
        if (right_sum_method1.coeffs[i] != right_sum_method2.coeffs[i]) right1_eq_right2 = false;
    }
    
    printf("Left == Right Method 1: %s\n", left_eq_right1 ? "✅ PASS" : "❌ FAIL");
    printf("Left == Right Method 2: %s\n", left_eq_right2 ? "✅ PASS" : "❌ FAIL");
    printf("Right Method 1 == Right Method 2: %s\n", right1_eq_right2 ? "✅ PASS" : "❌ FAIL");
    
    if (left_eq_right1 && left_eq_right2 && right1_eq_right2) {
        printf("\n🎉 MATHEMATICAL EQUATION IS CONSISTENT!\n");
        printf("The HOTS verification equation works correctly.\n");
        return 0;
    } else {
        printf("\n💥 MATHEMATICAL INCONSISTENCY DETECTED!\n");
        printf("This indicates a bug in our implementation.\n");
        
        // Show detailed differences
        printf("\n🔍 DETAILED COEFFICIENT DIFFERENCES (first 8 coeffs):\n");
        for (int i = 0; i < 8; i++) {
            printf("Coeff[%d]: Left=%d, Right1=%d, Right2=%d\n", i,
                   left_sum.coeffs[i], right_sum_method1.coeffs[i], right_sum_method2.coeffs[i]);
        }
        
        return 1;
    }
} 