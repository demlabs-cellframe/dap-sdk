/*
 * Authors:
 * DAP JSON Native Implementation Team
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
 * @file test_stage2_ref.c
 * @brief Unit tests for Stage 2: DOM Building Reference Implementation
 * 
 * Тестирует:
 * - Value creation (null, bool, number, string, array, object)
 * - Value parsing (strings, numbers, literals)
 * - DOM traversal (objects, arrays)
 * - Integration со Stage 1
 * - Error handling
 * 
 * @author DAP JSON Native Implementation Team
 * @date 2025-01-07
 */

#include "dap_common.h"
#include "dap_test.h"
#include "internal/dap_json_stage1.h"
#include "internal/dap_json_stage2.h"

#include <string.h>
#include <math.h>

#define LOG_TAG "test_stage2_ref"

/* ========================================================================== */
/*                         VALUE CREATION TESTS                               */
/* ========================================================================== */

static bool s_test_value_creation_null(void)
{
    dap_json_value_t *l_value = dap_json_value_v2_create_null();
    dap_assert(l_value != NULL, "Value creation failed");
    dap_assert(l_value->type == DAP_JSON_TYPE_NULL, "Value type mismatch");
    
    dap_json_value_v2_free(l_value);
    return true;
}

static bool s_test_value_creation_bool(void)
{
    dap_json_value_t *l_true = dap_json_value_v2_create_bool(true);
    dap_assert(l_true != NULL, "Bool(true) creation failed");
    dap_assert(l_true->type == DAP_JSON_TYPE_BOOLEAN, "Bool type mismatch");
    dap_assert(l_true->boolean == true, "Bool value mismatch (true)");
    
    dap_json_value_t *l_false = dap_json_value_v2_create_bool(false);
    dap_assert(l_false != NULL, "Bool(false) creation failed");
    dap_assert(l_false->boolean == false, "Bool value mismatch (false)");
    
    dap_json_value_v2_free(l_true);
    dap_json_value_v2_free(l_false);
    return true;
}

static bool s_test_value_creation_int(void)
{
    dap_json_value_t *l_zero = dap_json_value_v2_create_int(0);
    dap_assert(l_zero != NULL, "Int(0) creation failed");
    dap_assert(l_zero->type == DAP_JSON_TYPE_INT, "Int type mismatch");
    dap_assert(l_zero->number.is_double == false, "Int should not be double");
    dap_assert(l_zero->number.i == 0, "Int value mismatch (0)");
    
    dap_json_value_t *l_positive = dap_json_value_v2_create_int(123456);
    dap_assert(l_positive != NULL, "Int(positive) creation failed");
    dap_assert(l_positive->number.i == 123456, "Int value mismatch (positive)");
    
    dap_json_value_t *l_negative = dap_json_value_v2_create_int(-999);
    dap_assert(l_negative != NULL, "Int(negative) creation failed");
    dap_assert(l_negative->number.i == -999, "Int value mismatch (negative)");
    
    dap_json_value_v2_free(l_zero);
    dap_json_value_v2_free(l_positive);
    dap_json_value_v2_free(l_negative);
    return true;
}

static bool s_test_value_creation_double(void)
{
    dap_json_value_t *l_zero = dap_json_value_v2_create_double(0.0);
    dap_assert(l_zero != NULL, "Double(0.0) creation failed");
    dap_assert(l_zero->type == DAP_JSON_TYPE_DOUBLE, "Double type mismatch");
    dap_assert(l_zero->number.is_double == true, "Double should be double");
    dap_assert(l_zero->number.d == 0.0, "Double value mismatch (0.0)");
    
    dap_json_value_t *l_pi = dap_json_value_v2_create_double(3.14159);
    dap_assert(l_pi != NULL, "Double(pi) creation failed");
    dap_assert(fabs(l_pi->number.d - 3.14159) < 0.00001, "Double value mismatch (pi)");
    
    dap_json_value_t *l_negative = dap_json_value_v2_create_double(-2.71828);
    dap_assert(l_negative != NULL, "Double(negative) creation failed");
    dap_assert(fabs(l_negative->number.d + 2.71828) < 0.00001, "Double value mismatch (negative)");
    
    dap_json_value_v2_free(l_zero);
    dap_json_value_v2_free(l_pi);
    dap_json_value_v2_free(l_negative);
    return true;
}

