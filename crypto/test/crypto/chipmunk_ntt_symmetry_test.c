/*
 * Simple NTT/invNTT symmetry test
 * Tests if: invNTT(NTT(x)) == x for polynomials
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "dap_common.h"
#include "chipmunk.h"
#include "chipmunk_ntt.h"

int main() {
    printf("=== NTT/INVNTT SYMMETRY TEST ===\n\n");
    
    dap_common_init("chipmunk-ntt-symmetry", NULL);
    
    // Test 1: Simple constant polynomial
    printf("🔬 Test 1: Constant polynomial [1, 1, 1, ...]\n");
    int32_t poly1[CHIPMUNK_N];
    int32_t poly1_backup[CHIPMUNK_N];
    
    // Fill with 1s
    for (int i = 0; i < CHIPMUNK_N; i++) {
        poly1[i] = 1;
        poly1_backup[i] = 1;
    }
    
    printf("Original first coeffs: %d %d %d %d\n", 
           poly1[0], poly1[1], poly1[2], poly1[3]);
    
    // Apply NTT
    chipmunk_ntt(poly1);
    printf("After NTT first coeffs: %d %d %d %d\n", 
           poly1[0], poly1[1], poly1[2], poly1[3]);
    
    // Apply invNTT
    chipmunk_invntt(poly1);
    printf("After invNTT first coeffs: %d %d %d %d\n", 
           poly1[0], poly1[1], poly1[2], poly1[3]);
    
    // Check if identical
    bool test1_pass = true;
    for (int i = 0; i < CHIPMUNK_N; i++) {
        if (poly1[i] != poly1_backup[i]) {
            test1_pass = false;
            if (i < 8) {  // Show first few differences
                printf("Coeff[%d]: %d != %d (diff: %d)\n", i, 
                       poly1[i], poly1_backup[i], poly1[i] - poly1_backup[i]);
            }
        }
    }
    printf("Test 1 result: %s\n\n", test1_pass ? "✅ PASS" : "❌ FAIL");
    
    // Test 2: Simple delta function [1, 0, 0, ...]
    printf("🔬 Test 2: Delta function [1, 0, 0, ...]\n");
    int32_t poly2[CHIPMUNK_N];
    int32_t poly2_backup[CHIPMUNK_N];
    
    // Fill with delta
    memset(poly2, 0, sizeof(poly2));
    memset(poly2_backup, 0, sizeof(poly2_backup));
    poly2[0] = 1;
    poly2_backup[0] = 1;
    
    printf("Original first coeffs: %d %d %d %d\n", 
           poly2[0], poly2[1], poly2[2], poly2[3]);
    
    // Apply NTT
    chipmunk_ntt(poly2);
    printf("After NTT first coeffs: %d %d %d %d\n", 
           poly2[0], poly2[1], poly2[2], poly2[3]);
    
    // Apply invNTT
    chipmunk_invntt(poly2);
    printf("After invNTT first coeffs: %d %d %d %d\n", 
           poly2[0], poly2[1], poly2[2], poly2[3]);
    
    // Check if identical
    bool test2_pass = true;
    for (int i = 0; i < CHIPMUNK_N; i++) {
        if (poly2[i] != poly2_backup[i]) {
            test2_pass = false;
            if (i < 8) {  // Show first few differences
                printf("Coeff[%d]: %d != %d (diff: %d)\n", i, 
                       poly2[i], poly2_backup[i], poly2[i] - poly2_backup[i]);
            }
        }
    }
    printf("Test 2 result: %s\n\n", test2_pass ? "✅ PASS" : "❌ FAIL");
    
    // Test 3: Random polynomial
    printf("🔬 Test 3: Random polynomial\n");
    int32_t poly3[CHIPMUNK_N];
    int32_t poly3_backup[CHIPMUNK_N];
    
    srand(12345);  // Fixed seed for reproducibility
    for (int i = 0; i < CHIPMUNK_N; i++) {
        poly3[i] = rand() % 1000;  // Small random values
        poly3_backup[i] = poly3[i];
    }
    
    printf("Original first coeffs: %d %d %d %d\n", 
           poly3[0], poly3[1], poly3[2], poly3[3]);
    
    // Apply NTT
    chipmunk_ntt(poly3);
    printf("After NTT first coeffs: %d %d %d %d\n", 
           poly3[0], poly3[1], poly3[2], poly3[3]);
    
    // Apply invNTT
    chipmunk_invntt(poly3);
    printf("After invNTT first coeffs: %d %d %d %d\n", 
           poly3[0], poly3[1], poly3[2], poly3[3]);
    
    // Check if identical
    bool test3_pass = true;
    int diff_count = 0;
    for (int i = 0; i < CHIPMUNK_N; i++) {
        if (poly3[i] != poly3_backup[i]) {
            test3_pass = false;
            diff_count++;
            if (diff_count <= 8) {  // Show first few differences
                printf("Coeff[%d]: %d != %d (diff: %d)\n", i, 
                       poly3[i], poly3_backup[i], poly3[i] - poly3_backup[i]);
            }
        }
    }
    printf("Test 3 result: %s", test3_pass ? "✅ PASS" : "❌ FAIL");
    if (!test3_pass) {
        printf(" (%d/%d coefficients differ)", diff_count, CHIPMUNK_N);
    }
    printf("\n\n");
    
    // Summary
    int passed = (test1_pass ? 1 : 0) + (test2_pass ? 1 : 0) + (test3_pass ? 1 : 0);
    printf("📊 SUMMARY: %d/3 tests passed\n", passed);
    
    if (passed == 3) {
        printf("🎉 NTT/invNTT symmetry is PERFECT!\n");
        return 0;
    } else {
        printf("💥 NTT/invNTT symmetry is BROKEN!\n");
        printf("This explains the HOTS verification failures.\n");
        return 1;
    }
} 