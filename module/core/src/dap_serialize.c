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

// Internal helper functions
static size_t s_calc_field_size(const dap_serialize_field_t *a_field, 
                                const void *a_object, 
                                void *a_context);
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

/**
 * @brief Calculate required buffer size for serialization
 */
size_t dap_serialize_calc_size(const dap_serialize_schema_t *a_schema,
                               const void *a_object,
                               void *a_context)
{
    if (!a_schema || !a_object) {
        log_it(L_ERROR, "Invalid parameters for size calculation");
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
    
    // Calculate size for each field
    for (size_t i = 0; i < a_schema->field_count; i++) {
        const dap_serialize_field_t *field = &a_schema->fields[i];
        
        // Check if field should be included
        if (!s_check_condition(field, a_object, a_context)) {
            continue;
        }
        
        size_t field_size = s_calc_field_size(field, a_object, a_context);
        if (field_size == 0 && field->type != DAP_SERIALIZE_TYPE_PADDING) {
            log_it(L_WARNING, "Field '%s' has zero size", field->name);
        }
        
        total_size += field_size;
    }
    
    log_it(L_DEBUG, "Calculated serialization size: %zu bytes for schema '%s'", 
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
    
    if (!a_schema || !a_object || !a_buffer) {
        result.error_code = DAP_SERIALIZE_ERROR_INVALID_SCHEMA;
        result.error_message = "Invalid parameters";
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
        
        int field_result = s_serialize_field(field, a_object, &ctx);
        if (field_result != 0) {
            result.error_code = field_result;
            result.error_message = "Field serialization failed";
            result.failed_field = field->name;
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
                                void *a_context)
{
    const uint8_t *obj_ptr = (const uint8_t*)a_object;
    size_t size = 0;
    
    switch (a_field->type) {
        case DAP_SERIALIZE_TYPE_UINT8:
        case DAP_SERIALIZE_TYPE_INT8:
        case DAP_SERIALIZE_TYPE_BOOL:
            size = 1;
            break;
        case DAP_SERIALIZE_TYPE_UINT16:
        case DAP_SERIALIZE_TYPE_INT16:
            size = 2;
            break;
        case DAP_SERIALIZE_TYPE_UINT32:
        case DAP_SERIALIZE_TYPE_INT32:
        case DAP_SERIALIZE_TYPE_FLOAT32:
        case DAP_SERIALIZE_TYPE_VERSION:
            size = 4;
            break;
        case DAP_SERIALIZE_TYPE_UINT64:
        case DAP_SERIALIZE_TYPE_INT64:
        case DAP_SERIALIZE_TYPE_FLOAT64:
            size = 8;
            break;
        case DAP_SERIALIZE_TYPE_UINT128:
            size = 16;
            break;
        case DAP_SERIALIZE_TYPE_UINT256:
            size = 32;
            break;
        case DAP_SERIALIZE_TYPE_UINT512:
            size = 64;
            break;
        case DAP_SERIALIZE_TYPE_BYTES_FIXED:
            size = a_field->size;
            break;
        case DAP_SERIALIZE_TYPE_BYTES_DYNAMIC: {
            const size_t *size_ptr = (const size_t*)(obj_ptr + a_field->size_offset);
            size = sizeof(uint32_t) + *size_ptr;  // size prefix + data
            break;
        }
        case DAP_SERIALIZE_TYPE_STRING_FIXED:
            size = a_field->size;
            break;
        case DAP_SERIALIZE_TYPE_STRING_DYNAMIC: {
            const size_t *size_ptr = (const size_t*)(obj_ptr + a_field->size_offset);
            size = sizeof(uint32_t) + *size_ptr;  // length prefix + string data
            if (a_field->flags & DAP_SERIALIZE_FLAG_NULL_TERMINATED) {
                size += 1;  // null terminator
            }
            break;
        }
        case DAP_SERIALIZE_TYPE_ARRAY_DYNAMIC: {
            const size_t *count_ptr = (const size_t*)(obj_ptr + a_field->count_offset);
            size = sizeof(uint32_t);  // count prefix
            if (a_field->nested_schema) {
                // For nested structures, need to calculate each element
                const void **array_ptr = (const void**)(obj_ptr + a_field->offset);
                if (*array_ptr) {
                    const uint8_t *element_ptr = (const uint8_t*)*array_ptr;
                    for (size_t i = 0; i < *count_ptr; i++) {
                        size += dap_serialize_calc_size(a_field->nested_schema, 
                                                       element_ptr + i * a_field->nested_schema->struct_size,
                                                       a_context);
                    }
                }
            } else {
                // Simple array of fixed-size elements
                size += (*count_ptr) * a_field->size;
            }
            break;
        }
        case DAP_SERIALIZE_TYPE_CHECKSUM:
            size = a_field->size;  // Usually 32 bytes for SHA256
            break;
        case DAP_SERIALIZE_TYPE_PADDING:
            size = a_field->size;
            break;
        default:
            log_it(L_WARNING, "Unknown field type %d for field '%s'", a_field->type, a_field->name);
            break;
    }
    
    return size;
}

static bool s_check_condition(const dap_serialize_field_t *a_field,
                             const void *a_object,
                             void *a_context)
{
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
    
    // Check buffer space
    size_t field_size = s_calc_field_size(a_field, a_object, a_ctx->user_context);
    if (a_ctx->offset + field_size > a_ctx->buffer_size) {
        return DAP_SERIALIZE_ERROR_BUFFER_TOO_SMALL;
    }
    
    switch (a_field->type) {
        case DAP_SERIALIZE_TYPE_UINT8: {
            const uint8_t *value = (const uint8_t*)(obj_ptr + a_field->offset);
            a_ctx->buffer[a_ctx->offset] = *value;
            a_ctx->offset += 1;
            break;
        }
        case DAP_SERIALIZE_TYPE_UINT32:
        case DAP_SERIALIZE_TYPE_VERSION: {
            const uint32_t *value = (const uint32_t*)(obj_ptr + a_field->offset);
            s_write_uint32_le(a_ctx->buffer + a_ctx->offset, *value);
            a_ctx->offset += 4;
            break;
        }
        case DAP_SERIALIZE_TYPE_UINT64: {
            const uint64_t *value = (const uint64_t*)(obj_ptr + a_field->offset);
            s_write_uint64_le(a_ctx->buffer + a_ctx->offset, *value);
            a_ctx->offset += 8;
            break;
        }
        case DAP_SERIALIZE_TYPE_BYTES_DYNAMIC: {
            const void **data_ptr = (const void**)(obj_ptr + a_field->offset);
            const size_t *size_ptr = (const size_t*)(obj_ptr + a_field->size_offset);
            
            // Write size prefix
            s_write_uint32_le(a_ctx->buffer + a_ctx->offset, (uint32_t)*size_ptr);
            a_ctx->offset += sizeof(uint32_t);
            
            // Write data
            if (*data_ptr && *size_ptr > 0) {
                memcpy(a_ctx->buffer + a_ctx->offset, *data_ptr, *size_ptr);
                a_ctx->offset += *size_ptr;
            }
            break;
        }
        // TODO: Implement other field types
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
        case DAP_SERIALIZE_TYPE_UINT8: {
            if (a_ctx->offset + 1 > a_ctx->buffer_size) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            uint8_t *value = (uint8_t*)(obj_ptr + a_field->offset);
            *value = a_ctx->buffer[a_ctx->offset];
            a_ctx->offset += 1;
            break;
        }
        case DAP_SERIALIZE_TYPE_UINT32:
        case DAP_SERIALIZE_TYPE_VERSION: {
            if (a_ctx->offset + 4 > a_ctx->buffer_size) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            uint32_t *value = (uint32_t*)(obj_ptr + a_field->offset);
            *value = s_read_uint32_le(a_ctx->buffer + a_ctx->offset);
            a_ctx->offset += 4;
            break;
        }
        case DAP_SERIALIZE_TYPE_UINT64: {
            if (a_ctx->offset + 8 > a_ctx->buffer_size) {
                return DAP_SERIALIZE_ERROR_INVALID_DATA;
            }
            uint64_t *value = (uint64_t*)(obj_ptr + a_field->offset);
            *value = s_read_uint64_le(a_ctx->buffer + a_ctx->offset);
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
        // TODO: Implement other field types
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