static bool s_test_value_creation_string(void)
{
    dap_json_value_t *l_empty = dap_json_value_v2_create_string("", 0);
    dap_assert(l_empty != NULL, "String(empty) creation failed");
    dap_assert(l_empty->type == DAP_JSON_TYPE_STRING, "String type mismatch");
    dap_assert(l_empty->string.length == 0, "String length mismatch (empty)");
    dap_assert(l_empty->string.data[0] == '\0', "String not null-terminated");
    
    const char *l_hello = "Hello, World!";
    dap_json_value_t *l_str = dap_json_value_v2_create_string(l_hello, strlen(l_hello));
    dap_assert(l_str != NULL, "String creation failed");
    dap_assert(l_str->string.length == strlen(l_hello), "String length mismatch");
    dap_assert(strcmp(l_str->string.data, l_hello) == 0, "String content mismatch");
    
    dap_json_value_v2_free(l_empty);
    dap_json_value_v2_free(l_str);
    return true;
}

static bool s_test_value_creation_array(void)
{
    dap_json_value_t *l_array = dap_json_value_v2_create_array();
    dap_assert(l_array != NULL, "Array creation failed");
    dap_assert(l_array->type == DAP_JSON_TYPE_ARRAY, "Array type mismatch");
    dap_assert(l_array->array.count == 0, "Array should be empty");
    
    dap_json_value_v2_free(l_array);
    return true;
}

static bool s_test_value_creation_object(void)
{
    dap_json_value_t *l_object = dap_json_value_v2_create_object();
    dap_assert(l_object != NULL, "Object creation failed");
    dap_assert(l_object->type == DAP_JSON_TYPE_OBJECT, "Object type mismatch");
    dap_assert(l_object->object.count == 0, "Object should be empty");
    
    dap_json_value_v2_free(l_object);
    return true;
}

/* ========================================================================== */
/*                         ARRAY/OBJECT OPERATIONS                            */
/* ========================================================================== */

static bool s_test_array_operations(void)
{
    dap_json_value_t *l_array = dap_json_value_v2_create_array();
    dap_assert(l_array != NULL, "Array creation failed");
    
    // Add elements
    dap_json_value_t *l_elem1 = dap_json_value_v2_create_int(10);
    dap_json_value_t *l_elem2 = dap_json_value_v2_create_int(20);
    dap_json_value_t *l_elem3 = dap_json_value_v2_create_int(30);
    
    dap_assert(dap_json_array_v2_add(l_array, l_elem1) == true, "Array add failed (1)");
    dap_assert(dap_json_array_v2_add(l_array, l_elem2) == true, "Array add failed (2)");
    dap_assert(dap_json_array_v2_add(l_array, l_elem3) == true, "Array add failed (3)");
    
    dap_assert(l_array->array.count == 3, "Array count mismatch");
    
    // Get elements
    dap_json_value_t *l_get1 = dap_json_array_v2_get(l_array, 0);
    dap_assert(l_get1 != NULL, "Array get failed (0)");
    dap_assert(l_get1->number.i == 10, "Array element mismatch (0)");
    
    dap_json_value_t *l_get2 = dap_json_array_v2_get(l_array, 1);
    dap_assert(l_get2 != NULL, "Array get failed (1)");
    dap_assert(l_get2->number.i == 20, "Array element mismatch (1)");
    
    dap_json_value_t *l_get3 = dap_json_array_v2_get(l_array, 2);
    dap_assert(l_get3 != NULL, "Array get failed (2)");
    dap_assert(l_get3->number.i == 30, "Array element mismatch (2)");
    
    // Out of bounds
    dap_json_value_t *l_get_invalid = dap_json_array_v2_get(l_array, 999);
    dap_assert(l_get_invalid == NULL, "Array should return NULL for out-of-bounds");
    
    dap_json_value_v2_free(l_array);
    return true;
}

