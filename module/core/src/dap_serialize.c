/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Ltd   https://demlabs.net
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

#include "dap_serialize.h"
#include "dap_common.h"
#include "dap_strfuncs.h"
#include <string.h>
#include <arpa/inet.h>  // for htonl, ntohl

#define LOG_TAG "dap_serialize"

// Debug flag for detailed logging
static bool s_debug_more = true;

// Helper functions for arguments (indexed access for performance)
const dap_serialize_arg_t* dap_serialize_get_arg_by_index(const dap_serialize_size_params_t *a_params, size_t a_index) {
    if (!a_params || !a_params->args || a_index >= a_params->args_count) {
        return NULL;
    }
    
    return &a_params->args[a_index];
}

uint64_t dap_serialize_get_arg_uint_by_index(const dap_serialize_size_params_t *a_params, size_t a_index, uint64_t a_default) {
    const dap_serialize_arg_t *arg = dap_serialize_get_arg_by_index(a_params, a_index);
    if (!arg || arg->type != 0) { // type 0 = uint
        return a_default;
    }
    return arg->value.uint_value;
}

// Internal helper functions
// Removed: legacy s_calc_field_size wrapper - consolidated into main function
static int s_serialize_field(const dap_serialize_field_t *a_field,
                            const void *a_object,
                            dap_serialize_context_t *a_ctx);
static int s_deserialize_field(const dap_serialize_field_t *a_field,
                              void *a_object,
                              dap_serialize_context_t *a_ctx);
static bool s_check_condition(const dap_serialize_field_t *a_field,
                             const void *a_object,
                             void *a_context);
static void s_write_uint32_le(uint8_t *a_buffer, uint32_t a_value);
static uint32_t s_read_uint32_le(const uint8_t *a_buffer);
static void s_write_uint64_le(uint8_t *a_buffer, uint64_t a_value);
static uint64_t s_read_uint64_le(const uint8_t *a_buffer);
static void s_write_uint16_le(uint8_t *a_buffer, uint16_t a_value);
static uint16_t s_read_uint16_le(const uint8_t *a_buffer);
static void s_write_bigint_le(uint8_t *a_buffer, const uint8_t *a_value, size_t a_size);
static void s_read_bigint_le(const uint8_t *a_buffer, uint8_t *a_value, size_t a_size);
static size_t s_calc_field_size(const dap_serialize_field_t *a_field, 
                                const void *a_object,
                                const dap_serialize_size_params_t *a_params,
                                size_t a_field_index,
                                void *a_context,
                                const dap_serialize_schema_t *a_parent_schema);

/**
 * @brief Calculate required buffer size for serialization
 * @details Supports both object-based and parameter-based calculation
 */
// Global recursion depth counter to prevent infinite recursion
static __thread int s_recursion_depth = 0;
#define MAX_RECURSION_DEPTH 10

size_t dap_serialize_calc_size(const dap_serialize_schema_t *a_schema,
                               const dap_serialize_size_params_t *a_params,
                               const void *a_object,
                               void *a_context)
{
    // Prevent infinite recursion
    if (s_recursion_depth >= MAX_RECURSION_DEPTH) {
        log_it(L_ERROR, "Maximum recursion depth exceeded in serializer");
        return 0;
    }
    s_recursion_depth++;
    
    if (!a_schema || (!a_params && !a_object)) {
        log_it(L_ERROR, "Invalid parameters for size calculation - need either params or object");
        s_recursion_depth--;
        return 0;
    }
    
    if (a_schema->magic != DAP_SERIALIZE_MAGIC_NUMBER) {
        log_it(L_ERROR, "Invalid schema magic number: 0x%08X (expected 0x%08X)", 
               a_schema->magic, DAP_SERIALIZE_MAGIC_NUMBER);
        s_recursion_depth--;
        return 0;
    }
    
    size_t total_size = 0;
    
    // Add header size (version + magic + field count)
    total_size += sizeof(uint32_t) * 3;
    
    // Calculate size for each field
    debug_if(s_debug_more, L_DEBUG, "Starting field loop, field_count=%zu, using %s mode", 
             a_schema->field_count, a_params ? "parameter" : "object");
    
    for (size_t i = 0; i < a_schema->field_count; i++) {
        const dap_serialize_field_t *l_field = &a_schema->fields[i];
        debug_if(s_debug_more, L_DEBUG, "Processing field %zu/%zu: name=%s, type=%d", 
                 i, a_schema->field_count, l_field->name ? l_field->name : "NULL", l_field->type);
        
        // Check if field should be included
        // In parameter-based mode (a_object == NULL), include all conditional fields for conservative estimate
        if (a_object && !s_check_condition(l_field, a_object, a_context)) {
            debug_if(s_debug_more, L_DEBUG, "Field %zu ('%s') skipped due to condition", i, l_field->name);
            continue;
        }
        
        // In parameter-based mode, include conditional fields for worst-case sizing
        if (!a_object && (l_field->flags & DAP_SERIALIZE_FLAG_CONDITIONAL)) {
            debug_if(s_debug_more, L_DEBUG, "Field %zu ('%s') included in parameter-based mode (conditional)", i, l_field->name);
        }
        
        size_t l_field_size = s_calc_field_size(l_field, a_object, a_params, i, a_context, a_schema);
        debug_if(s_debug_more, L_DEBUG, "Field %zu ('%s') size: %zu", i, l_field->name, l_field_size);
        
        if (l_field_size == 0 && l_field->type != DAP_SERIALIZE_TYPE_PADDING) {
            log_it(L_WARNING, "Field '%s' has zero size", l_field->name);
        }
        
        total_size += l_field_size;
        debug_if(s_debug_more, L_DEBUG, "Total size after field %zu: %zu", i, total_size);
    }
    
    log_it(L_INFO, "CALCULATED TOTAL SIZE: %zu bytes for schema '%s' (header: 12, fields: %zu)", 
           total_size, a_schema->name, total_size - 12);
    
    s_recursion_depth--;
    return total_size;
}

/**
 * @brief Calculate size using parameters or object (extended version)
 */
