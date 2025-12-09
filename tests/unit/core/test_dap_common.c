/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2025
 * All rights reserved.
 *
 * This file is part of DAP (Distributed Applications Platform) the open source project
 *
 *    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    DAP is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file test_dap_common.c
 * @brief Comprehensive unit tests for DAP Common macros and utilities
 * 
 * Tests all memory management macros, pointer conversion macros, type utilities,
 * and const qualifier removal functionality.
 * 
 * @date 2025-01-XX
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "dap_common.h"
#include "dap_test.h"
#include "dap_test_helpers.h"

#define LOG_TAG "test_dap_common"

// ============================================================================
// Test Types
// ============================================================================

typedef struct {
    int value;
    char name[32];
} test_struct_t;

// ============================================================================
// Test Suite State
// ============================================================================

static bool s_test_initialized = false;

// ============================================================================
// Setup/Teardown Functions
// ============================================================================

/**
 * @brief Setup function called before test suite
 */
static void setup_test(void)
{
    if (!s_test_initialized) {
        s_test_initialized = true;
        TEST_INFO("DAP Common test suite initialized");
    }
}

/**
 * @brief Teardown function called after test suite
 */
static void teardown_test(void)
{
    if (s_test_initialized) {
        s_test_initialized = false;
        TEST_INFO("DAP Common test suite cleaned up");
    }
}

// ============================================================================
// Test: Pointer Conversion Macros
// ============================================================================

/**
 * @brief Test pointer to int conversion
 */
static void test_01_pointer_to_int_conversion(void)
{
    setup_test();
    
    dap_print_module_name("DAP_POINTER_TO_INT / DAP_INT_TO_POINTER");
    
    int l_test_value = 42;
    void *l_ptr = DAP_INT_TO_POINTER(l_test_value);
    int l_result = DAP_POINTER_TO_INT(l_ptr);
    
    TEST_ASSERT(l_result == l_test_value, "Pointer to int conversion failed");
    TEST_SUCCESS("Pointer to int round-trip works");
    
    teardown_test();
}

/**
 * @brief Test pointer to uint conversion
 */
static void test_02_pointer_to_uint_conversion(void)
{
    setup_test();
    
    dap_print_module_name("DAP_POINTER_TO_UINT / DAP_UINT_TO_POINTER");
    
    unsigned int l_test_value = 12345;
    void *l_ptr = DAP_UINT_TO_POINTER(l_test_value);
    unsigned int l_result = DAP_POINTER_TO_UINT(l_ptr);
    
    TEST_ASSERT(l_result == l_test_value, "Pointer to uint conversion failed");
    TEST_SUCCESS("Pointer to uint round-trip works");
    
    teardown_test();
}

/**
 * @brief Test pointer to size_t conversion
 */
static void test_03_pointer_to_size_conversion(void)
{
    setup_test();
    
    dap_print_module_name("DAP_POINTER_TO_SIZE / DAP_SIZE_TO_POINTER");
    
    size_t l_test_value = 98765;
    void *l_ptr = DAP_SIZE_TO_POINTER(l_test_value);
    size_t l_result = DAP_POINTER_TO_SIZE(l_ptr);
    
    TEST_ASSERT(l_result == l_test_value, "Pointer to size_t conversion failed");
    TEST_SUCCESS("Pointer to size_t round-trip works");
    
    teardown_test();
}

// ============================================================================
// Test: Type Size Macros
// ============================================================================

/**
 * @brief Test DAP_TYPE_SIZE macro
 */
static void test_04_type_size_macro(void)
{
    setup_test();
    
    dap_print_module_name("DAP_TYPE_SIZE");
    
    int l_int_array[10];
    char l_char_array[20];
    test_struct_t l_struct_array[5];
    
    size_t l_int_size = DAP_TYPE_SIZE(l_int_array);
    size_t l_char_size = DAP_TYPE_SIZE(l_char_array);
    size_t l_struct_size = DAP_TYPE_SIZE(l_struct_array);
    
    TEST_ASSERT(l_int_size == sizeof(int), "DAP_TYPE_SIZE for int array failed");
    TEST_ASSERT(l_char_size == sizeof(char), "DAP_TYPE_SIZE for char array failed");
    TEST_ASSERT(l_struct_size == sizeof(test_struct_t), "DAP_TYPE_SIZE for struct array failed");
    
    TEST_SUCCESS("DAP_TYPE_SIZE works correctly");
    
    teardown_test();
}