static bool s_test_object_operations(void)
{
    dap_json_value_t *l_object = dap_json_value_v2_create_object();
    dap_assert(l_object != NULL, "Object creation failed");
    
    // Add key-value pairs
    dap_json_value_t *l_val1 = dap_json_value_v2_create_int(100);
    dap_json_value_t *l_val2 = dap_json_value_v2_create_string("test", 4);
    dap_json_value_t *l_val3 = dap_json_value_v2_create_bool(true);
    
    dap_assert(dap_json_object_v2_add(l_object, "number", l_val1) == true, "Object add failed (1)");
    dap_assert(dap_json_object_v2_add(l_object, "string", l_val2) == true, "Object add failed (2)");
    dap_assert(dap_json_object_v2_add(l_object, "boolean", l_val3) == true, "Object add failed (3)");
    
    dap_assert(l_object->object.count == 3, "Object count mismatch");
    
    // Get values
    dap_json_value_t *l_get1 = dap_json_object_v2_get(l_object, "number");
    dap_assert(l_get1 != NULL, "Object get failed (number)");
    dap_assert(l_get1->number.i == 100, "Object value mismatch (number)");
    
    dap_json_value_t *l_get2 = dap_json_object_v2_get(l_object, "string");
    dap_assert(l_get2 != NULL, "Object get failed (string)");
    dap_assert(strcmp(l_get2->string.data, "test") == 0, "Object value mismatch (string)");
    
    dap_json_value_t *l_get3 = dap_json_object_v2_get(l_object, "boolean");
    dap_assert(l_get3 != NULL, "Object get failed (boolean)");
    dap_assert(l_get3->boolean == true, "Object value mismatch (boolean)");
    
    // Non-existent key
    dap_json_value_t *l_get_invalid = dap_json_object_v2_get(l_object, "nonexistent");
    dap_assert(l_get_invalid == NULL, "Object should return NULL for nonexistent key");
    
    // Duplicate key
    dap_json_value_t *l_duplicate = dap_json_value_v2_create_int(999);
    dap_assert(dap_json_object_v2_add(l_object, "number", l_duplicate) == false, 
               "Object should reject duplicate key");
    dap_json_value_v2_free(l_duplicate);
    
    dap_json_value_v2_free(l_object);
    return true;
}

/* ========================================================================== */
/*                         END-TO-END PARSING TESTS                           */
/* ========================================================================== */

static bool s_test_parse_simple_values(void)
{
    // Test null
    {
        const char *l_json = "null";
        dap_json_stage1_t *l_s1 = dap_json_stage1_init((const uint8_t*)l_json, strlen(l_json));
        dap_assert(l_s1 != NULL, "Stage 1 init failed (null)");
        dap_assert(dap_json_stage1_run(l_s1) == STAGE1_SUCCESS, "Stage 1 run failed (null)");
        
        dap_json_stage2_t *l_s2 = dap_json_stage2_init(l_s1);
        dap_assert(l_s2 != NULL, "Stage 2 init failed (null)");
        dap_assert(dap_json_stage2_run(l_s2) == STAGE2_SUCCESS, "Stage 2 run failed (null)");
        
        dap_json_value_t *l_root = dap_json_stage2_get_root(l_s2);
        dap_assert(l_root != NULL, "Root value is NULL");
        dap_assert(l_root->type == DAP_JSON_TYPE_NULL, "Expected null type");
        
        // NOTE: l_root is Arena-based - don't call dap_json_value_v2_free()
        // Arena will free it when dap_json_stage2_free() is called
        dap_json_stage2_free(l_s2);
        dap_json_stage1_free(l_s1);
    }
    
    // Test true
    {
        const char *l_json = "true";
        dap_json_stage1_t *l_s1 = dap_json_stage1_init((const uint8_t*)l_json, strlen(l_json));
        dap_assert(dap_json_stage1_run(l_s1) == STAGE1_SUCCESS, "Stage 1 run failed (true)");
        
        dap_json_stage2_t *l_s2 = dap_json_stage2_init(l_s1);
        dap_assert(dap_json_stage2_run(l_s2) == STAGE2_SUCCESS, "Stage 2 run failed (true)");
        
        dap_json_value_t *l_root = dap_json_stage2_get_root(l_s2);
        dap_assert(l_root->type == DAP_JSON_TYPE_BOOLEAN, "Expected bool type");
        dap_assert(l_root->boolean == true, "Expected true value");
        
        // NOTE: l_root is Arena-based - don't call dap_json_value_v2_free()
        dap_json_stage2_free(l_s2);
        dap_json_stage1_free(l_s1);
    }
    
    // Test integer
    {
        const char *l_json = "42";
        dap_json_stage1_t *l_s1 = dap_json_stage1_init((const uint8_t*)l_json, strlen(l_json));
        dap_assert(dap_json_stage1_run(l_s1) == STAGE1_SUCCESS, "Stage 1 run failed (int)");
        
        dap_json_stage2_t *l_s2 = dap_json_stage2_init(l_s1);
        dap_assert(dap_json_stage2_run(l_s2) == STAGE2_SUCCESS, "Stage 2 run failed (int)");
        
        dap_json_value_t *l_root = dap_json_stage2_get_root(l_s2);
        dap_assert(l_root->type == DAP_JSON_TYPE_INT, "Expected number type");
        dap_assert(l_root->number.is_double == false, "Expected integer");
        dap_assert(l_root->number.i == 42, "Expected 42");
        
        // NOTE: l_root is Arena-based - don't call dap_json_value_v2_free()
        // dap_json_value_v2_free(l_root);
        dap_json_stage2_free(l_s2);
        dap_json_stage1_free(l_s1);
    }
    
    // Test string
    {
        const char *l_json = "\"Hello\"";
        dap_json_stage1_t *l_s1 = dap_json_stage1_init((const uint8_t*)l_json, strlen(l_json));
        dap_assert(dap_json_stage1_run(l_s1) == STAGE1_SUCCESS, "Stage 1 run failed (string)");
        
        dap_json_stage2_t *l_s2 = dap_json_stage2_init(l_s1);
        dap_assert(dap_json_stage2_run(l_s2) == STAGE2_SUCCESS, "Stage 2 run failed (string)");
        
        dap_json_value_t *l_root = dap_json_stage2_get_root(l_s2);
        dap_assert(l_root->type == DAP_JSON_TYPE_STRING, "Expected string type");
        dap_assert(strcmp(l_root->string.data, "Hello") == 0, "Expected 'Hello'");
        
        // NOTE: l_root is Arena-based - don't call dap_json_value_v2_free()
        // dap_json_value_v2_free(l_root);
        dap_json_stage2_free(l_s2);
        dap_json_stage1_free(l_s1);
    }
    
    return true;
}

