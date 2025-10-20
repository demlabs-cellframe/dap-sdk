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

#include "dap_json.h"
#include "dap_strfuncs.h"
#include "json.h"
#include <string.h>

#define LOG_TAG "dap_json"

/**
 * @brief Internal DAP JSON structure - opaque to users
 * Can represent both JSON objects and arrays internally
 */
struct dap_json {
    void *pvt;  // Internal json-c object pointer (struct json_object*)
};

// Helper functions for internal type conversion
static inline struct json_object* _dap_json_to_json_c(dap_json_t* a_dap_json) {
    return a_dap_json ? (struct json_object*)a_dap_json->pvt : NULL;
}

static inline dap_json_t* _json_c_to_dap_json(struct json_object* a_json_obj) {
    if (!a_json_obj) return NULL;
    
    dap_json_t* l_dap_json = DAP_NEW_Z(dap_json_t);
    if (!l_dap_json) {
        // OOM: Failed to allocate wrapper, must release JSON-C object
        log_it(L_CRITICAL, "Out of memory: failed to allocate dap_json_t wrapper");
        json_object_put(a_json_obj);  // Release JSON-C object to avoid leak
        return NULL;
    }
    
    l_dap_json->pvt = a_json_obj;
    return l_dap_json;
}

// Object creation and destruction
dap_json_t* dap_json_object_new(void)
{
    struct json_object* l_json_obj = json_object_new_object();
    if (!l_json_obj) {
        log_it(L_CRITICAL, "Out of memory: failed to create JSON-C object");
        return NULL;
    }
    return _json_c_to_dap_json(l_json_obj);
}

dap_json_t* dap_json_parse_string(const char* a_json_string)
{
    if (!a_json_string) {
        log_it(L_ERROR, "JSON string is NULL");
        return NULL;
    }
    
    enum json_tokener_error jerr;
    struct json_object *l_json = json_tokener_parse_verbose(a_json_string, &jerr);
    
    if (jerr != json_tokener_success) {
        log_it(L_ERROR, "Failed to parse JSON: %s", json_tokener_error_desc(jerr));
        return NULL;
    }
    
    return _json_c_to_dap_json(l_json);
}

void dap_json_object_free(dap_json_t* a_json)
{
    if (a_json) {
        // Check if wrapper is not invalidated (pvt != NULL)
        // After ownership transfer (add_object/add_array), pvt is set to NULL
        if (a_json->pvt != NULL) {
            json_object_put(a_json->pvt);
        }
        DAP_DELETE(a_json);
    }
}

// Array creation and manipulation
dap_json_t* dap_json_array_new(void)
{
    struct json_object* l_json_array = json_object_new_array();
    if (!l_json_array) {
        log_it(L_CRITICAL, "Out of memory: failed to create JSON-C array");
        return NULL;
    }
    return _json_c_to_dap_json(l_json_array);
}

void dap_json_array_free(dap_json_t* a_array)
{
    if (a_array) {
        // Check if wrapper is not invalidated (pvt != NULL)
        // After ownership transfer (add_object/add_array), pvt is set to NULL
        if (a_array->pvt != NULL) {
            json_object_put(a_array->pvt);
        }
        DAP_DELETE(a_array);
    }
}

int dap_json_array_add(dap_json_t* a_array, dap_json_t* a_item)
{
    if (!a_array || !a_item) {
        log_it(L_ERROR, "Array or item is NULL");
        return -1;
    }
    
    struct json_object* l_arr = _dap_json_to_json_c(a_array);
    struct json_object* l_item = _dap_json_to_json_c(a_item);
    int ret = json_object_array_add(l_arr, l_item);
    
    // Invalidate wrapper after ownership transfer to array
    if (ret == 0) {
        a_item->pvt = NULL;
    }
    
    return ret;
}

int dap_json_array_del_idx(dap_json_t* a_array, size_t a_idx, size_t a_count)
{
    if (!a_array) {
        log_it(L_ERROR, "Array is NULL");
        return -1;
    }
    
    json_object *l_arr = _dap_json_to_json_c(a_array);
    if (!l_arr) return -1;
    
    // Delete elements one by one
    for (size_t i = 0; i < a_count; i++) {
        json_object_array_del_idx(l_arr, a_idx, 1);
    }
    
    return 0;
}

size_t dap_json_array_length(dap_json_t* a_array)
{
    if (!a_array) {
        return 0;
    }
    
    return json_object_array_length(_dap_json_to_json_c(a_array));
}

