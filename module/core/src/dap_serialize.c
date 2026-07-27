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
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>  // for htonl, ntohl

#define LOG_TAG "dap_serialize"

// Debug flag for detailed logging
static bool s_debug_more = false;

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
        debug_if(s_debug_more, L_DEBUG, "dap_serialize_get_arg_uint_by_index: index=%zu, arg=%p, returning default=%lu", 
                 a_index, arg, a_default);
        return a_default;
    }
    
    debug_if(s_debug_more, L_DEBUG, "dap_serialize_get_arg_uint_by_index: index=%zu, value=%lu", 
             a_index, arg->value.uint_value);
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

// Thread-local nesting depth counter used by serialize/deserialize paths for
// ARRAY_FIXED, ARRAY_DYNAMIC(nested) and NESTED_STRUCT to stop stack-exhaustion
// from pathological self-referential schemas.  Kept separate from
// s_recursion_depth, which guards size-calculation.
//
// DAP_SERIALIZE_MAX_FIELD_NESTING is defined publicly in dap_serialize.h.
static __thread int s_field_nesting_depth = 0;

/**
 * @brief Compute @p a * @p b with overflow detection.
 * @return true on success, false when the multiplication overflows size_t.
 */
static inline bool s_safe_mul_size(size_t a, size_t b, size_t *a_out)
{
    if (__builtin_mul_overflow(a, b, a_out)) {
        return false;
    }
    return true;
}

/**
 * @brief Compute @p a + @p b with overflow detection.
 */
