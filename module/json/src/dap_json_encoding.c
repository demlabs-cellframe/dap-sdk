/**
 * @file dap_json_encoding.c
 * @brief JSON input encoding detection and utilities
 * @details Zero-copy multi-encoding support with branch prediction
 */

#include "internal/dap_json_encoding.h"
#include "dap_common.h"
#include <string.h>

#define LOG_TAG "dap_json_encoding"

// Debug flag: detailed logs (below WARNING level)
static bool s_debug_more = false;

/**
 * @brief Detect encoding by BOM
 */
static bool s_detect_bom(
    const uint8_t *a_input,
    size_t a_len,
    dap_json_encoding_info_t *a_info
)
{
    if (a_len < 2) {
        return false;  // Too short for any BOM
    }
    
    // Check UTF-32 BOMs first (4 bytes)
    if (a_len >= 4) {
        // UTF-32LE: FF FE 00 00
        if (a_input[0] == BOM_UTF32LE_0 &&
            a_input[1] == BOM_UTF32LE_1 &&
            a_input[2] == BOM_UTF32LE_2 &&
            a_input[3] == BOM_UTF32LE_3) {
            a_info->encoding = DAP_JSON_ENCODING_UTF32LE;
            a_info->bom_size = BOM_UTF32_SIZE;
            a_info->has_bom = true;
            return true;
        }
        
        // UTF-32BE: 00 00 FE FF
        if (a_input[0] == BOM_UTF32BE_0 &&
            a_input[1] == BOM_UTF32BE_1 &&
            a_input[2] == BOM_UTF32BE_2 &&
            a_input[3] == BOM_UTF32BE_3) {
            a_info->encoding = DAP_JSON_ENCODING_UTF32BE;
            a_info->bom_size = BOM_UTF32_SIZE;
            a_info->has_bom = true;
            return true;
        }
    }
    
    // Check UTF-8 BOM (3 bytes): EF BB BF
    if (a_len >= 3) {
        if (a_input[0] == BOM_UTF8_0 &&
            a_input[1] == BOM_UTF8_1 &&
            a_input[2] == BOM_UTF8_2) {
            a_info->encoding = DAP_JSON_ENCODING_UTF8;
            a_info->bom_size = BOM_UTF8_SIZE;
            a_info->has_bom = true;
            return true;
        }
    }
    
    // Check UTF-16 BOMs (2 bytes)
    // UTF-16LE: FF FE (but not followed by 00 00)
    if (a_input[0] == BOM_UTF16LE_0 && a_input[1] == BOM_UTF16LE_1) {
        // Disambiguate from UTF-32LE
        if (a_len >= 4 && a_input[2] == 0x00 && a_input[3] == 0x00) {
            // This is UTF-32LE, handled above
            return false;
        }
        a_info->encoding = DAP_JSON_ENCODING_UTF16LE;
        a_info->bom_size = BOM_UTF16_SIZE;
        a_info->has_bom = true;
        return true;
    }
    
    // UTF-16BE: FE FF
    if (a_input[0] == BOM_UTF16BE_0 && a_input[1] == BOM_UTF16BE_1) {
        a_info->encoding = DAP_JSON_ENCODING_UTF16BE;
        a_info->bom_size = BOM_UTF16_SIZE;
        a_info->has_bom = true;
        return true;
    }
    
    return false;  // No BOM found
}

/**
 * @brief Detect encoding by heuristics (structural character patterns)
 * @details JSON always starts with '{', '[', '"', digit, 't', 'f', or 'n'
 *          We can detect encoding by null byte patterns
 */
static dap_json_encoding_t s_detect_heuristic(
    const uint8_t *a_input,
    size_t a_len
)
{
    if (a_len < 4) {
        // Too short for heuristics, assume UTF-8
        return DAP_JSON_ENCODING_UTF8;
    }
    
    // Check first 4 bytes for null patterns
    uint8_t b0 = a_input[0];
    uint8_t b1 = a_input[1];
    uint8_t b2 = a_input[2];
    uint8_t b3 = a_input[3];
    
    // UTF-32LE: ASCII char followed by 3 nulls (e.g., 7B 00 00 00 for '{')
    if (b0 != 0 && b1 == 0 && b2 == 0 && b3 == 0) {
        return DAP_JSON_ENCODING_UTF32LE;
    }
    
    // UTF-32BE: 3 nulls followed by ASCII char (e.g., 00 00 00 7B for '{')
    if (b0 == 0 && b1 == 0 && b2 == 0 && b3 != 0) {
        return DAP_JSON_ENCODING_UTF32BE;
    }
    
    // UTF-16LE: ASCII char followed by null (e.g., 7B 00 for '{')
    if (b0 != 0 && b1 == 0) {
        return DAP_JSON_ENCODING_UTF16LE;
    }
    
    // UTF-16BE: null followed by ASCII char (e.g., 00 7B for '{')
    if (b0 == 0 && b1 != 0) {
        return DAP_JSON_ENCODING_UTF16BE;
    }
    
    // No nulls in first 4 bytes - likely UTF-8
    return DAP_JSON_ENCODING_UTF8;
}

