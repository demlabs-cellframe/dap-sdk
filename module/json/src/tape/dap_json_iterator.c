/**
 * @file dap_json_iterator.c
 * @brief DAP JSON Iterator implementation - Zero-copy tape traversal
 * @details  High-performance iterator for tape format
 * 
 * Our Key Innovations:
 * - Zero-copy strings (pointer into input buffer)
 * - Lazy number parsing (parse on demand)
 * - O(1) skip via jump pointers
 * - Arena-based allocation (no malloc)
 * - Sequential tape access (cache-friendly)
 * 
 * @author DAP SDK Team
 * @date 2026-01-21
 */

#include "dap_json_iterator.h"
#include "internal/dap_json_tape.h"
#include "internal/dap_json_stage1.h"  // For dap_json_stage1_t
#include "internal/dap_json_internal.h"
#include <math.h>    // For HUGE_VAL
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "dap_common.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define LOG_TAG "dap_json_iterator"

/* ========================================================================== */
/*                           ITERATOR STRUCTURE                               */
/* ========================================================================== */

/**
 * @brief Iterator state for tape traversal
 */
struct dap_json_iterator {
    dap_json_tape_entry_t *tape;      ///< Tape array
    size_t tape_count;                 ///< Total tape entries
    size_t position;                   ///< Current position in tape
    const char *input_buffer;          ///< Input buffer for zero-copy strings
    size_t input_len;                  ///< Input buffer length
    
    // Depth tracking for enter/exit
    size_t depth;                      ///< Current nesting depth
    size_t depth_stack[64];            ///< Stack of container close positions
};

/* ========================================================================== */
/*                         ITERATOR LIFECYCLE                                 */
/* ========================================================================== */

dap_json_iterator_t* dap_json_iterator_new(dap_json_t *a_json)
{
    if (!a_json) {
        log_it(L_ERROR, "NULL JSON object");
        return NULL;
    }
    
    // Only works for ARENA_IMMUTABLE mode (tape-based parsing)
    if (a_json->mode != DAP_JSON_MODE_IMMUTABLE) {
        log_it(L_ERROR, "Iterator only works for parsed JSON (ARENA_IMMUTABLE mode)");
        return NULL;
    }
    
    if (!a_json->mode_data.immutable.tape || a_json->mode_data.immutable.tape_count == 0) {
        log_it(L_ERROR, "Empty tape");
        return NULL;
    }
    
    dap_json_iterator_t *l_iter = DAP_NEW_Z(dap_json_iterator_t);
    if (!l_iter) {
        log_it(L_ERROR, "Failed to allocate iterator");
        return NULL;
    }
    
    l_iter->tape = a_json->mode_data.immutable.tape;
    l_iter->tape_count = a_json->mode_data.immutable.tape_count;
    l_iter->input_buffer = a_json->mode_data.immutable.input_buffer;
    l_iter->input_len = a_json->mode_data.immutable.input_len;
    l_iter->depth = 0;

    // Start position: tape_offset (0 for root wrapper, non-zero for sub-wrapper)
    size_t l_start_pos = a_json->mode_data.immutable.tape_offset;
    
    // For root wrappers (tape_offset == 0): skip ROOT_START marker if present
    if (l_start_pos == 0 && l_iter->tape_count > 0) {
        uint8_t l_first_type = dap_tape_get_type(l_iter->tape[0]);
        if (l_first_type == TAPE_TYPE_ROOT_START && l_iter->tape_count > 1) {
            l_start_pos = 1;  // Start at actual content
        }
    }
    
    l_iter->position = l_start_pos;
    
    return l_iter;
}

size_t dap_json_iterator_get_position(const dap_json_iterator_t *a_iter)
{
    if (!a_iter) {
        return 0;
    }
    return a_iter->position;
}

void dap_json_iterator_free(dap_json_iterator_t *a_iter)
{
    // Iterator doesn't own tape or input_buffer, just free structure
    DAP_DELETE(a_iter);
}

void dap_json_iterator_reset(dap_json_iterator_t *a_iter)
{
    if (!a_iter) return;
    
    a_iter->position = 0;
    a_iter->depth = 0;
}

/* ========================================================================== */
/*                         ITERATOR NAVIGATION                                */
/* ========================================================================== */

