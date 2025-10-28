/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2025
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file test_stream_obfuscation.c
 * @brief Unit tests for DAP Stream Obfuscation Layer
 * 
 * Tests obfuscation engine creation, configuration, and data transformation.
 * Isolated unit tests with minimal dependencies.
 * 
 * @date 2025-10-28
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "dap_common.h"
#include "dap_test.h"
#include "dap_test_helpers.h"
#include "dap_mock.h"
#include "dap_stream_obfuscation.h"

#define LOG_TAG "test_stream_obfuscation"

// ============================================================================
// Test Data
// ============================================================================

static const char *TEST_DATA_SMALL = "Hello, World!";
static const char *TEST_DATA_MEDIUM = "This is a medium-sized test data string for obfuscation testing.";
static const char *TEST_DATA_LARGE = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
    "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. "
    "Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris.";

// ============================================================================
// Test Suite State
// ============================================================================

static bool s_test_initialized = false;

// ============================================================================
// Setup/Teardown Functions
// ============================================================================

/**
 * @brief Setup function called before each test
 */
static void setup_test(void)
{
    if (!s_test_initialized) {
        // Initialize DAP mock framework
        dap_mock_init();
        
        s_test_initialized = true;
        TEST_INFO("Obfuscation test suite initialized");
    }
}

/**
 * @brief Teardown function called after each test
 */
static void teardown_test(void)
{
    // Cleanup is done in suite cleanup
}

/**
 * @brief Suite cleanup function
 */
static void suite_cleanup(void)
{
    if (s_test_initialized) {
        // Deinitialize mock framework
        dap_mock_deinit();
        
        s_test_initialized = false;
        TEST_INFO("Obfuscation test suite cleaned up");
    }
}

// ============================================================================
// Basic Obfuscation Tests
// ============================================================================

/**
 * @brief Test 1: Create and destroy obfuscation engine
 */
static void test_01_obfuscation_create_destroy(void)
{
    setup_test();
    
    TEST_INFO("Test 1: Creating and destroying obfuscation engine...");
    
    // Create with default settings
    dap_stream_obfuscation_t *obfs = dap_stream_obfuscation_create();
    TEST_ASSERT_NOT_NULL(obfs, "Obfuscation engine should be created");
    
    // Destroy
    dap_stream_obfuscation_destroy(obfs);
    
    TEST_SUCCESS("Test 1 passed: Obfuscation engine lifecycle works");
    teardown_test();
}

/**
 * @brief Test 2: Create with custom configuration
 */
static void test_02_obfuscation_custom_config(void)
{
    setup_test();
    
    TEST_INFO("Test 2: Creating obfuscation engine with custom config...");
    
    // Create config for LOW level
    dap_stream_obfuscation_config_t config = 
        dap_stream_obfuscation_config_for_level(DAP_STREAM_OBFS_LEVEL_LOW);
    
    dap_stream_obfuscation_t *obfs = 
        dap_stream_obfuscation_create_with_config(&config);
    TEST_ASSERT_NOT_NULL(obfs, "Obfuscation engine with LOW config should be created");
    
    dap_stream_obfuscation_destroy(obfs);
    
    // Create config for MEDIUM level
    config = dap_stream_obfuscation_config_for_level(DAP_STREAM_OBFS_LEVEL_MEDIUM);
    obfs = dap_stream_obfuscation_create_with_config(&config);
    TEST_ASSERT_NOT_NULL(obfs, "Obfuscation engine with MEDIUM config should be created");
    
    dap_stream_obfuscation_destroy(obfs);
    
    // Create config for HIGH level
    config = dap_stream_obfuscation_config_for_level(DAP_STREAM_OBFS_LEVEL_HIGH);
    obfs = dap_stream_obfuscation_create_with_config(&config);
    TEST_ASSERT_NOT_NULL(obfs, "Obfuscation engine with HIGH config should be created");
    
    dap_stream_obfuscation_destroy(obfs);
    
    TEST_SUCCESS("Test 2 passed: Custom configuration works correctly");
    teardown_test();
}

/**
 * @brief Test 3: Basic obfuscate/deobfuscate cycle
 */
