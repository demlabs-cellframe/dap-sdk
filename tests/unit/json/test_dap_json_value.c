/**
 * @file test_dap_json_value.c
 * @brief Unit tests for compact JSON values
 * 
 * @details Tests all operations on dap_json_value_ref_t:
 * - Creation and initialization
 * - Accessor functions
 * - Conversion (value → ref → value)
 * - Materialization (lazy string/number parsing)
 * - Array and object operations
 * - Validation and error handling
 * 
 * @note Phase 2.0.2: Memory Crisis Resolution - Testing
 * @date 2026-01-18
 */

#include "dap_test.h"
#include "dap_json_value_internal.h"
#include "dap_json_type.h"
#include "dap_arena.h"
#include <string.h>
#include <stdlib.h>

// Test source JSON buffer
static const char *s_test_json = 
    "{"
    "\"name\":\"Alice\","
    "\"age\":30,"
    "\"active\":true,"
    "\"balance\":123.45,"
    "\"data\":null"
    "}";

/**
 * @brief Test: Create simple refs
 */
DAP_TEST(test_ref_create_simple) {
    // Create a simple string ref
    dap_json_value_ref_t ref = dap_json_ref_create(TYPE_STRING, 10, 5, 0);
    
    DAP_ASSERT_EQUAL(ref.type, TYPE_STRING);
    DAP_ASSERT_EQUAL(ref.offset, 10);
    DAP_ASSERT_EQUAL(ref.length, 5);
    DAP_ASSERT_EQUAL(ref.flags, 0);
    
    // Verify size
    DAP_ASSERT_EQUAL(sizeof(dap_json_value_ref_t), 8);
    
    return 0;
}

/**
 * @brief Test: Extended refs (>64KB)
 */
DAP_TEST(test_ref_create_extended) {
    // Create extended ref for huge value
    dap_json_value_ref_ext_t ext = dap_json_ref_create_extended(
        TYPE_STRING,
        1000,
        100000, // 100 KB
        0
    );
    
    DAP_ASSERT_EQUAL(ext.base.type, TYPE_STRING);
    DAP_ASSERT_EQUAL(ext.base.offset, 1000);
    DAP_ASSERT_EQUAL(ext.base.length, 0xFFFF); // Sentinel
    DAP_ASSERT_TRUE(ext.base.flags & DAP_JSON_REF_FLAG_OVERFLOW);
    DAP_ASSERT_EQUAL(ext.ext_length, 100000);
    
    // Test extended length getter
    size_t len = dap_json_ref_get_length(&ext.base);
    DAP_ASSERT_EQUAL(len, 100000);
    
    return 0;
}

/**
 * @brief Test: Get pointer to data
 */
DAP_TEST(test_ref_get_ptr) {
    const char *source = "Hello, World!";
    size_t source_len = strlen(source);
    
    // Create ref pointing to "World"
    dap_json_value_ref_t ref = dap_json_ref_create(TYPE_STRING, 7, 5, 0);
    
    const char *ptr = dap_json_ref_get_ptr(&ref, source);
    DAP_ASSERT_NOT_NULL(ptr);
    DAP_ASSERT_EQUAL(memcmp(ptr, "World", 5), 0);
    
    return 0;
}

/**
 * @brief Test: String materialization (null-termination)
 */
DAP_TEST(test_ref_materialize_string) {
    const char *source = s_test_json;
    struct dap_arena *arena = dap_arena_create(1024);
    DAP_ASSERT_NOT_NULL(arena);
    
    // Create ref to "Alice" (offset 9, length 5)
    dap_json_value_ref_t ref = dap_json_ref_create(TYPE_STRING, 9, 5, 0);
    
    // Materialize (add null terminator)
    char *materialized = dap_json_ref_materialize_string(&ref, source, arena);
    DAP_ASSERT_NOT_NULL(materialized);
    DAP_ASSERT_STRING_EQUAL(materialized, "Alice");
    
    dap_arena_delete(arena);
    return 0;
}

/**
 * @brief Test: Parse integer from ref
 */
DAP_TEST(test_ref_parse_int64) {
    const char *source = s_test_json;
    
    // Create ref to "30" (age field, offset ~20)
    // Find actual offset by searching
    const char *age_pos = strstr(source, "30");
    DAP_ASSERT_NOT_NULL(age_pos);
    
    ptrdiff_t offset = age_pos - source;
    dap_json_value_ref_t ref = dap_json_ref_create(TYPE_INT, offset, 2, 0);
    
    // Parse integer
    int64_t value = 0;
    int result = dap_json_ref_parse_int64(&ref, source, &value);
    DAP_ASSERT_EQUAL(result, 0);
    DAP_ASSERT_EQUAL(value, 30);
    
    return 0;
}

/**
 * @brief Test: Parse double from ref
 */