dap_json_type_t dap_json_iterator_type(const dap_json_iterator_t *a_iter)
{
    if (!a_iter || a_iter->position >= a_iter->tape_count) {
        return DAP_JSON_TYPE_NULL;
    }
    
    uint8_t l_type = dap_tape_get_type(a_iter->tape[a_iter->position]);
    
    switch (l_type) {
        case TAPE_TYPE_ROOT_START:    // Skip root marker, get actual content
            if (a_iter->position + 1 < a_iter->tape_count) {
                l_type = dap_tape_get_type(a_iter->tape[a_iter->position + 1]);
                // Fall through to decode actual type
            } else {
                return DAP_JSON_TYPE_NULL;
            }
            // Intentional fallthrough to decode actual content type
            
        case TAPE_TYPE_OBJECT_START:  return DAP_JSON_TYPE_OBJECT;
        case TAPE_TYPE_ARRAY_START:   return DAP_JSON_TYPE_ARRAY;
        case TAPE_TYPE_STRING:        return DAP_JSON_TYPE_STRING;
        case TAPE_TYPE_NUMBER: {
            // Determine if INT or DOUBLE by checking number content
            uint64_t l_offset = dap_tape_get_payload(a_iter->tape[a_iter->position]);
            if (l_offset < a_iter->input_len) {
                const char *l_num = a_iter->input_buffer + l_offset;
                // Check for '.' or 'e'/'E' = floating point
                while (*l_num && !isspace(*l_num) && *l_num != ',' && *l_num != ']' && *l_num != '}') {
                    if (*l_num == '.' || *l_num == 'e' || *l_num == 'E') {
                        return DAP_JSON_TYPE_DOUBLE;
                    }
                    l_num++;
                }
            }
            return DAP_JSON_TYPE_INT;
        }
        case TAPE_TYPE_TRUE:          return DAP_JSON_TYPE_BOOLEAN;
        case TAPE_TYPE_FALSE:         return DAP_JSON_TYPE_BOOLEAN;
        case TAPE_TYPE_NULL:          return DAP_JSON_TYPE_NULL;
        default:                      return DAP_JSON_TYPE_NULL;
    }
}

bool dap_json_iterator_next(dap_json_iterator_t *a_iter)
{
    if (!a_iter || a_iter->position >= a_iter->tape_count) {
        return false;
    }
    
    // Use tape navigation helper to skip current value
    size_t l_next = dap_tape_next(a_iter->tape, a_iter->tape_count, a_iter->position);
    
    // Check if we're at the end of current container
    if (a_iter->depth > 0) {
        size_t l_container_close = a_iter->depth_stack[a_iter->depth - 1];
        if (l_next >= l_container_close) {
            return false;  // At end of container
        }
    }
    
    if (l_next >= a_iter->tape_count) {
        return false;  // At end of tape
    }
    
    a_iter->position = l_next;
    return true;
}

bool dap_json_iterator_skip(dap_json_iterator_t *a_iter)
{
    // skip() is same as next() - both use O(1) jump pointers
    return dap_json_iterator_next(a_iter);
}

bool dap_json_iterator_enter(dap_json_iterator_t *a_iter)
{
    if (!a_iter || a_iter->position >= a_iter->tape_count) {
        return false;
    }
    
    uint8_t l_type = dap_tape_get_type(a_iter->tape[a_iter->position]);
    
    if (l_type != TAPE_TYPE_OBJECT_START && l_type != TAPE_TYPE_ARRAY_START) {
        return false;  // Not a container
    }
    
    // Get close position from jump pointer
    uint64_t l_close_idx = dap_tape_get_payload(a_iter->tape[a_iter->position]);
    
    if (a_iter->depth >= 64) {
        log_it(L_ERROR, "Iterator depth overflow (max 64 levels)");
        return false;
    }
    
    // Push close position to depth stack
    a_iter->depth_stack[a_iter->depth] = (size_t)l_close_idx;
    a_iter->depth++;
    
    // Move to first element inside container
    a_iter->position++;
    
    return true;
}

bool dap_json_iterator_exit(dap_json_iterator_t *a_iter)
{
    if (!a_iter || a_iter->depth == 0) {
        return false;  // At root level
    }
    
    // Pop depth stack
    a_iter->depth--;
    size_t l_close_idx = a_iter->depth_stack[a_iter->depth];
    
    // Move to position after container close
    a_iter->position = l_close_idx + 1;
    
    return true;
}