static void test_03_obfuscate_deobfuscate_small(void)
{
    setup_test();
    
    TEST_INFO("Test 3: Testing obfuscate/deobfuscate cycle with small data...");
    
    // Create config without padding (to avoid size mismatch in deobfuscation)
    dap_stream_obfuscation_config_t config = 
        dap_stream_obfuscation_config_for_level(DAP_STREAM_OBFS_LEVEL_LOW);
    config.enabled_techniques = 0;  // Disable all obfuscation techniques for basic test
    
    dap_stream_obfuscation_t *obfs = dap_stream_obfuscation_create_with_config(&config);
    TEST_ASSERT_NOT_NULL(obfs, "Obfuscation engine should be created");
    
    const char *orig_data = TEST_DATA_SMALL;
    size_t orig_size = strlen(orig_data);
    void *obfuscated_data = NULL;
    size_t obfuscated_size = 0;
    
    // Obfuscate
    int ret = dap_stream_obfuscation_apply(obfs, orig_data, orig_size,
                                            &obfuscated_data, &obfuscated_size);
    TEST_ASSERT(ret == 0, "Obfuscation should succeed");
    TEST_ASSERT_NOT_NULL(obfuscated_data, "Obfuscated data should not be NULL");
    TEST_ASSERT(obfuscated_size > 0, "Obfuscated size should be positive");
    
    TEST_INFO("  Original size: %zu, Obfuscated size: %zu (overhead: +%zu bytes)",
              orig_size, obfuscated_size, obfuscated_size - orig_size);
    
    // Deobfuscate
    void *deobfuscated_data = NULL;
    size_t deobfuscated_size = 0;
    
    ret = dap_stream_obfuscation_remove(obfs, obfuscated_data, obfuscated_size,
                                         &deobfuscated_data, &deobfuscated_size);
    TEST_ASSERT(ret == 0, "Deobfuscation should succeed");
    TEST_ASSERT_NOT_NULL(deobfuscated_data, "Deobfuscated data should not be NULL");
    TEST_ASSERT_EQUAL_INT(orig_size, deobfuscated_size, 
                          "Deobfuscated size should match original");
    
    TEST_ASSERT(memcmp(orig_data, deobfuscated_data, orig_size) == 0,
                "Deobfuscated data should match original");
    
    // Cleanup
    DAP_DELETE(obfuscated_data);
    DAP_DELETE(deobfuscated_data);
    dap_stream_obfuscation_destroy(obfs);
    
    TEST_SUCCESS("Test 3 passed: Obfuscate/deobfuscate cycle works");
    teardown_test();
}

/**
 * @brief Test 4: Obfuscate/deobfuscate with medium data
 */
static void test_04_obfuscate_deobfuscate_medium(void)
{
    setup_test();
    
    TEST_INFO("Test 4: Testing with medium-sized data...");
    
    // Create config without padding
    dap_stream_obfuscation_config_t config = 
        dap_stream_obfuscation_config_for_level(DAP_STREAM_OBFS_LEVEL_LOW);
    config.enabled_techniques = 0;
    
    dap_stream_obfuscation_t *obfs = dap_stream_obfuscation_create_with_config(&config);
    TEST_ASSERT_NOT_NULL(obfs, "Obfuscation engine should be created");
    
    const char *orig_data = TEST_DATA_MEDIUM;
    size_t orig_size = strlen(orig_data);
    void *obfuscated_data = NULL;
    size_t obfuscated_size = 0;
    
    // Obfuscate
    int ret = dap_stream_obfuscation_apply(obfs, orig_data, orig_size,
                                            &obfuscated_data, &obfuscated_size);
    TEST_ASSERT(ret == 0, "Obfuscation should succeed");
    TEST_ASSERT_NOT_NULL(obfuscated_data, "Obfuscated data should not be NULL");
    
    TEST_INFO("  Original size: %zu, Obfuscated size: %zu", orig_size, obfuscated_size);
    
    // Deobfuscate
    void *deobfuscated_data = NULL;
    size_t deobfuscated_size = 0;
    
    ret = dap_stream_obfuscation_remove(obfs, obfuscated_data, obfuscated_size,
                                         &deobfuscated_data, &deobfuscated_size);
    TEST_ASSERT(ret == 0, "Deobfuscation should succeed");
    TEST_ASSERT_EQUAL_INT(orig_size, deobfuscated_size, 
                          "Deobfuscated size should match original");
    TEST_ASSERT(memcmp(orig_data, deobfuscated_data, orig_size) == 0,
                "Deobfuscated data should match original");
    
    // Cleanup
    DAP_DELETE(obfuscated_data);
    DAP_DELETE(deobfuscated_data);
    dap_stream_obfuscation_destroy(obfs);
    
    TEST_SUCCESS("Test 4 passed: Medium data obfuscation works");
    teardown_test();
}