size_t dap_serialize_calc_size_ex(const dap_serialize_schema_t *a_schema,
                                  const dap_serialize_size_params_t *a_params,
                                  const void *a_object,
                                  void *a_context)
{
    if (!a_schema || !a_params) {
        log_it(L_ERROR, "Invalid parameters for size calculation by params");
        return 0;
    }
    
    if (a_schema->magic != DAP_SERIALIZE_MAGIC_NUMBER) {
        log_it(L_ERROR, "Invalid schema magic number: 0x%08X (expected 0x%08X)", 
               a_schema->magic, DAP_SERIALIZE_MAGIC_NUMBER);
        return 0;
    }
    
    size_t total_size = 0;
    
    // Add header size (version + magic + field count)
    total_size += sizeof(uint32_t) * 3;
    
    // Calculate size for each field using parameters
    for (size_t i = 0; i < a_schema->field_count && i < 16; i++) {
        const dap_serialize_field_t *field = &a_schema->fields[i];
        
        // Check if field should be included
        if (field->flags & DAP_SERIALIZE_FLAG_OPTIONAL && !a_params->field_present[i]) {
            continue;
        }
        
        size_t field_size = 0;
        
        switch (field->type) {
            case DAP_SERIALIZE_TYPE_UINT8:
            case DAP_SERIALIZE_TYPE_INT8:
            case DAP_SERIALIZE_TYPE_BOOL:
                field_size = 1;
                break;
            case DAP_SERIALIZE_TYPE_UINT16:
            case DAP_SERIALIZE_TYPE_INT16:
                field_size = 2;
                break;
            case DAP_SERIALIZE_TYPE_UINT32:
            case DAP_SERIALIZE_TYPE_INT32:
            case DAP_SERIALIZE_TYPE_FLOAT32:
            case DAP_SERIALIZE_TYPE_VERSION:
                field_size = 4;
                break;
            case DAP_SERIALIZE_TYPE_UINT64:
            case DAP_SERIALIZE_TYPE_INT64:
            case DAP_SERIALIZE_TYPE_FLOAT64:
                field_size = 8;
                break;
            case DAP_SERIALIZE_TYPE_UINT128:
                field_size = 16;
                break;
            case DAP_SERIALIZE_TYPE_UINT256:
                field_size = 32;
                break;
            case DAP_SERIALIZE_TYPE_UINT512:
                field_size = 64;
                break;
            case DAP_SERIALIZE_TYPE_BYTES_FIXED:
                field_size = field->size;
                break;
            case DAP_SERIALIZE_TYPE_BYTES_DYNAMIC:
            case DAP_SERIALIZE_TYPE_STRING_DYNAMIC:
                field_size = sizeof(uint32_t) + a_params->data_sizes[i];
                break;
            case DAP_SERIALIZE_TYPE_ARRAY_DYNAMIC:
                field_size = sizeof(uint32_t); // count prefix
                if (field->nested_schema) {
                    // Prevent infinite recursion for nested schemas
                    if (field->nested_schema == a_schema) {
                        log_it(L_ERROR, "Circular dependency detected in nested schema for field %zu", i);
                        s_recursion_depth--;
                        return 0;
                    }
                    // For nested structures, multiply element size by count
                    size_t element_size = dap_serialize_calc_size(field->nested_schema, a_params, NULL, a_context);
                    if (element_size == 0) {
                        log_it(L_ERROR, "Failed to calculate nested schema size for field %zu", i);
                        s_recursion_depth--;
                        return 0;
                    }
                    field_size += element_size * a_params->array_counts[i];
                } else {
                    // Simple array of fixed-size elements
                    field_size += a_params->array_counts[i] * field->size;
                }
                break;
            case DAP_SERIALIZE_TYPE_CHECKSUM:
                field_size = field->size;
                break;
            case DAP_SERIALIZE_TYPE_PADDING:
                field_size = field->size;
                break;
            default:
                log_it(L_WARNING, "Unknown field type %d for field '%s'", field->type, field->name);
                break;
        }
        
        total_size += field_size;
    }
    
    log_it(L_DEBUG, "Calculated size by params: %zu bytes for schema '%s'", 
           total_size, a_schema->name);
    
    return total_size;
}

/**
 * @brief Serialize object to buffer
 */
dap_serialize_result_t dap_serialize_to_buffer(const dap_serialize_schema_t *a_schema,
                                               const void *a_object,
                                               uint8_t *a_buffer,
                                               size_t a_buffer_size,
                                               void *a_context)
{
    dap_serialize_result_t result = {0};
    
    debug_if(s_debug_more, L_DEBUG, "dap_serialize_to_buffer ENTRY: schema='%s', object=%p, buffer=%p, size=%zu", 
             a_schema ? a_schema->name : "NULL", a_object, a_buffer, a_buffer_size);
    
    if (!a_schema || !a_object || !a_buffer) {
        result.error_code = DAP_SERIALIZE_ERROR_INVALID_SCHEMA;
        result.error_message = "Invalid parameters";
        log_it(L_ERROR, "Invalid parameters: schema=%p, object=%p, buffer=%p", a_schema, a_object, a_buffer);
        return result;
    }
    
    if (a_schema->magic != DAP_SERIALIZE_MAGIC_NUMBER) {
        result.error_code = DAP_SERIALIZE_ERROR_INVALID_SCHEMA;
        result.error_message = "Invalid schema magic number";
        return result;
    }
    
    // Validate object if validation function provided
    if (a_schema->validate_func && !a_schema->validate_func(a_object)) {
        result.error_code = DAP_SERIALIZE_ERROR_INVALID_OBJECT;
        result.error_message = "Object validation failed";
        return result;
    }
    
    // Initialize context
    dap_serialize_context_t ctx = {
        .buffer = a_buffer,
        .buffer_size = a_buffer_size,
        .offset = 0,
        .version = a_schema->version,
        .user_context = a_context,
        .is_deserializing = false,
        .objects_serialized = 0,
        .bytes_processed = 0
    };
    
    // Write header
    if (ctx.offset + sizeof(uint32_t) * 3 > a_buffer_size) {
        result.error_code = DAP_SERIALIZE_ERROR_BUFFER_TOO_SMALL;
        result.error_message = "Buffer too small for header";
        return result;
    }
    
    s_write_uint32_le(a_buffer + ctx.offset, a_schema->magic);
    ctx.offset += sizeof(uint32_t);
    
    s_write_uint32_le(a_buffer + ctx.offset, a_schema->version);
    ctx.offset += sizeof(uint32_t);
    
    s_write_uint32_le(a_buffer + ctx.offset, (uint32_t)a_schema->field_count);
    ctx.offset += sizeof(uint32_t);
    
    // Serialize each field
    for (size_t i = 0; i < a_schema->field_count; i++) {
        const dap_serialize_field_t *field = &a_schema->fields[i];
        
        // Check if field should be included
        if (!s_check_condition(field, a_object, a_context)) {
            continue;
        }
        
        int l_field_result = s_serialize_field(field, a_object, &ctx);
        if (l_field_result != 0) {
            result.error_code = l_field_result;
            result.error_message = "Field serialization failed";
            result.failed_field = field->name;
            log_it(L_ERROR, "Field '%s' (type %d) serialization failed with error %d", 
                   field->name, field->type, l_field_result);
            return result;
        }
        
        ctx.objects_serialized++;
    }
    
    result.error_code = DAP_SERIALIZE_ERROR_SUCCESS;
    result.bytes_written = ctx.offset;
    ctx.bytes_processed = ctx.offset;
    
    log_it(L_DEBUG, "Serialized %zu objects, %zu bytes for schema '%s'",
           ctx.objects_serialized, result.bytes_written, a_schema->name);
    
    return result;
}