dap_json_t* dap_json_array_get_idx(dap_json_t* a_array, size_t a_idx)
{
    if (!a_array) {
        return NULL;
    }
    
    struct json_object* l_item = json_object_array_get_idx(_dap_json_to_json_c(a_array), a_idx);
    if (l_item) {
        // Take ownership by incrementing refcount
        // User MUST call dap_json_object_free() on returned wrapper
        json_object_get(l_item);
        return _json_c_to_dap_json(l_item);
    }
    return NULL;
}

void dap_json_array_sort(dap_json_t* a_array, int (*a_sort_fn)(const void *, const void *))
{
    if (!a_array || !a_sort_fn) {
        return;
    }
    
    json_object_array_sort(_dap_json_to_json_c(a_array), a_sort_fn);
}

// Object field manipulation
int dap_json_object_add_string(dap_json_t* a_json, const char* a_key, const char* a_value)
{
    if (!a_json || !a_key || !a_value) {
        log_it(L_ERROR, "JSON object, key or value is NULL");
        return -1;
    }
    
    struct json_object *l_string = json_object_new_string(a_value);
    if (!l_string) {
        log_it(L_ERROR, "Failed to create JSON string object");
        return -1;
    }
    
    return json_object_object_add(_dap_json_to_json_c(a_json), a_key, l_string);
}

int dap_json_object_add_string_len(dap_json_t* a_json, const char* a_key, const char* a_value, const int a_len)
{
    if (!a_json || !a_key || !a_value) {
        log_it(L_ERROR, "JSON object, key or value is NULL");
        return -1;
    }
    
    struct json_object *l_string = json_object_new_string_len(a_value, a_len);
    if (!l_string) {
        log_it(L_ERROR, "Failed to create JSON string object");
        return -1;
    }
    
    return json_object_object_add(_dap_json_to_json_c(a_json), a_key, l_string);
}

int dap_json_object_add_int(dap_json_t* a_json, const char* a_key, int a_value)
{
    if (!a_json || !a_key) {
        log_it(L_ERROR, "JSON object or key is NULL");
        return -1;
    }
    
    struct json_object *l_int = json_object_new_int(a_value);
    if (!l_int) {
        log_it(L_ERROR, "Failed to create JSON int object");
        return -1;
    }
    
    return json_object_object_add(_dap_json_to_json_c(a_json), a_key, l_int);
}

int dap_json_object_add_int64(dap_json_t* a_json, const char* a_key, int64_t a_value)
{
    if (!a_json || !a_key) {
        log_it(L_ERROR, "JSON object or key is NULL");
        return -1;
    }
    
    struct json_object *l_int64 = json_object_new_int64(a_value);
    if (!l_int64) {
        log_it(L_ERROR, "Failed to create JSON int64 object");
        return -1;
    }
    
    return json_object_object_add(_dap_json_to_json_c(a_json), a_key, l_int64);
}

int dap_json_object_add_uint64(dap_json_t* a_json, const char* a_key, uint64_t a_value)
{
    if (!a_json || !a_key) {
        log_it(L_ERROR, "JSON object or key is NULL");
        return -1;
    }
    
    struct json_object *l_uint64 = json_object_new_uint64(a_value);
    if (!l_uint64) {
        log_it(L_ERROR, "Failed to create JSON uint64 object");
        return -1;
    }
    
    return json_object_object_add(_dap_json_to_json_c(a_json), a_key, l_uint64);
}

int dap_json_object_add_uint256(dap_json_t* a_json, const char* a_key, uint256_t a_value)
{
    if (!a_json || !a_key) {
        log_it(L_ERROR, "JSON object or key is NULL");
        return -1;
    }
    
    // Convert uint256 to string representation
    char *l_str = dap_uint256_uninteger_to_char(a_value);
    if (!l_str) {
        log_it(L_ERROR, "Failed to convert uint256 to string");
        return -1;
    }
    
    struct json_object *l_string = json_object_new_string(l_str);
    DAP_DELETE(l_str);
    
    if (!l_string) {
        log_it(L_ERROR, "Failed to create JSON string object for uint256");
        return -1;
    }
    
    return json_object_object_add(_dap_json_to_json_c(a_json), a_key, l_string);
}

int dap_json_object_add_double(dap_json_t* a_json, const char* a_key, double a_value)
{
    if (!a_json || !a_key) {
        log_it(L_ERROR, "JSON object or key is NULL");
        return -1;
    }
    
    struct json_object *l_double = json_object_new_double(a_value);
    if (!l_double) {
        log_it(L_ERROR, "Failed to create JSON double object");
        return -1;
    }
    
    return json_object_object_add(_dap_json_to_json_c(a_json), a_key, l_double);
}

int dap_json_object_add_bool(dap_json_t* a_json, const char* a_key, bool a_value)
{
    if (!a_json || !a_key) {
        log_it(L_ERROR, "JSON object or key is NULL");
        return -1;
    }
    
    struct json_object *l_bool = json_object_new_boolean(a_value);
    if (!l_bool) {
        log_it(L_ERROR, "Failed to create JSON boolean object");
        return -1;
    }
    
    return json_object_object_add(_dap_json_to_json_c(a_json), a_key, l_bool);
}