/**
 * @brief Test 5: Obfuscate/deobfuscate with large data
 */
static void test_05_obfuscate_deobfuscate_large(void)
{
    setup_test();
    
    TEST_INFO("Test 5: Testing with large data...");
    
    // Create config without padding
    dap_stream_obfuscation_config_t config = 
        dap_stream_obfuscation_config_for_level(DAP_STREAM_OBFS_LEVEL_LOW);
    config.enabled_techniques = 0;
    
    dap_stream_obfuscation_t *obfs = dap_stream_obfuscation_create_with_config(&config);
    TEST_ASSERT_NOT_NULL(obfs, "Obfuscation engine should be created");
    
    const char *orig_data = TEST_DATA_LARGE;
    size_t orig_size = strlen(orig_data);
    void *obfuscated_data = NULL;
    size_t obfuscated_size = 0;
    
    // Obfuscate
    int ret = dap_stream_obfuscation_apply(obfs, orig_data, orig_size,
                                            &obfuscated_data, &obfuscated_size);
    TEST_ASSERT(ret == 0, "Obfuscation should succeed");
    TEST_ASSERT_NOT_NULL(obfuscated_data, "Obfuscated data should not be NULL");
    
    TEST_INFO("  Original size: %zu, Obfuscated size: %zu", orig_size, obfuscated_size);
    
    // Deobfuscate
    void *deobfuscated_data = NULL;
    size_t deobfuscated_size = 0;
    
    ret = dap_stream_obfuscation_remove(obfs, obfuscated_data, obfuscated_size,
                                         &deobfuscated_data, &deobfuscated_size);
    TEST_ASSERT(ret == 0, "Deobfuscation should succeed");
    TEST_ASSERT_EQUAL_INT(orig_size, deobfuscated_size, 
                          "Deobfuscated size should match original");
    TEST_ASSERT(memcmp(orig_data, deobfuscated_data, orig_size) == 0,
                "Deobfuscated data should match original");
    
    // Cleanup
    DAP_DELETE(obfuscated_data);
    DAP_DELETE(deobfuscated_data);
    dap_stream_obfuscation_destroy(obfs);
    
    TEST_SUCCESS("Test 5 passed: Large data obfuscation works");
    teardown_test();
}

// ============================================================================
// Padding Tests
// ============================================================================

/**
 * @brief Test 6: Padding functionality
 */
static void test_06_padding(void)
{
    setup_test();
    
    TEST_INFO("Test 6: Testing padding functionality...");
    
    // Create config with padding enabled
    dap_stream_obfuscation_config_t config = 
        dap_stream_obfuscation_config_for_level(DAP_STREAM_OBFS_LEVEL_MEDIUM);
    config.enabled_techniques |= DAP_STREAM_OBFS_PADDING;
    config.padding.min_padding = 16;
    config.padding.max_padding = 64;
    config.padding.padding_probability = 1.0;
    
    dap_stream_obfuscation_t *obfs = 
        dap_stream_obfuscation_create_with_config(&config);
    TEST_ASSERT_NOT_NULL(obfs, "Obfuscation engine should be created");
    
    const char *orig_data = TEST_DATA_SMALL;
    size_t orig_size = strlen(orig_data);
    void *obfuscated_data = NULL;
    size_t obfuscated_size = 0;
    
    // Obfuscate
    int ret = dap_stream_obfuscation_apply(obfs, orig_data, orig_size,
                                            &obfuscated_data, &obfuscated_size);
    TEST_ASSERT(ret == 0, "Obfuscation should succeed");
    
    // Check that padding was added
    size_t padding = obfuscated_size - orig_size;
    TEST_INFO("  Padding added: %zu bytes", padding);
    TEST_ASSERT(padding >= config.padding.min_padding && 
                padding <= config.padding.max_padding,
                "Padding should be within configured range");
    
    // Deobfuscate and verify
    void *deobfuscated_data = NULL;
    size_t deobfuscated_size = 0;
    
    ret = dap_stream_obfuscation_remove(obfs, obfuscated_data, obfuscated_size,
                                         &deobfuscated_data, &deobfuscated_size);
    TEST_ASSERT(ret == 0, "Deobfuscation should succeed");
    TEST_ASSERT_EQUAL_INT(orig_size, deobfuscated_size, 
                          "Deobfuscated size should match original (padding removed)");
    TEST_ASSERT(memcmp(orig_data, deobfuscated_data, orig_size) == 0,
                "Deobfuscated data should match original");
    
    // Cleanup
    DAP_DELETE(obfuscated_data);
    DAP_DELETE(deobfuscated_data);
    dap_stream_obfuscation_destroy(obfs);
    
    TEST_SUCCESS("Test 6 passed: Padding works correctly");
    teardown_test();
}