/**
 * @brief Deserialize object from buffer
 */
dap_serialize_result_t dap_serialize_from_buffer(const dap_serialize_schema_t *a_schema,
                                                 const uint8_t *a_buffer,
                                                 size_t a_buffer_size,
                                                 void *a_object,
                                                 void *a_context)
{
    dap_serialize_result_t result = {0};
    
    if (!a_schema || !a_buffer || !a_object) {
        result.error_code = DAP_SERIALIZE_ERROR_INVALID_SCHEMA;
        result.error_message = "Invalid parameters";
        return result;
    }
    
    if (a_buffer_size < sizeof(uint32_t) * 3) {
        result.error_code = DAP_SERIALIZE_ERROR_INVALID_DATA;
        result.error_message = "Buffer too small for header";
        return result;
    }
    
    // Initialize context
    dap_serialize_context_t ctx = {
        .buffer = (uint8_t*)a_buffer,  // Cast away const for context
        .buffer_size = a_buffer_size,
        .offset = 0,
        .user_context = a_context,
        .is_deserializing = true,
        .objects_serialized = 0,
        .bytes_processed = 0
    };
    
    // Read and validate header
    uint32_t magic = s_read_uint32_le(a_buffer + ctx.offset);
    ctx.offset += sizeof(uint32_t);
    
    if (magic != DAP_SERIALIZE_MAGIC_NUMBER) {
        result.error_code = DAP_SERIALIZE_ERROR_INVALID_DATA;
        result.error_message = "Invalid magic number in data";
        return result;
    }
    
    uint32_t version = s_read_uint32_le(a_buffer + ctx.offset);
    ctx.offset += sizeof(uint32_t);
    ctx.version = version;
    
    if (version > a_schema->version) {
        result.error_code = DAP_SERIALIZE_ERROR_VERSION_MISMATCH;
        result.error_message = "Data version newer than schema";
        return result;
    }
    
    uint32_t field_count = s_read_uint32_le(a_buffer + ctx.offset);
    ctx.offset += sizeof(uint32_t);
    
    // Initialize object memory
    memset(a_object, 0, a_schema->struct_size);
    
    // Deserialize each field
    for (size_t i = 0; i < a_schema->field_count && ctx.offset < a_buffer_size; i++) {
        const dap_serialize_field_t *field = &a_schema->fields[i];
        
        // Check version compatibility
        if (field->version_min > 0 && version < field->version_min) {
            continue;  // Skip field not supported in this version
        }
        if (field->version_max > 0 && version > field->version_max) {
            continue;  // Skip field deprecated in this version
        }
        
        // Check if field should be included
        if (!s_check_condition(field, a_object, a_context)) {
            continue;
        }
        
        int field_result = s_deserialize_field(field, a_object, &ctx);
        if (field_result != 0) {
            result.error_code = field_result;
            result.error_message = "Field deserialization failed";
            result.failed_field = field->name;
            return result;
        }
        
        ctx.objects_serialized++;
    }
    
    result.error_code = DAP_SERIALIZE_ERROR_SUCCESS;
    result.bytes_read = ctx.offset;
    ctx.bytes_processed = ctx.offset;
    
    log_it(L_DEBUG, "Deserialized %zu objects, %zu bytes for schema '%s'",
           ctx.objects_serialized, result.bytes_read, a_schema->name);
    
    return result;
}

/**
 * @brief Validate serialized data
 */
bool dap_serialize_validate_buffer(const dap_serialize_schema_t *a_schema,
                                   const uint8_t *a_buffer,
                                   size_t a_buffer_size)
{
    if (!a_schema || !a_buffer || a_buffer_size < sizeof(uint32_t) * 3) {
        return false;
    }
    
    // Check magic number
    uint32_t magic = s_read_uint32_le(a_buffer);
    if (magic != DAP_SERIALIZE_MAGIC_NUMBER) {
        return false;
    }
    
    // Check version compatibility
    uint32_t version = s_read_uint32_le(a_buffer + sizeof(uint32_t));
    if (version > a_schema->version) {
        return false;
    }
    
    // TODO: Add more comprehensive validation
    // - Field count consistency
    // - Checksum validation if present
    // - Structure integrity checks
    
    return true;
}

// Internal helper functions