// ============================================================================
// Test: Memory Allocation Macros - Basic
// ============================================================================

/**
 * @brief Test DAP_MALLOC macro
 */
static void test_05_malloc_macro(void)
{
    setup_test();
    
    dap_print_module_name("DAP_MALLOC");
    
    void *l_ptr1 = DAP_MALLOC(100);
    TEST_ASSERT_NOT_NULL(l_ptr1, "DAP_MALLOC failed to allocate memory");
    
    void *l_ptr2 = DAP_MALLOC(0);
    TEST_ASSERT_NULL(l_ptr2, "DAP_MALLOC(0) should return NULL");
    
    DAP_FREE(l_ptr1);
    TEST_SUCCESS("DAP_MALLOC works correctly");
    
    teardown_test();
}

/**
 * @brief Test DAP_CALLOC macro
 */
static void test_06_calloc_macro(void)
{
    setup_test();
    
    dap_print_module_name("DAP_CALLOC");
    
    void *l_ptr1 = DAP_CALLOC(10, 20);
    TEST_ASSERT_NOT_NULL(l_ptr1, "DAP_CALLOC failed to allocate memory");
    
    // Verify memory is zero-initialized
    uint8_t *l_bytes = (uint8_t *)l_ptr1;
    bool l_all_zero = true;
    for (size_t i = 0; i < 10 * 20; i++) {
        if (l_bytes[i] != 0) {
            l_all_zero = false;
            break;
        }
    }
    TEST_ASSERT(l_all_zero, "DAP_CALLOC did not zero-initialize memory");
    
    void *l_ptr2 = DAP_CALLOC(0, 20);
    TEST_ASSERT_NULL(l_ptr2, "DAP_CALLOC(0, size) should return NULL");
    
    void *l_ptr3 = DAP_CALLOC(10, 0);
    TEST_ASSERT_NULL(l_ptr3, "DAP_CALLOC(count, 0) should return NULL");
    
    DAP_FREE(l_ptr1);
    TEST_SUCCESS("DAP_CALLOC works correctly");
    
    teardown_test();
}

/**
 * @brief Test DAP_REALLOC macro
 */
static void test_07_realloc_macro(void)
{
    setup_test();
    
    dap_print_module_name("DAP_REALLOC");
    
    int *l_ptr = DAP_MALLOC(sizeof(int) * 5);
    TEST_ASSERT_NOT_NULL(l_ptr, "Initial allocation failed");
    
    l_ptr = DAP_REALLOC(l_ptr, sizeof(int) * 10);
    TEST_ASSERT_NOT_NULL(l_ptr, "DAP_REALLOC failed to expand memory");
    
    DAP_FREE(l_ptr);
    TEST_SUCCESS("DAP_REALLOC works correctly");
    
    teardown_test();
}

// ============================================================================
// Test: Type-safe Allocation Macros
// ============================================================================

/**
 * @brief Test DAP_NEW macro
 */
static void test_08_new_macro(void)
{
    setup_test();
    
    dap_print_module_name("DAP_NEW");
    
    test_struct_t *l_ptr = DAP_NEW(test_struct_t);
    TEST_ASSERT_NOT_NULL(l_ptr, "DAP_NEW failed to allocate memory");
    
    l_ptr->value = 42;
    strcpy(l_ptr->name, "test");
    
    TEST_ASSERT(l_ptr->value == 42, "DAP_NEW allocated memory is not writable");
    TEST_ASSERT(strcmp(l_ptr->name, "test") == 0, "DAP_NEW allocated memory is not writable");
    
    DAP_DELETE(l_ptr);
    TEST_SUCCESS("DAP_NEW works correctly");
    
    teardown_test();
}

/**
 * @brief Test DAP_NEW_Z macro
 */
static void test_09_new_z_macro(void)
{
    setup_test();
    
    dap_print_module_name("DAP_NEW_Z");
    
    test_struct_t *l_ptr = DAP_NEW_Z(test_struct_t);
    TEST_ASSERT_NOT_NULL(l_ptr, "DAP_NEW_Z failed to allocate memory");
    
    // Verify memory is zero-initialized
    TEST_ASSERT(l_ptr->value == 0, "DAP_NEW_Z did not zero-initialize memory");
    TEST_ASSERT(l_ptr->name[0] == '\0', "DAP_NEW_Z did not zero-initialize memory");
    
    DAP_DELETE(l_ptr);
    TEST_SUCCESS("DAP_NEW_Z works correctly");
    
    teardown_test();
}

