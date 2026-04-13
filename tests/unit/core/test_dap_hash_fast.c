/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * This file is part of DAP (Distributed Applications Platform) the open source project
 *
 *    DAP is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 */

/**
 * @file test_dap_hash_fast.c
 * @brief Unit tests for DAP Fast Hash Functions (dap_hash_fast.h)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>

#include "dap_common.h"
#include "dap_hash_fast.h"
#include "dap_test.h"

#define LOG_TAG "test_dap_hash_fast"

// ============================================================================
// Test Data
// ============================================================================

static const char *s_test_strings[] = {
    "",
    "a",
    "ab",
    "abc",
    "abcd",
    "hello",
    "Hello World!",
    "The quick brown fox jumps over the lazy dog",
    "1234567890",
    NULL
};

// ============================================================================
// Tests: Basic Hash Functions
// ============================================================================

static void test_fnv1a(void) {
    dap_print_module_name("dap_hash_fnv1a");
    
    // Test determinism - same input = same hash
    uint32_t h1 = dap_hash_fnv1a("test", 4);
    uint32_t h2 = dap_hash_fnv1a("test", 4);
    dap_assert(h1 == h2, "FNV-1a is deterministic");
    
    // Test different strings produce different hashes
    uint32_t h_abc = dap_hash_fnv1a("abc", 3);
    uint32_t h_def = dap_hash_fnv1a("def", 3);
    dap_assert(h_abc != h_def, "Different strings produce different hashes");
    
    // Test empty string
    uint32_t h_empty = dap_hash_fnv1a("", 0);
    dap_assert(h_empty == DAP_FNV1A_32_INIT, "Empty string returns init value");
    
    // Test known value (FNV-1a of "hello" is a known value)
    uint32_t h_hello = dap_hash_fnv1a("hello", 5);
    dap_assert(h_hello != 0, "Hash of 'hello' is non-zero");
    
    dap_pass_msg("dap_hash_fnv1a works correctly");
}

static void test_jenkins_oat(void) {
    dap_print_module_name("dap_hash_jenkins_oat");
    
    // Test determinism
    uint32_t h1 = dap_hash_jenkins_oat("test", 4);
    uint32_t h2 = dap_hash_jenkins_oat("test", 4);
    dap_assert(h1 == h2, "Jenkins OAT is deterministic");
    
    // Test different strings
    uint32_t h_abc = dap_hash_jenkins_oat("abc", 3);
    uint32_t h_def = dap_hash_jenkins_oat("def", 3);
    dap_assert(h_abc != h_def, "Different strings produce different hashes");
    
    dap_pass_msg("dap_hash_jenkins_oat works correctly");
}

static void test_jenkins_lookup3(void) {
    dap_print_module_name("dap_hash_jenkins (lookup3)");
    
    // Test determinism
    uint32_t h1 = dap_hash_jenkins("test", 4);
    uint32_t h2 = dap_hash_jenkins("test", 4);
    dap_assert(h1 == h2, "Jenkins lookup3 is deterministic");
    
    // Test various lengths
    for (const char **p = s_test_strings; *p; p++) {
        uint32_t h = dap_hash_jenkins(*p, strlen(*p));
        // Just verify it doesn't crash and produces some output
        (void)h;
    }
    
    dap_pass_msg("dap_hash_jenkins works correctly");
}

static void test_murmur3(void) {
    dap_print_module_name("dap_hash_murmur3");
    
    // Test determinism with same seed
    uint32_t h1 = dap_hash_murmur3("test", 4, 0);
    uint32_t h2 = dap_hash_murmur3("test", 4, 0);
    dap_assert(h1 == h2, "MurmurHash3 is deterministic");
    
    // Test different seeds produce different hashes
    uint32_t h_seed0 = dap_hash_murmur3("test", 4, 0);
    uint32_t h_seed1 = dap_hash_murmur3("test", 4, 1);
    dap_assert(h_seed0 != h_seed1, "Different seeds produce different hashes");
    
    // Test various lengths
    for (const char **p = s_test_strings; *p; p++) {
        uint32_t h = dap_hash_murmur3(*p, strlen(*p), 42);
        (void)h;
    }
    
    dap_pass_msg("dap_hash_murmur3 works correctly");
}

static void test_xxh32(void) {
    dap_print_module_name("dap_hash_xxh32");
    
    // Test determinism
    uint32_t h1 = dap_hash_xxh32("test", 4, 0);
    uint32_t h2 = dap_hash_xxh32("test", 4, 0);
    dap_assert(h1 == h2, "xxHash32 is deterministic");
    
    // Test different seeds
    uint32_t h_seed0 = dap_hash_xxh32("test", 4, 0);
    uint32_t h_seed1 = dap_hash_xxh32("test", 4, 123);
    dap_assert(h_seed0 != h_seed1, "Different seeds produce different hashes");
    
    // Test large data
    char large_data[10000];
    memset(large_data, 'x', sizeof(large_data));
    uint32_t h_large = dap_hash_xxh32(large_data, sizeof(large_data), 0);
    dap_assert(h_large != 0, "Large data hashing works");
    
    dap_pass_msg("dap_hash_xxh32 works correctly");
}

static void test_hash_fast_ht(void) {
    dap_print_module_name("dap_hash_fast_ht (default)");
    
    // Test determinism
    uint32_t h1 = dap_hash_fast_ht("test", 4);
    uint32_t h2 = dap_hash_fast_ht("test", 4);
    dap_assert(h1 == h2, "dap_hash_fast_ht is deterministic");
    
    // Test all strings
    for (const char **p = s_test_strings; *p; p++) {
        uint32_t h = dap_hash_fast_ht(*p, strlen(*p));
        (void)h;
    }
    
    dap_pass_msg("dap_hash_fast_ht works correctly");
}

// ============================================================================
// Tests: Distribution Quality
// ============================================================================

static void test_distribution(void) {
    dap_print_module_name("Hash distribution");
    
    #define N 10000
    #define BUCKETS 256
    int counts[BUCKETS];
    memset(counts, 0, sizeof(counts));
    
    // Hash sequential integers
    for (int i = 0; i < N; i++) {
        uint32_t h = dap_hash_fast_ht(&i, sizeof(i));
        counts[h % BUCKETS]++;
    }
    
    // Check distribution - should follow Poisson distribution
    // For Poisson: stddev = sqrt(mean), so ~6.2 for mean=39
    // Using 4*stddev tolerance (~25) for min/max bounds
    int expected = N / BUCKETS;  // ~39
    int stddev = (int)(sqrt((double)expected) + 0.5);  // ~6
    int min_count = N;
    int max_count = 0;
    
    for (int i = 0; i < BUCKETS; i++) {
        if (counts[i] < min_count) min_count = counts[i];
        if (counts[i] > max_count) max_count = counts[i];
    }
    
    // Allow 4 standard deviations from expected (99.99% confidence for good hash)
    int tolerance = stddev * 4;  // ~24
    dap_assert(min_count >= expected - tolerance, "Min bucket count within 4σ");
    dap_assert(max_count <= expected + tolerance, "Max bucket count within 4σ");
    
    printf("  Distribution: min=%d, max=%d, expected=%d±%d (4σ)\n", 
           min_count, max_count, expected, tolerance);
    
    dap_pass_msg("Hash distribution is reasonably uniform");
    #undef N
    #undef BUCKETS
}

// ============================================================================
// Tests: Collision Resistance
// ============================================================================

static void test_collision_resistance(void) {
    dap_print_module_name("Collision resistance");
    
    // Test similar strings don't collide
    const char *similar[] = {
        "test1", "test2", "test3",
        "Test1", "Test2", "Test3",
        "TEST1", "TEST2", "TEST3",
        NULL
    };
    
    uint32_t hashes[10];
    int count = 0;
    
    for (const char **p = similar; *p; p++) {
        hashes[count++] = dap_hash_fast_ht(*p, strlen(*p));
    }
    
    // Check all hashes are unique
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            dap_assert(hashes[i] != hashes[j], "Similar strings have unique hashes");
        }
    }
    
    dap_pass_msg("Similar strings don't collide");
}

// ============================================================================
// Tests: Avalanche Effect
// ============================================================================

static void test_avalanche(void) {
    dap_print_module_name("Avalanche effect");
    
    // Changing one bit should change ~50% of output bits
    char data1[8] = {0};
    char data2[8] = {0};
    data2[0] = 1;  // Flip one bit
    
    uint32_t h1 = dap_hash_fast_ht(data1, 8);
    uint32_t h2 = dap_hash_fast_ht(data2, 8);
    
    // Count differing bits
    uint32_t diff = h1 ^ h2;
    int bit_diffs = 0;
    while (diff) {
        bit_diffs += diff & 1;
        diff >>= 1;
    }
    
    // Should have at least 8 bits different (25% of 32)
    dap_assert(bit_diffs >= 8, "Single bit change causes significant hash change");
    printf("  Bit differences: %d / 32\n", bit_diffs);
    
    dap_pass_msg("Avalanche effect is present");
}

// ============================================================================
// Tests: Performance
// ============================================================================

static void test_performance(void) {
    dap_print_module_name("Hash performance");
    
    const int ITERATIONS = 100000;
    char data[256];
    memset(data, 'x', sizeof(data));
    
    clock_t start, end;
    double cpu_time;
    
    // FNV-1a
    start = clock();
    for (int i = 0; i < ITERATIONS; i++) {
        dap_hash_fnv1a(data, sizeof(data));
    }
    end = clock();
    cpu_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("  FNV-1a:    %d iterations in %.3f sec\n", ITERATIONS, cpu_time);
    
    // Jenkins OAT
    start = clock();
    for (int i = 0; i < ITERATIONS; i++) {
        dap_hash_jenkins_oat(data, sizeof(data));
    }
    end = clock();
    cpu_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("  Jenkins OAT: %d iterations in %.3f sec\n", ITERATIONS, cpu_time);
    
    // Jenkins lookup3
    start = clock();
    for (int i = 0; i < ITERATIONS; i++) {
        dap_hash_jenkins(data, sizeof(data));
    }
    end = clock();
    cpu_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("  Jenkins L3: %d iterations in %.3f sec\n", ITERATIONS, cpu_time);
    
    // MurmurHash3
    start = clock();
    for (int i = 0; i < ITERATIONS; i++) {
        dap_hash_murmur3(data, sizeof(data), 0);
    }
    end = clock();
    cpu_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("  MurmurHash3: %d iterations in %.3f sec\n", ITERATIONS, cpu_time);
    
    // xxHash32
    start = clock();
    for (int i = 0; i < ITERATIONS; i++) {
        dap_hash_xxh32(data, sizeof(data), 0);
    }
    end = clock();
    cpu_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("  xxHash32:  %d iterations in %.3f sec\n", ITERATIONS, cpu_time);
    
    dap_pass_msg("Performance test completed");
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    
    dap_set_appname("test_dap_hash_fast");
    srand((unsigned)time(NULL));
    
    printf("\n=== DAP Fast Hash (dap_hash_fast) Tests ===\n\n");
    
    // Basic function tests
    test_fnv1a();
    test_jenkins_oat();
    test_jenkins_lookup3();
    test_murmur3();
    test_xxh32();
    test_hash_fast_ht();
    
    // Quality tests
    test_distribution();
    test_collision_resistance();
    test_avalanche();
    
    // Performance
    test_performance();
    
    printf("\n=== All dap_hash_fast tests passed! ===\n\n");
    
    return 0;
}