int dap_json_object_add_nanotime(dap_json_t* a_json, const char* a_key, dap_nanotime_t a_value)
{
    if (!a_json || !a_key) {
        log_it(L_ERROR, "JSON object or key is NULL");
        return -1;
    }
    
    // Store as int64 for compatibility
    return dap_json_object_add_int64(a_json, a_key, (int64_t)a_value);
}

int dap_json_object_add_time(dap_json_t* a_json, const char* a_key, dap_time_t a_value)
{
    if (!a_json || !a_key) {
        log_it(L_ERROR, "JSON object or key is NULL");
        return -1;
    }
    
    // Store as int64 for compatibility
    return dap_json_object_add_int64(a_json, a_key, (int64_t)a_value);
}

/**
 * @brief Add a nested JSON object to parent with key
 * @param a_json Parent JSON object
 * @param a_key Key name for the nested object
 * @param a_value Nested JSON object to add
 * @return 0 on success, -1 on error
 * 
 * IMPORTANT: Ownership transfer semantics
 * - After successful add, ownership of a_value transfers to parent
 * - The a_value wrapper is INVALIDATED (pvt set to NULL)
 * - Calling dap_json_object_free(a_value) after add is safe (frees only wrapper)
 * - Do NOT use a_value after calling this function (it's invalidated)
 * 
 * Example:
 *   dap_json_t *child = dap_json_object_new();
 *   dap_json_object_add_string(child, "name", "test");
 *   dap_json_object_add_object(parent, "child_key", child);
 *   dap_json_object_free(child);  // Safe: only frees wrapper (pvt is NULL)
 *   // Do NOT use child after this point!
 */
int dap_json_object_add_object(dap_json_t* a_json, const char* a_key, dap_json_t* a_value)
{
    if (!a_json || !a_key || !a_value) {
        log_it(L_ERROR, "JSON object, key or value is NULL");
        return -1;
    }
    
    // Ownership transfers to parent - json_object_object_add() takes ownership
    // NO refcount increment needed
    struct json_object* l_parent = _dap_json_to_json_c(a_json);
    struct json_object* l_value = _dap_json_to_json_c(a_value);
    int ret = json_object_object_add(l_parent, a_key, l_value);
    
    // Invalidate wrapper after ownership transfer
    if (ret == 0) {
        a_value->pvt = NULL;
    }
    
    return ret;
}

int dap_json_object_add_array(dap_json_t* a_json, const char* a_key, dap_json_t* a_array)
{
    if (!a_json || !a_key || !a_array) {
        log_it(L_ERROR, "JSON object, key or array is NULL");
        return -1;
    }
    
    // Ownership transfers to parent - json_object_object_add() takes ownership
    // NO refcount increment needed
    struct json_object* l_parent = _dap_json_to_json_c(a_json);
    struct json_object* l_array = _dap_json_to_json_c(a_array);
    int ret = json_object_object_add(l_parent, a_key, l_array);
    
    // Invalidate wrapper after ownership transfer
    if (ret == 0) {
        a_array->pvt = NULL;
    }
    
    return ret;
}

// Object field access
const char* dap_json_object_get_string(dap_json_t* a_json, const char* a_key)
{
    if (!a_json || !a_key) {
        return NULL;
    }
    
    struct json_object *l_obj = NULL;
    if (!json_object_object_get_ex(_dap_json_to_json_c(a_json), a_key, &l_obj)) {
        return NULL;
    }
    
    return json_object_get_string(l_obj);
}

int dap_json_object_get_int(dap_json_t* a_json, const char* a_key)
{
    if (!a_json || !a_key) {
        return 0;
    }
    
    struct json_object *l_obj = NULL;
    if (!json_object_object_get_ex(_dap_json_to_json_c(a_json), a_key, &l_obj)) {
        return 0;
    }
    
    return json_object_get_int(l_obj);
}

int64_t dap_json_object_get_int64(dap_json_t* a_json, const char* a_key)
{
    if (!a_json || !a_key) {
        return 0;
    }
    
    struct json_object *l_obj = NULL;
    if (!json_object_object_get_ex(_dap_json_to_json_c(a_json), a_key, &l_obj)) {
        return 0;
    }
    
    return json_object_get_int64(l_obj);
}

uint64_t dap_json_object_get_uint64(dap_json_t* a_json, const char* a_key)
{
    if (!a_json || !a_key) {
        return 0;
    }
    
    struct json_object *l_obj = NULL;
    if (!json_object_object_get_ex(_dap_json_to_json_c(a_json), a_key, &l_obj)) {
        return 0;
    }
    
    return json_object_get_uint64(l_obj);
}

