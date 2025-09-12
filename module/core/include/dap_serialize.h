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

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "dap_common.h"

/**
 * @brief Universal serializer/deserializer for DAP SDK structures
 * @details Provides schema-based serialization with automatic type handling,
 *          endianness conversion, and memory management
 */

/**
 * @brief Field types supported by the serializer
 */
typedef enum dap_serialize_field_type {
    DAP_SERIALIZE_TYPE_UINT8 = 0,           ///< 1-byte integer (8 bits)
    DAP_SERIALIZE_TYPE_UINT16,              ///< 2-byte integer (16 bits, with endianness handling)
    DAP_SERIALIZE_TYPE_UINT32,              ///< 4-byte integer (32 bits, with endianness handling)  
    DAP_SERIALIZE_TYPE_UINT64,              ///< 8-byte integer (64 bits, with endianness handling)
    DAP_SERIALIZE_TYPE_UINT128,             ///< 16-byte integer (128 bits, with endianness handling)
    DAP_SERIALIZE_TYPE_UINT256,             ///< 32-byte integer (256 bits, with endianness handling)
    DAP_SERIALIZE_TYPE_UINT512,             ///< 64-byte integer (512 bits, with endianness handling)
    DAP_SERIALIZE_TYPE_INT8,                ///< 1-byte signed integer
    DAP_SERIALIZE_TYPE_INT16,               ///< 2-byte signed integer (with endianness handling)
    DAP_SERIALIZE_TYPE_INT32,               ///< 4-byte signed integer (with endianness handling)
    DAP_SERIALIZE_TYPE_INT64,               ///< 8-byte signed integer (with endianness handling)
    DAP_SERIALIZE_TYPE_FLOAT32,             ///< 4-byte IEEE 754 float
    DAP_SERIALIZE_TYPE_FLOAT64,             ///< 8-byte IEEE 754 double
    DAP_SERIALIZE_TYPE_BOOL,                ///< Boolean value (1 byte: 0=false, 1=true)
    DAP_SERIALIZE_TYPE_BYTES_FIXED,         ///< Fixed-size byte array
    DAP_SERIALIZE_TYPE_BYTES_DYNAMIC,       ///< Dynamic byte array with size prefix
    DAP_SERIALIZE_TYPE_STRING_FIXED,        ///< Fixed-size null-terminated string
    DAP_SERIALIZE_TYPE_STRING_DYNAMIC,      ///< Dynamic string with length prefix
    DAP_SERIALIZE_TYPE_ARRAY_FIXED,         ///< Fixed-size array of elements
    DAP_SERIALIZE_TYPE_ARRAY_DYNAMIC,       ///< Dynamic array with count prefix
    DAP_SERIALIZE_TYPE_NESTED_STRUCT,       ///< Nested structure with own schema
    DAP_SERIALIZE_TYPE_CONDITIONAL,         ///< Field present only if condition is met
    DAP_SERIALIZE_TYPE_UNION,               ///< Union type with discriminator
    DAP_SERIALIZE_TYPE_PADDING,             ///< Alignment padding
    DAP_SERIALIZE_TYPE_CHECKSUM,            ///< Automatic checksum/hash calculation
    DAP_SERIALIZE_TYPE_VERSION,             ///< Version field for compatibility
    DAP_SERIALIZE_TYPE_RESERVED             ///< Reserved space for future extensions
} dap_serialize_field_type_t;

/**
 * @brief Field flags for special handling
 */
typedef enum dap_serialize_field_flags {
    DAP_SERIALIZE_FLAG_NONE = 0,            ///< No special flags
    DAP_SERIALIZE_FLAG_OPTIONAL = (1 << 0), ///< Field is optional
    DAP_SERIALIZE_FLAG_ENCRYPTED = (1 << 1), ///< Field should be encrypted
    DAP_SERIALIZE_FLAG_COMPRESSED = (1 << 2), ///< Field should be compressed
    DAP_SERIALIZE_FLAG_BIG_ENDIAN = (1 << 3), ///< Force big-endian encoding
    DAP_SERIALIZE_FLAG_LITTLE_ENDIAN = (1 << 4), ///< Force little-endian encoding
    DAP_SERIALIZE_FLAG_NO_SIZE_PREFIX = (1 << 5), ///< Don't add size prefix for dynamic data
    DAP_SERIALIZE_FLAG_NULL_TERMINATED = (1 << 6), ///< Add null terminator for strings
    DAP_SERIALIZE_FLAG_ZERO_FILL = (1 << 7), ///< Zero-fill unused space
    DAP_SERIALIZE_FLAG_SECURE_CLEAR = (1 << 8) ///< Securely clear sensitive data
} dap_serialize_field_flags_t;

