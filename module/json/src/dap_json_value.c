/**
 * @file dap_json_value.c
 * @brief Implementation of compact JSON value operations
 * 
 * @details This file implements the accessor and conversion functions for
 * the 8-byte zero-copy JSON value references. These utilities enable:
 * - Conversion between old 56-byte values and new 8-byte refs
 * - Lazy materialization of strings (null-termination on demand)
 * - Zero-copy access to strings and numbers
 * - Cache management for parsed values
 * 
 * @note Phase 2.0.2: Memory Crisis Resolution - Implementation
 * @date 2026-01-18
 * @author DAP SDK Development Team
 */

#include "dap_json_value.h"
#include "dap_json_type.h"
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_arena.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

#define LOG_TAG "json_value"

/**
 * @defgroup ref_creation Direct Ref Creation from Source
 * @brief Create refs directly from JSON source buffer (used by Stage 2 parser)
 * @{
 */

/**
 * @brief Create string ref from source position
 * 
 * Stage 2 parser calls this when it finds a string in the source buffer.
 * 
 * @param source Source buffer
 * @param offset Start offset (pointing to first char after opening quote)
 * @param length String length (excluding quotes)
 * @param has_escapes Whether string contains escape sequences
 * @return String ref
 */
dap_json_value_t dap_json_from_string(
    const char *source,
    uint32_t offset,
    size_t length,
    bool has_escapes
) {
    (void)source; // Unused in Phase 2.0.4
    
    uint8_t flags = has_escapes ? DAP_JSON_FLAG_ESCAPED : 0;
    uint16_t l_len = (uint16_t)(length > 0xFFFF ? 0xFFFF : length);
    
    if (length > 0xFFFF) {
        flags |= DAP_JSON_FLAG_OVERFLOW;
    }
    
    dap_json_value_t result = {
        .type = DAP_JSON_TYPE_STRING,
        .flags = flags,
        .length = l_len,
        .offset = offset
    };
    
    return result;
}

/**
 * @brief Create number ref from source position
 * 
 * Stage 2 parser calls this when it finds a number.
 * The actual parsing happens lazily on first access.
 */
dap_json_value_t dap_json_from_number(
    const char *source,
    uint32_t offset,
    size_t length,
    bool is_integer
) {
    (void)source; // Unused in Phase 2.0.4
    
    dap_json_type_t type = is_integer ? DAP_JSON_TYPE_INT : DAP_JSON_TYPE_DOUBLE;
    uint8_t flags = 0;
    uint16_t l_len = (uint16_t)(length > 0xFFFF ? 0xFFFF : length);
    
    if (length > 0xFFFF) {
        flags |= DAP_JSON_FLAG_OVERFLOW;
    }
    
    dap_json_value_t result = {
        .type = type,
        .flags = flags,
        .length = l_len,
        .offset = offset
    };
    
    return result;
}

/**
 * @brief Create boolean ref from source position
 */
dap_json_value_t dap_json_from_boolean(
    const char *source,
    uint32_t offset,
    bool value
) {
    (void)source; // Unused in Phase 2.0.4
    
    // length = 4 for "true", 5 for "false"
    dap_json_value_t result = {
        .type = DAP_JSON_TYPE_BOOLEAN,
        .flags = 0,
        .length = value ? 4 : 5,
        .offset = offset
    };
    
    return result;
}

/**
 * @brief Create null ref from source position
 */
dap_json_value_t dap_json_from_null(
    const char *source,
    uint32_t offset
) {
    (void)source; // Unused in Phase 2.0.4
    
    dap_json_value_t result = {
        .type = DAP_JSON_TYPE_NULL,
        .flags = 0,
        .length = 4, // "null" = 4 bytes
        .offset = offset
    };
    
    return result;
}

/** @} */ // end of ref_creation

/* ========================================================================== */
/*                    LEGACY FUNCTIONS REMOVED IN PHASE 2.0.4                */
/* ========================================================================== */
/* The following legacy functions were removed as they are obsolete in       */
/* Phase 2.0.4 architecture with unified 8-byte dap_json_value_t:            */
/*                                                                            */
/* - dap_json_ref_materialize_string()  - Use s_materialize_string()         */
/* - dap_json_ref_parse_int64()         - Use dap_json_object_get_int64()    */
/* - dap_json_ref_parse_double()        - Use dap_json_object_get_double()   */
/* - dap_json_ref_parse_boolean()       - Use dap_json_object_get_bool()     */
/* - dap_json_ref_debug_print()         - No replacement (obsolete)          */
/* - dap_json_ref_validate()            - No replacement (obsolete)          */
/* - dap_json_ref_array_init/add/get    - Use Stage 2 API directly           */
/* - dap_json_ref_object_init/add/get   - Use Stage 2 API directly           */
/*                                                                            */
/* All value access and materialization is now handled internally by:        */
/* - dap_json.c (public API layer)                                           */
/* - dap_json_stage2_ref.c (internal Stage 2 implementation)                 */
/* ========================================================================== */