uint256_t dap_json_object_get_uint256(dap_json_t* a_json, const char* a_key)
{
    if (!a_json || !a_key) {
        return uint256_0;  // Return zero value for uint256
    }
    
    struct json_object *l_obj = NULL;
    if (!json_object_object_get_ex(_dap_json_to_json_c(a_json), a_key, &l_obj)) {
        return uint256_0;
    }
    
    const char *l_str = json_object_get_string(l_obj);
    if (!l_str) {
        return uint256_0;
    }
    
    return dap_uint256_scan_uninteger(l_str);
}

double dap_json_object_get_double(dap_json_t* a_json, const char* a_key)
{
    if (!a_json || !a_key) {
        return 0.0;
    }
    
    struct json_object *l_obj = NULL;
    if (!json_object_object_get_ex(_dap_json_to_json_c(a_json), a_key, &l_obj)) {
        return 0.0;
    }
    
    return json_object_get_double(l_obj);
}

bool dap_json_object_get_bool(dap_json_t* a_json, const char* a_key)
{
    if (!a_json || !a_key) {
        return false;
    }
    
    struct json_object *l_obj = NULL;
    if (!json_object_object_get_ex(_dap_json_to_json_c(a_json), a_key, &l_obj)) {
        return false;
    }
    
    return json_object_get_boolean(l_obj);
}

/**
 * @brief Get a nested JSON object by key
 * @param a_json Parent JSON object
 * @param a_key Key name
 * @return New wrapper for the nested object, or NULL if not found
 * 
 * IMPORTANT: This function increments the refcount of the underlying JSON-C object.
 * The caller MUST call dap_json_object_free() on the returned wrapper to avoid memory leaks.
 * 
 * Example:
 *   dap_json_t *child = dap_json_object_get_object(parent, "child_key");
 *   if (child) {
 *       // Use child...
 *       dap_json_object_free(child);  // MUST free the wrapper
 *   }
 */
dap_json_t* dap_json_object_get_object(dap_json_t* a_json, const char* a_key)
{
    if (!a_json || !a_key) {
        return NULL;
    }
    
    struct json_object *l_obj = NULL;
    if (!json_object_object_get_ex(_dap_json_to_json_c(a_json), a_key, &l_obj)) {
        return NULL;
    }
    
    // Take ownership by incrementing refcount
    // User MUST call dap_json_object_free() on returned wrapper
    json_object_get(l_obj);
    return _json_c_to_dap_json(l_obj);
}

dap_json_t* dap_json_object_get_array(dap_json_t* a_json, const char* a_key)
{
    if (!a_json || !a_key) {
        return NULL;
    }
    
    struct json_object *l_obj = NULL;
    if (!json_object_object_get_ex(_dap_json_to_json_c(a_json), a_key, &l_obj)) {
        return NULL;
    }
    
    // Take ownership by incrementing refcount
    // User MUST call dap_json_object_free() on returned wrapper
    json_object_get(l_obj);
    return _json_c_to_dap_json(l_obj);
}

// String conversion
char* dap_json_to_string(dap_json_t* a_json)
{
    if (!a_json) {
        return NULL;
    }
    
    const char* l_json_str = json_object_to_json_string(_dap_json_to_json_c(a_json));
    return l_json_str ? dap_strdup(l_json_str) : NULL;
}

char* dap_json_to_string_pretty(dap_json_t* a_json)
{
    if (!a_json) {
        return NULL;
    }
    
    const char *l_json_str = json_object_to_json_string_ext(_dap_json_to_json_c(a_json), JSON_C_TO_STRING_PRETTY);
    return l_json_str ? dap_strdup(l_json_str) : NULL;
}

// Type checking
bool dap_json_is_null(dap_json_t* a_json)
{
    if (!a_json) {
        return true;
    }
    
    return json_object_is_type(_dap_json_to_json_c(a_json), json_type_null);
}

bool dap_json_is_string(dap_json_t* a_json)
{
    if (!a_json) {
        return false;
    }
    
    return json_object_is_type(_dap_json_to_json_c(a_json), json_type_string);
}

bool dap_json_is_int(dap_json_t* a_json)
{
    if (!a_json) {
        return false;
    }
    
    return json_object_is_type(_dap_json_to_json_c(a_json), json_type_int);
}

bool dap_json_is_double(dap_json_t* a_json)
{
    if (!a_json) {
        return false;
    }
    
    return json_object_is_type(_dap_json_to_json_c(a_json), json_type_double);
}

