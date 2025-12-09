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
 * @file dap_test_helpers.h
 * @brief Common test helper macros and utilities for DAP SDK tests
 * 
 * Provides modern testing macros with enhanced output formatting
 * and assertion capabilities. Builds on top of base dap_test.h.
 * 
 * @date 2025-10-28
 */

#pragma once

#include "dap_test.h"
#include <stdarg.h>

// ============================================================================
// Test Output Formatting
// ============================================================================

/**
 * @brief Print informational message during test execution
 * @param fmt Format string (printf-style)
 */
#define TEST_INFO(fmt, ...) do { \
    printf("  ℹ️  " fmt "\n", ##__VA_ARGS__); \
    fflush(stdout); \
} while(0)

/**
 * @brief Print success message
 * @param fmt Format string (printf-style)
 */
#define TEST_SUCCESS(fmt, ...) do { \
    printf("  " TEXT_COLOR_GRN "✓ " fmt TEXT_COLOR_RESET "\n", ##__VA_ARGS__); \
    fflush(stdout); \
} while(0)

/**
 * @brief Print warning message
 * @param fmt Format string (printf-style)
 */
#define TEST_WARN(fmt, ...) do { \
    printf("  " TEXT_COLOR_YEL "⚠️  " fmt TEXT_COLOR_RESET "\n", ##__VA_ARGS__); \
    fflush(stdout); \
} while(0)

/**
 * @brief Print error message
 * @param fmt Format string (printf-style)
 */
#define TEST_ERROR(fmt, ...) do { \
    printf("  " TEXT_COLOR_RED "✗ " fmt TEXT_COLOR_RESET "\n", ##__VA_ARGS__); \
    fflush(stdout); \
} while(0)

// ============================================================================
// Test Suite Management
// ============================================================================

/**
 * @brief Start a test suite
 * @param suite_name Name of the test suite
 */
#define TEST_SUITE_START(suite_name) do { \
    printf("\n" TEXT_COLOR_CYN "═══════════════════════════════════════════════════════════\n"); \
    printf("  %s\n", suite_name); \
    printf("═══════════════════════════════════════════════════════════" TEXT_COLOR_RESET "\n\n"); \
    fflush(stdout); \
} while(0)

/**
 * @brief End a test suite
 */
#define TEST_SUITE_END() do { \
    printf("\n" TEXT_COLOR_GRN "═══════════════════════════════════════════════════════════\n"); \
    printf("  All tests completed successfully!\n"); \
    printf("═══════════════════════════════════════════════════════════" TEXT_COLOR_RESET "\n\n"); \
    fflush(stdout); \
} while(0)

/**
 * @brief Run a test function
 * @param test_func Test function to run
 */
#define TEST_RUN(test_func) do { \
    printf(TEXT_COLOR_BLU "➜ Running: %s" TEXT_COLOR_RESET "\n", #test_func); \
    fflush(stdout); \
    test_func(); \
    printf("\n"); \
} while(0)

// ============================================================================
// Enhanced Assertions
// ============================================================================

/**
 * @brief Assert a condition is true
 * @param condition Condition to check
 * @param fmt Error message format string
 */