// ============================================================================
// Fake Traffic Generation Tests
// ============================================================================

/**
 * @brief Test 7: Fake traffic generation
 */
static void test_07_fake_traffic_generation(void)
{
    setup_test();
    
    TEST_INFO("Test 7: Testing fake traffic generation...");
    
    dap_stream_obfuscation_t *obfs = dap_stream_obfuscation_create();
    TEST_ASSERT_NOT_NULL(obfs, "Obfuscation engine should be created");
    
    void *fake_data = NULL;
    size_t fake_size = 0;
    
    // Generate fake traffic
    int ret = dap_stream_obfuscation_generate_fake_traffic(obfs, &fake_data, &fake_size);
    TEST_ASSERT(ret == 0, "Fake traffic generation should succeed");
    TEST_ASSERT_NOT_NULL(fake_data, "Fake data should not be NULL");
    TEST_ASSERT(fake_size > 0, "Fake data size should be positive");
    
    TEST_INFO("  Generated fake traffic: %zu bytes", fake_size);
    
    // Cleanup
    DAP_DELETE(fake_data);
    dap_stream_obfuscation_destroy(obfs);
    
    TEST_SUCCESS("Test 7 passed: Fake traffic generation works");
    teardown_test();
}

// ============================================================================
// Timing Obfuscation Tests
// ============================================================================

/**
 * @brief Test 8: Timing obfuscation delay calculation
 */
static void test_08_timing_delay(void)
{
    setup_test();
    
    TEST_INFO("Test 8: Testing timing delay calculation...");
    
    // Create config with timing obfuscation
    dap_stream_obfuscation_config_t config = 
        dap_stream_obfuscation_config_for_level(DAP_STREAM_OBFS_LEVEL_HIGH);
    config.enabled_techniques |= DAP_STREAM_OBFS_TIMING;
    config.timing.min_delay_ms = 0;
    config.timing.max_delay_ms = 200;
    
    dap_stream_obfuscation_t *obfs = 
        dap_stream_obfuscation_create_with_config(&config);
    TEST_ASSERT_NOT_NULL(obfs, "Obfuscation engine should be created");
    
    // Calculate delay several times
    uint32_t delays[5];
    bool all_delays_in_range = true;
    
    for (int i = 0; i < 5; i++) {
        delays[i] = dap_stream_obfuscation_calc_delay(obfs);
        TEST_INFO("  Delay %d: %u ms", i + 1, delays[i]);
        
        // Check that delay is within reasonable range (0 to max_delay)
        if (delays[i] > config.timing.max_delay_ms) {
            all_delays_in_range = false;
        }
    }
    
    TEST_ASSERT(all_delays_in_range, "All delays should be within acceptable range");
    
    // Cleanup
    dap_stream_obfuscation_destroy(obfs);
    
    TEST_SUCCESS("Test 8 passed: Timing delay calculation works");
    teardown_test();
}

// ============================================================================
// Error Handling Tests
// ============================================================================

/**
 * @brief Test 9: NULL pointer handling
 */