bool dap_json_iterator_at_end(const dap_json_iterator_t *a_iter)
{
    if (!a_iter) return true;
    
    if (a_iter->position >= a_iter->tape_count) {
        return true;
    }
    
    // Check if at container end
    if (a_iter->depth > 0) {
        size_t l_container_close = a_iter->depth_stack[a_iter->depth - 1];
        return a_iter->position >= l_container_close;
    }
    
    return false;
}

/* ========================================================================== */
/*                      VALUE ACCESSORS (ZERO-COPY)                           */
/* ========================================================================== */

bool dap_json_iterator_get_string(
    const dap_json_iterator_t *a_iter,
    const char **out_str,
    size_t *out_len
)
{
    if (!a_iter || !out_str || !out_len) {
        return false;
    }
    
    if (a_iter->position >= a_iter->tape_count) {
        return false;
    }
    
    uint8_t l_type = dap_tape_get_type(a_iter->tape[a_iter->position]);
    if (l_type != TAPE_TYPE_STRING) {
        return false;
    }
    
    // Payload contains offset into input buffer
    uint64_t l_offset = dap_tape_get_payload(a_iter->tape[a_iter->position]);
    
    if (l_offset >= a_iter->input_len) {
        log_it(L_ERROR, "String offset out of bounds: %lu >= %zu", l_offset, a_iter->input_len);
        return false;
    }
    
    // Find string length (scan until closing quote)
    const char *l_str = a_iter->input_buffer + l_offset;
    size_t l_len = 0;
    
    if (l_str[0] != '"') {
        log_it(L_ERROR, "String does not start with quote at offset %lu", l_offset);
        return false;
    }
    
    l_str++;  // Skip opening quote
    l_offset++;
    
    while (l_offset + l_len < a_iter->input_len && l_str[l_len] != '"') {
        if (l_str[l_len] == '\\') {
            l_len++;  // Skip escape character
        }
        l_len++;
    }
    
    *out_str = l_str;
    *out_len = l_len;
    
    return true;
}