static size_t s_calc_field_size(const dap_serialize_field_t *a_field, 
                                const void *a_object,
                                const dap_serialize_size_params_t *a_params,
                                size_t a_field_index,
                                void *a_context,
                                const dap_serialize_schema_t *a_parent_schema)
{
    debug_if(s_debug_more, L_DEBUG, "Calculating field size: name=%s, type=%d, index=%zu", 
             a_field->name ? a_field->name : "NULL", a_field->type, a_field_index);
    
    const uint8_t *l_obj_ptr = (const uint8_t*)a_object;
    size_t l_size = 0;
    
    switch (a_field->type) {
        case DAP_SERIALIZE_TYPE_UINT8:
        case DAP_SERIALIZE_TYPE_INT8:
        case DAP_SERIALIZE_TYPE_BOOL:
            l_size = 1;
            break;
        case DAP_SERIALIZE_TYPE_UINT16:
        case DAP_SERIALIZE_TYPE_INT16:
            l_size = 2;
            break;
        case DAP_SERIALIZE_TYPE_UINT32:
        case DAP_SERIALIZE_TYPE_INT32:
        case DAP_SERIALIZE_TYPE_FLOAT32:
        case DAP_SERIALIZE_TYPE_VERSION:
            l_size = 4;
            break;
        case DAP_SERIALIZE_TYPE_UINT64:
        case DAP_SERIALIZE_TYPE_INT64:
        case DAP_SERIALIZE_TYPE_FLOAT64:
            l_size = 8;
            break;
        case DAP_SERIALIZE_TYPE_UINT128:
            l_size = 16;
            break;
        case DAP_SERIALIZE_TYPE_UINT256:
            l_size = 32;
            break;
        case DAP_SERIALIZE_TYPE_UINT512:
            l_size = 64;
            break;
        case DAP_SERIALIZE_TYPE_BYTES_FIXED:
            l_size = a_field->size;
            break;
        case DAP_SERIALIZE_TYPE_BYTES_DYNAMIC: {
            // Check for parametric size function first (priority over legacy arrays)
            if (a_field->param_size_func && a_params) {
                size_t data_size = a_field->param_size_func(a_params, a_context);
                debug_if(s_debug_more, L_DEBUG, "BYTES_DYNAMIC using param_size_func: %zu", data_size);
                l_size = sizeof(uint32_t) + data_size;  // size prefix + data size
            } else if (a_params && a_field_index < a_params->field_count) {
                // Static parameter-based calculation
                size_t data_size = a_params->data_sizes[a_field_index];
                debug_if(s_debug_more, L_DEBUG, "BYTES_DYNAMIC parameter mode, using data_sizes[%zu] = %zu", 
                         a_field_index, data_size);
                l_size = sizeof(uint32_t) + data_size;  // size prefix + data size
            } else if (l_obj_ptr) {
                // Object-based calculation
                const size_t *l_size_ptr = (const size_t*)(l_obj_ptr + a_field->size_offset);
                l_size = sizeof(uint32_t) + *l_size_ptr;  // size prefix + data size
            } else {
                debug_if(s_debug_more, L_DEBUG, "BYTES_DYNAMIC: no object and no params, using size prefix only");
                l_size = sizeof(uint32_t);  // Just size prefix
            }
            break;
        }
        case DAP_SERIALIZE_TYPE_STRING_FIXED:
            l_size = a_field->size;
            break;
        case DAP_SERIALIZE_TYPE_STRING_DYNAMIC: {
            if (a_params && a_field_index < a_params->field_count) {
                // Parameter-based calculation
                l_size = sizeof(uint32_t) + a_params->data_sizes[a_field_index];  // length prefix + string data
            } else if (l_obj_ptr) {
                // Object-based calculation
                const size_t *l_size_ptr = (const size_t*)(l_obj_ptr + a_field->size_offset);
                l_size = sizeof(uint32_t) + *l_size_ptr;  // length prefix + string data
            } else {
                l_size = sizeof(uint32_t);  // Just length prefix
            }
            if (a_field->flags & DAP_SERIALIZE_FLAG_NULL_TERMINATED) {
                l_size += 1;  // null terminator
            }
            break;
        }
        case DAP_SERIALIZE_TYPE_ARRAY_DYNAMIC: {
            size_t l_count_value = 0;
            
            // Check for parametric count function first (priority over legacy arrays)
            if (a_field->param_count_func && a_params) {
                l_count_value = a_field->param_count_func(a_params, a_context);
                debug_if(s_debug_more, L_DEBUG, "ARRAY_DYNAMIC using param_count_func: %zu", l_count_value);
            } else if (a_params && a_field_index < a_params->field_count) {
                // Static parameter-based calculation
                l_count_value = a_params->array_counts[a_field_index];
                debug_if(s_debug_more, L_DEBUG, "ARRAY_DYNAMIC parameter mode, using array_counts[%zu] = %zu", 
                         a_field_index, l_count_value);
            } else if (l_obj_ptr) {
                // Object-based calculation - ALL count fields MUST be uint32_t for cross-platform compatibility
                const uint32_t *l_count_ptr = (const uint32_t*)(l_obj_ptr + a_field->count_offset);
                
                // Validate count_ptr before dereferencing
                if ((uintptr_t)l_count_ptr < (uintptr_t)a_object || 
                    (uintptr_t)l_count_ptr >= (uintptr_t)a_object + 4096) {
                    log_it(L_WARNING, "Array field '%s' count_ptr out of bounds, using 0", a_field->name);
                    l_size = sizeof(uint32_t);  // Just count prefix
                    break;
                }
                
                l_count_value = (size_t)*l_count_ptr;
                debug_if(s_debug_more, L_DEBUG, "Array field '%s' using uint32_t count: %u", 
                         a_field->name, *l_count_ptr);
                
                // Validate count value for sanity
                if (l_count_value > 1000000) {
                    log_it(L_ERROR, "Array field '%s' has invalid count value %zu (max allowed: 1000000), using 0", 
                           a_field->name, l_count_value);
                    l_count_value = 0;
                }
            } else {
                debug_if(s_debug_more, L_DEBUG, "ARRAY_DYNAMIC: no object and no params, using count prefix only");
                l_size = sizeof(uint32_t);  // Just count prefix
                break;
            }
            
            
            debug_if(s_debug_more, L_DEBUG, "ARRAY_DYNAMIC count=%zu", l_count_value);
            
            l_size = sizeof(uint32_t);  // count prefix
            
            // Calculate array element sizes
            if (a_field->nested_schema) {
                // For nested structures, calculate element size using nested schema
                debug_if(s_debug_more, L_DEBUG, "ARRAY_DYNAMIC nested schema calculation");
                
                // Guard against circular dependency
                if (a_field->nested_schema == a_parent_schema) {
                    debug_if(s_debug_more, L_DEBUG, "Circular dependency detected, using struct size");
                    l_size += a_field->nested_schema->struct_size * l_count_value;
                } else {
                    // Calculate exact element size using nested schema
                    size_t element_size = dap_serialize_calc_size(a_field->nested_schema, a_params, NULL, a_context);
                    if (element_size == 0) {
                        log_it(L_ERROR, "Failed to calculate nested schema size for field '%s'", a_field->name);
                        l_size += a_field->nested_schema->struct_size * l_count_value; // fallback
                    } else {
                        l_size += element_size * l_count_value;
                    }
                }
            } else {
                // Simple array of fixed-size elements
                debug_if(s_debug_more, L_DEBUG, "ARRAY_DYNAMIC simple array: count=%zu, element_size=%zu", 
                         l_count_value, a_field->size);
                l_size += l_count_value * a_field->size;
            }
            break;
        }
        case DAP_SERIALIZE_TYPE_CHECKSUM:
            l_size = a_field->size;  // Usually 32 bytes for SHA3-256 
            break;
        case DAP_SERIALIZE_TYPE_PADDING:
            l_size = a_field->size;
            break;
        default:
            log_it(L_WARNING, "Unknown field type %d for field '%s'", a_field->type, a_field->name);
            break;
    }
    
    return l_size;
}