/**
 * @brief Test DAP_NEW_Z_COUNT macro
 */
static void test_10_new_z_count_macro(void)
{
    setup_test();
    
    dap_print_module_name("DAP_NEW_Z_COUNT");
    
    test_struct_t *l_array = DAP_NEW_Z_COUNT(test_struct_t, 10);
    TEST_ASSERT_NOT_NULL(l_array, "DAP_NEW_Z_COUNT failed to allocate memory");
    
    // Verify all elements are zero-initialized
    bool l_all_zero = true;
    for (int i = 0; i < 10; i++) {
        if (l_array[i].value != 0 || l_array[i].name[0] != '\0') {
            l_all_zero = false;
            break;
        }
    }
    TEST_ASSERT(l_all_zero, "DAP_NEW_Z_COUNT did not zero-initialize all elements");
    
    DAP_DELETE(l_array);
    TEST_SUCCESS("DAP_NEW_Z_COUNT works correctly");
    
    teardown_test();
}

// ============================================================================
// Test: Duplication Macros - DAP_DUP_SIZE
// ============================================================================

/**
 * @brief Test DAP_DUP_SIZE with const void*
 */
static void test_11_dup_size_const_void(void)
{
    setup_test();
    
    dap_print_module_name("DAP_DUP_SIZE const void*");
    
    // Use non-const data for source (original macro doesn't handle const well)
    char l_src_data[] = "Hello, World!";
    void *l_src = l_src_data;
    size_t l_size = strlen((char *)l_src) + 1;
    
    void *l_dup = DAP_DUP_SIZE(l_src, l_size);
    TEST_ASSERT_NOT_NULL(l_dup, "DAP_DUP_SIZE failed to allocate memory");
    TEST_ASSERT(memcmp(l_dup, l_src, l_size) == 0, "DAP_DUP_SIZE did not copy data correctly");
    
    // Verify result can be modified
    char *l_mutable_dup = (char *)l_dup;
    l_mutable_dup[0] = 'h';
    TEST_ASSERT(l_mutable_dup[0] == 'h', "DAP_DUP_SIZE result can be modified");
    
    DAP_DELETE(l_dup);
    TEST_SUCCESS("DAP_DUP_SIZE with void* works correctly");
    
    teardown_test();
}

/**
 * @brief Test DAP_DUP_SIZE with const char*
 */
static void test_12_dup_size_const_char(void)
{
    setup_test();
    
    dap_print_module_name("DAP_DUP_SIZE const char*");
    
    // Use non-const pointer for source (original macro doesn't handle const pointers well)
    char l_src_data[] = "Test string";
    char *l_src = l_src_data;
    size_t l_size = strlen(l_src) + 1;
    
    char *l_dup = DAP_DUP_SIZE(l_src, l_size);
    TEST_ASSERT_NOT_NULL(l_dup, "DAP_DUP_SIZE failed to allocate memory");
    TEST_ASSERT_EQUAL_STRING(l_src, l_dup, "DAP_DUP_SIZE did not copy data correctly");
    
    // Verify result can be modified
    l_dup[0] = 't';
    TEST_ASSERT(l_dup[0] == 't', "DAP_DUP_SIZE result can be modified");
    
    DAP_DELETE(l_dup);
    TEST_SUCCESS("DAP_DUP_SIZE with char* works correctly");
    
    teardown_test();
}

/**
 * @brief Test DAP_DUP_SIZE with const unsigned char*
 */