char* dap_json_iterator_get_string_dup(const dap_json_iterator_t *a_iter)
{
    const char *l_str;
    size_t l_len;
    
    if (!dap_json_iterator_get_string(a_iter, &l_str, &l_len)) {
        return NULL;
    }
    
    // Check if string contains escape sequences
    bool l_has_escapes = false;
    for (size_t i = 0; i < l_len; i++) {
        if (l_str[i] == '\\') {
            l_has_escapes = true;
            break;
        }
    }
    
    // If no escapes - simple copy
    if (!l_has_escapes) {
        char *l_dup = (char*)malloc(l_len + 1);
        if (!l_dup) {
            log_it(L_ERROR, "Failed to allocate string: %zu bytes", l_len + 1);
            return NULL;
        }
        
        memcpy(l_dup, l_str, l_len);
        l_dup[l_len] = '\0';
        
        return l_dup;
    }
    
    // Has escapes - need to unescape
    char *l_output = (char*)malloc(l_len + 1);  // worst case size
    if (!l_output) {
        log_it(L_ERROR, "Failed to allocate for unescaping: %zu bytes", l_len + 1);
        return NULL;
    }
    
    size_t l_output_pos = 0;
    size_t l_input_pos = 0;
    
    while (l_input_pos < l_len) {
        if (l_str[l_input_pos] == '\\' && l_input_pos + 1 < l_len) {
            // Escape sequence
            l_input_pos++; // skip backslash
            char l_escaped = l_str[l_input_pos++];
            
            switch (l_escaped) {
                case '"':  l_output[l_output_pos++] = '"'; break;
                case '\\': l_output[l_output_pos++] = '\\'; break;
                case '/':  l_output[l_output_pos++] = '/'; break;
                case 'b':  l_output[l_output_pos++] = '\b'; break;
                case 'f':  l_output[l_output_pos++] = '\f'; break;
                case 'n':  l_output[l_output_pos++] = '\n'; break;
                case 'r':  l_output[l_output_pos++] = '\r'; break;
                case 't':  l_output[l_output_pos++] = '\t'; break;
                
                case 'u': {
                    // Unicode escape \uXXXX
                    if (l_input_pos + 4 > l_len) {
                        log_it(L_ERROR, "Incomplete Unicode escape");
                        free(l_output);
                        return NULL;
                    }
                    
                    // Parse hex digits
                    uint32_t l_codepoint = 0;
                    for (int i = 0; i < 4; i++) {
                        char c = l_str[l_input_pos++];
                        uint32_t digit;
                        if (c >= '0' && c <= '9') digit = c - '0';
                        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
                        else {
                            log_it(L_ERROR, "Invalid hex digit in Unicode escape");
                            free(l_output);
                            return NULL;
                        }
                        l_codepoint = (l_codepoint << 4) | digit;
                    }
                    
                    // Check for UTF-16 surrogate pairs
                    if (l_codepoint >= 0xD800 && l_codepoint <= 0xDBFF) {
                        // High surrogate - MUST be followed by low surrogate
                        if (l_input_pos + 6 > l_len || l_str[l_input_pos] != '\\' || l_str[l_input_pos + 1] != 'u') {
                            log_it(L_ERROR, "Unpaired high surrogate U+%04X (missing low surrogate)", l_codepoint);
                            free(l_output);
                            return NULL;
                        }
                        
                        // Parse low surrogate
                        l_input_pos += 2; // skip \u
                        uint32_t l_low = 0;
                        for (int i = 0; i < 4; i++) {
                            char c = l_str[l_input_pos++];
                            uint32_t digit;
                            if (c >= '0' && c <= '9') digit = c - '0';
                            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
                            else {
                                log_it(L_ERROR, "Invalid hex digit in low surrogate");
                                free(l_output);
                                return NULL;
                            }
                            l_low = (l_low << 4) | digit;
                        }
                        
                        // Validate low surrogate range
                        if (l_low < 0xDC00 || l_low > 0xDFFF) {
                            log_it(L_ERROR, "Invalid low surrogate U+%04X (expected DC00-DFFF)", l_low);
                            free(l_output);
                            return NULL;
                        }
                        
                        // Combine surrogates into full codepoint
                        // Formula: (high - 0xD800) * 0x400 + (low - 0xDC00) + 0x10000
                        l_codepoint = ((l_codepoint - 0xD800) << 10) + (l_low - 0xDC00) + 0x10000;
                        
                        // Now l_codepoint is in range U+10000 to U+10FFFF (4-byte UTF-8)
                        // Fall through to UTF-8 encoding below
                    } 
                    else if (l_codepoint >= 0xDC00 && l_codepoint <= 0xDFFF) {
                        // Low surrogate without high surrogate - INVALID
                        log_it(L_ERROR, "Unpaired low surrogate U+%04X (no preceding high surrogate)", l_codepoint);
                        free(l_output);
                        return NULL;
                    }
                    
                    // Convert codepoint to UTF-8
                    if (l_codepoint <= 0x7F) {
                        // 1-byte UTF-8: 0xxxxxxx
                        l_output[l_output_pos++] = (char)l_codepoint;
                    } else if (l_codepoint <= 0x7FF) {
                        // 2-byte UTF-8: 110xxxxx 10xxxxxx
                        l_output[l_output_pos++] = (char)(0xC0 | (l_codepoint >> 6));
                        l_output[l_output_pos++] = (char)(0x80 | (l_codepoint & 0x3F));
                    } else if (l_codepoint <= 0xFFFF) {
                        // 3-byte UTF-8: 1110xxxx 10xxxxxx 10xxxxxx
                        l_output[l_output_pos++] = (char)(0xE0 | (l_codepoint >> 12));
                        l_output[l_output_pos++] = (char)(0x80 | ((l_codepoint >> 6) & 0x3F));
                        l_output[l_output_pos++] = (char)(0x80 | (l_codepoint & 0x3F));
                    } else if (l_codepoint <= 0x10FFFF) {
                        // 4-byte UTF-8: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
                        l_output[l_output_pos++] = (char)(0xF0 | (l_codepoint >> 18));
                        l_output[l_output_pos++] = (char)(0x80 | ((l_codepoint >> 12) & 0x3F));
                        l_output[l_output_pos++] = (char)(0x80 | ((l_codepoint >> 6) & 0x3F));
                        l_output[l_output_pos++] = (char)(0x80 | (l_codepoint & 0x3F));
                    } else {
                        log_it(L_ERROR, "Invalid Unicode codepoint U+%06X", l_codepoint);
                        free(l_output);
                        return NULL;
                    }
                    break;
                }
                
                default:
                    log_it(L_ERROR, "Invalid escape sequence '\\%c'", l_escaped);
                    free(l_output);
                    return NULL;
            }
        } else {
            // Regular character
            l_output[l_output_pos++] = l_str[l_input_pos++];
        }
    }
    
    l_output[l_output_pos] = '\0';
    return l_output;
}