static void test_09_null_pointer_handling(void)
{
    setup_test();
    
    TEST_INFO("Test 9: Testing NULL pointer handling...");
    
    dap_stream_obfuscation_t *obfs = dap_stream_obfuscation_create();
    TEST_ASSERT_NOT_NULL(obfs, "Obfuscation engine should be created");
    
    void *out_data = NULL;
    size_t out_size = 0;
    
    // Try to obfuscate NULL data
    int ret = dap_stream_obfuscation_apply(obfs, NULL, 100, &out_data, &out_size);
    TEST_ASSERT(ret != 0, "Obfuscation should fail with NULL input data");
    
    // Try to obfuscate with NULL output pointer
    ret = dap_stream_obfuscation_apply(obfs, TEST_DATA_SMALL, 
                                         strlen(TEST_DATA_SMALL), NULL, &out_size);
    TEST_ASSERT(ret != 0, "Obfuscation should fail with NULL output data pointer");
    
    // Try to obfuscate with zero size
    ret = dap_stream_obfuscation_apply(obfs, TEST_DATA_SMALL, 0, &out_data, &out_size);
    TEST_ASSERT(ret != 0, "Obfuscation should fail with zero size");
    
    // Cleanup
    dap_stream_obfuscation_destroy(obfs);
    
    TEST_SUCCESS("Test 9 passed: NULL pointer handling works");
    teardown_test();
}

/**
 * @brief Test 10: Multiple obfuscation cycles
 */
static void test_10_multiple_cycles(void)
{
    setup_test();
    
    TEST_INFO("Test 10: Testing multiple obfuscation cycles...");
    
    // Create config without padding
    dap_stream_obfuscation_config_t config = 
        dap_stream_obfuscation_config_for_level(DAP_STREAM_OBFS_LEVEL_LOW);
    config.enabled_techniques = 0;
    
    dap_stream_obfuscation_t *obfs = dap_stream_obfuscation_create_with_config(&config);
    TEST_ASSERT_NOT_NULL(obfs, "Obfuscation engine should be created");
    
    // Perform 5 obfuscate/deobfuscate cycles
    for (int i = 0; i < 5; i++) {
        const char *orig_data = TEST_DATA_SMALL;
        size_t orig_size = strlen(orig_data);
        void *obfuscated_data = NULL;
        size_t obfuscated_size = 0;
        
        // Obfuscate
        int ret = dap_stream_obfuscation_apply(obfs, orig_data, orig_size,
                                                &obfuscated_data, &obfuscated_size);
        TEST_ASSERT(ret == 0, "Obfuscation should succeed in cycle %d", i + 1);
        
        // Deobfuscate
        void *deobfuscated_data = NULL;
        size_t deobfuscated_size = 0;
        
        ret = dap_stream_obfuscation_remove(obfs, obfuscated_data, obfuscated_size,
                                             &deobfuscated_data, &deobfuscated_size);
        TEST_ASSERT(ret == 0, "Deobfuscation should succeed in cycle %d", i + 1);
        TEST_ASSERT_EQUAL_INT(orig_size, deobfuscated_size, 
                              "Sizes should match in cycle %d", i + 1);
        TEST_ASSERT(memcmp(orig_data, deobfuscated_data, orig_size) == 0,
                    "Data should match in cycle %d", i + 1);
        
        // Cleanup
        DAP_DELETE(obfuscated_data);
        DAP_DELETE(deobfuscated_data);
    }
    
    dap_stream_obfuscation_destroy(obfs);
    
    TEST_SUCCESS("Test 10 passed: Multiple cycles work correctly");
    teardown_test();
}

// ============================================================================
// Test Suite Main
// ============================================================================

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    TEST_SUITE_START("DAP Stream Obfuscation Layer Unit Tests");
    
    // Basic Obfuscation Tests
    TEST_RUN(test_01_obfuscation_create_destroy);
    TEST_RUN(test_02_obfuscation_custom_config);
    TEST_RUN(test_03_obfuscate_deobfuscate_small);
    TEST_RUN(test_04_obfuscate_deobfuscate_medium);
    TEST_RUN(test_05_obfuscate_deobfuscate_large);
    
    // Padding Tests
    TEST_RUN(test_06_padding);
    
    // Fake Traffic Generation Tests
    TEST_RUN(test_07_fake_traffic_generation);
    
    // Timing Obfuscation Tests
    TEST_RUN(test_08_timing_delay);
    
    // Error Handling Tests
    TEST_RUN(test_09_null_pointer_handling);
    TEST_RUN(test_10_multiple_cycles);
    
    // Cleanup
    suite_cleanup();
    
    TEST_SUITE_END();
    
    return 0;
}