DAP_TEST(test_ref_parse_double) {
    const char *source = s_test_json;
    
    // Create ref to "123.45" (balance field)
    const char *balance_pos = strstr(source, "123.45");
    DAP_ASSERT_NOT_NULL(balance_pos);
    
    ptrdiff_t offset = balance_pos - source;
    dap_json_value_ref_t ref = dap_json_ref_create(TYPE_DOUBLE, offset, 6, 0);
    
    // Parse double
    double value = 0.0;
    int result = dap_json_ref_parse_double(&ref, source, &value);
    DAP_ASSERT_EQUAL(result, 0);
    DAP_ASSERT_TRUE(fabs(value - 123.45) < 0.0001);
    
    return 0;
}

/**
 * @brief Test: Parse boolean from ref
 */
DAP_TEST(test_ref_parse_boolean) {
    const char *source = s_test_json;
    
    // Create ref to "true" (active field)
    const char *true_pos = strstr(source, "true");
    DAP_ASSERT_NOT_NULL(true_pos);
    
    ptrdiff_t offset = true_pos - source;
    dap_json_value_ref_t ref = dap_json_ref_create(TYPE_BOOLEAN, offset, 4, 0);
    
    // Parse boolean
    bool value = false;
    int result = dap_json_ref_parse_boolean(&ref, source, &value);
    DAP_ASSERT_EQUAL(result, 0);
    DAP_ASSERT_TRUE(value);
    
    return 0;
}

/**
 * @brief Test: Ref equality
 */
DAP_TEST(test_ref_equal) {
    dap_json_value_ref_t ref1 = dap_json_ref_create(TYPE_STRING, 100, 50, 0);
    dap_json_value_ref_t ref2 = dap_json_ref_create(TYPE_STRING, 100, 50, 0);
    dap_json_value_ref_t ref3 = dap_json_ref_create(TYPE_STRING, 100, 51, 0);
    
    DAP_ASSERT_TRUE(dap_json_ref_equal(&ref1, &ref2));
    DAP_ASSERT_FALSE(dap_json_ref_equal(&ref1, &ref3));
    
    return 0;
}

/**
 * @brief Test: Ref array operations
 */
DAP_TEST(test_ref_array) {
    struct dap_arena *arena = dap_arena_create(4096);
    DAP_ASSERT_NOT_NULL(arena);
    
    // Initialize array
    dap_json_ref_array_t array;
    int result = dap_json_ref_array_init(&array, 4, arena);
    DAP_ASSERT_EQUAL(result, 0);
    DAP_ASSERT_EQUAL(array.count, 0);
    DAP_ASSERT_EQUAL(array.capacity, 4);
    
    // Add refs
    for (int i = 0; i < 6; i++) {
        dap_json_value_ref_t ref = dap_json_ref_create(TYPE_INT, i * 10, 2, 0);
        result = dap_json_ref_array_add(&array, &ref, arena);
        DAP_ASSERT_EQUAL(result, 0);
    }
    
    DAP_ASSERT_EQUAL(array.count, 6);
    DAP_ASSERT_TRUE(array.capacity >= 6); // Should have grown
    
    // Get ref
    const dap_json_value_ref_t *ref = dap_json_ref_array_get(&array, 3);
    DAP_ASSERT_NOT_NULL(ref);
    DAP_ASSERT_EQUAL(ref->offset, 30);
    
    // Out of bounds
    ref = dap_json_ref_array_get(&array, 100);
    DAP_ASSERT_NULL(ref);
    
    dap_arena_delete(arena);
    return 0;
}

/**
 * @brief Test: Ref object operations
 */
DAP_TEST(test_ref_object) {
    struct dap_arena *arena = dap_arena_create(4096);
    DAP_ASSERT_NOT_NULL(arena);
    
    const char *source = "{\"name\":\"Alice\",\"age\":30}";
    
    // Initialize object
    dap_json_ref_object_t object;
    int result = dap_json_ref_object_init(&object, 4, arena);
    DAP_ASSERT_EQUAL(result, 0);
    
    // Add key-value pairs
    dap_json_value_ref_t key1 = dap_json_ref_create(TYPE_STRING, 2, 4, 0); // "name"
    dap_json_value_ref_t val1 = dap_json_ref_create(TYPE_STRING, 9, 5, 0); // "Alice"
    result = dap_json_ref_object_add(&object, &key1, &val1, arena);
    DAP_ASSERT_EQUAL(result, 0);
    
    dap_json_value_ref_t key2 = dap_json_ref_create(TYPE_STRING, 17, 3, 0); // "age"
    dap_json_value_ref_t val2 = dap_json_ref_create(TYPE_INT, 23, 2, 0);    // 30
    result = dap_json_ref_object_add(&object, &key2, &val2, arena);
    DAP_ASSERT_EQUAL(result, 0);
    
    DAP_ASSERT_EQUAL(object.count, 2);
    
    // Lookup by key
    const dap_json_value_ref_t *found = dap_json_ref_object_get(&object, "name", 4, source);
    DAP_ASSERT_NOT_NULL(found);
    DAP_ASSERT_EQUAL(found->offset, 9); // "Alice"
    
    // Not found
    found = dap_json_ref_object_get(&object, "unknown", 7, source);
    DAP_ASSERT_NULL(found);
    
    dap_arena_delete(arena);
    return 0;
}