static bool s_check_condition(const dap_serialize_field_t *a_field,
                             const void *a_object,
                             void *a_context)
{
    // Check conditional flag
    if (a_field->flags & DAP_SERIALIZE_FLAG_CONDITIONAL) {
        // Conditional field - must have condition function
        if (!a_field->condition) {
            log_it(L_WARNING, "Conditional field '%s' has no condition function", a_field->name);
            return false;
        }
        return a_field->condition(a_object, a_context);
    }
    
    // For non-conditional fields, check if condition function exists
    if (!a_field->condition) {
        return true;  // No condition means always include
    }
    
    return a_field->condition(a_object, a_context);
}

static int s_serialize_field(const dap_serialize_field_t *a_field,
                            const void *a_object,
                            dap_serialize_context_t *a_ctx)
{
    const uint8_t *obj_ptr = (const uint8_t*)a_object;
    
    debug_if(s_debug_more, L_DEBUG, "s_serialize_field ENTRY: field='%s', type=%d", 
             a_field->name, a_field->type);
    
    // Check buffer space for all fields except problematic nested arrays
    if (!(a_field->type == DAP_SERIALIZE_TYPE_ARRAY_DYNAMIC && a_field->nested_schema)) {
        size_t l_field_size = s_calc_field_size(a_field, a_object, NULL, 0, a_ctx->user_context, NULL);
        debug_if(s_debug_more, L_DEBUG, "s_calc_field_size returned: %zu for field '%s'", 
                 l_field_size, a_field->name);
        
        if (a_ctx->offset + l_field_size > a_ctx->buffer_size) {
            log_it(L_ERROR, "Buffer too small for field '%s': offset=%zu + field_size=%zu > buffer_size=%zu", 
                   a_field->name, a_ctx->offset, l_field_size, a_ctx->buffer_size);
            return DAP_SERIALIZE_ERROR_BUFFER_TOO_SMALL;
        }
    }
    
    switch (a_field->type) {
        case DAP_SERIALIZE_TYPE_UINT8:
        case DAP_SERIALIZE_TYPE_INT8:
        case DAP_SERIALIZE_TYPE_BOOL: {
            // Check buffer space
            if (a_ctx->offset + 1 > a_ctx->buffer_size) {
                log_it(L_ERROR, "Buffer overflow in field '%s': offset=%zu + 1 > buffer_size=%zu", 
                       a_field->name, a_ctx->offset, a_ctx->buffer_size);
                return DAP_SERIALIZE_ERROR_BUFFER_TOO_SMALL;
            }
            const uint8_t *value = (const uint8_t*)(obj_ptr + a_field->offset);
            a_ctx->buffer[a_ctx->offset] = *value;
            a_ctx->offset += 1;
            break;
        }
        case DAP_SERIALIZE_TYPE_UINT16:
        case DAP_SERIALIZE_TYPE_INT16: {
            const uint16_t *value = (const uint16_t*)(obj_ptr + a_field->offset);
            s_write_uint16_le(a_ctx->buffer + a_ctx->offset, *value);
            a_ctx->offset += 2;
            break;
        }
        case DAP_SERIALIZE_TYPE_UINT32:
        case DAP_SERIALIZE_TYPE_INT32: {
            const uint32_t *value = (const uint32_t*)(obj_ptr + a_field->offset);
            s_write_uint32_le(a_ctx->buffer + a_ctx->offset, *value);
            a_ctx->offset += 4;
            break;
        }
        case DAP_SERIALIZE_TYPE_UINT64:
        case DAP_SERIALIZE_TYPE_INT64: {
            const uint64_t *value = (const uint64_t*)(obj_ptr + a_field->offset);
            s_write_uint64_le(a_ctx->buffer + a_ctx->offset, *value);
            a_ctx->offset += 8;
            break;
        }
        case DAP_SERIALIZE_TYPE_UINT128:
        case DAP_SERIALIZE_TYPE_UINT256:
        case DAP_SERIALIZE_TYPE_UINT512: {
            const uint8_t *value = (const uint8_t*)(obj_ptr + a_field->offset);
            size_t type_size = (a_field->type == DAP_SERIALIZE_TYPE_UINT128) ? 16 :
                              (a_field->type == DAP_SERIALIZE_TYPE_UINT256) ? 32 : 64;
            s_write_bigint_le(a_ctx->buffer + a_ctx->offset, value, type_size);
            a_ctx->offset += type_size;
            break;
        }
        case DAP_SERIALIZE_TYPE_FLOAT32: {
            const float *value = (const float*)(obj_ptr + a_field->offset);
            // Convert float to uint32 for endianness handling
            union { float f; uint32_t u; } converter = { .f = *value };
            s_write_uint32_le(a_ctx->buffer + a_ctx->offset, converter.u);
            a_ctx->offset += 4;
            break;
        }
        case DAP_SERIALIZE_TYPE_FLOAT64: {
            const double *value = (const double*)(obj_ptr + a_field->offset);
            // Convert double to uint64 for endianness handling
            union { double d; uint64_t u; } converter = { .d = *value };
            s_write_uint64_le(a_ctx->buffer + a_ctx->offset, converter.u);
            a_ctx->offset += 8;
            break;
        }
        // REMOVED: duplicate BYTES_DYNAMIC case - consolidated below
        case DAP_SERIALIZE_TYPE_STRING_DYNAMIC: {
            const char **string_ptr = (const char**)(obj_ptr + a_field->offset);
            const size_t *size_ptr = (const size_t*)(obj_ptr + a_field->size_offset);
            
            // Write length prefix
            s_write_uint32_le(a_ctx->buffer + a_ctx->offset, (uint32_t)*size_ptr);
            a_ctx->offset += sizeof(uint32_t);
            
            // Write string data
            if (*string_ptr && *size_ptr > 0) {
                memcpy(a_ctx->buffer + a_ctx->offset, *string_ptr, *size_ptr);
                a_ctx->offset += *size_ptr;
                
                // Add null terminator if requested
                if (a_field->flags & DAP_SERIALIZE_FLAG_NULL_TERMINATED) {
                    a_ctx->buffer[a_ctx->offset] = '\0';
                    a_ctx->offset += 1;
                }
            }
            break;
        }
        case DAP_SERIALIZE_TYPE_BYTES_FIXED: {
            const uint8_t *value = (const uint8_t*)(obj_ptr + a_field->offset);
            memcpy(a_ctx->buffer + a_ctx->offset, value, a_field->size);
            a_ctx->offset += a_field->size;
            break;
        }
        case DAP_SERIALIZE_TYPE_STRING_FIXED: {
            const char *value = (const char*)(obj_ptr + a_field->offset);
            memcpy(a_ctx->buffer + a_ctx->offset, value, a_field->size);
            a_ctx->offset += a_field->size;
            break;
        }
        case DAP_SERIALIZE_TYPE_BYTES_DYNAMIC: {
            const void **l_data_ptr = (const void**)(obj_ptr + a_field->offset);
            const size_t *l_size_ptr = (const size_t*)(obj_ptr + a_field->size_offset);
            
            debug_if(s_debug_more, L_DEBUG, "BYTES_DYNAMIC field '%s': data_ptr=%p, size=%zu", 
                     a_field->name, *l_data_ptr, *l_size_ptr);
            
            // Robust validation for BYTES_DYNAMIC fields
            if (*l_size_ptr > 0 && !*l_data_ptr) {
                log_it(L_WARNING, "BYTES_DYNAMIC field '%s' has NULL data pointer but non-zero size %zu, writing zeros", 
                       a_field->name, *l_size_ptr);
                // Don't fail - write zeros instead for robustness
            }
            
            // Validate size is reasonable
            if (*l_size_ptr > 100*1024*1024) {  // 100MB max per field
                log_it(L_ERROR, "BYTES_DYNAMIC field '%s' has unreasonable size %zu (max: 100MB)", 
                       a_field->name, *l_size_ptr);
                return DAP_SERIALIZE_ERROR_FIELD_VALIDATION;
            }
            
            // Check buffer space
            if (a_ctx->offset + sizeof(uint32_t) + *l_size_ptr > a_ctx->buffer_size) {
                log_it(L_ERROR, "Buffer overflow in BYTES_DYNAMIC field '%s': offset=%zu + size=%zu > buffer_size=%zu", 
                       a_field->name, a_ctx->offset, sizeof(uint32_t) + *l_size_ptr, a_ctx->buffer_size);
                return DAP_SERIALIZE_ERROR_BUFFER_TOO_SMALL;
            }
            
            // Write size prefix
            s_write_uint32_le(a_ctx->buffer + a_ctx->offset, (uint32_t)*l_size_ptr);
            a_ctx->offset += sizeof(uint32_t);
            
            // Write data - if NULL, write zeros
            if (*l_data_ptr && *l_size_ptr > 0) {
                memcpy(a_ctx->buffer + a_ctx->offset, *l_data_ptr, *l_size_ptr);
            } else if (*l_size_ptr > 0) {
                // NULL pointer but non-zero size - write zeros
                memset(a_ctx->buffer + a_ctx->offset, 0, *l_size_ptr);
            }
            a_ctx->offset += *l_size_ptr;
            break;
        }
        case DAP_SERIALIZE_TYPE_VERSION: {
            // Version field - write field size (usually 4 bytes for uint32_t)
            uint32_t l_version = 1; // Default version
            s_write_uint32_le(a_ctx->buffer + a_ctx->offset, l_version);
            a_ctx->offset += sizeof(uint32_t);
            break;
        }
        case DAP_SERIALIZE_TYPE_CHECKSUM: {
            // Skip checksum during serialization - will be calculated later
            memset(a_ctx->buffer + a_ctx->offset, 0, a_field->size);
            a_ctx->offset += a_field->size;
            break;
        }
        case DAP_SERIALIZE_TYPE_ARRAY_DYNAMIC: {
            const void **l_array_ptr = (const void**)(obj_ptr + a_field->offset);
            
            // ALL count fields MUST be uint32_t for cross-platform serialization compatibility
            const uint32_t *l_count_ptr = (const uint32_t*)(obj_ptr + a_field->count_offset);
            uint32_t l_count_value_u32 = *l_count_ptr;
            
            debug_if(s_debug_more, L_DEBUG, "Array field '%s' serializing uint32_t count: %u", 
                     a_field->name, l_count_value_u32);
            
            // Robust validation for ARRAY_DYNAMIC fields
            if (l_count_value_u32 > 1000000) {
                log_it(L_ERROR, "Array field '%s' has invalid count value %u (max allowed: 1000000), using 0", 
                       a_field->name, l_count_value_u32);
                l_count_value_u32 = 0;
            }
            
            // Check buffer space for count prefix
            if (a_ctx->offset + sizeof(uint32_t) > a_ctx->buffer_size) {
                log_it(L_ERROR, "Buffer overflow in ARRAY_DYNAMIC field '%s' count: offset=%zu + 4 > buffer_size=%zu", 
                       a_field->name, a_ctx->offset, a_ctx->buffer_size);
                return DAP_SERIALIZE_ERROR_BUFFER_TOO_SMALL;
            }
            
            // Write count prefix
            s_write_uint32_le(a_ctx->buffer + a_ctx->offset, l_count_value_u32);
            a_ctx->offset += sizeof(uint32_t);
            
            // For nested arrays, rely on per-field checks during element serialization.
            // For simple arrays, pre-check aggregated size.
            if (!a_field->nested_schema) {
                size_t l_array_data_size = l_count_value_u32 * a_field->size;
                if (a_ctx->offset + l_array_data_size > a_ctx->buffer_size) {
                    log_it(L_ERROR, "Buffer overflow in ARRAY_DYNAMIC field '%s' data: offset=%zu + array_size=%zu > buffer_size=%zu", 
                           a_field->name, a_ctx->offset, l_array_data_size, a_ctx->buffer_size);
                    return DAP_SERIALIZE_ERROR_BUFFER_TOO_SMALL;
                }
            }
            
            // Serialize array elements
            if (*l_array_ptr && l_count_value_u32 > 0) {
                if (a_field->nested_schema) {
                    // Nested structures - validate array pointer
                    const uint8_t *l_element_ptr = (const uint8_t*)*l_array_ptr;
                    
                    // Safety check for array pointer validity
                    if (!l_element_ptr) {
                        log_it(L_WARNING, "Array field '%s' has NULL data pointer but non-zero count %u", 
                               a_field->name, l_count_value_u32);
                        // Write count as 0 and skip data
                        s_write_uint32_le(a_ctx->buffer + a_ctx->offset - sizeof(uint32_t), 0);
                        return DAP_SERIALIZE_ERROR_SUCCESS;
                    }
                    
                    for (size_t i = 0; i < l_count_value_u32; i++) {
                        const uint8_t *l_current_element = l_element_ptr + i * a_field->nested_schema->struct_size;
                        
                        // Additional safety check for element pointer
                        if (!l_current_element) {
                            log_it(L_ERROR, "Array field '%s' element %zu is NULL", a_field->name, i);
                            return DAP_SERIALIZE_ERROR_INVALID_DATA;
                        }
                        
                        // Serialize nested schema fields directly without header
                        // This avoids the overhead of schema headers for each array element
                        for (size_t f = 0; f < a_field->nested_schema->field_count; f++) {
                            const dap_serialize_field_t *l_nested_field = &a_field->nested_schema->fields[f];
                            
                            // Check condition for nested field
                            if (!s_check_condition(l_nested_field, l_current_element, a_ctx->user_context)) {
                                continue;
                            }
                            
                            int l_nested_result = s_serialize_field(l_nested_field, l_current_element, a_ctx);
                            if (l_nested_result != 0) {
                                return l_nested_result;
                            }
                        }
                    }
                } else {
                    // Simple array of fixed-size elements
                    size_t l_total_size = l_count_value_u32 * a_field->size;
                    
                    // Safety check for array data
                    if (!*l_array_ptr && l_total_size > 0) {
                        log_it(L_WARNING, "Array field '%s' has NULL data pointer but non-zero size %zu", 
                               a_field->name, l_total_size);
                        // Write zeros instead
                        memset(a_ctx->buffer + a_ctx->offset, 0, l_total_size);
                    } else {
                        memcpy(a_ctx->buffer + a_ctx->offset, *l_array_ptr, l_total_size);
                    }
                    a_ctx->offset += l_total_size;
                }
            }
            break;
        }
        default:
            log_it(L_WARNING, "Serialization not implemented for field type %d", a_field->type);
            return DAP_SERIALIZE_ERROR_FIELD_VALIDATION;
    }
    
    return DAP_SERIALIZE_ERROR_SUCCESS;
}