#define TEST_ASSERT(condition, fmt, ...) do { \
    if (!(condition)) { \
        printf("  " TEXT_COLOR_RED "✗ ASSERTION FAILED: " fmt TEXT_COLOR_RESET "\n", ##__VA_ARGS__); \
        printf("    at %s:%d in %s()\n", __FILE__, __LINE__, __func__); \
        printf("    condition: %s\n", #condition); \
        fflush(stdout); \
        abort(); \
    } \
} while(0)

/**
 * @brief Assert two integers are equal
 * @param expected Expected value
 * @param actual Actual value
 * @param fmt Error message format string
 */
#define TEST_ASSERT_EQUAL_INT(expected, actual, fmt, ...) do { \
    int _exp = (expected); \
    int _act = (actual); \
    if (_exp != _act) { \
        printf("  " TEXT_COLOR_RED "✗ ASSERTION FAILED: " fmt TEXT_COLOR_RESET "\n", ##__VA_ARGS__); \
        printf("    at %s:%d in %s()\n", __FILE__, __LINE__, __func__); \
        printf("    expected: %d\n", _exp); \
        printf("    actual:   %d\n", _act); \
        fflush(stdout); \
        abort(); \
    } \
} while(0)

/**
 * @brief Assert two strings are equal
 * @param expected Expected string
 * @param actual Actual string
 * @param fmt Error message format string
 */
#define TEST_ASSERT_EQUAL_STRING(expected, actual, fmt, ...) do { \
    const char *_exp = (expected); \
    const char *_act = (actual); \
    if (strcmp(_exp, _act) != 0) { \
        printf("  " TEXT_COLOR_RED "✗ ASSERTION FAILED: " fmt TEXT_COLOR_RESET "\n", ##__VA_ARGS__); \
        printf("    at %s:%d in %s()\n", __FILE__, __LINE__, __func__); \
        printf("    expected: \"%s\"\n", _exp); \
        printf("    actual:   \"%s\"\n", _act); \
        fflush(stdout); \
        abort(); \
    } \
} while(0)

/**
 * @brief Assert a pointer is NULL
 * @param ptr Pointer to check
 * @param fmt Error message format string
 */
#define TEST_ASSERT_NULL(ptr, fmt, ...) do { \
    void *_p = (void*)(ptr); \
    if (_p != NULL) { \
        printf("  " TEXT_COLOR_RED "✗ ASSERTION FAILED: " fmt TEXT_COLOR_RESET "\n", ##__VA_ARGS__); \
        printf("    at %s:%d in %s()\n", __FILE__, __LINE__, __func__); \
        printf("    expected: NULL\n"); \
        printf("    actual:   %p\n", _p); \
        fflush(stdout); \
        abort(); \
    } \
} while(0)

/**
 * @brief Assert a pointer is NOT NULL
 * @param ptr Pointer to check
 * @param fmt Error message format string
 */
#define TEST_ASSERT_NOT_NULL(ptr, fmt, ...) do { \
    void *_p = (void*)(ptr); \
    if (_p == NULL) { \
        printf("  " TEXT_COLOR_RED "✗ ASSERTION FAILED: " fmt TEXT_COLOR_RESET "\n", ##__VA_ARGS__); \
        printf("    at %s:%d in %s()\n", __FILE__, __LINE__, __func__); \
        printf("    expected: non-NULL pointer\n"); \
        printf("    actual:   NULL\n"); \
        fflush(stdout); \
        abort(); \
    } \
} while(0)

/**
 * @brief Unconditionally fail a test
 * @param fmt Error message format string
 */
#define TEST_FAIL(fmt, ...) do { \
    printf("  " TEXT_COLOR_RED "✗ TEST FAILED: " fmt TEXT_COLOR_RESET "\n", ##__VA_ARGS__); \
    printf("    at %s:%d in %s()\n", __FILE__, __LINE__, __func__); \
    fflush(stdout); \
    abort(); \
} while(0)

// ============================================================================
// Test Expectations (non-fatal)
// ============================================================================

static int g_test_expectations_failed = 0;

/**
 * @brief Expect a condition (non-fatal, test continues)
 * @param condition Condition to check
 * @param fmt Error message format string
 */
#define TEST_EXPECT(condition, fmt, ...) do { \
    if (!(condition)) { \
        printf("  " TEXT_COLOR_YEL "⚠️  EXPECTATION FAILED: " fmt TEXT_COLOR_RESET "\n", ##__VA_ARGS__); \
        printf("    at %s:%d in %s()\n", __FILE__, __LINE__, __func__); \
        printf("    condition: %s\n", #condition); \
        fflush(stdout); \
        g_test_expectations_failed++; \
    } \
} while(0)

/**
 * @brief Check if any expectations failed and abort if so
 */
#define TEST_CHECK_EXPECTATIONS() do { \
    if (g_test_expectations_failed > 0) { \
        printf("  " TEXT_COLOR_RED "✗ %d expectation(s) failed!" TEXT_COLOR_RESET "\n", \
               g_test_expectations_failed); \
        fflush(stdout); \
        g_test_expectations_failed = 0; \
        abort(); \
    } \
} while(0)

/**
 * @brief Reset expectations counter (use between tests)
 */
#define TEST_RESET_EXPECTATIONS() do { \
    g_test_expectations_failed = 0; \
} while(0)