static bool s_test_parse_array(void)
{
    const char *l_json = "[1, 2, 3]";
    
    dap_json_stage1_t *l_s1 = dap_json_stage1_init((const uint8_t*)l_json, strlen(l_json));
    dap_assert(l_s1 != NULL, "Stage 1 init failed");
    dap_assert(dap_json_stage1_run(l_s1) == STAGE1_SUCCESS, "Stage 1 run failed");
    
    dap_json_stage2_t *l_s2 = dap_json_stage2_init(l_s1);
    dap_assert(l_s2 != NULL, "Stage 2 init failed");
    dap_assert(dap_json_stage2_run(l_s2) == STAGE2_SUCCESS, "Stage 2 run failed");
    
    dap_json_value_t *l_root = dap_json_stage2_get_root(l_s2);
    dap_assert(l_root != NULL, "Root value is NULL");
    dap_assert(l_root->type == DAP_JSON_TYPE_ARRAY, "Expected array type");
    dap_assert(l_root->array.count == 3, "Expected 3 elements");
    
    dap_json_value_t *l_elem0 = dap_json_array_v2_get(l_root, 0);
    dap_assert(l_elem0->number.i == 1, "Expected 1");
    
    dap_json_value_t *l_elem1 = dap_json_array_v2_get(l_root, 1);
    dap_assert(l_elem1->number.i == 2, "Expected 2");
    
    dap_json_value_t *l_elem2 = dap_json_array_v2_get(l_root, 2);
    dap_assert(l_elem2->number.i == 3, "Expected 3");
    
    // NOTE: l_root is Arena-based - don't call dap_json_value_v2_free()
    // dap_json_value_v2_free(l_root);
    dap_json_stage2_free(l_s2);
    dap_json_stage1_free(l_s1);
    return true;
}