static int s_deserialize_field(const dap_serialize_field_t *a_field,
                              void *a_object,
                              dap_serialize_context_t *a_ctx)
{
    uint8_t *obj_ptr = (uint8_t*)a_object;
    
    switch (a_field->type) {
        case DAP_SERIALIZE_TYPE_UINT8:
        case DAP_SERIALIZE_TYPE_INT8:
        case DAP_SERIALIZE_TYPE_BOOL: {
            if (a_ctx->offset + 1 > a_ctx->buffer_size) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            uint8_t *value = (uint8_t*)(obj_ptr + a_field->offset);
            *value = a_ctx->buffer[a_ctx->offset];
            a_ctx->offset += 1;
            break;
        }
        case DAP_SERIALIZE_TYPE_UINT16:
        case DAP_SERIALIZE_TYPE_INT16: {
            if (a_ctx->offset + 2 > a_ctx->buffer_size) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            uint16_t *value = (uint16_t*)(obj_ptr + a_field->offset);
            *value = s_read_uint16_le(a_ctx->buffer + a_ctx->offset);
            a_ctx->offset += 2;
            break;
        }
        case DAP_SERIALIZE_TYPE_UINT32:
        case DAP_SERIALIZE_TYPE_INT32:
        case DAP_SERIALIZE_TYPE_VERSION: {
            if (a_ctx->offset + 4 > a_ctx->buffer_size) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            uint32_t *value = (uint32_t*)(obj_ptr + a_field->offset);
            *value = s_read_uint32_le(a_ctx->buffer + a_ctx->offset);
            a_ctx->offset += 4;
            break;
        }
        case DAP_SERIALIZE_TYPE_UINT64:
        case DAP_SERIALIZE_TYPE_INT64: {
            if (a_ctx->offset + 8 > a_ctx->buffer_size) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            uint64_t *value = (uint64_t*)(obj_ptr + a_field->offset);
            *value = s_read_uint64_le(a_ctx->buffer + a_ctx->offset);
            a_ctx->offset += 8;
            break;
        }
        case DAP_SERIALIZE_TYPE_UINT128:
        case DAP_SERIALIZE_TYPE_UINT256:
        case DAP_SERIALIZE_TYPE_UINT512: {
            size_t type_size = (a_field->type == DAP_SERIALIZE_TYPE_UINT128) ? 16 :
                              (a_field->type == DAP_SERIALIZE_TYPE_UINT256) ? 32 : 64;
            if (a_ctx->offset + type_size > a_ctx->buffer_size) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            uint8_t *value = (uint8_t*)(obj_ptr + a_field->offset);
            s_read_bigint_le(a_ctx->buffer + a_ctx->offset, value, type_size);
            a_ctx->offset += type_size;
            break;
        }
        case DAP_SERIALIZE_TYPE_FLOAT32: {
            if (a_ctx->offset + 4 > a_ctx->buffer_size) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            float *value = (float*)(obj_ptr + a_field->offset);
            union { float f; uint32_t u; } converter;
            converter.u = s_read_uint32_le(a_ctx->buffer + a_ctx->offset);
            *value = converter.f;
            a_ctx->offset += 4;
            break;
        }
        case DAP_SERIALIZE_TYPE_FLOAT64: {
            if (a_ctx->offset + 8 > a_ctx->buffer_size) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            double *value = (double*)(obj_ptr + a_field->offset);
            union { double d; uint64_t u; } converter;
            converter.u = s_read_uint64_le(a_ctx->buffer + a_ctx->offset);
            *value = converter.d;
            a_ctx->offset += 8;
            break;
        }
        case DAP_SERIALIZE_TYPE_BYTES_DYNAMIC: {
            if (a_ctx->offset + sizeof(uint32_t) > a_ctx->buffer_size) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            
            // Read size
            uint32_t size = s_read_uint32_le(a_ctx->buffer + a_ctx->offset);
            a_ctx->offset += sizeof(uint32_t);
            
            if (a_ctx->offset + size > a_ctx->buffer_size) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            
            // Set size field
            size_t *size_ptr = (size_t*)(obj_ptr + a_field->size_offset);
            *size_ptr = size;
            
            // Allocate and copy data
            void **data_ptr = (void**)(obj_ptr + a_field->offset);
            if (size > 0) {
                *data_ptr = DAP_NEW_SIZE(uint8_t, size);
                if (!*data_ptr) {
                    return DAP_SERIALIZE_ERROR_MEMORY_ALLOCATION;
                }
                memcpy(*data_ptr, a_ctx->buffer + a_ctx->offset, size);
                a_ctx->offset += size;
            } else {
                *data_ptr = NULL;
            }
            break;
        }
        case DAP_SERIALIZE_TYPE_STRING_DYNAMIC: {
            if (a_ctx->offset + sizeof(uint32_t) > a_ctx->buffer_size) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            
            // Read length
            uint32_t length = s_read_uint32_le(a_ctx->buffer + a_ctx->offset);
            a_ctx->offset += sizeof(uint32_t);
            
            if (a_ctx->offset + length > a_ctx->buffer_size) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            
            // Set length field
            size_t *size_ptr = (size_t*)(obj_ptr + a_field->size_offset);
            *size_ptr = length;
            
            // Allocate and copy string
            char **string_ptr = (char**)(obj_ptr + a_field->offset);
            if (length > 0) {
                *string_ptr = DAP_NEW_SIZE(char, length + 1);  // +1 for null terminator
                if (!*string_ptr) {
                    return DAP_SERIALIZE_ERROR_MEMORY_ALLOCATION;
                }
                memcpy(*string_ptr, a_ctx->buffer + a_ctx->offset, length);
                (*string_ptr)[length] = '\0';  // Ensure null termination
                a_ctx->offset += length;
                
                // Skip null terminator if present in data
                if (a_field->flags & DAP_SERIALIZE_FLAG_NULL_TERMINATED) {
                    if (a_ctx->offset < a_ctx->buffer_size && a_ctx->buffer[a_ctx->offset] == '\0') {
                        a_ctx->offset += 1;
                    }
                }
            } else {
                *string_ptr = NULL;
            }
            break;
        }
        case DAP_SERIALIZE_TYPE_BYTES_FIXED: {
            if (a_ctx->offset + a_field->size > a_ctx->buffer_size) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            uint8_t *value = (uint8_t*)(obj_ptr + a_field->offset);
            memcpy(value, a_ctx->buffer + a_ctx->offset, a_field->size);
            a_ctx->offset += a_field->size;
            break;
        }
        case DAP_SERIALIZE_TYPE_STRING_FIXED: {
            if (a_ctx->offset + a_field->size > a_ctx->buffer_size) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            char *value = (char*)(obj_ptr + a_field->offset);
            memcpy(value, a_ctx->buffer + a_ctx->offset, a_field->size);
            a_ctx->offset += a_field->size;
            break;
        }
        case DAP_SERIALIZE_TYPE_ARRAY_DYNAMIC: {
            uint8_t *obj_ptr = (uint8_t*)a_object;
            // Read count prefix
            if (a_ctx->offset + sizeof(uint32_t) > a_ctx->buffer_size) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            uint32_t count = s_read_uint32_le(a_ctx->buffer + a_ctx->offset);
            a_ctx->offset += sizeof(uint32_t);

            if (count > 1000000) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }

            // Store count if needed
            if (a_field->count_offset) {
                uint32_t *count_ptr = (uint32_t*)(obj_ptr + a_field->count_offset);
                *count_ptr = count;
            }

            void **array_ptr = (void**)(obj_ptr + a_field->offset);
            *array_ptr = NULL;

            if (count == 0) {
                break;
            }

            if (!a_field->nested_schema) {
                // Simple array
                size_t total_size = (size_t)count * a_field->size;
                if (a_ctx->offset + total_size > a_ctx->buffer_size) {
                    return DAP_SERIALIZE_ERROR_INVALID_DATA;
                }
                *array_ptr = DAP_NEW_SIZE(uint8_t, total_size);
                if (!*array_ptr) {
                    return DAP_SERIALIZE_ERROR_MEMORY_ALLOCATION;
                }
                memcpy(*array_ptr, a_ctx->buffer + a_ctx->offset, total_size);
                a_ctx->offset += total_size;
            } else {
                // Nested structures: allocate contiguous array of elements and deserialize each
                const dap_serialize_schema_t *ns = a_field->nested_schema;
                size_t element_size = ns->struct_size;
                size_t total_size = (size_t)count * element_size;
                *array_ptr = DAP_NEW_Z_SIZE(uint8_t, total_size);
                if (!*array_ptr) {
                    return DAP_SERIALIZE_ERROR_MEMORY_ALLOCATION;
                }
                for (size_t i = 0; i < count; i++) {
                    uint8_t *element_obj = (uint8_t*)(*array_ptr) + i * element_size;
                    for (size_t f = 0; f < ns->field_count; f++) {
                        const dap_serialize_field_t *nf = &ns->fields[f];
                        int r = s_deserialize_field(nf, element_obj, a_ctx);
                        if (r != 0) {
                            return r;
                        }
                    }
                }
            }
            break;
        }
        default:
            log_it(L_WARNING, "Deserialization not implemented for field type %d", a_field->type);
            return DAP_SERIALIZE_ERROR_FIELD_VALIDATION;
    }
    
    return DAP_SERIALIZE_ERROR_SUCCESS;
}