/**
 * @brief Test: Ref validation
 */
DAP_TEST(test_ref_validate) {
    const char *source = "Hello, World!";
    size_t source_len = strlen(source);
    
    // Valid ref
    dap_json_value_ref_t valid = dap_json_ref_create(TYPE_STRING, 0, 5, 0);
    DAP_ASSERT_TRUE(dap_json_ref_validate(&valid, source, source_len));
    
    // Invalid offset
    dap_json_value_ref_t invalid_offset = dap_json_ref_create(TYPE_STRING, 100, 5, 0);
    DAP_ASSERT_FALSE(dap_json_ref_validate(&invalid_offset, source, source_len));
    
    // Invalid length (overflow)
    dap_json_value_ref_t invalid_length = dap_json_ref_create(TYPE_STRING, 0, 100, 0);
    DAP_ASSERT_FALSE(dap_json_ref_validate(&invalid_length, source, source_len));
    
    return 0;
}

/**
 * @brief Test: Flags operations
 */
DAP_TEST(test_ref_flags) {
    dap_json_value_ref_t ref = dap_json_ref_create(TYPE_STRING, 0, 10, 0);
    
    // Initially no flags
    DAP_ASSERT_FALSE(dap_json_ref_has_flag(&ref, DAP_JSON_REF_FLAG_ESCAPED));
    DAP_ASSERT_FALSE(dap_json_ref_has_flag(&ref, DAP_JSON_REF_FLAG_CACHED));
    
    // Set flag
    dap_json_ref_set_flag(&ref, DAP_JSON_REF_FLAG_ESCAPED);
    DAP_ASSERT_TRUE(dap_json_ref_has_flag(&ref, DAP_JSON_REF_FLAG_ESCAPED));
    
    // Set another flag
    dap_json_ref_set_flag(&ref, DAP_JSON_REF_FLAG_CACHED);
    DAP_ASSERT_TRUE(dap_json_ref_has_flag(&ref, DAP_JSON_REF_FLAG_ESCAPED));
    DAP_ASSERT_TRUE(dap_json_ref_has_flag(&ref, DAP_JSON_REF_FLAG_CACHED));
    
    // Clear flag
    dap_json_ref_clear_flag(&ref, DAP_JSON_REF_FLAG_ESCAPED);
    DAP_ASSERT_FALSE(dap_json_ref_has_flag(&ref, DAP_JSON_REF_FLAG_ESCAPED));
    DAP_ASSERT_TRUE(dap_json_ref_has_flag(&ref, DAP_JSON_REF_FLAG_CACHED));
    
    return 0;
}

/**
 * @brief Test: Memory savings calculation
 */
DAP_TEST(test_ref_memory_savings) {
    // Test memory calculations
    size_t array_mem = dap_json_ref_array_memory(1000);
    DAP_ASSERT_EQUAL(array_mem, 8000); // 1000 * 8 bytes
    
    size_t object_mem = dap_json_ref_object_memory(100);
    DAP_ASSERT_EQUAL(object_mem, 1600); // 100 * 16 bytes (2 refs per pair)
    
    // Calculate savings
    size_t savings = dap_json_ref_memory_savings(1000);
    DAP_ASSERT_EQUAL(savings, 48000); // 1000 * (56 - 8) = 48,000 bytes
    
    return 0;
}

/**
 * @brief Main test suite
 */
int main(int argc, char *argv[]) {
    DAP_TEST_RUN(test_ref_create_simple);
    DAP_TEST_RUN(test_ref_create_extended);
    DAP_TEST_RUN(test_ref_get_ptr);
    DAP_TEST_RUN(test_ref_materialize_string);
    DAP_TEST_RUN(test_ref_parse_int64);
    DAP_TEST_RUN(test_ref_parse_double);
    DAP_TEST_RUN(test_ref_parse_boolean);
    DAP_TEST_RUN(test_ref_equal);
    DAP_TEST_RUN(test_ref_array);
    DAP_TEST_RUN(test_ref_object);
    DAP_TEST_RUN(test_ref_validate);
    DAP_TEST_RUN(test_ref_flags);
    DAP_TEST_RUN(test_ref_memory_savings);
    
    DAP_TEST_SUMMARY();
    return 0;
}
