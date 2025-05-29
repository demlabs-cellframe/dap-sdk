/*
 * File: dap_file_utils_test.c
 * Authors: Cellframe Team  
 * Copyright (c) 2023 Cellframe
 *
 * Tests for file utils functions including disk space checking
 */

#include "dap_file_utils_test.h"
#include "dap_file_utils.h"
#include "dap_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

static const char *TEST_DIR = "./test_disk_space_dir";
static const char *TEST_FILE = "./test_disk_space_file.txt";

void create_test_files() {
    // Create test directory
    dap_mkdir_with_parents(TEST_DIR);
    
    // Create test file
    FILE *f = fopen(TEST_FILE, "w");
    if (f) {
        fprintf(f, "Test file for disk space testing\n");
        fclose(f);
    }
}

void cleanup_test_files() {
    // Remove test file
    remove(TEST_FILE);
    
    // Remove test directory  
    rmdir(TEST_DIR);
}

void test_dap_disk_space_get_valid_path() {
    // Test 1: Get disk space for current directory
    uint64_t free_bytes = dap_disk_space_get(".");
    dap_assert(free_bytes > 0, "Get disk space for current directory");
    
    // Test 2: Get disk space for test directory
    uint64_t free_bytes_dir = dap_disk_space_get(TEST_DIR);
    dap_assert(free_bytes_dir > 0, "Get disk space for test directory");
    
    // Test 3: Get disk space for test file (should check parent directory)
    uint64_t free_bytes_file = dap_disk_space_get(TEST_FILE);
    dap_assert(free_bytes_file > 0, "Get disk space for test file");
    
    // The free space should be approximately the same for all locations on same filesystem
    dap_test_msg("Current dir: %llu bytes, Test dir: %llu bytes, Test file: %llu bytes", 
                 (unsigned long long)free_bytes, 
                 (unsigned long long)free_bytes_dir,
                 (unsigned long long)free_bytes_file);
}

void test_dap_disk_space_get_invalid_path() {
    // Test with non-existent path - parent directory might still exist
    uint64_t free_bytes = dap_disk_space_get("/nonexistent/deeply/nested/path/that/does/not/exist");
    // Should return 0 because we can't find any accessible parent directory
    dap_assert(free_bytes == 0, "Get disk space for completely invalid path");
    
    // Test with NULL path
    uint64_t free_bytes_null = dap_disk_space_get(NULL);
    dap_assert(free_bytes_null == 0, "Get disk space for NULL path");
    
    // Test with empty path
    uint64_t free_bytes_empty = dap_disk_space_get("");
    dap_assert(free_bytes_empty == 0, "Get disk space for empty path");
}

void test_dap_disk_space_check_sufficient() {
    // Test checking for very small amount (1MB) - should pass
    bool has_space_1mb = dap_disk_space_check(".", 1);
    dap_assert(has_space_1mb == true, "Check disk space - 1MB available");
    
    // Test checking for reasonable amount (10MB) - should pass on most systems
    bool has_space_10mb = dap_disk_space_check(".", 10);
    dap_assert(has_space_10mb == true, "Check disk space - 10MB available");
    
    // Test with test directory
    bool has_space_dir = dap_disk_space_check(TEST_DIR, 1);
    dap_assert(has_space_dir == true, "Check disk space for test directory");
    
    // Test with test file
    bool has_space_file = dap_disk_space_check(TEST_FILE, 1);
    dap_assert(has_space_file == true, "Check disk space for test file");
}

void test_dap_disk_space_check_insufficient() {
    // Test checking for unreasonably large amount - should fail  
    bool has_space_huge = dap_disk_space_check(".", 1000000); // 1TB
    dap_assert(has_space_huge == false, "Check disk space - 1TB not available");
    
    // Test with invalid path
    bool has_space_invalid = dap_disk_space_check("/nonexistent/path", 1);
    dap_assert(has_space_invalid == false, "Check disk space for invalid path");
    
    // Test with NULL path
    bool has_space_null = dap_disk_space_check(NULL, 1);
    dap_assert(has_space_null == false, "Check disk space for NULL path");
}