bool dap_json_is_bool(dap_json_t* a_json)
{
    if (!a_json) {
        return false;
    }
    
    return json_object_is_type(_dap_json_to_json_c(a_json), json_type_boolean);
}

bool dap_json_is_object(dap_json_t* a_json)
{
    if (!a_json) {
        return false;
    }
    
    return json_object_is_type(_dap_json_to_json_c(a_json), json_type_object);
}

bool dap_json_is_array(dap_json_t* a_json)
{
    if (!a_json) {
        return false;
    }
    
    return json_object_is_type(_dap_json_to_json_c(a_json), json_type_array);
}

// Advanced object manipulation
dap_json_t* dap_json_from_file(const char* a_file_path)
{
    if (!a_file_path) {
        log_it(L_ERROR, "File path is NULL");
        return NULL;
    }
    
    return _json_c_to_dap_json(json_object_from_file(a_file_path));
}

bool dap_json_object_get_ex(dap_json_t* a_json, const char* a_key, dap_json_t** a_value)
{
    if (!a_json || !a_key || !a_value) {
        return false;
    }
    
    struct json_object* l_temp_obj = NULL;
    bool l_result = json_object_object_get_ex(_dap_json_to_json_c(a_json), a_key, &l_temp_obj);
    
    if (l_result && l_temp_obj) {
        // Take ownership by incrementing refcount
        // json_object_object_get_ex returns borrowed reference, we need our own
        json_object_get(l_temp_obj);
        *a_value = _json_c_to_dap_json(l_temp_obj);
    } else {
        *a_value = NULL;
    }
    
    return l_result;
}

/**
 * @brief Convenience function to check if a key exists in JSON object
 * @param a_json JSON object to check
 * @param a_key Key name to check for
 * @return true if key exists, false otherwise
 * 
 * This is a lightweight alternative to dap_json_object_get_ex() when you only
 * need to check key existence without retrieving the value.
 */
bool dap_json_object_has_key(dap_json_t* a_json, const char* a_key)
{
    if (!a_json || !a_key) {
        return false;
    }
    
    struct json_object* l_temp_obj = NULL;
    return json_object_object_get_ex(_dap_json_to_json_c(a_json), a_key, &l_temp_obj);
}

int dap_json_object_del(dap_json_t* a_json, const char* a_key)
{
    if (!a_json || !a_key) {
        log_it(L_ERROR, "JSON object or key is NULL");
        return -1;
    }
    
    json_object_object_del(_dap_json_to_json_c(a_json), a_key);
    return 0;
}

// Extended value getters with default
const char* dap_json_object_get_string_default(dap_json_t* a_json, const char* a_key, const char* a_default)
{
    const char* l_value = dap_json_object_get_string(a_json, a_key);
    return l_value ? l_value : a_default;
}

int dap_json_object_get_int_default(dap_json_t* a_json, const char* a_key, int a_default)
{
    if (!a_json || !a_key) {
        return a_default;
    }
    
    struct json_object *l_obj = NULL;
    if (!json_object_object_get_ex(_dap_json_to_json_c(a_json), a_key, &l_obj)) {
        return a_default;
    }
    
    return json_object_get_int(l_obj);
}

int64_t dap_json_object_get_int64_default(dap_json_t* a_json, const char* a_key, int64_t a_default)
{
    if (!a_json || !a_key) {
        return a_default;
    }
    
    struct json_object *l_obj = NULL;
    if (!json_object_object_get_ex(_dap_json_to_json_c(a_json), a_key, &l_obj)) {
        return a_default;
    }
    
    return json_object_get_int64(l_obj);
}

dap_json_type_t dap_json_get_type(dap_json_t* a_json)
{
    if (!a_json) {
        return DAP_JSON_TYPE_NULL;
    }
    
    enum json_type l_type = json_object_get_type(_dap_json_to_json_c(a_json));
    
    switch (l_type) {
        case json_type_null:
            return DAP_JSON_TYPE_NULL;
        case json_type_boolean:
            return DAP_JSON_TYPE_BOOLEAN;
        case json_type_double:
            return DAP_JSON_TYPE_DOUBLE;
        case json_type_int:
            return DAP_JSON_TYPE_INT;
        case json_type_object:
            return DAP_JSON_TYPE_OBJECT;
        case json_type_array:
            return DAP_JSON_TYPE_ARRAY;
        case json_type_string:
            return DAP_JSON_TYPE_STRING;
        default:
            return DAP_JSON_TYPE_NULL;
    }
}