bool dap_json_iterator_get_int64(const dap_json_iterator_t *a_iter, int64_t *out)
{
    if (!a_iter || !out) {
        return false;
    }
    
    if (a_iter->position >= a_iter->tape_count) {
        return false;
    }
    
    uint8_t l_type = dap_tape_get_type(a_iter->tape[a_iter->position]);
    if (l_type != TAPE_TYPE_NUMBER) {
        return false;
    }
    
    // Payload contains offset into input buffer
    uint64_t l_offset = dap_tape_get_payload(a_iter->tape[a_iter->position]);
    
    if (l_offset >= a_iter->input_len) {
        log_it(L_ERROR, "Number offset out of bounds: %lu >= %zu", l_offset, a_iter->input_len);
        return false;
    }
    
    // Lazy parse number from input buffer
    const char *l_num_str = a_iter->input_buffer + l_offset;
    char *l_endptr;
    errno = 0;
    
    int64_t l_value = strtoll(l_num_str, &l_endptr, 10);
    
    if (errno == ERANGE) {
        log_it(L_ERROR, "Number overflow at offset %lu", l_offset);
        return false;
    }
    
    *out = l_value;
    return true;
}

bool dap_json_iterator_get_uint64(const dap_json_iterator_t *a_iter, uint64_t *out)
{
    if (!a_iter || !out) {
        return false;
    }
    
    if (a_iter->position >= a_iter->tape_count) {
        return false;
    }
    
    uint8_t l_type = dap_tape_get_type(a_iter->tape[a_iter->position]);
    if (l_type != TAPE_TYPE_NUMBER) {
        return false;
    }
    
    uint64_t l_offset = dap_tape_get_payload(a_iter->tape[a_iter->position]);
    
    if (l_offset >= a_iter->input_len) {
        log_it(L_ERROR, "Number offset out of bounds: %lu >= %zu", l_offset, a_iter->input_len);
        return false;
    }
    
    const char *l_num_str = a_iter->input_buffer + l_offset;
    char *l_endptr;
    errno = 0;
    
    uint64_t l_value = strtoull(l_num_str, &l_endptr, 10);
    
    if (errno == ERANGE) {
        log_it(L_ERROR, "Number overflow at offset %lu", l_offset);
        return false;
    }
    
    *out = l_value;
    return true;
}

bool dap_json_iterator_get_double(const dap_json_iterator_t *a_iter, double *out)
{
    if (!a_iter || !out) {
        return false;
    }
    
    if (a_iter->position >= a_iter->tape_count) {
        return false;
    }
    
    uint8_t l_type = dap_tape_get_type(a_iter->tape[a_iter->position]);
    if (l_type != TAPE_TYPE_NUMBER) {
        return false;
    }
    
    uint64_t l_offset = dap_tape_get_payload(a_iter->tape[a_iter->position]);
    
    if (l_offset >= a_iter->input_len) {
        log_it(L_ERROR, "Number offset out of bounds: %lu >= %zu", l_offset, a_iter->input_len);
        return false;
    }
    
    const char *l_num_str = a_iter->input_buffer + l_offset;
    char *l_endptr;
    errno = 0;
    
    double l_value = strtod(l_num_str, &l_endptr);
    
    // Check for overflow (not underflow - subnormal doubles are valid)
    if (errno == ERANGE && (l_value == HUGE_VAL || l_value == -HUGE_VAL)) {
        log_it(L_ERROR, "Number overflow at offset %lu", l_offset);
        return false;
    }
    
    *out = l_value;
    return true;
}