/**
 * @brief Detect JSON input encoding
 */
bool dap_json_detect_encoding(
    const uint8_t *a_input,
    size_t a_len,
    dap_json_encoding_info_t *a_info
)
{
    if (!a_input || !a_info || a_len == 0) {
        return false;
    }
    
    // Initialize
    memset(a_info, 0, sizeof(dap_json_encoding_info_t));
    
    // Try BOM detection first (definitive)
    if (s_detect_bom(a_input, a_len, a_info)) {
        a_info->data_start = a_input + a_info->bom_size;
        a_info->data_len = a_len - a_info->bom_size;
        debug_if(s_debug_more, L_DEBUG, "Encoding detected by BOM: %s (%zu byte BOM)",
               dap_json_encoding_name(a_info->encoding), a_info->bom_size);
        return true;
    }
    
    // No BOM - use heuristics
    a_info->encoding = s_detect_heuristic(a_input, a_len);
    a_info->has_bom = false;
    a_info->bom_size = 0;
    a_info->data_start = a_input;
    a_info->data_len = a_len;
    
    debug_if(s_debug_more, L_DEBUG, "Encoding detected by heuristics: %s",
           dap_json_encoding_name(a_info->encoding));
    
    return true;
}

/**
 * @brief Get encoding name
 */
const char* dap_json_encoding_name(dap_json_encoding_t a_encoding)
{
    switch (a_encoding) {
        case DAP_JSON_ENCODING_UTF8:    return "UTF-8";
        case DAP_JSON_ENCODING_UTF16LE: return "UTF-16LE";
        case DAP_JSON_ENCODING_UTF16BE: return "UTF-16BE";
        case DAP_JSON_ENCODING_UTF32LE: return "UTF-32LE";
        case DAP_JSON_ENCODING_UTF32BE: return "UTF-32BE";
        default:                        return "Unknown";
    }
}

/**
 * @brief Get character size for encoding
 */
size_t dap_json_encoding_char_size(dap_json_encoding_t a_encoding)
{
    switch (a_encoding) {
        case DAP_JSON_ENCODING_UTF8:    return 1;  // Variable 1-4
        case DAP_JSON_ENCODING_UTF16LE:
        case DAP_JSON_ENCODING_UTF16BE: return 2;  // Variable 2-4
        case DAP_JSON_ENCODING_UTF32LE:
        case DAP_JSON_ENCODING_UTF32BE: return 4;  // Fixed 4
        default:                        return 1;
    }
}

/**
 * @brief Read UTF-16LE character
 */
static bool s_read_utf16le(
    const uint8_t *a_input,
    size_t a_pos,
    size_t a_len,
    uint32_t *a_codepoint,
    size_t *a_bytes_read
)
{
    if (a_pos + 2 > a_len) {
        return false;
    }
    
    uint16_t l_unit1 = (uint16_t)a_input[a_pos] | ((uint16_t)a_input[a_pos + 1] << 8);
    
    // Check for surrogate pair
    if (l_unit1 >= 0xD800 && l_unit1 <= 0xDBFF) {
        // High surrogate - need low surrogate
        if (a_pos + 4 > a_len) {
            return false;
        }
        uint16_t l_unit2 = (uint16_t)a_input[a_pos + 2] | ((uint16_t)a_input[a_pos + 3] << 8);
        if (l_unit2 < 0xDC00 || l_unit2 > 0xDFFF) {
            return false;  // Invalid low surrogate
        }
        *a_codepoint = 0x10000 + ((l_unit1 - 0xD800) << 10) + (l_unit2 - 0xDC00);
        *a_bytes_read = 4;
    } else {
        *a_codepoint = l_unit1;
        *a_bytes_read = 2;
    }
    
    return true;
}

/**
 * @brief Read UTF-16BE character
 */
static bool s_read_utf16be(
    const uint8_t *a_input,
    size_t a_pos,
    size_t a_len,
    uint32_t *a_codepoint,
    size_t *a_bytes_read
)
{
    if (a_pos + 2 > a_len) {
        return false;
    }
    
    uint16_t l_unit1 = ((uint16_t)a_input[a_pos] << 8) | (uint16_t)a_input[a_pos + 1];
    
    // Check for surrogate pair
    if (l_unit1 >= 0xD800 && l_unit1 <= 0xDBFF) {
        // High surrogate - need low surrogate
        if (a_pos + 4 > a_len) {
            return false;
        }
        uint16_t l_unit2 = ((uint16_t)a_input[a_pos + 2] << 8) | (uint16_t)a_input[a_pos + 3];
        if (l_unit2 < 0xDC00 || l_unit2 > 0xDFFF) {
            return false;  // Invalid low surrogate
        }
        *a_codepoint = 0x10000 + ((l_unit1 - 0xD800) << 10) + (l_unit2 - 0xDC00);
        *a_bytes_read = 4;
    } else {
        *a_codepoint = l_unit1;
        *a_bytes_read = 2;
    }
    
    return true;
}