// Tokener functions for parsing with error handling
dap_json_t* dap_json_tokener_parse_verbose(const char* a_str, dap_json_tokener_error_t* a_error)
{
    if (!a_str) {
        if (a_error) {
            *a_error = DAP_JSON_TOKENER_ERROR_PARSE_NULL;
        }
        return NULL;
    }
    
    enum json_tokener_error l_jerr;
    struct json_object *l_json = json_tokener_parse_verbose(a_str, &l_jerr);
    
    if (a_error) {
        // Map json-c error codes to our enum
        switch (l_jerr) {
            case json_tokener_success:
                *a_error = DAP_JSON_TOKENER_SUCCESS;
                break;
            case json_tokener_error_depth:
                *a_error = DAP_JSON_TOKENER_ERROR_DEPTH;
                break;
            case json_tokener_error_parse_eof:
                *a_error = DAP_JSON_TOKENER_ERROR_PARSE_EOF;
                break;
            case json_tokener_error_parse_unexpected:
                *a_error = DAP_JSON_TOKENER_ERROR_PARSE_UNEXPECTED;
                break;
            case json_tokener_error_parse_null:
                *a_error = DAP_JSON_TOKENER_ERROR_PARSE_NULL;
                break;
            case json_tokener_error_parse_boolean:
                *a_error = DAP_JSON_TOKENER_ERROR_PARSE_BOOLEAN;
                break;
            case json_tokener_error_parse_number:
                *a_error = DAP_JSON_TOKENER_ERROR_PARSE_NUMBER;
                break;
            case json_tokener_error_parse_array:
                *a_error = DAP_JSON_TOKENER_ERROR_PARSE_ARRAY;
                break;
            case json_tokener_error_parse_object_key_name:
                *a_error = DAP_JSON_TOKENER_ERROR_PARSE_OBJECT_KEY_NAME;
                break;
            case json_tokener_error_parse_object_key_sep:
                *a_error = DAP_JSON_TOKENER_ERROR_PARSE_OBJECT_KEY_SEP;
                break;
            case json_tokener_error_parse_object_value_sep:
                *a_error = DAP_JSON_TOKENER_ERROR_PARSE_OBJECT_VALUE_SEP;
                break;
            case json_tokener_error_parse_string:
                *a_error = DAP_JSON_TOKENER_ERROR_PARSE_STRING;
                break;
            case json_tokener_error_parse_comment:
                *a_error = DAP_JSON_TOKENER_ERROR_PARSE_COMMENT;
                break;
            default:
                *a_error = DAP_JSON_TOKENER_ERROR_SIZE;
                break;
        }
    }
    
    return _json_c_to_dap_json(l_json);
}

const char* dap_json_tokener_error_desc(dap_json_tokener_error_t a_jerr)
{
    // Map our error codes back to json-c for description
    enum json_tokener_error l_jerr;
    
    switch (a_jerr) {
        case DAP_JSON_TOKENER_SUCCESS:
            l_jerr = json_tokener_success;
            break;
        case DAP_JSON_TOKENER_ERROR_DEPTH:
            l_jerr = json_tokener_error_depth;
            break;
        case DAP_JSON_TOKENER_ERROR_PARSE_EOF:
            l_jerr = json_tokener_error_parse_eof;
            break;
        case DAP_JSON_TOKENER_ERROR_PARSE_UNEXPECTED:
            l_jerr = json_tokener_error_parse_unexpected;
            break;
        case DAP_JSON_TOKENER_ERROR_PARSE_NULL:
            l_jerr = json_tokener_error_parse_null;
            break;
        case DAP_JSON_TOKENER_ERROR_PARSE_BOOLEAN:
            l_jerr = json_tokener_error_parse_boolean;
            break;
        case DAP_JSON_TOKENER_ERROR_PARSE_NUMBER:
            l_jerr = json_tokener_error_parse_number;
            break;
        case DAP_JSON_TOKENER_ERROR_PARSE_ARRAY:
            l_jerr = json_tokener_error_parse_array;
            break;
        case DAP_JSON_TOKENER_ERROR_PARSE_OBJECT_KEY_NAME:
            l_jerr = json_tokener_error_parse_object_key_name;
            break;
        case DAP_JSON_TOKENER_ERROR_PARSE_OBJECT_KEY_SEP:
            l_jerr = json_tokener_error_parse_object_key_sep;
            break;
        case DAP_JSON_TOKENER_ERROR_PARSE_OBJECT_VALUE_SEP:
            l_jerr = json_tokener_error_parse_object_value_sep;
            break;
        case DAP_JSON_TOKENER_ERROR_PARSE_STRING:
            l_jerr = json_tokener_error_parse_string;
            break;
        case DAP_JSON_TOKENER_ERROR_PARSE_COMMENT:
            l_jerr = json_tokener_error_parse_comment;
            break;
        default:
            l_jerr = json_tokener_error_size;
            break;
    }
    
    return json_tokener_error_desc(l_jerr);
}