bool dap_json_iterator_get_bool(const dap_json_iterator_t *a_iter, bool *out)
{
    if (!a_iter || !out) {
        return false;
    }
    
    if (a_iter->position >= a_iter->tape_count) {
        return false;
    }
    
    uint8_t l_type = dap_tape_get_type(a_iter->tape[a_iter->position]);
    
    if (l_type == TAPE_TYPE_TRUE) {
        *out = true;
        return true;
    } else if (l_type == TAPE_TYPE_FALSE) {
        *out = false;
        return true;
    }
    
    return false;
}

bool dap_json_iterator_is_null(const dap_json_iterator_t *a_iter)
{
    if (!a_iter || a_iter->position >= a_iter->tape_count) {
        return false;
    }
    
    uint8_t l_type = dap_tape_get_type(a_iter->tape[a_iter->position]);
    return l_type == TAPE_TYPE_NULL;
}

/* ========================================================================== */
/*                      OBJECT/ARRAY HELPERS                                  */
/* ========================================================================== */

bool dap_json_iterator_find_key(
    dap_json_iterator_t *a_iter,
    const char *a_key,
    size_t a_key_len
)
{
    if (!a_iter || !a_key) {
        return false;
    }
    
    if (a_iter->depth == 0) {
        log_it(L_ERROR, "find_key() called outside of object");
        return false;
    }
    
    // Save starting position
    size_t l_start_pos = a_iter->position;
    size_t l_container_close = a_iter->depth_stack[a_iter->depth - 1];
    
    // Scan for matching key
    while (a_iter->position < l_container_close) {
        uint8_t l_type = dap_tape_get_type(a_iter->tape[a_iter->position]);
        
        if (l_type != TAPE_TYPE_STRING) {
            // Skip non-string entries
            if (!dap_json_iterator_next(a_iter)) break;
            continue;
        }
        
        // Check if this string matches key
        const char *l_str;
        size_t l_len;
        if (dap_json_iterator_get_string(a_iter, &l_str, &l_len)) {
            if (l_len == a_key_len && memcmp(l_str, a_key, a_key_len) == 0) {
                // Found! Move to value
                dap_json_iterator_next(a_iter);
                return true;
            }
        }
        
        // Skip key and value
        if (!dap_json_iterator_next(a_iter)) break;  // Skip key
        if (!dap_json_iterator_next(a_iter)) break;  // Skip value
    }
    
    // Not found, restore position
    a_iter->position = l_start_pos;
    return false;
}

size_t dap_json_iterator_array_length(const dap_json_iterator_t *a_iter)
{
    if (!a_iter || a_iter->position >= a_iter->tape_count) {
        return 0;
    }
    
    uint8_t l_type = dap_tape_get_type(a_iter->tape[a_iter->position]);
    if (l_type != TAPE_TYPE_ARRAY_START) {
        return 0;
    }
    
    // Get close position from jump pointer
    uint64_t l_close_idx = dap_tape_get_payload(a_iter->tape[a_iter->position]);
    
    // Count elements between start and close
    size_t l_count = 0;
    size_t l_pos = a_iter->position + 1;  // Skip array start
    
    while (l_pos < l_close_idx && l_pos < a_iter->tape_count) {
        l_count++;
        l_pos = dap_tape_next(a_iter->tape, a_iter->tape_count, l_pos);
    }
    
    return l_count;
}

size_t dap_json_iterator_object_size(const dap_json_iterator_t *a_iter)
{
    if (!a_iter || a_iter->position >= a_iter->tape_count) {
        return 0;
    }
    
    uint8_t l_type = dap_tape_get_type(a_iter->tape[a_iter->position]);
    if (l_type != TAPE_TYPE_OBJECT_START) {
        return 0;
    }
    
    // Get close position from jump pointer
    uint64_t l_close_idx = dap_tape_get_payload(a_iter->tape[a_iter->position]);
    
    // Count key-value pairs between start and close
    size_t l_count = 0;
    size_t l_pos = a_iter->position + 1;  // Skip object start
    
    while (l_pos < l_close_idx && l_pos < a_iter->tape_count) {
        l_count++;
        l_pos = dap_tape_next(a_iter->tape, a_iter->tape_count, l_pos);  // Skip key
        if (l_pos >= l_close_idx) break;
        l_pos = dap_tape_next(a_iter->tape, a_iter->tape_count, l_pos);  // Skip value
    }
    
    return l_count;
}
