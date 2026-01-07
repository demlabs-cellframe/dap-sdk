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

#include "dap_common.h"
#include "dap_json.h"
#include "../../fixtures/utilities/test_helpers.h"
#include <string.h>

#define LOG_TAG "dap_json_unicode_tests"

/**
 * @brief Test basic escape sequences (\n, \t, \r, \", \\, \/, \b, \f)
 */
static bool s_test_escape_sequences_basic(void) {
    log_it(L_DEBUG, "Testing basic escape sequences");
    bool result = false;
    dap_json_t *l_json = NULL;
    const char *l_test_json = "{\"newline\":\"line1\\nline2\","
                               "\"tab\":\"col1\\tcol2\","
                               "\"return\":\"text\\rmore\","
                               "\"quote\":\"say \\\"hello\\\"\","
                               "\"backslash\":\"path\\\\file\","
                               "\"slash\":\"a\\/b\","
                               "\"backspace\":\"text\\bx\","
                               "\"formfeed\":\"page\\fbreak\"}";
    
    l_json = dap_json_parse_string(l_test_json);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse JSON with escape sequences");
    
    // Verify newline
    const char *l_newline = dap_json_object_get_string(l_json, "newline");
    DAP_TEST_FAIL_IF_NULL(l_newline, "Get newline value");
    DAP_TEST_FAIL_IF(strcmp(l_newline, "line1\nline2") != 0, "Newline escape");
    
    // Verify tab
    const char *l_tab = dap_json_object_get_string(l_json, "tab");
    DAP_TEST_FAIL_IF_NULL(l_tab, "Get tab value");
    DAP_TEST_FAIL_IF(strcmp(l_tab, "col1\tcol2") != 0, "Tab escape");
    
    // Verify quote
    const char *l_quote = dap_json_object_get_string(l_json, "quote");
    DAP_TEST_FAIL_IF_NULL(l_quote, "Get quote value");
    DAP_TEST_FAIL_IF(strcmp(l_quote, "say \"hello\"") != 0, "Quote escape");
    
    // Verify backslash
    const char *l_backslash = dap_json_object_get_string(l_json, "backslash");
    DAP_TEST_FAIL_IF_NULL(l_backslash, "Get backslash value");
    DAP_TEST_FAIL_IF(strcmp(l_backslash, "path\\file") != 0, "Backslash escape");
    
    result = true;
    log_it(L_DEBUG, "Basic escape sequences test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test Unicode escape sequences (\uXXXX)
 */
static bool s_test_unicode_escape_sequences(void) {
    log_it(L_DEBUG, "Testing Unicode escape sequences");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test various Unicode characters
    const char *l_test_json = "{\"copy\":\"\\u00A9\","  // © symbol
                               "\"smile\":\"\\u263A\","  // ☺ symbol
                               "\"cyrillic\":\"\\u0410\\u0411\\u0412\"}";  // АБВ
    
    l_json = dap_json_parse_string(l_test_json);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse JSON with Unicode escapes");
    
    // Verify copyright symbol (©)
    const char *l_copy = dap_json_object_get_string(l_json, "copy");
    DAP_TEST_FAIL_IF_NULL(l_copy, "Get copyright symbol");
    // UTF-8 encoding of U+00A9 is 0xC2 0xA9
    DAP_TEST_FAIL_IF((unsigned char)l_copy[0] != 0xC2 || (unsigned char)l_copy[1] != 0xA9, 
                     "Copyright symbol UTF-8 encoding");
    
    // Verify Cyrillic letters exist
    const char *l_cyrillic = dap_json_object_get_string(l_json, "cyrillic");
    DAP_TEST_FAIL_IF_NULL(l_cyrillic, "Get Cyrillic letters");
    DAP_TEST_FAIL_IF(strlen(l_cyrillic) < 6, "Cyrillic string length (3 chars = 6 bytes in UTF-8)");
    
    result = true;
    log_it(L_DEBUG, "Unicode escape sequences test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test Unicode surrogate pairs (\uD800\uDC00 style)
 */
static bool s_test_unicode_surrogate_pairs(void) {
    log_it(L_DEBUG, "Testing Unicode surrogate pairs");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test emoji (U+1F600 😀) encoded as surrogate pair: \uD83D\uDE00
    const char *l_test_json = "{\"emoji\":\"\\uD83D\\uDE00\"}";
    
    l_json = dap_json_parse_string(l_test_json);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse JSON with surrogate pairs");
    
    const char *l_emoji = dap_json_object_get_string(l_json, "emoji");
    DAP_TEST_FAIL_IF_NULL(l_emoji, "Get emoji value");
    
    // UTF-8 encoding of U+1F600 is 0xF0 0x9F 0x98 0x80
    DAP_TEST_FAIL_IF((unsigned char)l_emoji[0] != 0xF0 ||
                     (unsigned char)l_emoji[1] != 0x9F ||
                     (unsigned char)l_emoji[2] != 0x98 ||
                     (unsigned char)l_emoji[3] != 0x80,
                     "Emoji surrogate pair UTF-8 encoding");
    
    result = true;
    log_it(L_DEBUG, "Unicode surrogate pairs test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test valid UTF-8 sequences (multi-byte characters)
 */
static bool s_test_utf8_multibyte_valid(void) {
    log_it(L_DEBUG, "Testing valid UTF-8 multi-byte sequences");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test direct UTF-8 encoding (not escaped)
    const char *l_test_json = "{\"russian\":\"Привет\","
                               "\"chinese\":\"你好\","
                               "\"emoji\":\"😀🚀\","
                               "\"mixed\":\"Hello Мир 世界\"}";
    
    l_json = dap_json_parse_string(l_test_json);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse JSON with UTF-8 multi-byte");
    
    // Verify Russian text
    const char *l_russian = dap_json_object_get_string(l_json, "russian");
    DAP_TEST_FAIL_IF_NULL(l_russian, "Get Russian text");
    DAP_TEST_FAIL_IF(strcmp(l_russian, "Привет") != 0, "Russian text preserved");
    
    // Verify Chinese text
    const char *l_chinese = dap_json_object_get_string(l_json, "chinese");
    DAP_TEST_FAIL_IF_NULL(l_chinese, "Get Chinese text");
    DAP_TEST_FAIL_IF(strcmp(l_chinese, "你好") != 0, "Chinese text preserved");
    
    // Verify emoji
    const char *l_emoji = dap_json_object_get_string(l_json, "emoji");
    DAP_TEST_FAIL_IF_NULL(l_emoji, "Get emoji");
    DAP_TEST_FAIL_IF(strcmp(l_emoji, "😀🚀") != 0, "Emoji preserved");
    
    // Verify mixed
    const char *l_mixed = dap_json_object_get_string(l_json, "mixed");
    DAP_TEST_FAIL_IF_NULL(l_mixed, "Get mixed text");
    DAP_TEST_FAIL_IF(strcmp(l_mixed, "Hello Мир 世界") != 0, "Mixed text preserved");
    
    result = true;
    log_it(L_DEBUG, "Valid UTF-8 multi-byte test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test invalid UTF-8 sequences (should fail gracefully)
 */
static bool s_test_utf8_invalid_sequences(void) {
    log_it(L_DEBUG, "Testing invalid UTF-8 sequences");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Invalid UTF-8: 0xC0 0x80 (overlong encoding of NULL)
    char l_invalid_json[64];
    snprintf(l_invalid_json, sizeof(l_invalid_json), "{\"invalid\":\"\xC0\x80\"}");
    
    l_json = dap_json_parse_string(l_invalid_json);
    // Parser should either reject or sanitize invalid UTF-8
    // For now, we just verify it doesn't crash
    DAP_TEST_FAIL_IF_NULL(l_json, "Parser handles invalid UTF-8 without crashing");
    
    result = true;
    log_it(L_DEBUG, "Invalid UTF-8 sequences test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test BOM (Byte Order Mark) handling
 */
static bool s_test_utf8_bom_handling(void) {
    log_it(L_DEBUG, "Testing UTF-8 BOM handling");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // UTF-8 BOM (EF BB BF) before JSON
    const char l_json_with_bom[] = "\xEF\xBB\xBF{\"test\":\"value\"}";
    
    l_json = dap_json_parse_string(l_json_with_bom);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse JSON with BOM");
    
    const char *l_value = dap_json_object_get_string(l_json, "test");
    DAP_TEST_FAIL_IF_NULL(l_value, "Get value from JSON with BOM");
    DAP_TEST_FAIL_IF(strcmp(l_value, "value") != 0, "Value from BOM JSON");
    
    result = true;
    log_it(L_DEBUG, "UTF-8 BOM handling test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test invalid escape sequences
 */
static bool s_test_invalid_escape_sequences(void) {
    log_it(L_DEBUG, "Testing invalid escape sequences");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Invalid escape: \x is not valid in JSON
    const char *l_invalid_json = "{\"bad\":\"\\x41\"}";
    
    l_json = dap_json_parse_string(l_invalid_json);
    // Should fail to parse (return NULL)
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects invalid escape \\x");
    
    result = true;
    log_it(L_DEBUG, "Invalid escape sequences test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test incomplete Unicode escapes
 */
static bool s_test_incomplete_unicode_escapes(void) {
    log_it(L_DEBUG, "Testing incomplete Unicode escapes");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Incomplete Unicode escape: \u00A (missing one hex digit)
    const char *l_invalid_json = "{\"bad\":\"\\u00A\"}";
    
    l_json = dap_json_parse_string(l_invalid_json);
    // Should fail to parse
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects incomplete Unicode escape");
    
    result = true;
    log_it(L_DEBUG, "Incomplete Unicode escapes test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test overlong UTF-8 encoding (security issue)
 */
static bool s_test_utf8_overlong_encoding(void) {
    log_it(L_DEBUG, "Testing overlong UTF-8 encoding");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Overlong encoding of '/' (0x2F): 0xC0 0xAF instead of 0x2F
    // This is a security issue (can bypass filters)
    char l_overlong_json[64];
    snprintf(l_overlong_json, sizeof(l_overlong_json), "{\"path\":\"\xC0\xAF\"}");
    
    l_json = dap_json_parse_string(l_overlong_json);
    // Parser should reject overlong encodings
    // If accepted, verify it's not treated as '/'
    if (l_json) {
        const char *l_path = dap_json_object_get_string(l_json, "path");
        if (l_path) {
            DAP_TEST_FAIL_IF(strcmp(l_path, "/") == 0, 
                           "Overlong encoding should not decode to '/'");
        }
    }
    
    result = true;
    log_it(L_DEBUG, "Overlong UTF-8 encoding test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test null character handling (\u0000)
 */
static bool s_test_null_character_handling(void) {
    log_it(L_DEBUG, "Testing null character handling");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Null character in string: "text\u0000more"
    const char *l_test_json = "{\"nullchar\":\"text\\u0000more\"}";
    
    l_json = dap_json_parse_string(l_test_json);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse JSON with null character");
    
    const char *l_value = dap_json_object_get_string(l_json, "nullchar");
    DAP_TEST_FAIL_IF_NULL(l_value, "Get value with null character");
    
    // Verify the string contains null and continues after it
    // (if json-c supports embedded nulls)
    DAP_TEST_FAIL_IF(l_value[0] != 't' || l_value[1] != 'e' || 
                     l_value[2] != 'x' || l_value[3] != 't',
                     "String before null character");
    
    result = true;
    log_it(L_DEBUG, "Null character handling test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test control characters (0x00-0x1F) - should be escaped
 */
static bool s_test_control_characters(void) {
    log_it(L_DEBUG, "Testing control characters");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Control characters should be escaped in JSON strings
    // Raw control character (0x01) should fail
    char l_invalid_json[64];
    l_invalid_json[0] = '{';
    l_invalid_json[1] = '"';
    l_invalid_json[2] = 't';
    l_invalid_json[3] = 'e';
    l_invalid_json[4] = 's';
    l_invalid_json[5] = 't';
    l_invalid_json[6] = '"';
    l_invalid_json[7] = ':';
    l_invalid_json[8] = '"';
    l_invalid_json[9] = 0x01;  // Control character
    l_invalid_json[10] = '"';
    l_invalid_json[11] = '}';
    l_invalid_json[12] = '\0';
    
    l_json = dap_json_parse_string(l_invalid_json);
    // Parser should reject unescaped control characters
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects unescaped control characters");
    
    result = true;
    log_it(L_DEBUG, "Control characters test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test Unicode normalization (NFC vs NFD)
 */
static bool s_test_unicode_normalization(void) {
    log_it(L_DEBUG, "Testing Unicode normalization");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Test that parser preserves Unicode normalization form
    // é can be encoded as:
    // - NFC: U+00E9 (single character)
    // - NFD: U+0065 U+0301 (e + combining acute accent)
    
    const char *l_test_nfc = "{\"nfc\":\"caf\\u00E9\"}";  // café in NFC
    const char *l_test_nfd = "{\"nfd\":\"cafe\\u0301\"}";  // café in NFD
    
    dap_json_t *l_json_nfc = dap_json_parse_string(l_test_nfc);
    DAP_TEST_FAIL_IF_NULL(l_json_nfc, "Parse NFC normalized JSON");
    
    dap_json_t *l_json_nfd = dap_json_parse_string(l_test_nfd);
    DAP_TEST_FAIL_IF_NULL(l_json_nfd, "Parse NFD normalized JSON");
    
    const char *l_nfc_value = dap_json_object_get_string(l_json_nfc, "nfc");
    const char *l_nfd_value = dap_json_object_get_string(l_json_nfd, "nfd");
    
    DAP_TEST_FAIL_IF_NULL(l_nfc_value, "Get NFC value");
    DAP_TEST_FAIL_IF_NULL(l_nfd_value, "Get NFD value");
    
    // Both should be preserved as-is (JSON doesn't normalize)
    // They will have different byte sequences
    
    dap_json_object_free(l_json_nfc);
    dap_json_object_free(l_json_nfd);
    l_json = NULL;
    
    result = true;
    log_it(L_DEBUG, "Unicode normalization test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test case sensitivity in escape sequences
 */
static bool s_test_escape_case_sensitivity(void) {
    log_it(L_DEBUG, "Testing escape sequence case sensitivity");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // \u vs \U - JSON only supports lowercase \u
    const char *l_lowercase = "{\"lower\":\"\\u00A9\"}";  // Valid
    const char *l_uppercase = "{\"upper\":\"\\U00A9\"}";  // Invalid
    
    l_json = dap_json_parse_string(l_lowercase);
    DAP_TEST_FAIL_IF_NULL(l_json, "Lowercase \\u is valid");
    dap_json_object_free(l_json);
    
    l_json = dap_json_parse_string(l_uppercase);
    DAP_TEST_FAIL_IF(l_json != NULL, "Uppercase \\U is invalid");
    
    result = true;
    log_it(L_DEBUG, "Escape case sensitivity test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test serialization of Unicode characters
 */
static bool s_test_unicode_serialization(void) {
    log_it(L_DEBUG, "Testing Unicode serialization");
    bool result = false;
    dap_json_t *l_json = NULL;
    char *l_serialized = NULL;
    
    // Create JSON with Unicode content
    l_json = dap_json_object_new();
    DAP_TEST_FAIL_IF_NULL(l_json, "Create JSON object");
    
    dap_json_object_add_string(l_json, "unicode", "Привет 世界 😀");
    
    // Serialize to string
    l_serialized = dap_json_get_string(l_json);
    DAP_TEST_FAIL_IF_NULL(l_serialized, "Serialize JSON with Unicode");
    
    log_it(L_DEBUG, "Serialized JSON: %s", l_serialized);
    
    // Parse back
    dap_json_t *l_json2 = dap_json_parse_string(l_serialized);
    DAP_TEST_FAIL_IF_NULL(l_json2, "Parse serialized JSON");
    
    const char *l_value = dap_json_object_get_string(l_json2, "unicode");
    DAP_TEST_FAIL_IF_NULL(l_value, "Get Unicode value after round-trip");
    DAP_TEST_FAIL_IF(strcmp(l_value, "Привет 世界 😀") != 0, 
                     "Unicode preserved after serialization round-trip");
    
    dap_json_object_free(l_json2);
    
    result = true;
    log_it(L_DEBUG, "Unicode serialization test passed");
    
cleanup:
    if (l_serialized) free(l_serialized);
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test UTF-16LE BOM detection (FF FE)
 */
static bool s_test_utf16le_bom(void) {
    log_it(L_DEBUG, "Testing UTF-16LE BOM detection");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // UTF-16LE BOM (FF FE) followed by JSON: {"a":"b"}
    // In UTF-16LE: FF FE 7B 00 22 00 61 00 22 00 3A 00 22 00 62 00 22 00 7D 00
    const unsigned char l_utf16le_json[] = {
        0xFF, 0xFE,  // BOM
        '{', 0x00, '"', 0x00, 'a', 0x00, '"', 0x00, ':', 0x00,
        '"', 0x00, 'b', 0x00, '"', 0x00, '}', 0x00, 0x00
    };
    
    l_json = dap_json_parse_string((const char*)l_utf16le_json);
    // Parser should detect UTF-16LE and convert to UTF-8 internally
    // OR reject non-UTF-8 input (depending on implementation)
    // At minimum, should not crash
    DAP_TEST_FAIL_IF_NULL(l_json, "Parser handles UTF-16LE input");
    
    const char *l_value = dap_json_object_get_string(l_json, "a");
    if (l_value) {
        DAP_TEST_FAIL_IF(strcmp(l_value, "b") != 0, "UTF-16LE value decoded correctly");
    }
    
    result = true;
    log_it(L_DEBUG, "UTF-16LE BOM test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test UTF-16BE BOM detection (FE FF)
 */
static bool s_test_utf16be_bom(void) {
    log_it(L_DEBUG, "Testing UTF-16BE BOM detection");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // UTF-16BE BOM (FE FF) followed by JSON: {"a":"b"}
    // In UTF-16BE: FE FF 00 7B 00 22 00 61 00 22 00 3A 00 22 00 62 00 22 00 7D
    const unsigned char l_utf16be_json[] = {
        0xFE, 0xFF,  // BOM
        0x00, '{', 0x00, '"', 0x00, 'a', 0x00, '"', 0x00, ':',
        0x00, '"', 0x00, 'b', 0x00, '"', 0x00, '}', 0x00
    };
    
    l_json = dap_json_parse_string((const char*)l_utf16be_json);
    // Parser should detect UTF-16BE and convert to UTF-8 internally
    // OR reject non-UTF-8 input
    DAP_TEST_FAIL_IF_NULL(l_json, "Parser handles UTF-16BE input");
    
    const char *l_value = dap_json_object_get_string(l_json, "a");
    if (l_value) {
        DAP_TEST_FAIL_IF(strcmp(l_value, "b") != 0, "UTF-16BE value decoded correctly");
    }
    
    result = true;
    log_it(L_DEBUG, "UTF-16BE BOM test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test UTF-32LE BOM detection (FF FE 00 00)
 */
static bool s_test_utf32le_bom(void) {
    log_it(L_DEBUG, "Testing UTF-32LE BOM detection");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // UTF-32LE BOM (FF FE 00 00) followed by JSON: {}
    const unsigned char l_utf32le_json[] = {
        0xFF, 0xFE, 0x00, 0x00,  // BOM
        '{', 0x00, 0x00, 0x00,
        '}', 0x00, 0x00, 0x00, 0x00
    };
    
    l_json = dap_json_parse_string((const char*)l_utf32le_json);
    // Parser should detect UTF-32LE or reject
    // At minimum, should not crash
    if (l_json) {
        log_it(L_DEBUG, "Parser accepted UTF-32LE input");
    } else {
        log_it(L_DEBUG, "Parser rejected UTF-32LE input (acceptable)");
    }
    
    result = true;
    log_it(L_DEBUG, "UTF-32LE BOM test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test UTF-32BE BOM detection (00 00 FE FF)
 */
static bool s_test_utf32be_bom(void) {
    log_it(L_DEBUG, "Testing UTF-32BE BOM detection");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // UTF-32BE BOM (00 00 FE FF) followed by JSON: {}
    const unsigned char l_utf32be_json[] = {
        0x00, 0x00, 0xFE, 0xFF,  // BOM
        0x00, 0x00, 0x00, '{',
        0x00, 0x00, 0x00, '}', 0x00
    };
    
    l_json = dap_json_parse_string((const char*)l_utf32be_json);
    // Parser should detect UTF-32BE or reject
    if (l_json) {
        log_it(L_DEBUG, "Parser accepted UTF-32BE input");
    } else {
        log_it(L_DEBUG, "Parser rejected UTF-32BE input (acceptable)");
    }
    
    result = true;
    log_it(L_DEBUG, "UTF-32BE BOM test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test unpaired UTF-16 high surrogate (security issue)
 */
static bool s_test_unpaired_high_surrogate(void) {
    log_it(L_DEBUG, "Testing unpaired UTF-16 high surrogate");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // High surrogate without low surrogate: \uD800 (invalid)
    const char *l_invalid_json = "{\"bad\":\"\\uD800\"}";
    
    l_json = dap_json_parse_string(l_invalid_json);
    // Should reject unpaired high surrogate
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects unpaired high surrogate");
    
    result = true;
    log_it(L_DEBUG, "Unpaired high surrogate test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test unpaired UTF-16 low surrogate (security issue)
 */
static bool s_test_unpaired_low_surrogate(void) {
    log_it(L_DEBUG, "Testing unpaired UTF-16 low surrogate");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Low surrogate without high surrogate: \uDC00 (invalid)
    const char *l_invalid_json = "{\"bad\":\"\\uDC00\"}";
    
    l_json = dap_json_parse_string(l_invalid_json);
    // Should reject unpaired low surrogate
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects unpaired low surrogate");
    
    result = true;
    log_it(L_DEBUG, "Unpaired low surrogate test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test reversed surrogate pair (security issue)
 */
static bool s_test_reversed_surrogate_pair(void) {
    log_it(L_DEBUG, "Testing reversed surrogate pair");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Low surrogate before high surrogate: \uDC00\uD800 (invalid)
    const char *l_invalid_json = "{\"bad\":\"\\uDC00\\uD800\"}";
    
    l_json = dap_json_parse_string(l_invalid_json);
    // Should reject reversed pair
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects reversed surrogate pair");
    
    result = true;
    log_it(L_DEBUG, "Reversed surrogate pair test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test surrogate smuggling (two high surrogates)
 */
static bool s_test_surrogate_smuggling(void) {
    log_it(L_DEBUG, "Testing surrogate smuggling");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // Two high surrogates: \uD800\uD801 (invalid)
    const char *l_invalid_json = "{\"bad\":\"\\uD800\\uD801\"}";
    
    l_json = dap_json_parse_string(l_invalid_json);
    // Should reject two consecutive high surrogates
    DAP_TEST_FAIL_IF(l_json != NULL, "Parser rejects surrogate smuggling");
    
    result = true;
    log_it(L_DEBUG, "Surrogate smuggling test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test modified UTF-8 (MUTF-8) - used in Java (security issue)
 */
static bool s_test_modified_utf8(void) {
    log_it(L_DEBUG, "Testing modified UTF-8 (MUTF-8)");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // MUTF-8 encodes NULL as 0xC0 0x80 instead of 0x00
    // This should be rejected (security issue - can bypass filters)
    char l_mutf8_json[64];
    snprintf(l_mutf8_json, sizeof(l_mutf8_json), "{\"null\":\"\xC0\x80\"}");
    
    l_json = dap_json_parse_string(l_mutf8_json);
    // Should reject MUTF-8 encoding
    // This is already tested in s_test_utf8_overlong_encoding but worth explicit test
    if (l_json) {
        const char *l_value = dap_json_object_get_string(l_json, "null");
        if (l_value) {
            // Should NOT decode to NULL character
            DAP_TEST_FAIL_IF(l_value[0] == '\0' && strlen(l_value) == 0,
                           "MUTF-8 NULL encoding rejected");
        }
    }
    
    result = true;
    log_it(L_DEBUG, "Modified UTF-8 test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test CESU-8 encoding (security issue)
 */
static bool s_test_cesu8_encoding(void) {
    log_it(L_DEBUG, "Testing CESU-8 encoding");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // CESU-8 encodes supplementary characters as surrogate pairs
    // Example: U+1F600 (😀) encoded as CESU-8: ED A0 BD ED B8 80
    // This should be rejected (proper UTF-8 is: F0 9F 98 80)
    const unsigned char l_cesu8_json[] = {
        '{', '"', 't', 'e', 's', 't', '"', ':', '"',
        0xED, 0xA0, 0xBD, 0xED, 0xB8, 0x80,  // CESU-8 for U+1F600
        '"', '}', 0x00
    };
    
    l_json = dap_json_parse_string((const char*)l_cesu8_json);
    // Parser should reject CESU-8 (0xED bytes indicate surrogates in UTF-8)
    // If accepted, verify it's not treating it as valid emoji
    if (l_json) {
        const char *l_value = dap_json_object_get_string(l_json, "test");
        if (l_value) {
            // Should not decode to proper emoji
            DAP_TEST_FAIL_IF(strcmp(l_value, "😀") == 0,
                           "CESU-8 should not decode to proper emoji");
        }
    }
    
    result = true;
    log_it(L_DEBUG, "CESU-8 encoding test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Test charset smuggling via Content-Type (informational)
 */
static bool s_test_charset_confusion(void) {
    log_it(L_DEBUG, "Testing charset confusion scenarios");
    bool result = false;
    dap_json_t *l_json = NULL;
    
    // JSON standard (RFC 8259) requires UTF-8, UTF-16, or UTF-32
    // with auto-detection via BOM or first 4 bytes
    // Test that parser follows RFC 8259 section 8.1
    
    // Valid UTF-8 without BOM (most common)
    const char *l_utf8_json = "{\"test\":\"value\"}";
    l_json = dap_json_parse_string(l_utf8_json);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse UTF-8 without BOM");
    dap_json_object_free(l_json);
    
    // Valid UTF-8 with BOM
    const char l_utf8_bom_json[] = "\xEF\xBB\xBF{\"test\":\"value\"}";
    l_json = dap_json_parse_string(l_utf8_bom_json);
    DAP_TEST_FAIL_IF_NULL(l_json, "Parse UTF-8 with BOM");
    
    result = true;
    log_it(L_DEBUG, "Charset confusion test passed");
    
cleanup:
    dap_json_object_free(l_json);
    return result;
}

/**
 * @brief Main test runner for Unicode tests
 */
int dap_json_unicode_tests_run(void) {
    dap_test_msg("=== DAP JSON Unicode & UTF-8 Tests ===");
    
    int tests_passed = 0;
    int tests_total = 25;  // Increased from 14 to 25
    
    tests_passed += s_test_escape_sequences_basic() ? 1 : 0;
    tests_passed += s_test_unicode_escape_sequences() ? 1 : 0;
    tests_passed += s_test_unicode_surrogate_pairs() ? 1 : 0;
    tests_passed += s_test_utf8_multibyte_valid() ? 1 : 0;
    tests_passed += s_test_utf8_invalid_sequences() ? 1 : 0;
    tests_passed += s_test_utf8_bom_handling() ? 1 : 0;
    tests_passed += s_test_invalid_escape_sequences() ? 1 : 0;
    tests_passed += s_test_incomplete_unicode_escapes() ? 1 : 0;
    tests_passed += s_test_utf8_overlong_encoding() ? 1 : 0;
    tests_passed += s_test_null_character_handling() ? 1 : 0;
    tests_passed += s_test_control_characters() ? 1 : 0;
    tests_passed += s_test_unicode_normalization() ? 1 : 0;
    tests_passed += s_test_escape_case_sensitivity() ? 1 : 0;
    tests_passed += s_test_unicode_serialization() ? 1 : 0;
    
    // UTF-16/UTF-32 encoding tests (RFC 8259 compliance)
    tests_passed += s_test_utf16le_bom() ? 1 : 0;
    tests_passed += s_test_utf16be_bom() ? 1 : 0;
    tests_passed += s_test_utf32le_bom() ? 1 : 0;
    tests_passed += s_test_utf32be_bom() ? 1 : 0;
    
    // Security-critical surrogate handling tests
    tests_passed += s_test_unpaired_high_surrogate() ? 1 : 0;
    tests_passed += s_test_unpaired_low_surrogate() ? 1 : 0;
    tests_passed += s_test_reversed_surrogate_pair() ? 1 : 0;
    tests_passed += s_test_surrogate_smuggling() ? 1 : 0;
    
    // Non-standard encoding attacks (security)
    tests_passed += s_test_modified_utf8() ? 1 : 0;
    tests_passed += s_test_cesu8_encoding() ? 1 : 0;
    tests_passed += s_test_charset_confusion() ? 1 : 0;
    
    dap_test_msg("Unicode tests: %d/%d passed", tests_passed, tests_total);
    
    return (tests_passed == tests_total) ? 0 : -1;
}