// Endianness helper functions
static void s_write_uint32_le(uint8_t *a_buffer, uint32_t a_value)
{
    a_buffer[0] = (uint8_t)(a_value & 0xFF);
    a_buffer[1] = (uint8_t)((a_value >> 8) & 0xFF);
    a_buffer[2] = (uint8_t)((a_value >> 16) & 0xFF);
    a_buffer[3] = (uint8_t)((a_value >> 24) & 0xFF);
}

static uint32_t s_read_uint32_le(const uint8_t *a_buffer)
{
    return (uint32_t)a_buffer[0] |
           ((uint32_t)a_buffer[1] << 8) |
           ((uint32_t)a_buffer[2] << 16) |
           ((uint32_t)a_buffer[3] << 24);
}

static void s_write_uint64_le(uint8_t *a_buffer, uint64_t a_value)
{
    s_write_uint32_le(a_buffer, (uint32_t)(a_value & 0xFFFFFFFF));
    s_write_uint32_le(a_buffer + 4, (uint32_t)(a_value >> 32));
}

static uint64_t s_read_uint64_le(const uint8_t *a_buffer)
{
    uint64_t low = s_read_uint32_le(a_buffer);
    uint64_t high = s_read_uint32_le(a_buffer + 4);
    return low | (high << 32);
}

static void s_write_uint16_le(uint8_t *a_buffer, uint16_t a_value)
{
    a_buffer[0] = (uint8_t)(a_value & 0xFF);
    a_buffer[1] = (uint8_t)((a_value >> 8) & 0xFF);
}

static uint16_t s_read_uint16_le(const uint8_t *a_buffer)
{
    return (uint16_t)a_buffer[0] | ((uint16_t)a_buffer[1] << 8);
}

static void s_write_bigint_le(uint8_t *a_buffer, const uint8_t *a_value, size_t a_size)
{
    // For big integers, we store in little-endian byte order
    for (size_t i = 0; i < a_size; i++) {
        a_buffer[i] = a_value[i];
    }
}

static void s_read_bigint_le(const uint8_t *a_buffer, uint8_t *a_value, size_t a_size)
{
    // For big integers, we read from little-endian byte order
    for (size_t i = 0; i < a_size; i++) {
        a_value[i] = a_buffer[i];
    }
}