// Reference counting (important for json-c compatibility)
dap_json_t* dap_json_object_get_ref(dap_json_t* a_json)
{
    if (!a_json) {
        return NULL;
    }
    
    return _json_c_to_dap_json(json_object_get(_dap_json_to_json_c(a_json)));
}

// Value object creation (for simple types)
dap_json_t* dap_json_object_new_int(int a_value)
{
    return _json_c_to_dap_json(json_object_new_int(a_value));
}

dap_json_t* dap_json_object_new_int64(int64_t a_value)
{
    return _json_c_to_dap_json(json_object_new_int64(a_value));
}

dap_json_t* dap_json_object_new_uint64(uint64_t a_value)
{
    return _json_c_to_dap_json(json_object_new_uint64(a_value));
}

dap_json_t* dap_json_object_new_uint256(uint256_t a_value)
{
    char *l_str = dap_uint256_uninteger_to_char(a_value);
    if (!l_str) {
        log_it(L_ERROR, "Failed to convert uint256 to string");
        return NULL;
    }
    
    struct json_object *l_string = json_object_new_string(l_str);
    DAP_DELETE(l_str);
    
    if (!l_string) {
        log_it(L_CRITICAL, "Out of memory: failed to create JSON-C string for uint256");
        return NULL;
    }
    
    return _json_c_to_dap_json(l_string);
}

dap_json_t* dap_json_object_new_string(const char* a_value)
{
    if (!a_value) {
        log_it(L_ERROR, "String value is NULL");
        return NULL;
    }
    
    return _json_c_to_dap_json(json_object_new_string(a_value));
}

dap_json_t* dap_json_object_new_string_len(const char* a_value, int a_len)
{
    if (!a_value) {
        log_it(L_ERROR, "String value is NULL");
        return NULL;
    }
    
    return _json_c_to_dap_json(json_object_new_string_len(a_value, a_len));
}

dap_json_t* dap_json_object_new_double(double a_value)
{
    return _json_c_to_dap_json(json_object_new_double(a_value));
}

dap_json_t* dap_json_object_new_bool(bool a_value)
{
    return _json_c_to_dap_json(json_object_new_boolean(a_value));
}

#define INDENTATION_LEVEL "    "

void dap_json_print_object(dap_json_t *a_json, FILE *a_stream, int a_indent_level) {
    if (!a_json) {
        fprintf(a_stream, "null");
        return;
    }

    if (dap_json_is_object(a_json)) {
        fprintf(a_stream, "{\n");
        // Use raw json-c object for iteration until dap_json gets object iteration API
        json_object *raw_obj = (json_object*)((struct dap_json*)a_json)->pvt;
        bool first = true;
        
        json_object_object_foreach(raw_obj, key, val) {
            if (!first) {
                fprintf(a_stream, ",\n");
            }
            first = false;
            
            // Print indentation
            for (int i = 0; i <= a_indent_level + 1; i++) {
                fprintf(a_stream, INDENTATION_LEVEL);
            }
            
            // Use stack wrapper to avoid memory leak
            dap_json_t wrapper = { .pvt = val };
            fprintf(a_stream, "\"%s\": ", key);
            dap_json_print_value(&wrapper, key, a_stream, a_indent_level + 1, false);
        }
        
        fprintf(a_stream, "\n");
        for (int i = 0; i <= a_indent_level; i++) {
            fprintf(a_stream, INDENTATION_LEVEL);
        }
        fprintf(a_stream, "}");
        
    } else if (dap_json_is_array(a_json)) {
        size_t length = dap_json_array_length(a_json);
        if (length == 0) {
            fprintf(a_stream, "[]");
            return;
        }
        
        fprintf(a_stream, "[\n");
        for (size_t i = 0; i < length; i++) {
            // Print indentation
            for (int j = 0; j <= a_indent_level + 1; j++) {
                fprintf(a_stream, INDENTATION_LEVEL);
            }
            
            dap_json_t *item = dap_json_array_get_idx(a_json, i);
            dap_json_print_value(item, NULL, a_stream, a_indent_level + 1, false);
            dap_json_object_free(item);  // MUST free wrapper to avoid memory leak
            
            if (i < length - 1) {
                fprintf(a_stream, ",");
            }
            fprintf(a_stream, "\n");
        }
        
        for (int i = 0; i <= a_indent_level; i++) {
            fprintf(a_stream, INDENTATION_LEVEL);
        }
        fprintf(a_stream, "]");
        
    } else {
        dap_json_print_value(a_json, NULL, a_stream, a_indent_level, false);
    }
}