static inline bool s_safe_add_size(size_t a, size_t b, size_t *a_out)
{
    if (__builtin_add_overflow(a, b, a_out)) {
        return false;
    }
    return true;
}

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

    // Accept the standard magic number AND any non-zero custom magic, in
    // line with dap_serialize_to_buffer_raw().  Without this fan-out, nested
    // schemas that carry an application-specific magic (e.g. CHMA for the
    // chipmunk multi-signature codec) would be rejected here even when the
    // top-level path uses _raw() and never even consults the magic.
    if (a_schema->magic != DAP_SERIALIZE_MAGIC_NUMBER && a_schema->magic == 0) {
        log_it(L_ERROR, "Invalid schema magic number: 0x%08X (expected 0x%08X or any non-zero custom value)",
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
        bool field_included = true;
        
        if (l_field->flags & DAP_SERIALIZE_FLAG_CONDITIONAL) {
            debug_if(s_debug_more, L_DEBUG, "Processing conditional field '%s': a_object=%p, param_condition=%p, a_params=%p", 
                     l_field->name, a_object, l_field->param_condition, a_params);
            
            if (a_object) {
                // Object-based condition check
                field_included = s_check_condition(l_field, a_object, a_context);
                debug_if(s_debug_more, L_DEBUG, "Object-based condition for '%s': %s", l_field->name, field_included ? "included" : "excluded");
            } else if (l_field->param_condition && a_params) {
                // Parameter-based condition check
                field_included = l_field->param_condition(a_params, a_context);
                debug_if(s_debug_more, L_DEBUG, "Parametric condition for '%s': %s", l_field->name, field_included ? "included" : "excluded");
            } else {
                // No parametric condition - include for conservative estimate
                field_included = true;
                debug_if(s_debug_more, L_DEBUG, "No parametric condition for '%s', including by default", l_field->name);
            }
        }
        
        if (!field_included) {
            debug_if(s_debug_more, L_DEBUG, "Field %zu ('%s') skipped due to condition", i, l_field->name);
            continue;
        }
        
        size_t l_field_size = s_calc_field_size(l_field, a_object, a_params, i, a_context, a_schema);
        debug_if(s_debug_more, L_DEBUG, "Field %zu ('%s') size: %zu", i, l_field->name, l_field_size);
        
        if (l_field_size == 0 && l_field->type != DAP_SERIALIZE_TYPE_PADDING) {
            log_it(L_WARNING, "Field '%s' has zero size", l_field->name);
        }
        
        size_t l_next = 0;
        if (!s_safe_add_size(total_size, l_field_size, &l_next)) {
            log_it(L_ERROR, "calc_size overflow accumulating field '%s'", l_field->name);
            s_recursion_depth--;
            return 0;
        }
        total_size = l_next;
        debug_if(s_debug_more, L_DEBUG, "Total size after field %zu: %zu", i, total_size);
    }
    
    debug_if(s_debug_more, L_INFO, "CALCULATED TOTAL SIZE: %zu bytes for schema '%s' (header: 12, fields: %zu)", 
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

    if (a_schema->magic != DAP_SERIALIZE_MAGIC_NUMBER && a_schema->magic == 0) {
        log_it(L_ERROR, "Invalid schema magic number: 0x%08X (expected 0x%08X or any non-zero custom value)",
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
            case DAP_SERIALIZE_TYPE_ARRAY_DYNAMIC: {
                field_size = (field->flags & DAP_SERIALIZE_FLAG_NO_COUNT_PREFIX) ? 0u : sizeof(uint32_t);
                size_t elem_sz = 0;
                if (field->nested_schema) {
                    // Prevent infinite recursion for nested schemas
                    if (field->nested_schema == a_schema) {
                        log_it(L_ERROR, "Circular dependency detected in nested schema for field %zu", i);
                        return 0;
                    }
                    elem_sz = dap_serialize_calc_size(field->nested_schema, a_params, NULL, a_context);
                    if (elem_sz == 0) {
                        log_it(L_ERROR, "Failed to calculate nested schema size for field %zu", i);
                        return 0;
                    }
                } else {
                    elem_sz = field->size;
                }
                size_t arr_total = 0;
                size_t combined = 0;
                if (!s_safe_mul_size(elem_sz, a_params->array_counts[i], &arr_total) ||
                    !s_safe_add_size(field_size, arr_total, &combined)) {
                    log_it(L_ERROR, "calc_size_ex overflow for field '%s'", field->name ? field->name : "?");
                    return 0;
                }
                field_size = combined;
                break;
            }
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
        
        size_t l_ex_next = 0;
        if (!s_safe_add_size(total_size, field_size, &l_ex_next)) {
            log_it(L_ERROR, "calc_size_ex total overflow at field '%s'", field->name ? field->name : "?");
            return 0;
        }
        total_size = l_ex_next;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Calculated size by params: %zu bytes for schema '%s'", 
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
    
    // Validate schema magic: accept both standard and custom magic numbers
    // Standard magic (DAP_SERIALIZE_MAGIC_NUMBER) is always valid
    // Custom magic numbers (non-zero) are also valid for extended schemas
    if (a_schema->magic != DAP_SERIALIZE_MAGIC_NUMBER && a_schema->magic == 0) {
        result.error_code = DAP_SERIALIZE_ERROR_INVALID_SCHEMA;
        result.error_message = "Schema magic must be either DAP_SERIALIZE_MAGIC_NUMBER or custom (non-zero)";
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
        .bytes_processed = 0,
        .current_schema = a_schema
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
    
    debug_if(s_debug_more, L_DEBUG, "Serialized %zu objects, %zu bytes for schema '%s'",
           ctx.objects_serialized, result.bytes_written, a_schema->name);
    
    return result;
}

/**
 * @brief Calculate size for raw serialization (fields only, no header)
 */
size_t dap_serialize_calc_size_raw(const dap_serialize_schema_t *a_schema,
                                   const dap_serialize_size_params_t *a_params,
                                   const void *a_object,
                                   void *a_context)
{
    if (!a_schema) {
        return 0;
    }
    
    // Share the size-calc recursion counter with dap_serialize_calc_size so
    // that mutually-recursive schemas cannot bypass the guard by alternating
    // calls between the two entry points (calc_size → nested → calc_size_raw).
    if (s_recursion_depth >= MAX_RECURSION_DEPTH) {
        log_it(L_ERROR, "Maximum recursion depth exceeded in calc_size_raw");
        return 0;
    }
    s_recursion_depth++;
    
    size_t l_total_size = 0;
    
    // Calculate size of each field (no header!)
    for (size_t i = 0; i < a_schema->field_count; i++) {
        const dap_serialize_field_t *field = &a_schema->fields[i];
        
        // Check condition
        if (a_object && !s_check_condition(field, a_object, a_context)) {
            continue;
        }
        
        size_t l_field_size = s_calc_field_size(field, a_object, a_params, i, a_context, a_schema);
        size_t l_next = 0;
        if (!s_safe_add_size(l_total_size, l_field_size, &l_next)) {
            log_it(L_ERROR, "calc_size_raw overflow for schema '%s' field '%s'",
                   a_schema->name ? a_schema->name : "?", field->name ? field->name : "?");
            s_recursion_depth--;
            return 0;
        }
        l_total_size = l_next;
    }

    s_recursion_depth--;
    return l_total_size;
}

/**
 * @brief Serialize object to buffer WITHOUT metadata header (raw fields only)
 */
dap_serialize_result_t dap_serialize_to_buffer_raw(const dap_serialize_schema_t *a_schema,
                                                   const void *a_object,
                                                   uint8_t *a_buffer,
                                                   size_t a_buffer_size,
                                                   void *a_context)
{
    dap_serialize_result_t result = {0};
    
    if (!a_schema || !a_object || !a_buffer) {
        result.error_code = DAP_SERIALIZE_ERROR_INVALID_SCHEMA;
        result.error_message = "Invalid parameters";
        return result;
    }
    
    // Validate schema magic: accept both standard and custom magic numbers
    // Standard magic (DAP_SERIALIZE_MAGIC_NUMBER) is always valid
    // Custom magic numbers (non-zero) are also valid for extended schemas
    if (a_schema->magic != DAP_SERIALIZE_MAGIC_NUMBER && a_schema->magic == 0) {
        result.error_code = DAP_SERIALIZE_ERROR_INVALID_SCHEMA;
        result.error_message = "Schema magic must be either DAP_SERIALIZE_MAGIC_NUMBER or custom (non-zero)";
        return result;
    }
    
    // Validate object if validation function provided
    if (a_schema->validate_func && !a_schema->validate_func(a_object)) {
        result.error_code = DAP_SERIALIZE_ERROR_INVALID_OBJECT;
        result.error_message = "Object validation failed";
        return result;
    }
    
    // Initialize context (NO HEADER!)
    dap_serialize_context_t ctx = {
        .buffer = a_buffer,
        .buffer_size = a_buffer_size,
        .offset = 0,
        .version = a_schema->version,
        .user_context = a_context,
        .is_deserializing = false,
        .objects_serialized = 0,
        .bytes_processed = 0,
        .current_schema = a_schema
    };

    // Serialize each field (NO HEADER!)
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
            return result;
        }
        
        ctx.objects_serialized++;
    }
    
    result.error_code = DAP_SERIALIZE_ERROR_SUCCESS;
    result.bytes_written = ctx.offset;
    
    return result;
}

/* Shared core for from_buffer_raw / from_buffer_raw_preserve. */
static dap_serialize_result_t s_from_buffer_raw_core(const dap_serialize_schema_t *a_schema,
                                                     const uint8_t *a_buffer,
                                                     size_t a_buffer_size,
                                                     void *a_object,
                                                     void *a_context,
                                                     bool a_zero_init)
{
    dap_serialize_result_t result = {0};

    if (!a_schema || !a_buffer || !a_object) {
        result.error_code = DAP_SERIALIZE_ERROR_INVALID_SCHEMA;
        result.error_message = "Invalid parameters";
        return result;
    }

    // Initialize context (NO HEADER PARSING!)
    dap_serialize_context_t ctx = {
        .buffer = (uint8_t*)a_buffer,
        .buffer_size = a_buffer_size,
        .offset = 0,
        .version = a_schema->version,
        .user_context = a_context,
        .is_deserializing = true,
        .objects_serialized = 0,
        .bytes_processed = 0,
        .current_schema = a_schema
    };

    // Initialize object memory ONLY when a_zero_init is true.
    // Default raw path leaves the object intact so partial schemas can
    // fill selected fields on a live structure (wallet, NO_COUNT_PREFIX, …).
    if (a_zero_init) {
        memset(a_object, 0, a_schema->struct_size);
    }

    // Deserialize each field (NO HEADER!)
    for (size_t i = 0; i < a_schema->field_count; i++) {
        const dap_serialize_field_t *field = &a_schema->fields[i];

        int l_field_result = s_deserialize_field(field, a_object, &ctx);
        if (l_field_result != 0) {
            result.error_code = l_field_result;
            result.error_message = "Field deserialization failed";
            result.failed_field = field->name;
            return result;
        }

        ctx.objects_serialized++;
    }

    result.error_code = DAP_SERIALIZE_ERROR_SUCCESS;
    result.bytes_read = ctx.offset;

    return result;
}

/**
 * @brief Deserialize object from buffer WITHOUT metadata header (raw fields only)
 *
 * Does NOT zero @p a_object — only schema fields are written.
 */
dap_serialize_result_t dap_serialize_from_buffer_raw(const dap_serialize_schema_t *a_schema,
                                                     const uint8_t *a_buffer,
                                                     size_t a_buffer_size,
                                                     void *a_object,
                                                     void *a_context)
{
    return s_from_buffer_raw_core(a_schema, a_buffer, a_buffer_size, a_object, a_context, false);
}

/**
 * @brief Raw deserialize with memset of @p a_object before decoding.
 */
dap_serialize_result_t dap_serialize_from_buffer_raw_zero(const dap_serialize_schema_t *a_schema,
                                                          const uint8_t *a_buffer,
                                                          size_t a_buffer_size,
                                                          void *a_object,
                                                          void *a_context)
{
    return s_from_buffer_raw_core(a_schema, a_buffer, a_buffer_size, a_object, a_context, true);
}

/**
 * @brief Alias of dap_serialize_from_buffer_raw (no zero-init).
 */
dap_serialize_result_t dap_serialize_from_buffer_raw_preserve(const dap_serialize_schema_t *a_schema,
                                                              const uint8_t *a_buffer,
                                                              size_t a_buffer_size,
                                                              void *a_object,
                                                              void *a_context)
{
    return s_from_buffer_raw_core(a_schema, a_buffer, a_buffer_size, a_object, a_context, false);
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
        .bytes_processed = 0,
        .current_schema = a_schema
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
    
    debug_if(s_debug_more, L_DEBUG, "Deserialized %zu objects, %zu bytes for schema '%s'",
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

                // Validate count_ptr before dereferencing.  Use the parent
                // schema's struct_size as the upper bound so structures
                // larger than the historical 4 KiB heuristic (e.g. those
                // embedding lattice polynomials) are accepted without a
                // false-positive warning that silently drops the count.
                const size_t l_struct_size = a_parent_schema ? a_parent_schema->struct_size : 4096u;
                if ((uintptr_t)l_count_ptr < (uintptr_t)a_object ||
                    (uintptr_t)l_count_ptr + sizeof(uint32_t) > (uintptr_t)a_object + l_struct_size) {
                    log_it(L_WARNING, "Array field '%s' count_ptr out of bounds (struct_size=%zu, count_offset=%zu), using 0",
                           a_field->name, l_struct_size, a_field->count_offset);
                    l_size = (a_field->flags & DAP_SERIALIZE_FLAG_NO_COUNT_PREFIX) ? 0u : sizeof(uint32_t);
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
                l_size = (a_field->flags & DAP_SERIALIZE_FLAG_NO_COUNT_PREFIX) ? 0u : sizeof(uint32_t);
                break;
            }


            debug_if(s_debug_more, L_DEBUG, "ARRAY_DYNAMIC count=%zu", l_count_value);

            l_size = (a_field->flags & DAP_SERIALIZE_FLAG_NO_COUNT_PREFIX) ? 0u : sizeof(uint32_t);
            
            // Calculate array element sizes
            size_t element_size = 0;
            if (a_field->nested_schema) {
                // For nested structures, calculate element size using nested schema
                debug_if(s_debug_more, L_DEBUG, "ARRAY_DYNAMIC nested schema calculation");
                
                // Guard against circular dependency
                if (a_field->nested_schema == a_parent_schema) {
                    debug_if(s_debug_more, L_DEBUG, "Circular dependency detected, using struct size");
                    element_size = a_field->nested_schema->struct_size;
                } else {
                    if (a_object && l_obj_ptr) {
                        // Object-based calculation: use first element as template for size calculation
                        const void **l_array_ptr = (const void**)(l_obj_ptr + a_field->offset);
                        if (*l_array_ptr && l_count_value > 0) {
                            const uint8_t *l_first_element = (const uint8_t*)*l_array_ptr;
                            // Nested elements are serialised as raw fields (no per-element
                            // schema header), so size estimation must use the *_raw variant
                            // to avoid double-counting the 12-byte header.
                            element_size = dap_serialize_calc_size_raw(a_field->nested_schema,
                                                                       NULL,
                                                                       l_first_element,
                                                                       a_context);
                        }
                    }

                    // If object-based calculation failed or no object, try parametric
                    if (element_size == 0 && a_params) {
                        element_size = dap_serialize_calc_size_raw(a_field->nested_schema,
                                                                   a_params,
                                                                   NULL,
                                                                   a_context);
                    }
                    
                    // Final fallback to struct size
                    if (element_size == 0) {
                        log_it(L_ERROR, "Failed to calculate nested schema size for field '%s', using struct size fallback", a_field->name);
                        element_size = a_field->nested_schema->struct_size;
                    }
                }
            } else {
                debug_if(s_debug_more, L_DEBUG, "ARRAY_DYNAMIC simple array: count=%zu, element_size=%zu", 
                         l_count_value, a_field->size);
                element_size = a_field->size;
            }
            
            // Overflow-safe total = element_size * count, then total += count_prefix.
            size_t array_total = 0;
            if (!s_safe_mul_size(element_size, l_count_value, &array_total)) {
                log_it(L_ERROR, "ARRAY_DYNAMIC field '%s' size overflow (elem=%zu * count=%zu)",
                       a_field->name, element_size, l_count_value);
                l_size = 0;
                break;
            }
            size_t combined = 0;
            if (!s_safe_add_size(l_size, array_total, &combined)) {
                log_it(L_ERROR, "ARRAY_DYNAMIC field '%s' size overflow (prefix+array)",
                       a_field->name);
                l_size = 0;
                break;
            }
            l_size = combined;
            break;
        }
        case DAP_SERIALIZE_TYPE_ARRAY_FIXED: {
            // No count prefix on wire — count is known from schema.
            size_t count = a_field->fixed_count;

            // Reject obviously malicious / mis-configured schemas early.
            if (count > DAP_SERIALIZE_MAX_ARRAY_COUNT) {
                log_it(L_ERROR, "ARRAY_FIXED field '%s' has count=%zu exceeding limit %d",
                       a_field->name, count, DAP_SERIALIZE_MAX_ARRAY_COUNT);
                l_size = 0;
                break;
            }

            size_t element_size = 0;
            if (a_field->nested_schema) {
                // Guard against circular self-reference
                if (a_field->nested_schema == a_parent_schema) {
                    debug_if(s_debug_more, L_DEBUG, "ARRAY_FIXED circular ref, using struct_size");
                    element_size = a_field->nested_schema->struct_size;
                } else {
                    if (l_obj_ptr && count > 0) {
                        const uint8_t *l_array = l_obj_ptr + a_field->offset;
                        element_size = dap_serialize_calc_size_raw(a_field->nested_schema,
                                                                   NULL,
                                                                   l_array,
                                                                   a_context);
                    }
                    if (element_size == 0) {
                        element_size = a_field->nested_schema->struct_size;
                    }
                }
            } else {
                element_size = a_field->size;
            }

            if (!s_safe_mul_size(element_size, count, &l_size)) {
                log_it(L_ERROR, "ARRAY_FIXED field '%s' size overflow (elem=%zu * count=%zu)",
                       a_field->name, element_size, count);
                l_size = 0;
            }
            break;
        }
        case DAP_SERIALIZE_TYPE_NESTED_STRUCT: {
            // Embedded by-value struct — serialized flat through nested schema
            if (!a_field->nested_schema) {
                log_it(L_ERROR, "NESTED_STRUCT field '%s' has no nested_schema", a_field->name);
                l_size = 0;
                break;
            }
            if (a_field->nested_schema == a_parent_schema) {
                debug_if(s_debug_more, L_DEBUG, "NESTED_STRUCT circular ref, using struct_size");
                l_size = a_field->nested_schema->struct_size;
                break;
            }
            if (l_obj_ptr) {
                const uint8_t *l_nested = l_obj_ptr + a_field->offset;
                l_size = dap_serialize_calc_size_raw(a_field->nested_schema, NULL, l_nested, a_context);
                if (l_size == 0) {
                    l_size = a_field->nested_schema->struct_size;
                }
            } else {
                // Params path — conservative estimate via struct_size
                l_size = a_field->nested_schema->struct_size;
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
    
    // Check buffer space for all fields except ones with nested schemas (per-element checks apply)
    bool l_skip_precheck = (a_field->nested_schema != NULL) &&
                           (a_field->type == DAP_SERIALIZE_TYPE_ARRAY_DYNAMIC ||
                            a_field->type == DAP_SERIALIZE_TYPE_ARRAY_FIXED ||
                            a_field->type == DAP_SERIALIZE_TYPE_NESTED_STRUCT);
    if (!l_skip_precheck) {
        size_t l_field_size = s_calc_field_size(a_field, a_object, NULL, 0, a_ctx->user_context, a_ctx->current_schema);
        debug_if(s_debug_more, L_DEBUG, "s_calc_field_size returned: %zu for field '%s'",
                 l_field_size, a_field->name);
        
        if (a_ctx->offset + l_field_size > a_ctx->buffer_size) {
            log_it(L_ERROR, "Buffer too small for field '%s': offset=%zu + field_size=%zu > buffer_size=%zu", 
                   a_field->name, a_ctx->offset, l_field_size, a_ctx->buffer_size);
            return DAP_SERIALIZE_ERROR_BUFFER_TOO_SMALL;
        }
    }

    /* Constant fields: write const_value, ignore object storage */
    if (a_field->flags & DAP_SERIALIZE_FLAG_CONST) {
        switch (a_field->type) {
            case DAP_SERIALIZE_TYPE_UINT8:
            case DAP_SERIALIZE_TYPE_INT8:
            case DAP_SERIALIZE_TYPE_BOOL: {
                if (a_ctx->offset + 1 > a_ctx->buffer_size)
                    return DAP_SERIALIZE_ERROR_BUFFER_TOO_SMALL;
                a_ctx->buffer[a_ctx->offset] = (uint8_t)a_field->const_value;
                a_ctx->offset += 1;
                return DAP_SERIALIZE_ERROR_SUCCESS;
            }
            case DAP_SERIALIZE_TYPE_UINT16:
            case DAP_SERIALIZE_TYPE_INT16: {
                if (a_ctx->offset + 2 > a_ctx->buffer_size)
                    return DAP_SERIALIZE_ERROR_BUFFER_TOO_SMALL;
                s_write_uint16_le(a_ctx->buffer + a_ctx->offset, (uint16_t)a_field->const_value);
                a_ctx->offset += 2;
                return DAP_SERIALIZE_ERROR_SUCCESS;
            }
            case DAP_SERIALIZE_TYPE_UINT32:
            case DAP_SERIALIZE_TYPE_INT32:
            case DAP_SERIALIZE_TYPE_VERSION: {
                if (a_ctx->offset + 4 > a_ctx->buffer_size)
                    return DAP_SERIALIZE_ERROR_BUFFER_TOO_SMALL;
                s_write_uint32_le(a_ctx->buffer + a_ctx->offset, (uint32_t)a_field->const_value);
                a_ctx->offset += 4;
                return DAP_SERIALIZE_ERROR_SUCCESS;
            }
            case DAP_SERIALIZE_TYPE_UINT64:
            case DAP_SERIALIZE_TYPE_INT64: {
                if (a_ctx->offset + 8 > a_ctx->buffer_size)
                    return DAP_SERIALIZE_ERROR_BUFFER_TOO_SMALL;
                s_write_uint64_le(a_ctx->buffer + a_ctx->offset, a_field->const_value);
                a_ctx->offset += 8;
                return DAP_SERIALIZE_ERROR_SUCCESS;
            }
            default:
                log_it(L_ERROR, "CONST flag unsupported for field '%s' type=%d",
                       a_field->name, a_field->type);
                return DAP_SERIALIZE_ERROR_INVALID_SCHEMA;
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
            // Use memcpy to avoid alignment issues
            uint16_t value;
            memcpy(&value, obj_ptr + a_field->offset, sizeof(uint16_t));
            s_write_uint16_le(a_ctx->buffer + a_ctx->offset, value);
            a_ctx->offset += 2;
            break;
        }
        case DAP_SERIALIZE_TYPE_UINT32:
        case DAP_SERIALIZE_TYPE_INT32: {
            // Use memcpy to avoid alignment issues
            uint32_t value;
            memcpy(&value, obj_ptr + a_field->offset, sizeof(uint32_t));
            s_write_uint32_le(a_ctx->buffer + a_ctx->offset, value);
            a_ctx->offset += 4;
            break;
        }
        case DAP_SERIALIZE_TYPE_UINT64:
        case DAP_SERIALIZE_TYPE_INT64: {
            // Use memcpy to avoid alignment issues
            uint64_t value;
            memcpy(&value, obj_ptr + a_field->offset, sizeof(uint64_t));
            s_write_uint64_le(a_ctx->buffer + a_ctx->offset, value);
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
            
            // ALWAYS use size from object when serializing
            // Parametric functions are only for buffer size calculation before object exists
            size_t actual_size = *l_size_ptr;
            
            debug_if(s_debug_more, L_DEBUG, "BYTES_DYNAMIC field '%s': data_ptr=%p, size=%zu", 
                     a_field->name, *l_data_ptr, actual_size);
            
            // Robust validation for BYTES_DYNAMIC fields
            if (actual_size > 0 && !*l_data_ptr) {
                log_it(L_WARNING, "BYTES_DYNAMIC field '%s' has NULL data pointer but non-zero size %zu, writing zeros", 
                       a_field->name, actual_size);
                // Don't fail - write zeros instead for robustness
            }
            
            // Validate size is reasonable
            if (actual_size > DAP_SERIALIZE_MAX_DYNAMIC_PAYLOAD) {
                log_it(L_ERROR, "BYTES_DYNAMIC field '%s' has unreasonable size %zu (max: %u)",
                       a_field->name, actual_size, (unsigned)DAP_SERIALIZE_MAX_DYNAMIC_PAYLOAD);
                return DAP_SERIALIZE_ERROR_FIELD_VALIDATION;
            }
            
            // Check buffer space with overflow-safe arithmetic.
            size_t l_prefix_and_data = 0;
            size_t l_end_off = 0;
            if (!s_safe_add_size(sizeof(uint32_t), actual_size, &l_prefix_and_data) ||
                !s_safe_add_size(a_ctx->offset, l_prefix_and_data, &l_end_off)) {
                log_it(L_ERROR, "BYTES_DYNAMIC field '%s' size overflow (offset=%zu size=%zu)",
                       a_field->name, a_ctx->offset, actual_size);
                return DAP_SERIALIZE_ERROR_BUFFER_TOO_SMALL;
            }
            if (l_end_off > a_ctx->buffer_size) {
                log_it(L_ERROR, "Buffer too small for field '%s': offset=%zu + field_size=%zu > buffer_size=%zu",
                       a_field->name, a_ctx->offset, l_prefix_and_data, a_ctx->buffer_size);
                return DAP_SERIALIZE_ERROR_BUFFER_TOO_SMALL;
            }
            
            // Write size prefix
            s_write_uint32_le(a_ctx->buffer + a_ctx->offset, (uint32_t)actual_size);
            a_ctx->offset += sizeof(uint32_t);
            
            // Write data
            if (*l_data_ptr && actual_size > 0) {
                memcpy(a_ctx->buffer + a_ctx->offset, *l_data_ptr, actual_size);
            } else if (actual_size > 0) {
                // Write zeros if data is NULL but size is non-zero (for robustness)
                memset(a_ctx->buffer + a_ctx->offset, 0, actual_size);
            }
            a_ctx->offset += actual_size;
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
            const bool l_no_prefix = (a_field->flags & DAP_SERIALIZE_FLAG_NO_COUNT_PREFIX) != 0;
            
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
            
            if (!l_no_prefix) {
                // Check buffer space for count prefix
                if (a_ctx->offset + sizeof(uint32_t) > a_ctx->buffer_size) {
                    log_it(L_ERROR, "Buffer overflow in ARRAY_DYNAMIC field '%s' count: offset=%zu + 4 > buffer_size=%zu",
                           a_field->name, a_ctx->offset, a_ctx->buffer_size);
                    return DAP_SERIALIZE_ERROR_BUFFER_TOO_SMALL;
                }

                // Write count prefix
                s_write_uint32_le(a_ctx->buffer + a_ctx->offset, l_count_value_u32);
                a_ctx->offset += sizeof(uint32_t);
            }
            
            // For nested arrays, rely on per-field checks during element serialization.
            // For simple arrays, pre-check aggregated size with overflow-safe math.
            if (!a_field->nested_schema) {
                size_t l_array_data_size = 0;
                size_t l_end_off = 0;
                if (!s_safe_mul_size((size_t)l_count_value_u32, a_field->size, &l_array_data_size) ||
                    !s_safe_add_size(a_ctx->offset, l_array_data_size, &l_end_off)) {
                    log_it(L_ERROR, "ARRAY_DYNAMIC field '%s' size arithmetic overflow (count=%u size=%zu)",
                           a_field->name, l_count_value_u32, a_field->size);
                    return DAP_SERIALIZE_ERROR_BUFFER_TOO_SMALL;
                }
                if (l_end_off > a_ctx->buffer_size) {
                    log_it(L_ERROR, "Buffer overflow in ARRAY_DYNAMIC field '%s' data: offset=%zu + array_size=%zu > buffer_size=%zu",
                           a_field->name, a_ctx->offset, l_array_data_size, a_ctx->buffer_size);
                    return DAP_SERIALIZE_ERROR_BUFFER_TOO_SMALL;
                }
            }
            
            // Serialize array elements
            if (*l_array_ptr && l_count_value_u32 > 0) {
                if (a_field->nested_schema) {
                    // Nested structures - validate array pointer and guard recursion
                    const uint8_t *l_element_ptr = (const uint8_t*)*l_array_ptr;
                    
                    if (!l_element_ptr) {
                        log_it(L_WARNING, "Array field '%s' has NULL data pointer but non-zero count %u",
                               a_field->name, l_count_value_u32);
                        if (l_no_prefix) {
                            // No prefix to rewrite — caller framing already
                            // committed the count.  Treat as a hard error
                            // rather than silently emit a desynchronised blob.
                            return DAP_SERIALIZE_ERROR_INVALID_OBJECT;
                        }
                        // Rewrite count prefix as 0 and stop — byte buffer is trusted on sender side.
                        s_write_uint32_le(a_ctx->buffer + a_ctx->offset - sizeof(uint32_t), 0);
                        return DAP_SERIALIZE_ERROR_SUCCESS;
                    }
                    
                    if (s_field_nesting_depth >= DAP_SERIALIZE_MAX_FIELD_NESTING) {
                        log_it(L_ERROR, "ARRAY_DYNAMIC '%s' nesting depth exceeded", a_field->name);
                        return DAP_SERIALIZE_ERROR_INVALID_SCHEMA;
                    }
                    s_field_nesting_depth++;
                    const dap_serialize_schema_t *l_saved_schema = a_ctx->current_schema;
                    a_ctx->current_schema = a_field->nested_schema;

                    for (size_t i = 0; i < l_count_value_u32; i++) {
                        size_t elem_off = 0;
                        if (!s_safe_mul_size(i, a_field->nested_schema->struct_size, &elem_off)) {
                            a_ctx->current_schema = l_saved_schema;
                            s_field_nesting_depth--;
                            log_it(L_ERROR, "ARRAY_DYNAMIC '%s' element offset overflow at i=%zu", a_field->name, i);
                            return DAP_SERIALIZE_ERROR_FIELD_VALIDATION;
                        }
                        const uint8_t *l_current_element = l_element_ptr + elem_off;

                        for (size_t f = 0; f < a_field->nested_schema->field_count; f++) {
                            const dap_serialize_field_t *l_nested_field = &a_field->nested_schema->fields[f];

                            if (!s_check_condition(l_nested_field, l_current_element, a_ctx->user_context)) {
                                continue;
                            }

                            int l_nested_result = s_serialize_field(l_nested_field, l_current_element, a_ctx);
                            if (l_nested_result != 0) {
                                a_ctx->current_schema = l_saved_schema;
                                s_field_nesting_depth--;
                                return l_nested_result;
                            }
                        }
                    }
                    a_ctx->current_schema = l_saved_schema;
                    s_field_nesting_depth--;
                } else {
                    // Simple array of fixed-size elements — size already validated above.
                    size_t l_total_size = 0;
                    (void)s_safe_mul_size((size_t)l_count_value_u32, a_field->size, &l_total_size);
                    
                    if (!*l_array_ptr && l_total_size > 0) {
                        log_it(L_WARNING, "Array field '%s' has NULL data pointer but non-zero size %zu", 
                               a_field->name, l_total_size);
                        memset(a_ctx->buffer + a_ctx->offset, 0, l_total_size);
                    } else {
                        memcpy(a_ctx->buffer + a_ctx->offset, *l_array_ptr, l_total_size);
                    }
                    a_ctx->offset += l_total_size;
                }
            }
            break;
        }
        case DAP_SERIALIZE_TYPE_ARRAY_FIXED: {
            size_t count = a_field->fixed_count;
            if (count > DAP_SERIALIZE_MAX_ARRAY_COUNT) {
                log_it(L_ERROR, "ARRAY_FIXED '%s' count=%zu exceeds limit %d",
                       a_field->name, count, DAP_SERIALIZE_MAX_ARRAY_COUNT);
                return DAP_SERIALIZE_ERROR_FIELD_VALIDATION;
            }
            const uint8_t *l_array = obj_ptr + a_field->offset;
            if (a_field->nested_schema) {
                if (s_field_nesting_depth >= DAP_SERIALIZE_MAX_FIELD_NESTING) {
                    log_it(L_ERROR, "ARRAY_FIXED '%s' nesting depth exceeded", a_field->name);
                    return DAP_SERIALIZE_ERROR_INVALID_SCHEMA;
                }
                s_field_nesting_depth++;
                const dap_serialize_schema_t *ns = a_field->nested_schema;
                const dap_serialize_schema_t *l_saved_schema = a_ctx->current_schema;
                a_ctx->current_schema = ns;
                for (size_t i = 0; i < count; i++) {
                    size_t elem_off;
                    if (!s_safe_mul_size(i, ns->struct_size, &elem_off)) {
                        a_ctx->current_schema = l_saved_schema;
                        s_field_nesting_depth--;
                        return DAP_SERIALIZE_ERROR_FIELD_VALIDATION;
                    }
                    const uint8_t *l_elem = l_array + elem_off;
                    for (size_t f = 0; f < ns->field_count; f++) {
                        const dap_serialize_field_t *nf = &ns->fields[f];
                        if (!s_check_condition(nf, l_elem, a_ctx->user_context)) {
                            continue;
                        }
                        int r = s_serialize_field(nf, l_elem, a_ctx);
                        if (r != 0) {
                            a_ctx->current_schema = l_saved_schema;
                            s_field_nesting_depth--;
                            return r;
                        }
                    }
                }
                a_ctx->current_schema = l_saved_schema;
                s_field_nesting_depth--;
            } else {
                // Scalar element array — choose encoder based on element_type
                dap_serialize_field_type_t et = a_field->element_type;
                size_t elem_size = a_field->size;
                size_t total = 0, end_off = 0;
                if (!s_safe_mul_size(count, elem_size, &total) ||
                    !s_safe_add_size(a_ctx->offset, total, &end_off)) {
                    log_it(L_ERROR, "ARRAY_FIXED '%s' size arithmetic overflow", a_field->name);
                    return DAP_SERIALIZE_ERROR_BUFFER_TOO_SMALL;
                }
                if (end_off > a_ctx->buffer_size) {
                    log_it(L_ERROR, "Buffer overflow in ARRAY_FIXED field '%s': offset=%zu + total=%zu > buffer_size=%zu",
                           a_field->name, a_ctx->offset, total, a_ctx->buffer_size);
                    return DAP_SERIALIZE_ERROR_BUFFER_TOO_SMALL;
                }
                switch (et) {
                    case DAP_SERIALIZE_TYPE_UINT16:
                    case DAP_SERIALIZE_TYPE_INT16: {
                        if (elem_size != 2) {
                            log_it(L_ERROR, "ARRAY_FIXED '%s': element_type UINT16/INT16 but size=%zu", a_field->name, elem_size);
                            return DAP_SERIALIZE_ERROR_FIELD_VALIDATION;
                        }
                        for (size_t i = 0; i < count; i++) {
                            uint16_t v;
                            memcpy(&v, l_array + i * 2, 2);
                            s_write_uint16_le(a_ctx->buffer + a_ctx->offset, v);
                            a_ctx->offset += 2;
                        }
                        break;
                    }
                    case DAP_SERIALIZE_TYPE_UINT32:
                    case DAP_SERIALIZE_TYPE_INT32:
                    case DAP_SERIALIZE_TYPE_FLOAT32: {
                        if (elem_size != 4) {
                            log_it(L_ERROR, "ARRAY_FIXED '%s': element_type 32-bit but size=%zu", a_field->name, elem_size);
                            return DAP_SERIALIZE_ERROR_FIELD_VALIDATION;
                        }
                        for (size_t i = 0; i < count; i++) {
                            uint32_t v;
                            memcpy(&v, l_array + i * 4, 4);
                            s_write_uint32_le(a_ctx->buffer + a_ctx->offset, v);
                            a_ctx->offset += 4;
                        }
                        break;
                    }
                    case DAP_SERIALIZE_TYPE_UINT64:
                    case DAP_SERIALIZE_TYPE_INT64:
                    case DAP_SERIALIZE_TYPE_FLOAT64: {
                        if (elem_size != 8) {
                            log_it(L_ERROR, "ARRAY_FIXED '%s': element_type 64-bit but size=%zu", a_field->name, elem_size);
                            return DAP_SERIALIZE_ERROR_FIELD_VALIDATION;
                        }
                        for (size_t i = 0; i < count; i++) {
                            uint64_t v;
                            memcpy(&v, l_array + i * 8, 8);
                            s_write_uint64_le(a_ctx->buffer + a_ctx->offset, v);
                            a_ctx->offset += 8;
                        }
                        break;
                    }
                    case DAP_SERIALIZE_TYPE_UINT8:
                    case DAP_SERIALIZE_TYPE_INT8:
                    case DAP_SERIALIZE_TYPE_BOOL:
                    default: {
                        // Raw byte-level copy (legacy path).  Used also when element_type is not set.
                        memcpy(a_ctx->buffer + a_ctx->offset, l_array, total);
                        a_ctx->offset += total;
                        break;
                    }
                }
            }
            break;
        }
        case DAP_SERIALIZE_TYPE_NESTED_STRUCT: {
            if (!a_field->nested_schema) {
                log_it(L_ERROR, "NESTED_STRUCT field '%s' has no nested_schema", a_field->name);
                return DAP_SERIALIZE_ERROR_INVALID_SCHEMA;
            }
            if (s_field_nesting_depth >= DAP_SERIALIZE_MAX_FIELD_NESTING) {
                log_it(L_ERROR, "NESTED_STRUCT '%s' nesting depth exceeded", a_field->name);
                return DAP_SERIALIZE_ERROR_INVALID_SCHEMA;
            }
            s_field_nesting_depth++;
            const dap_serialize_schema_t *ns = a_field->nested_schema;
            const uint8_t *l_nested = obj_ptr + a_field->offset;
            const dap_serialize_schema_t *l_saved_schema = a_ctx->current_schema;
            a_ctx->current_schema = ns;
            for (size_t f = 0; f < ns->field_count; f++) {
                const dap_serialize_field_t *nf = &ns->fields[f];
                if (!s_check_condition(nf, l_nested, a_ctx->user_context)) {
                    continue;
                }
                int r = s_serialize_field(nf, l_nested, a_ctx);
                if (r != 0) {
                    a_ctx->current_schema = l_saved_schema;
                    s_field_nesting_depth--;
                    return r;
                }
            }
            a_ctx->current_schema = l_saved_schema;
            s_field_nesting_depth--;
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

    /* Constant fields: verify wire value against const_value, do not store */
    if (a_field->flags & DAP_SERIALIZE_FLAG_CONST) {
        switch (a_field->type) {
            case DAP_SERIALIZE_TYPE_UINT8:
            case DAP_SERIALIZE_TYPE_INT8:
            case DAP_SERIALIZE_TYPE_BOOL: {
                if (a_ctx->offset + 1 > a_ctx->buffer_size)
                    return DAP_SERIALIZE_ERROR_INVALID_DATA;
                uint8_t value = a_ctx->buffer[a_ctx->offset];
                a_ctx->offset += 1;
                if (value != (uint8_t)a_field->const_value) {
                    log_it(L_ERROR, "CONST field '%s' mismatch: got 0x%02x, expected 0x%02x",
                           a_field->name, value, (uint8_t)a_field->const_value);
                    return DAP_SERIALIZE_ERROR_FIELD_VALIDATION;
                }
                return DAP_SERIALIZE_ERROR_SUCCESS;
            }
            case DAP_SERIALIZE_TYPE_UINT16:
            case DAP_SERIALIZE_TYPE_INT16: {
                if (a_ctx->offset + 2 > a_ctx->buffer_size)
                    return DAP_SERIALIZE_ERROR_INVALID_DATA;
                uint16_t value = s_read_uint16_le(a_ctx->buffer + a_ctx->offset);
                a_ctx->offset += 2;
                if (value != (uint16_t)a_field->const_value) {
                    log_it(L_ERROR, "CONST field '%s' mismatch: got 0x%04x, expected 0x%04x",
                           a_field->name, value, (uint16_t)a_field->const_value);
                    return DAP_SERIALIZE_ERROR_FIELD_VALIDATION;
                }
                return DAP_SERIALIZE_ERROR_SUCCESS;
            }
            case DAP_SERIALIZE_TYPE_UINT32:
            case DAP_SERIALIZE_TYPE_INT32:
            case DAP_SERIALIZE_TYPE_VERSION: {
                if (a_ctx->offset + 4 > a_ctx->buffer_size)
                    return DAP_SERIALIZE_ERROR_INVALID_DATA;
                uint32_t value = s_read_uint32_le(a_ctx->buffer + a_ctx->offset);
                a_ctx->offset += 4;
                if (value != (uint32_t)a_field->const_value) {
                    log_it(L_ERROR, "CONST field '%s' mismatch: got 0x%08x, expected 0x%08x",
                           a_field->name, value, (uint32_t)a_field->const_value);
                    return DAP_SERIALIZE_ERROR_FIELD_VALIDATION;
                }
                return DAP_SERIALIZE_ERROR_SUCCESS;
            }
            case DAP_SERIALIZE_TYPE_UINT64:
            case DAP_SERIALIZE_TYPE_INT64: {
                if (a_ctx->offset + 8 > a_ctx->buffer_size)
                    return DAP_SERIALIZE_ERROR_INVALID_DATA;
                uint64_t value = s_read_uint64_le(a_ctx->buffer + a_ctx->offset);
                a_ctx->offset += 8;
                if (value != a_field->const_value) {
                    log_it(L_ERROR, "CONST field '%s' mismatch: got 0x%"DAP_UINT64_FORMAT_X
                           ", expected 0x%"DAP_UINT64_FORMAT_X,
                           a_field->name, value, a_field->const_value);
                    return DAP_SERIALIZE_ERROR_FIELD_VALIDATION;
                }
                return DAP_SERIALIZE_ERROR_SUCCESS;
            }
            default:
                log_it(L_ERROR, "CONST flag unsupported for field '%s' type=%d",
                       a_field->name, a_field->type);
                return DAP_SERIALIZE_ERROR_INVALID_SCHEMA;
        }
    }
    
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
            // Use memcpy to avoid alignment issues
            uint16_t value = s_read_uint16_le(a_ctx->buffer + a_ctx->offset);
            memcpy(obj_ptr + a_field->offset, &value, sizeof(uint16_t));
            a_ctx->offset += 2;
            break;
        }
        case DAP_SERIALIZE_TYPE_UINT32:
        case DAP_SERIALIZE_TYPE_INT32:
        case DAP_SERIALIZE_TYPE_VERSION: {
            if (a_ctx->offset + 4 > a_ctx->buffer_size) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            // Use memcpy to avoid alignment issues
            uint32_t value = s_read_uint32_le(a_ctx->buffer + a_ctx->offset);
            memcpy(obj_ptr + a_field->offset, &value, sizeof(uint32_t));
            a_ctx->offset += 4;
            break;
        }
        case DAP_SERIALIZE_TYPE_UINT64:
        case DAP_SERIALIZE_TYPE_INT64: {
            if (a_ctx->offset + 8 > a_ctx->buffer_size) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            // Use memcpy to avoid alignment issues
            uint64_t value = s_read_uint64_le(a_ctx->buffer + a_ctx->offset);
            memcpy(obj_ptr + a_field->offset, &value, sizeof(uint64_t));
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
            size_t l_end_hdr = 0;
            if (!s_safe_add_size(a_ctx->offset, sizeof(uint32_t), &l_end_hdr) ||
                l_end_hdr > a_ctx->buffer_size) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            
            // Read size
            uint32_t size = s_read_uint32_le(a_ctx->buffer + a_ctx->offset);
            a_ctx->offset += sizeof(uint32_t);
            
            // Reject clearly malicious length prefixes before touching memory.
            if (size > DAP_SERIALIZE_MAX_DYNAMIC_PAYLOAD) {
                log_it(L_ERROR, "BYTES_DYNAMIC field '%s' length %u exceeds max payload %u",
                       a_field->name, size, (unsigned)DAP_SERIALIZE_MAX_DYNAMIC_PAYLOAD);
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            
            size_t l_end_data = 0;
            if (!s_safe_add_size(a_ctx->offset, size, &l_end_data) ||
                l_end_data > a_ctx->buffer_size) {
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
            size_t l_end_hdr = 0;
            if (!s_safe_add_size(a_ctx->offset, sizeof(uint32_t), &l_end_hdr) ||
                l_end_hdr > a_ctx->buffer_size) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            
            // Read length
            uint32_t length = s_read_uint32_le(a_ctx->buffer + a_ctx->offset);
            a_ctx->offset += sizeof(uint32_t);
            
            if (length > DAP_SERIALIZE_MAX_DYNAMIC_PAYLOAD) {
                log_it(L_ERROR, "STRING_DYNAMIC field '%s' length %u exceeds max payload %u",
                       a_field->name, length, (unsigned)DAP_SERIALIZE_MAX_DYNAMIC_PAYLOAD);
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            
            size_t l_end_data = 0;
            if (!s_safe_add_size(a_ctx->offset, length, &l_end_data) ||
                l_end_data > a_ctx->buffer_size) {
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
            size_t l_end = 0;
            if (!s_safe_add_size(a_ctx->offset, a_field->size, &l_end) ||
                l_end > a_ctx->buffer_size) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            uint8_t *value = (uint8_t*)(obj_ptr + a_field->offset);
            memcpy(value, a_ctx->buffer + a_ctx->offset, a_field->size);
            a_ctx->offset += a_field->size;
            break;
        }
        case DAP_SERIALIZE_TYPE_STRING_FIXED: {
            size_t l_end = 0;
            if (!s_safe_add_size(a_ctx->offset, a_field->size, &l_end) ||
                l_end > a_ctx->buffer_size) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            char *value = (char*)(obj_ptr + a_field->offset);
            memcpy(value, a_ctx->buffer + a_ctx->offset, a_field->size);
            a_ctx->offset += a_field->size;
            break;
        }
        case DAP_SERIALIZE_TYPE_ARRAY_DYNAMIC: {
            uint8_t *obj_ptr = (uint8_t*)a_object;
            const bool l_no_prefix = (a_field->flags & DAP_SERIALIZE_FLAG_NO_COUNT_PREFIX) != 0;
            uint32_t count = 0;

            if (l_no_prefix) {
                // Count is supplied via count_offset by the caller — no prefix on wire.
                // count_offset == 0 is a *valid* layout (the count field can legitimately
                // be the first member), so we cannot reject it on that basis.  The
                // caller's contract is to ensure the count slot has been populated
                // before calling from_buffer_raw_preserve().
                const uint32_t *l_count_src = (const uint32_t*)(obj_ptr + a_field->count_offset);
                count = *l_count_src;
            } else {
                // Read count prefix
                size_t l_end_hdr = 0;
                if (!s_safe_add_size(a_ctx->offset, sizeof(uint32_t), &l_end_hdr) ||
                    l_end_hdr > a_ctx->buffer_size) {
                    return DAP_SERIALIZE_ERROR_INVALID_DATA;
                }
                count = s_read_uint32_le(a_ctx->buffer + a_ctx->offset);
                a_ctx->offset += sizeof(uint32_t);
            }

            if (count > DAP_SERIALIZE_MAX_ARRAY_COUNT) {
                log_it(L_ERROR, "ARRAY_DYNAMIC field '%s' count %u exceeds limit %d",
                       a_field->name, count, DAP_SERIALIZE_MAX_ARRAY_COUNT);
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }

            // Store count if needed (only when prefix was on the wire).
            if (!l_no_prefix && a_field->count_offset) {
                uint32_t *count_ptr = (uint32_t*)(obj_ptr + a_field->count_offset);
                *count_ptr = count;
            }

            void **array_ptr = (void**)(obj_ptr + a_field->offset);
            *array_ptr = NULL;

            if (count == 0) {
                break;
            }

            if (!a_field->nested_schema) {
                // Simple array — overflow-safe size math + sanity cap on total.
                size_t total_size = 0;
                size_t l_end_data = 0;
                if (!s_safe_mul_size((size_t)count, a_field->size, &total_size) ||
                    !s_safe_add_size(a_ctx->offset, total_size, &l_end_data)) {
                    log_it(L_ERROR, "ARRAY_DYNAMIC field '%s' simple-size overflow (count=%u elem=%zu)",
                           a_field->name, count, a_field->size);
                    return DAP_SERIALIZE_ERROR_INVALID_DATA;
                }
                if (total_size > DAP_SERIALIZE_MAX_DYNAMIC_PAYLOAD) {
                    log_it(L_ERROR, "ARRAY_DYNAMIC field '%s' simple-size %zu exceeds max payload %u",
                           a_field->name, total_size, (unsigned)DAP_SERIALIZE_MAX_DYNAMIC_PAYLOAD);
                    return DAP_SERIALIZE_ERROR_INVALID_DATA;
                }
                if (l_end_data > a_ctx->buffer_size) {
                    return DAP_SERIALIZE_ERROR_INVALID_DATA;
                }
                *array_ptr = DAP_NEW_SIZE(uint8_t, total_size);
                if (!*array_ptr) {
                    return DAP_SERIALIZE_ERROR_MEMORY_ALLOCATION;
                }
                memcpy(*array_ptr, a_ctx->buffer + a_ctx->offset, total_size);
                a_ctx->offset += total_size;
            } else {
                // Nested structures: allocate contiguous array and deserialize each.
                if (s_field_nesting_depth >= DAP_SERIALIZE_MAX_FIELD_NESTING) {
                    log_it(L_ERROR, "ARRAY_DYNAMIC '%s' nesting depth exceeded", a_field->name);
                    return DAP_SERIALIZE_ERROR_INVALID_SCHEMA;
                }
                const dap_serialize_schema_t *ns = a_field->nested_schema;
                size_t element_size = ns->struct_size;
                size_t total_size = 0;
                if (!s_safe_mul_size((size_t)count, element_size, &total_size)) {
                    log_it(L_ERROR, "ARRAY_DYNAMIC field '%s' nested-size overflow (count=%u struct=%zu)",
                           a_field->name, count, element_size);
                    return DAP_SERIALIZE_ERROR_INVALID_DATA;
                }
                // Cap post-deserialization memory footprint.  Wire data ≤ buffer_size
                // but struct representation can be bigger; still enforce absolute ceiling.
                if (total_size > DAP_SERIALIZE_MAX_DYNAMIC_PAYLOAD) {
                    log_it(L_ERROR, "ARRAY_DYNAMIC field '%s' nested-size %zu exceeds max payload %u",
                           a_field->name, total_size, (unsigned)DAP_SERIALIZE_MAX_DYNAMIC_PAYLOAD);
                    return DAP_SERIALIZE_ERROR_INVALID_DATA;
                }
                *array_ptr = DAP_NEW_Z_SIZE(uint8_t, total_size);
                if (!*array_ptr) {
                    return DAP_SERIALIZE_ERROR_MEMORY_ALLOCATION;
                }
                s_field_nesting_depth++;
                const dap_serialize_schema_t *l_saved_schema = a_ctx->current_schema;
                a_ctx->current_schema = ns;
                for (size_t i = 0; i < count; i++) {
                    size_t elem_off = 0;
                    if (!s_safe_mul_size(i, element_size, &elem_off)) {
                        a_ctx->current_schema = l_saved_schema;
                        s_field_nesting_depth--;
                        DAP_DELETE(*array_ptr);
                        *array_ptr = NULL;
                        return DAP_SERIALIZE_ERROR_INVALID_DATA;
                    }
                    uint8_t *element_obj = (uint8_t*)(*array_ptr) + elem_off;
                    for (size_t f = 0; f < ns->field_count; f++) {
                        const dap_serialize_field_t *nf = &ns->fields[f];
                        int r = s_deserialize_field(nf, element_obj, a_ctx);
                        if (r != 0) {
                            a_ctx->current_schema = l_saved_schema;
                            s_field_nesting_depth--;
                            return r;
                        }
                    }
                }
                a_ctx->current_schema = l_saved_schema;
                s_field_nesting_depth--;
            }
            break;
        }
        case DAP_SERIALIZE_TYPE_ARRAY_FIXED: {
            size_t count = a_field->fixed_count;
            if (count > DAP_SERIALIZE_MAX_ARRAY_COUNT) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            uint8_t *l_array = obj_ptr + a_field->offset;
            if (a_field->nested_schema) {
                if (s_field_nesting_depth >= DAP_SERIALIZE_MAX_FIELD_NESTING) {
                    return DAP_SERIALIZE_ERROR_INVALID_SCHEMA;
                }
                s_field_nesting_depth++;
                const dap_serialize_schema_t *ns = a_field->nested_schema;
                const dap_serialize_schema_t *l_saved_schema = a_ctx->current_schema;
                a_ctx->current_schema = ns;
                for (size_t i = 0; i < count; i++) {
                    size_t elem_off;
                    if (!s_safe_mul_size(i, ns->struct_size, &elem_off)) {
                        a_ctx->current_schema = l_saved_schema;
                        s_field_nesting_depth--;
                        return DAP_SERIALIZE_ERROR_INVALID_DATA;
                    }
                    uint8_t *l_elem = l_array + elem_off;
                    for (size_t f = 0; f < ns->field_count; f++) {
                        const dap_serialize_field_t *nf = &ns->fields[f];
                        int r = s_deserialize_field(nf, l_elem, a_ctx);
                        if (r != 0) {
                            a_ctx->current_schema = l_saved_schema;
                            s_field_nesting_depth--;
                            return r;
                        }
                    }
                }
                a_ctx->current_schema = l_saved_schema;
                s_field_nesting_depth--;
            } else {
                dap_serialize_field_type_t et = a_field->element_type;
                size_t elem_size = a_field->size;
                size_t total = 0, end_off = 0;
                if (!s_safe_mul_size(count, elem_size, &total) ||
                    !s_safe_add_size(a_ctx->offset, total, &end_off) ||
                    end_off > a_ctx->buffer_size) {
                    return DAP_SERIALIZE_ERROR_INVALID_DATA;
                }
                switch (et) {
                    case DAP_SERIALIZE_TYPE_UINT16:
                    case DAP_SERIALIZE_TYPE_INT16: {
                        if (elem_size != 2) return DAP_SERIALIZE_ERROR_FIELD_VALIDATION;
                        for (size_t i = 0; i < count; i++) {
                            uint16_t v = s_read_uint16_le(a_ctx->buffer + a_ctx->offset);
                            memcpy(l_array + i * 2, &v, 2);
                            a_ctx->offset += 2;
                        }
                        break;
                    }
                    case DAP_SERIALIZE_TYPE_UINT32:
                    case DAP_SERIALIZE_TYPE_INT32:
                    case DAP_SERIALIZE_TYPE_FLOAT32: {
                        if (elem_size != 4) return DAP_SERIALIZE_ERROR_FIELD_VALIDATION;
                        for (size_t i = 0; i < count; i++) {
                            uint32_t v = s_read_uint32_le(a_ctx->buffer + a_ctx->offset);
                            memcpy(l_array + i * 4, &v, 4);
                            a_ctx->offset += 4;
                        }
                        break;
                    }
                    case DAP_SERIALIZE_TYPE_UINT64:
                    case DAP_SERIALIZE_TYPE_INT64:
                    case DAP_SERIALIZE_TYPE_FLOAT64: {
                        if (elem_size != 8) return DAP_SERIALIZE_ERROR_FIELD_VALIDATION;
                        for (size_t i = 0; i < count; i++) {
                            uint64_t v = s_read_uint64_le(a_ctx->buffer + a_ctx->offset);
                            memcpy(l_array + i * 8, &v, 8);
                            a_ctx->offset += 8;
                        }
                        break;
                    }
                    case DAP_SERIALIZE_TYPE_UINT8:
                    case DAP_SERIALIZE_TYPE_INT8:
                    case DAP_SERIALIZE_TYPE_BOOL:
                    default:
                        memcpy(l_array, a_ctx->buffer + a_ctx->offset, total);
                        a_ctx->offset += total;
                        break;
                }
            }
            break;
        }
        case DAP_SERIALIZE_TYPE_NESTED_STRUCT: {
            if (!a_field->nested_schema) {
                return DAP_SERIALIZE_ERROR_INVALID_SCHEMA;
            }
            if (s_field_nesting_depth >= DAP_SERIALIZE_MAX_FIELD_NESTING) {
                return DAP_SERIALIZE_ERROR_INVALID_SCHEMA;
            }
            s_field_nesting_depth++;
            const dap_serialize_schema_t *ns = a_field->nested_schema;
            uint8_t *l_nested = obj_ptr + a_field->offset;
            const dap_serialize_schema_t *l_saved_schema = a_ctx->current_schema;
            a_ctx->current_schema = ns;
            for (size_t f = 0; f < ns->field_count; f++) {
                const dap_serialize_field_t *nf = &ns->fields[f];
                int r = s_deserialize_field(nf, l_nested, a_ctx);
                if (r != 0) {
                    a_ctx->current_schema = l_saved_schema;
                    s_field_nesting_depth--;
                    return r;
                }
            }
            a_ctx->current_schema = l_saved_schema;
            s_field_nesting_depth--;
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

/* --- Direct pointer serialization (no schema needed) --- */

int dap_serialize_ptr_to_buffer(const void *a_data, size_t a_size,
                                uint8_t *a_buffer, size_t a_buffer_size)
{
    if (!a_data || !a_buffer) return -EINVAL;
    if (a_buffer_size < a_size) return -ENOMEM;
    memcpy(a_buffer, a_data, a_size);
    return 0;
}

int dap_serialize_ptr_from_buffer(const uint8_t *a_buffer, size_t a_buffer_size,
                                  void *a_data, size_t a_size)
{
    if (!a_buffer || !a_data) return -EINVAL;
    if (a_buffer_size < a_size) return -EINVAL;
    memcpy(a_data, a_buffer, a_size);
    return 0;
}