static bool s_test_parse_object(void)
{
    const char *l_json = "{\"name\": \"Alice\", \"age\": 30}";
    
    dap_json_stage1_t *l_s1 = dap_json_stage1_init((const uint8_t*)l_json, strlen(l_json));
    dap_assert(l_s1 != NULL, "Stage 1 init failed");
    dap_assert(dap_json_stage1_run(l_s1) == STAGE1_SUCCESS, "Stage 1 run failed");
    
    dap_json_stage2_t *l_s2 = dap_json_stage2_init(l_s1);
    dap_assert(l_s2 != NULL, "Stage 2 init failed");
    dap_assert(dap_json_stage2_run(l_s2) == STAGE2_SUCCESS, "Stage 2 run failed");
    
    dap_json_value_t *l_root = dap_json_stage2_get_root(l_s2);
    dap_assert(l_root != NULL, "Root value is NULL");
    dap_assert(l_root->type == DAP_JSON_TYPE_OBJECT, "Expected object type");
    dap_assert(l_root->object.count == 2, "Expected 2 pairs");
    
    dap_json_value_t *l_name = dap_json_object_v2_get(l_root, "name");
    dap_assert(l_name != NULL, "Name not found");
    dap_assert(strcmp(l_name->string.data, "Alice") == 0, "Expected 'Alice'");
    
    dap_json_value_t *l_age = dap_json_object_v2_get(l_root, "age");
    dap_assert(l_age != NULL, "Age not found");
    dap_assert(l_age->number.i == 30, "Expected 30");
    
    // NOTE: l_root is Arena-based - don't call dap_json_value_v2_free()
    // dap_json_value_v2_free(l_root);
    dap_json_stage2_free(l_s2);
    dap_json_stage1_free(l_s1);
    return true;
}

static bool s_test_parse_nested(void)
{
    const char *l_json = "{\"users\": [{\"name\": \"Bob\", \"active\": true}, {\"name\": \"Carol\", \"active\": false}]}";
    
    dap_json_stage1_t *l_s1 = dap_json_stage1_init((const uint8_t*)l_json, strlen(l_json));
    dap_assert(dap_json_stage1_run(l_s1) == STAGE1_SUCCESS, "Stage 1 run failed");
    
    dap_json_stage2_t *l_s2 = dap_json_stage2_init(l_s1);
    dap_assert(dap_json_stage2_run(l_s2) == STAGE2_SUCCESS, "Stage 2 run failed");
    
    dap_json_value_t *l_root = dap_json_stage2_get_root(l_s2);
    dap_assert(l_root->type == DAP_JSON_TYPE_OBJECT, "Expected object");
    
    dap_json_value_t *l_users = dap_json_object_v2_get(l_root, "users");
    dap_assert(l_users != NULL, "Users not found");
    dap_assert(l_users->type == DAP_JSON_TYPE_ARRAY, "Expected array");
    dap_assert(l_users->array.count == 2, "Expected 2 users");
    
    dap_json_value_t *l_user0 = dap_json_array_v2_get(l_users, 0);
    dap_assert(l_user0->type == DAP_JSON_TYPE_OBJECT, "Expected object (user0)");
    
    dap_json_value_t *l_name0 = dap_json_object_v2_get(l_user0, "name");
    dap_assert(strcmp(l_name0->string.data, "Bob") == 0, "Expected 'Bob'");
    
    dap_json_value_t *l_active0 = dap_json_object_v2_get(l_user0, "active");
    dap_assert(l_active0->boolean == true, "Expected true");
    
    // NOTE: l_root is Arena-based - don't call dap_json_value_v2_free()
    // dap_json_value_v2_free(l_root);
    dap_json_stage2_free(l_s2);
    dap_json_stage1_free(l_s1);
    return true;
}

/* ========================================================================== */
/*                         TEST SUITE                                         */
/* ========================================================================== */

int main(void)
{
    dap_print_module_name("Stage 2 Reference Implementation Tests");
    
    // Value creation tests
    log_it(L_INFO, "=== Value Creation Tests ===");
    dap_assert(s_test_value_creation_null(), "Value creation: null");
    dap_assert(s_test_value_creation_bool(), "Value creation: bool");
    dap_assert(s_test_value_creation_int(), "Value creation: int");
    dap_assert(s_test_value_creation_double(), "Value creation: double");
    dap_assert(s_test_value_creation_string(), "Value creation: string");
    dap_assert(s_test_value_creation_array(), "Value creation: array");
    dap_assert(s_test_value_creation_object(), "Value creation: object");
    
    // Array/object operations
    log_it(L_INFO, "=== Array/Object Operations ===");
    dap_assert(s_test_array_operations(), "Array operations");
    dap_assert(s_test_object_operations(), "Object operations");
    
    // End-to-end parsing
    log_it(L_INFO, "=== End-to-End Parsing Tests ===");
    dap_assert(s_test_parse_simple_values(), "Parse simple values");
    dap_assert(s_test_parse_array(), "Parse array");
    dap_assert(s_test_parse_object(), "Parse object");
    dap_assert(s_test_parse_nested(), "Parse nested structures");
    
    log_it(L_INFO, "=== All Stage 2 Tests Passed ===");
    return 0;
}