void dap_json_print_value(dap_json_t *a_json, const char *a_key, FILE *a_stream, int a_indent_level, bool a_print_separator) {
    if (!a_json) {
        fprintf(a_stream, "null");
        return;
    }

    if (dap_json_is_string(a_json)) {
        const char *str_val = dap_json_object_get_string(a_json, NULL);
        fprintf(a_stream, a_print_separator ? "\"%s\", " : "\"%s\"", str_val ? str_val : "");
    } else if (dap_json_is_int(a_json)) {
        int64_t int_val = dap_json_object_get_int64(a_json, NULL);
        fprintf(a_stream, "%"DAP_INT64_FORMAT, int_val);
    } else if (dap_json_is_double(a_json)) {
        double double_val = dap_json_object_get_double(a_json, NULL);
        fprintf(a_stream, "%lf", double_val);
    } else if (dap_json_is_bool(a_json)) {
        bool bool_val = dap_json_object_get_bool(a_json, NULL);
        fprintf(a_stream, "%s", bool_val ? "true" : "false");
    } else if (dap_json_is_object(a_json) || dap_json_is_array(a_json)) {
        fprintf(a_stream, "\n");
        dap_json_print_object(a_json, a_stream, a_indent_level);
    } else {
        fprintf(a_stream, "null");
    }
}

int dap_json_object_add_null(dap_json_t* a_json, const char* a_key) {
    dap_return_val_if_fail(a_json && a_key, -1);
    
    json_object *l_obj = _dap_json_to_json_c(a_json);
    if (!l_obj) {
        log_it(L_ERROR, "Failed to convert dap_json to json_object");
        return -1;
    }
    
    // In this version of json-c, we add NULL directly which represents a null value
    log_it(L_DEBUG, "Adding null field '%s' to JSON object", a_key);
    int result = json_object_object_add(l_obj, a_key, NULL);
    log_it(L_DEBUG, "json_object_object_add with NULL result: %d", result);
    
    return result;
}

dap_json_t* dap_json_object_ref(dap_json_t* a_json) {
    dap_return_val_if_fail(a_json, NULL);
    
    json_object *l_obj = _dap_json_to_json_c(a_json);
    if (!l_obj) return NULL;
    
    json_object_get(l_obj); // Increase reference count
    return _json_c_to_dap_json(l_obj);  // Create NEW wrapper - each wrapper is independent
}

// Object iteration implementation
void dap_json_object_foreach(dap_json_t* a_json, dap_json_object_foreach_callback_t callback, void* user_data) {
    if (!a_json || !callback) return;
    
    json_object *l_obj = _dap_json_to_json_c(a_json);
    if (!l_obj) return;
    
    json_object_object_foreach(l_obj, key, val) {
        // Use stack-allocated wrapper for borrowed reference
        // IMPORTANT: Callback MUST NOT save this pointer - it's only valid during callback execution
        // Do NOT call dap_json_object_free() on this wrapper
        dap_json_t wrapper = { .pvt = val };
        callback(key, &wrapper, user_data);
    }
}

// Extended value access API implementation
const char* dap_json_get_string(dap_json_t* a_json) {
    if (!a_json) return NULL;
    
    json_object *l_obj = _dap_json_to_json_c(a_json);
    if (!l_obj) return NULL;
    
    return json_object_get_string(l_obj);
}

int64_t dap_json_get_int64(dap_json_t* a_json) {
    if (!a_json) return 0;
    
    json_object *l_obj = _dap_json_to_json_c(a_json);
    if (!l_obj) return 0;
    
    return json_object_get_int64(l_obj);
}

uint64_t dap_json_get_uint64(dap_json_t* a_json) {
    if (!a_json) return 0;
    
    json_object *l_obj = _dap_json_to_json_c(a_json);
    if (!l_obj) return 0;
    
    return json_object_get_uint64(l_obj);
}

dap_nanotime_t dap_json_get_nanotime(dap_json_t* a_json) {
    if (!a_json) return 0;
    
    json_object *l_obj = _dap_json_to_json_c(a_json);
    if (!l_obj) return 0;
    
    int64_t l_temp = json_object_get_int64(l_obj);
    // Handle both nanosecond timestamps and legacy second timestamps
    return l_temp >> 32 ? (dap_nanotime_t)l_temp : dap_nanotime_from_sec(l_temp);
}

size_t dap_json_object_length(dap_json_t* a_json) {
    if (!a_json) return 0;
    
    json_object *l_obj = _dap_json_to_json_c(a_json);
    if (!l_obj) return 0;
    
    return json_object_object_length(l_obj);
}

int dap_json_to_file(const char* a_file_path, dap_json_t* a_json) {
    if (!a_file_path || !a_json) {
        log_it(L_ERROR, "File path or JSON is NULL");
        return -1;
    }
    
    json_object *l_obj = _dap_json_to_json_c(a_json);
    if (!l_obj) return -1;
    
    return json_object_to_file(a_file_path, l_obj);
}