/**
 * @brief Read UTF-32LE character
 */
static bool s_read_utf32le(
    const uint8_t *a_input,
    size_t a_pos,
    size_t a_len,
    uint32_t *a_codepoint,
    size_t *a_bytes_read
)
{
    if (a_pos + 4 > a_len) {
        return false;
    }
    
    *a_codepoint = (uint32_t)a_input[a_pos] |
                   ((uint32_t)a_input[a_pos + 1] << 8) |
                   ((uint32_t)a_input[a_pos + 2] << 16) |
                   ((uint32_t)a_input[a_pos + 3] << 24);
    *a_bytes_read = 4;
    
    // Validate codepoint
    if (*a_codepoint > 0x10FFFF) {
        return false;
    }
    
    return true;
}

/**
 * @brief Read UTF-32BE character
 */
static bool s_read_utf32be(
    const uint8_t *a_input,
    size_t a_pos,
    size_t a_len,
    uint32_t *a_codepoint,
    size_t *a_bytes_read
)
{
    if (a_pos + 4 > a_len) {
        return false;
    }
    
    *a_codepoint = ((uint32_t)a_input[a_pos] << 24) |
                   ((uint32_t)a_input[a_pos + 1] << 16) |
                   ((uint32_t)a_input[a_pos + 2] << 8) |
                   (uint32_t)a_input[a_pos + 3];
    *a_bytes_read = 4;
    
    // Validate codepoint
    if (*a_codepoint > 0x10FFFF) {
        return false;
    }
    
    return true;
}

/**
 * @brief Read UTF-8 character
 */
static bool s_read_utf8(
    const uint8_t *a_input,
    size_t a_pos,
    size_t a_len,
    uint32_t *a_codepoint,
    size_t *a_bytes_read
)
{
    if (a_pos >= a_len) {
        return false;
    }
    
    uint8_t l_byte = a_input[a_pos];
    
    // 1-byte (ASCII)
    if ((l_byte & 0x80) == 0) {
        *a_codepoint = l_byte;
        *a_bytes_read = 1;
        return true;
    }
    
    // 2-byte
    if ((l_byte & 0xE0) == 0xC0) {
        if (a_pos + 2 > a_len) return false;
        *a_codepoint = ((l_byte & 0x1F) << 6) | (a_input[a_pos + 1] & 0x3F);
        *a_bytes_read = 2;
        return true;
    }
    
    // 3-byte
    if ((l_byte & 0xF0) == 0xE0) {
        if (a_pos + 3 > a_len) return false;
        *a_codepoint = ((l_byte & 0x0F) << 12) |
                       ((a_input[a_pos + 1] & 0x3F) << 6) |
                       (a_input[a_pos + 2] & 0x3F);
        *a_bytes_read = 3;
        return true;
    }
    
    // 4-byte
    if ((l_byte & 0xF8) == 0xF0) {
        if (a_pos + 4 > a_len) return false;
        *a_codepoint = ((l_byte & 0x07) << 18) |
                       ((a_input[a_pos + 1] & 0x3F) << 12) |
                       ((a_input[a_pos + 2] & 0x3F) << 6) |
                       (a_input[a_pos + 3] & 0x3F);
        *a_bytes_read = 4;
        return true;
    }
    
    return false;  // Invalid UTF-8
}

/**
 * @brief Read next character from buffer (encoding-aware)
 */
bool dap_json_read_char(
    const uint8_t *a_input,
    size_t a_pos,
    size_t a_len,
    dap_json_encoding_t a_encoding,
    uint32_t *a_codepoint,
    size_t *a_bytes_read
)
{
    if (!a_input || !a_codepoint || !a_bytes_read) {
        return false;
    }
    
    // Branch prediction: UTF-8 is most common (99.9% of JSON)
    if (__builtin_expect(a_encoding == DAP_JSON_ENCODING_UTF8, 1)) {
        return s_read_utf8(a_input, a_pos, a_len, a_codepoint, a_bytes_read);
    }
    
    switch (a_encoding) {
        case DAP_JSON_ENCODING_UTF16LE:
            return s_read_utf16le(a_input, a_pos, a_len, a_codepoint, a_bytes_read);
        case DAP_JSON_ENCODING_UTF16BE:
            return s_read_utf16be(a_input, a_pos, a_len, a_codepoint, a_bytes_read);
        case DAP_JSON_ENCODING_UTF32LE:
            return s_read_utf32le(a_input, a_pos, a_len, a_codepoint, a_bytes_read);
        case DAP_JSON_ENCODING_UTF32BE:
            return s_read_utf32be(a_input, a_pos, a_len, a_codepoint, a_bytes_read);
        default:
            return false;
    }
}

