/*
 * Authors:
 * Dmitry Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DAP SDK  https://gitlab.demlabs.net/dap/dap-sdk
 * Copyright  (c) 2025
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "dap_common.h"
#include "dap_time.h"

/**
 * @file test_helpers.h
 * @brief Common test utilities and helper functions
 * @details Shared utilities for all DAP SDK tests
 */

// Test assertion macros
#define DAP_TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            log_it(L_ERROR, "TEST FAILED: %s at %s:%d", message, __FILE__, __LINE__); \
            return false; \
        } \
    } while(0)

#define DAP_TEST_ASSERT_NOT_NULL(ptr, name) \
    DAP_TEST_ASSERT(ptr != NULL, name " should not be NULL")

#define DAP_TEST_ASSERT_NULL(ptr, name) \
    DAP_TEST_ASSERT(ptr == NULL, name " should be NULL")

#define DAP_TEST_ASSERT_EQUAL(expected, actual, name) \
    DAP_TEST_ASSERT(expected == actual, name " values should be equal")

#define DAP_TEST_ASSERT_STRING_EQUAL(expected, actual, name) \
    DAP_TEST_ASSERT(strcmp(expected, actual) == 0, name " strings should be equal")

// Test timing utilities
typedef struct {
    uint64_t start_time;
    uint64_t end_time;
} dap_test_timer_t;

/**
 * @brief Start performance timer
 * @param a_timer Timer structure
 */
static inline void dap_test_timer_start(dap_test_timer_t* a_timer) {
    a_timer->start_time = dap_time_now();
}

/**
 * @brief Stop performance timer and return elapsed time in microseconds
 * @param a_timer Timer structure
 * @return Elapsed time in microseconds
 */
static inline uint64_t dap_test_timer_stop(dap_test_timer_t* a_timer) {
    a_timer->end_time = dap_time_now();
    return a_timer->end_time - a_timer->start_time;
}

// Memory testing utilities
/**
 * @brief Test memory allocation and check for leaks
 * @param a_size Size to allocate
 * @return Allocated pointer or NULL on failure
 */
void* dap_test_mem_alloc(size_t a_size);

/**
 * @brief Free test memory and verify no corruption
 * @param a_ptr Pointer to free
 */
void dap_test_mem_free(void* a_ptr);

// Random data generation for tests
/**
 * @brief Generate random bytes for testing
 * @param a_buffer Output buffer
 * @param a_size Number of bytes to generate
 */
void dap_test_random_bytes(uint8_t* a_buffer, size_t a_size);

/**
 * @brief Generate random string for testing
 * @param a_length Length of string to generate
 * @return Allocated random string (must be freed)
 */
char* dap_test_random_string(size_t a_length);

// Test configuration helpers
/**
 * @brief Setup minimal DAP SDK environment for testing
 * @return 0 on success, negative error code on failure
 */
int dap_test_sdk_init(void);

/**
 * @brief Cleanup DAP SDK test environment
 */
void dap_test_sdk_cleanup(void);