/**
 * @brief Condition function for conditional fields
 * @param a_object Pointer to the object being serialized
 * @param a_context User context passed to serializer
 * @return true if field should be included, false otherwise
 */
typedef bool (*dap_serialize_condition_func_t)(const void *a_object, void *a_context);

/**
 * @brief Size calculation function for dynamic fields
 * @param a_object Pointer to the object being serialized
 * @param a_context User context passed to serializer
 * @return Size of the field in bytes
 */
typedef size_t (*dap_serialize_size_func_t)(const void *a_object, void *a_context);

/**
 * @brief Field descriptor for serialization schema
 */
typedef struct dap_serialize_field {
    const char *name;                       ///< Field name (for debugging)
    dap_serialize_field_type_t type;        ///< Field type
    dap_serialize_field_flags_t flags;      ///< Field flags
    size_t offset;                          ///< Offset in structure
    size_t size;                            ///< Size for fixed-size fields
    size_t size_offset;                     ///< Offset to size field for dynamic fields
    size_t count_offset;                    ///< Offset to count field for arrays
    dap_serialize_condition_func_t condition; ///< Condition function (optional)
    dap_serialize_size_func_t size_func;    ///< Size calculation function (optional)
    const struct dap_serialize_schema *nested_schema; ///< Schema for nested structures
    uint32_t version_min;                   ///< Minimum version supporting this field
    uint32_t version_max;                   ///< Maximum version supporting this field
} dap_serialize_field_t;

/**
 * @brief Serialization schema descriptor
 */
typedef struct dap_serialize_schema {
    const char *name;                       ///< Schema name
    uint32_t version;                       ///< Schema version
    size_t struct_size;                     ///< Size of the structure
    size_t field_count;                     ///< Number of fields
    const dap_serialize_field_t *fields;    ///< Array of field descriptors
    uint32_t magic;                         ///< Magic number for validation
    bool (*validate_func)(const void *a_object); ///< Validation function (optional)
} dap_serialize_schema_t;

/**
 * @brief Serialization context
 */
typedef struct dap_serialize_context {
    uint8_t *buffer;                        ///< Output/input buffer
    size_t buffer_size;                     ///< Buffer size
    size_t offset;                          ///< Current offset
    uint32_t version;                       ///< Format version
    void *user_context;                     ///< User-defined context
    bool is_deserializing;                  ///< Direction flag
    size_t objects_serialized;              ///< Statistics
    size_t bytes_processed;                 ///< Statistics
} dap_serialize_context_t;

/**
 * @brief Serialization result
 */
typedef struct dap_serialize_result {
    int error_code;                         ///< Error code (0 = success)
    const char *error_message;              ///< Human-readable error message
    size_t bytes_written;                   ///< Bytes written during serialization
    size_t bytes_read;                      ///< Bytes read during deserialization
    const char *failed_field;               ///< Field name that caused failure (if any)
} dap_serialize_result_t;

// API Functions

/**
 * @brief Calculate required buffer size for serialization
 * @param a_schema Serialization schema
 * @param a_object Object to serialize
 * @param a_context User context (optional)
 * @return Required buffer size in bytes, 0 on error
 */
size_t dap_serialize_calc_size(const dap_serialize_schema_t *a_schema,
                               const void *a_object,
                               void *a_context);

/**
 * @brief Serialize object to buffer
 * @param a_schema Serialization schema
 * @param a_object Object to serialize
 * @param a_buffer Output buffer
 * @param a_buffer_size Buffer size
 * @param a_context User context (optional)
 * @return Serialization result
 */
dap_serialize_result_t dap_serialize_to_buffer(const dap_serialize_schema_t *a_schema,
                                               const void *a_object,
                                               uint8_t *a_buffer,
                                               size_t a_buffer_size,
                                               void *a_context);

/**
 * @brief Deserialize object from buffer
 * @param a_schema Serialization schema
 * @param a_buffer Input buffer
 * @param a_buffer_size Buffer size
 * @param a_object Output object (must be pre-allocated)
 * @param a_context User context (optional)
 * @return Deserialization result
 */
