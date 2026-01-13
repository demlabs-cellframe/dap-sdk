/**
 * @file dap_json_transcode.c
 * @brief Fast UTF-16/32 to UTF-8 transcoding
 * @details Optimized conversion before parsing (fallback path)
 */

#include "internal/dap_json_transcode.h"
#include "internal/dap_json_encoding.h"
#include "dap_common.h"
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "dap_json_transcode"

// Debug flag: detailed logs (below WARNING level)
static bool s_debug_more = false;

/**
 * @brief Enable/disable detailed debug logs
 */
void dap_json_transcode_set_debug(bool a_enable)
{
    s_debug_more = a_enable;
    log_it(L_DEBUG, "Transcode debug logs %s", a_enable ? "ENABLED" : "DISABLED");
}

/**
 * @brief Encode UTF-8 from codepoint
 */
static size_t s_encode_utf8(uint32_t a_codepoint, uint8_t *a_output)
{
    if (a_codepoint <= 0x7F) {
        // 1-byte
        a_output[0] = (uint8_t)a_codepoint;
        return 1;
    }
    else if (a_codepoint <= 0x7FF) {
        // 2-byte
        a_output[0] = (uint8_t)(0xC0 | (a_codepoint >> 6));
        a_output[1] = (uint8_t)(0x80 | (a_codepoint & 0x3F));
        return 2;
    }
    else if (a_codepoint <= 0xFFFF) {
        // 3-byte
        a_output[0] = (uint8_t)(0xE0 | (a_codepoint >> 12));
        a_output[1] = (uint8_t)(0x80 | ((a_codepoint >> 6) & 0x3F));
        a_output[2] = (uint8_t)(0x80 | (a_codepoint & 0x3F));
        return 3;
    }
    else if (a_codepoint <= 0x10FFFF) {
        // 4-byte
        a_output[0] = (uint8_t)(0xF0 | (a_codepoint >> 18));
        a_output[1] = (uint8_t)(0x80 | ((a_codepoint >> 12) & 0x3F));
        a_output[2] = (uint8_t)(0x80 | ((a_codepoint >> 6) & 0x3F));
        a_output[3] = (uint8_t)(0x80 | (a_codepoint & 0x3F));
        return 4;
    }
    
    return 0;  // Invalid codepoint
}

/**
 * @brief Transcode UTF-16/32 to UTF-8
 */
bool dap_json_transcode_to_utf8(
    const uint8_t *a_input,
    size_t a_len,
    dap_json_encoding_t a_encoding,
    uint8_t **a_output,
    size_t *a_output_len
)
{
    if (!a_input || !a_output || !a_output_len || a_len == 0) {
        return false;
    }
    
    // UTF-8 - no transcoding needed
    if (a_encoding == DAP_JSON_ENCODING_UTF8) {
        *a_output = (uint8_t*)a_input;  // No copy
        *a_output_len = a_len;
        return true;
    }
    
    // Allocate worst-case output buffer
    // UTF-32 → UTF-8: max 4 bytes per input char
    // UTF-16 → UTF-8: max 4 bytes per input char (with surrogates)
    size_t l_max_output = a_len * 2;  // Conservative estimate
    uint8_t *l_output = DAP_NEW_Z_SIZE(uint8_t, l_max_output);
    if (!l_output) {
        log_it(L_ERROR, "Failed to allocate transcode buffer (%zu bytes)", l_max_output);
        return false;
    }
    
    size_t l_in_pos = 0;
    size_t l_out_pos = 0;
    
    debug_if(s_debug_more, L_DEBUG, "Starting transcode: %zu bytes (%s) → UTF-8",
           a_len, dap_json_encoding_name(a_encoding));
    
    // Transcode loop
    while (l_in_pos < a_len) {
        uint32_t l_codepoint = 0;
        size_t l_bytes_read = 0;
        
        // Read character in source encoding
        if (!dap_json_read_char(a_input, l_in_pos, a_len, a_encoding,
                                &l_codepoint, &l_bytes_read)) {
            log_it(L_ERROR, "Failed to read character at position %zu (encoding=%s, remaining=%zu bytes)",
                   l_in_pos, dap_json_encoding_name(a_encoding), a_len - l_in_pos);
            DAP_DELETE(l_output);
            return false;
        }
        
        debug_if(s_debug_more, L_DEBUG, "Read codepoint U+%04X (%zu bytes) at pos %zu",
               l_codepoint, l_bytes_read, l_in_pos);
        
        // Encode as UTF-8
        size_t l_bytes_written = s_encode_utf8(l_codepoint, l_output + l_out_pos);
        if (l_bytes_written == 0) {
            log_it(L_ERROR, "Failed to encode codepoint U+%06X", l_codepoint);
            DAP_DELETE(l_output);
            return false;
        }
        
        l_in_pos += l_bytes_read;
        l_out_pos += l_bytes_written;
        
        // Safety check
        if (l_out_pos + 4 > l_max_output) {
            log_it(L_ERROR, "Output buffer overflow");
            DAP_DELETE(l_output);
            return false;
        }
    }
    
    // Shrink to actual size (optional optimization)
    uint8_t *l_final = DAP_NEW_Z_SIZE(uint8_t, l_out_pos + 1);
    if (l_final) {
        memcpy(l_final, l_output, l_out_pos);
        l_final[l_out_pos] = '\0';  // Null-terminate for safety
        DAP_DELETE(l_output);
        l_output = l_final;
    }
    
    *a_output = l_output;
    *a_output_len = l_out_pos;
    
    debug_if(s_debug_more, L_DEBUG, "Transcoded %zu bytes (%s) → %zu bytes (UTF-8)",
           a_len, dap_json_encoding_name(a_encoding), l_out_pos);
    
    return true;
}