void test_dap_disk_space_check_edge_cases() {
    // Test with 0 MB requirement - should always pass if path is valid
    bool has_space_zero = dap_disk_space_check(".", 0);
    dap_assert(has_space_zero == true, "Check disk space - 0MB requirement");
    
    // Get actual free space and test with exact amount
    uint64_t free_bytes = dap_disk_space_get(".");
    if (free_bytes > 0) {
        uint64_t free_mb = free_bytes / (1024 * 1024);
        
        // Test with slightly less than available
        if (free_mb > 1) {
            bool has_space_less = dap_disk_space_check(".", free_mb - 1);
            dap_assert(has_space_less == true, "Check disk space - slightly less than available");
        }
        
        // Test with slightly more than available
        bool has_space_more = dap_disk_space_check(".", free_mb + 100);
        dap_assert(has_space_more == false, "Check disk space - more than available");
        
        dap_test_msg("Available space: %llu MB, tested with %llu MB", 
                     (unsigned long long)free_mb, 
                     (unsigned long long)(free_mb + 100));
    }
}

void test_integration_scenarios() {
    // Test scenario 1: Database path checking (simulate typical usage)
    const char *db_path = "./test_db_path";
    dap_mkdir_with_parents(db_path);
    
    // Check if we have enough space for database operations
    bool can_write_db = dap_disk_space_check(db_path, 100); // 100MB for DB
    dap_test_msg("Can write to DB path: %s", can_write_db ? "YES" : "NO");
    
    // Clean up
    rmdir(db_path);
    
    // Test scenario 2: Chain data path checking
    const char *chain_path = "./test_chain_data/chain1/blocks";
    dap_mkdir_with_parents(chain_path);
    
    // Check multiple levels
    bool can_write_chain1 = dap_disk_space_check(chain_path, 50);
    bool can_write_chain2 = dap_disk_space_check("./test_chain_data", 50);
    
    dap_assert(can_write_chain1 == can_write_chain2, "Consistent results for nested paths");
    
    // Clean up
    dap_rm_rf("./test_chain_data");
    
    dap_pass_msg("Integration scenarios testing");
}

void benchmark_disk_space_functions() {
    dap_test_msg("Running performance benchmarks...");
    
    // Benchmark dap_disk_space_get
    clock_t start = clock();
    for (int i = 0; i < 1000; i++) {
        dap_disk_space_get(".");
    }
    clock_t end = clock();
    double get_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    // Benchmark dap_disk_space_check  
    start = clock();
    for (int i = 0; i < 1000; i++) {
        dap_disk_space_check(".", 1);
    }
    end = clock();
    double check_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    dap_test_msg("Performance: get() 1000 calls in %.3f sec, check() 1000 calls in %.3f sec", 
                 get_time, check_time);
    
    // Ensure reasonable performance (less than 1 second for 1000 calls)
    dap_assert(get_time < 1.0, "dap_disk_space_get performance test");
    dap_assert(check_time < 1.0, "dap_disk_space_check performance test");
}

void dap_file_utils_tests_run(void) {
    dap_print_module_name("dap_file_utils");
    
    // Setup
    create_test_files();
    
    // Basic functionality tests
    test_dap_disk_space_get_valid_path();
    test_dap_disk_space_get_invalid_path();
    test_dap_disk_space_check_sufficient();
    test_dap_disk_space_check_insufficient();
    test_dap_disk_space_check_edge_cases();
    
    // Integration and real-world scenarios
    test_integration_scenarios();
    
    // Performance testing
    benchmark_disk_space_functions();
    
    // Cleanup
    cleanup_test_files();
    
    dap_test_msg("All dap_file_utils tests completed successfully!");
} 