dap_serialize_result_t dap_serialize_from_buffer(const dap_serialize_schema_t *a_schema,
                                                 const uint8_t *a_buffer,
                                                 size_t a_buffer_size,
                                                 void *a_object,
                                                 void *a_context);

/**
 * @brief Validate serialized data
 * @param a_schema Serialization schema
 * @param a_buffer Buffer to validate
 * @param a_buffer_size Buffer size
 * @return true if valid, false otherwise
 */
bool dap_serialize_validate_buffer(const dap_serialize_schema_t *a_schema,
                                   const uint8_t *a_buffer,
                                   size_t a_buffer_size);

/**
 * @brief Create a copy of object using serialization
 * @param a_schema Serialization schema
 * @param a_source Source object
 * @param a_dest Destination object (must be pre-allocated)
 * @param a_context User context (optional)
 * @return Copy result
 */
dap_serialize_result_t dap_serialize_copy_object(const dap_serialize_schema_t *a_schema,
                                                 const void *a_source,
                                                 void *a_dest,
                                                 void *a_context);

// Utility macros for schema definition

#define DAP_SERIALIZE_FIELD_SIMPLE(struct_type, field_name, field_type) \
    { \
        .name = #field_name, \
        .type = field_type, \
        .flags = DAP_SERIALIZE_FLAG_NONE, \
        .offset = offsetof(struct_type, field_name), \
        .size = sizeof(((struct_type*)0)->field_name) \
    }

#define DAP_SERIALIZE_FIELD_DYNAMIC_BYTES(struct_type, ptr_field, size_field) \
    { \
        .name = #ptr_field, \
        .type = DAP_SERIALIZE_TYPE_BYTES_DYNAMIC, \
        .flags = DAP_SERIALIZE_FLAG_NONE, \
        .offset = offsetof(struct_type, ptr_field), \
        .size_offset = offsetof(struct_type, size_field) \
    }

#define DAP_SERIALIZE_FIELD_DYNAMIC_ARRAY(struct_type, ptr_field, count_field, element_schema) \
    { \
        .name = #ptr_field, \
        .type = DAP_SERIALIZE_TYPE_ARRAY_DYNAMIC, \
        .flags = DAP_SERIALIZE_FLAG_NONE, \
        .offset = offsetof(struct_type, ptr_field), \
        .count_offset = offsetof(struct_type, count_field), \
        .nested_schema = element_schema \
    }

#define DAP_SERIALIZE_FIELD_CONDITIONAL(struct_type, field_name, field_type, condition_func) \
    { \
        .name = #field_name, \
        .type = field_type, \
        .flags = DAP_SERIALIZE_FLAG_OPTIONAL, \
        .offset = offsetof(struct_type, field_name), \
        .size = sizeof(((struct_type*)0)->field_name), \
        .condition = condition_func \
    }

#define DAP_SERIALIZE_SCHEMA_DEFINE(schema_name, struct_type, fields_array) \
    const dap_serialize_schema_t schema_name = { \
        .name = #schema_name, \
        .version = 1, \
        .struct_size = sizeof(struct_type), \
        .field_count = sizeof(fields_array) / sizeof(fields_array[0]), \
        .fields = fields_array, \
        .magic = DAP_SERIALIZE_MAGIC_NUMBER \
    }

// Constants
#define DAP_SERIALIZE_MAGIC_NUMBER              0xDAC5E412  ///< DAP Serialize magic number

// Error codes
#define DAP_SERIALIZE_ERROR_SUCCESS             0
#define DAP_SERIALIZE_ERROR_INVALID_SCHEMA      -1
#define DAP_SERIALIZE_ERROR_INVALID_OBJECT      -2
#define DAP_SERIALIZE_ERROR_BUFFER_TOO_SMALL    -3
#define DAP_SERIALIZE_ERROR_INVALID_DATA        -4
#define DAP_SERIALIZE_ERROR_VERSION_MISMATCH    -5
#define DAP_SERIALIZE_ERROR_CHECKSUM_FAILED     -6
#define DAP_SERIALIZE_ERROR_MEMORY_ALLOCATION   -7
#define DAP_SERIALIZE_ERROR_FIELD_VALIDATION    -8
#define DAP_SERIALIZE_ERROR_ENCRYPTION_FAILED   -9
#define DAP_SERIALIZE_ERROR_COMPRESSION_FAILED  -10