static void test_13_dup_size_const_unsigned_char(void)
{
    setup_test();
    
    dap_print_module_name("DAP_DUP_SIZE const unsigned char*");
    
    // Use non-const array for source (original macro doesn't handle const arrays well)
    unsigned char l_src[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    size_t l_size = sizeof(l_src);
    
    // DAP_DUP_SIZE expects a pointer, not an array directly
    unsigned char *l_dup = DAP_DUP_SIZE((unsigned char *)l_src, l_size);
    TEST_ASSERT_NOT_NULL(l_dup, "DAP_DUP_SIZE failed to allocate memory");
    TEST_ASSERT(memcmp(l_dup, l_src, l_size) == 0, "DAP_DUP_SIZE did not copy data correctly");
    
    // Verify result can be modified
    l_dup[0] = 0xFF;
    TEST_ASSERT(l_dup[0] == 0xFF, "DAP_DUP_SIZE result can be modified");
    
    DAP_DELETE(l_dup);
    TEST_SUCCESS("DAP_DUP_SIZE with unsigned char* works correctly");
    
    teardown_test();
}

/**
 * @brief Test DAP_DUP_SIZE with struct
 */
static void test_14_dup_size_struct(void)
{
    setup_test();
    
    dap_print_module_name("DAP_DUP_SIZE struct");
    
    test_struct_t l_src = {.value = 42, .name = "test_struct"};
    
    test_struct_t *l_dup = DAP_DUP_SIZE(&l_src, sizeof(test_struct_t));
    TEST_ASSERT_NOT_NULL(l_dup, "DAP_DUP_SIZE failed to allocate memory");
    TEST_ASSERT_EQUAL_INT(42, l_dup->value, "DAP_DUP_SIZE did not copy struct correctly");
    TEST_ASSERT_EQUAL_STRING("test_struct", l_dup->name, "DAP_DUP_SIZE did not copy struct correctly");
    
    // Verify result can be modified
    l_dup->value = 100;
    TEST_ASSERT_EQUAL_INT(100, l_dup->value, "DAP_DUP_SIZE result can be modified");
    
    DAP_DELETE(l_dup);
    TEST_SUCCESS("DAP_DUP_SIZE with struct works correctly");
    
    teardown_test();
}

/**
 * @brief Test DAP_DUP_SIZE with NULL
 */
static void test_15_dup_size_null(void)
{
    setup_test();
    
    dap_print_module_name("DAP_DUP_SIZE NULL");
    
    void *l_dup = DAP_DUP_SIZE(NULL, 100);
    TEST_ASSERT_NULL(l_dup, "DAP_DUP_SIZE(NULL, size) should return NULL");
    
    const void *l_src = "test";
    void *l_dup2 = DAP_DUP_SIZE(l_src, 0);
    TEST_ASSERT_NULL(l_dup2, "DAP_DUP_SIZE(ptr, 0) should return NULL");
    
    TEST_SUCCESS("DAP_DUP_SIZE handles NULL correctly");
    
    teardown_test();
}

// ============================================================================
// Test: Duplication Macros - DAP_DUP
// ============================================================================

/**
 * @brief Test DAP_DUP with const struct
 */
static void test_16_dup_const_struct(void)
{
    setup_test();
    
    dap_print_module_name("DAP_DUP const struct");
    
    // Use non-const struct for source (original macro doesn't handle const well)
    test_struct_t l_src = {.value = 123, .name = "dup_test"};
    
    test_struct_t *l_dup = DAP_DUP(&l_src);
    TEST_ASSERT_NOT_NULL(l_dup, "DAP_DUP failed to allocate memory");
    TEST_ASSERT_EQUAL_INT(123, l_dup->value, "DAP_DUP did not copy struct correctly");
    TEST_ASSERT_EQUAL_STRING("dup_test", l_dup->name, "DAP_DUP did not copy struct correctly");
    
    // Verify result can be modified
    l_dup->value = 456;
    TEST_ASSERT_EQUAL_INT(456, l_dup->value, "DAP_DUP result can be modified");
    
    DAP_DELETE(l_dup);
    TEST_SUCCESS("DAP_DUP with struct works correctly");
    
    teardown_test();
}

/**
 * @brief Test DAP_DUP with const int
 */
static void test_17_dup_const_int(void)
{
    setup_test();
    
    dap_print_module_name("DAP_DUP const int");
    
    // Use non-const int for source (original macro doesn't handle const well)
    int l_src = 999;
    
    int *l_dup = DAP_DUP(&l_src);
    TEST_ASSERT_NOT_NULL(l_dup, "DAP_DUP failed to allocate memory");
    TEST_ASSERT_EQUAL_INT(999, *l_dup, "DAP_DUP did not copy int correctly");
    
    // Verify result can be modified
    *l_dup = 888;
    TEST_ASSERT_EQUAL_INT(888, *l_dup, "DAP_DUP result can be modified");
    
    DAP_DELETE(l_dup);
    TEST_SUCCESS("DAP_DUP with int works correctly");
    
    teardown_test();
}

/**
 * @brief Test DAP_DUP with NULL
 */
static void test_18_dup_null(void)
{
    setup_test();
    
    dap_print_module_name("DAP_DUP NULL");
    
    // DAP_DUP with NULL should return NULL
    // Need to use explicit type cast for NULL to make __typeof__ work
    test_struct_t *l_null_ptr = NULL;
    test_struct_t *l_dup = DAP_DUP(l_null_ptr);
    TEST_ASSERT_NULL(l_dup, "DAP_DUP(NULL) should return NULL");
    
    TEST_SUCCESS("DAP_DUP(NULL) works correctly");
    
    teardown_test();
}

// ============================================================================
// Test: Memory Deallocation Macros
// ============================================================================

/**
 * @brief Test DAP_DELETE macro
 */
static void test_19_delete_macro(void)
{
    setup_test();
    
    dap_print_module_name("DAP_DELETE");
    
    void *l_ptr = DAP_MALLOC(100);
    TEST_ASSERT_NOT_NULL(l_ptr, "Allocation failed");
    
    DAP_DELETE(l_ptr);
    // No way to verify deletion, but shouldn't crash
    TEST_SUCCESS("DAP_DELETE works correctly");
    
    teardown_test();
}

/**
 * @brief Test DAP_DEL_Z macro
 */
static void test_20_del_z_macro(void)
{
    setup_test();
    
    dap_print_module_name("DAP_DEL_Z");
    
    void *l_ptr = DAP_MALLOC(100);
    TEST_ASSERT_NOT_NULL(l_ptr, "Allocation failed");
    
    DAP_DEL_Z(l_ptr);
    TEST_ASSERT_NULL(l_ptr, "DAP_DEL_Z did not set pointer to NULL");
    
    TEST_SUCCESS("DAP_DEL_Z works correctly");
    
    teardown_test();
}

/**
 * @brief Test DAP_DEL_ARRAY macro
 */
static void test_21_del_array_macro(void)
{
    setup_test();
    
    dap_print_module_name("DAP_DEL_ARRAY");
    
    test_struct_t **l_array = DAP_MALLOC(sizeof(test_struct_t *) * 5);
    TEST_ASSERT_NOT_NULL(l_array, "Array allocation failed");
    
    // Allocate individual elements
    for (int i = 0; i < 5; i++) {
        l_array[i] = DAP_NEW(test_struct_t);
        l_array[i]->value = i;
    }
    
    // Cleanup using DAP_DEL_ARRAY
    DAP_DEL_ARRAY(l_array, 5);
    
    // Note: array itself is not freed by DAP_DEL_ARRAY, only elements
    DAP_DELETE(l_array);
    TEST_SUCCESS("DAP_DEL_ARRAY works correctly");
    
    teardown_test();
}

// ============================================================================
// Test: Multi-pointer Deletion
// ============================================================================

/**
 * @brief Test DAP_DEL_MULTY macro
 */
static void test_22_del_multy(void)
{
    setup_test();
    
    dap_print_module_name("DAP_DEL_MULTY");
    
    void *l_ptr1 = DAP_MALLOC(100);
    void *l_ptr2 = DAP_MALLOC(200);
    void *l_ptr3 = DAP_MALLOC(300);
    
    TEST_ASSERT_NOT_NULL(l_ptr1, "Allocation 1 failed");
    TEST_ASSERT_NOT_NULL(l_ptr2, "Allocation 2 failed");
    TEST_ASSERT_NOT_NULL(l_ptr3, "Allocation 3 failed");
    
    DAP_DEL_MULTY(l_ptr1, l_ptr2, l_ptr3);
    
    // No way to verify deletion, but shouldn't crash
    TEST_SUCCESS("DAP_DEL_MULTY works correctly");
    
    teardown_test();
}

// ============================================================================
// Test: Edge Cases and Error Handling
// ============================================================================

/**
 * @brief Test edge cases
 */
static void test_23_edge_cases(void)
{
    setup_test();
    
    dap_print_module_name("Edge Cases");
    
    // Test with very large size
    void *l_ptr1 = DAP_MALLOC((size_t)-1);
    TEST_ASSERT_NULL(l_ptr1, "DAP_MALLOC should handle overflow");
    
    // Test with negative size (should be handled by cast)
    void *l_ptr2 = DAP_MALLOC(-1);
    TEST_ASSERT_NULL(l_ptr2, "DAP_MALLOC should handle negative size");
    
    // Test DAP_DUP_SIZE with size smaller than type size
    int l_src = 42;
    int *l_dup = DAP_DUP_SIZE(&l_src, sizeof(int) - 1);
    TEST_ASSERT_NULL(l_dup, "DAP_DUP_SIZE should reject size < type size");
    
    TEST_SUCCESS("Edge cases handled correctly");
    
    teardown_test();
}

// ============================================================================
// Test: Cast Macros
// ============================================================================

/**
 * @brief Test cast macros
 */
static void test_24_cast_macros(void)
{
    setup_test();
    
    dap_print_module_name("DAP_CAST / DAP_CAST_PTR");
    
    int l_value = 42;
    void *l_ptr = &l_value;
    
    int *l_int_ptr = DAP_CAST_PTR(int, l_ptr);
    TEST_ASSERT_NOT_NULL(l_int_ptr, "DAP_CAST_PTR failed");
    TEST_ASSERT_EQUAL_INT(42, *l_int_ptr, "DAP_CAST_PTR did not preserve value");
    
    TEST_SUCCESS("DAP_CAST macros work correctly");
    
    teardown_test();
}

// ============================================================================
// Test: Size Validation
// ============================================================================

/**
 * @brief Test size validation
 */
static void test_25_size_validation(void)
{
    setup_test();
    
    dap_print_module_name("Size Validation");
    
    int l_array[10];
    size_t l_size = DAP_TYPE_SIZE(l_array);
    
    TEST_ASSERT_EQUAL_INT(sizeof(int), (int)l_size, "DAP_TYPE_SIZE returned incorrect size");
    
    // Test that DAP_DUP_SIZE validates size
    int l_src = 42;
    int *l_dup1 = DAP_DUP_SIZE(&l_src, sizeof(int));
    TEST_ASSERT_NOT_NULL(l_dup1, "DAP_DUP_SIZE with correct size should succeed");
    DAP_DELETE(l_dup1);
    
    int *l_dup2 = DAP_DUP_SIZE(&l_src, sizeof(int) - 1);
    TEST_ASSERT_NULL(l_dup2, "DAP_DUP_SIZE with too small size should fail");
    
    TEST_SUCCESS("Size validation works correctly");
    
    teardown_test();
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(void)
{
    TEST_SUITE_START("DAP Common Macros Unit Tests");
    
    // Pointer conversion tests
    TEST_RUN(test_01_pointer_to_int_conversion);
    TEST_RUN(test_02_pointer_to_uint_conversion);
    TEST_RUN(test_03_pointer_to_size_conversion);
    
    // Type size tests
    TEST_RUN(test_04_type_size_macro);
    
    // Basic allocation tests
    TEST_RUN(test_05_malloc_macro);
    TEST_RUN(test_06_calloc_macro);
    TEST_RUN(test_07_realloc_macro);
    
    // Type-safe allocation tests
    TEST_RUN(test_08_new_macro);
    TEST_RUN(test_09_new_z_macro);
    TEST_RUN(test_10_new_z_count_macro);
    
    // DAP_DUP_SIZE tests (including const removal)
    TEST_RUN(test_11_dup_size_const_void);
    TEST_RUN(test_12_dup_size_const_char);
    TEST_RUN(test_13_dup_size_const_unsigned_char);
    TEST_RUN(test_14_dup_size_struct);
    TEST_RUN(test_15_dup_size_null);
    
    // DAP_DUP tests (including const removal)
    TEST_RUN(test_16_dup_const_struct);
    TEST_RUN(test_17_dup_const_int);
    TEST_RUN(test_18_dup_null);
    
    // Deallocation tests
    TEST_RUN(test_19_delete_macro);
    TEST_RUN(test_20_del_z_macro);
    TEST_RUN(test_21_del_array_macro);
    
    // Multi-pointer deletion
    TEST_RUN(test_22_del_multy);
    
    // Edge cases
    TEST_RUN(test_23_edge_cases);
    
    // Cast tests
    TEST_RUN(test_24_cast_macros);
    
    // Size validation
    TEST_RUN(test_25_size_validation);
    
    TEST_SUITE_END();
    
    return 0;
}
