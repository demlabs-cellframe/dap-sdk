/**
 * @file dap_json_encoding.h
 * @brief JSON input encoding detection and multi-encoding support
 * @details Supports UTF-8, UTF-16LE, UTF-16BE, UTF-32LE, UTF-32BE
 * 
 * Architecture:
 * - Zero-copy encoding-aware parsing (no conversion)
 * - BOM detection + heuristics
 * - Fast dispatch with branch prediction (UTF-8 is most common)
 * - Generic Stage 1 with encoding parameter
 * - Encoding-specific optimizations
 * 
 * @author AI Assistant
 * @date 2026-01-10
 */

#ifndef DAP_JSON_ENCODING_H
#define DAP_JSON_ENCODING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief JSON input encoding types
 */
typedef enum {
    DAP_JSON_ENCODING_UNKNOWN = 0,
    DAP_JSON_ENCODING_UTF8,       /**< UTF-8 (1-4 bytes per codepoint) */
    DAP_JSON_ENCODING_UTF16LE,    /**< UTF-16 Little Endian (2-4 bytes) */
    DAP_JSON_ENCODING_UTF16BE,    /**< UTF-16 Big Endian (2-4 bytes) */
    DAP_JSON_ENCODING_UTF32LE,    /**< UTF-32 Little Endian (4 bytes) */
    DAP_JSON_ENCODING_UTF32BE     /**< UTF-32 Big Endian (4 bytes) */
} dap_json_encoding_t;

/**
 * @brief BOM (Byte Order Mark) signatures
 */
#define BOM_UTF8_SIZE 3
#define BOM_UTF16_SIZE 2
#define BOM_UTF32_SIZE 4

// UTF-8 BOM: EF BB BF
#define BOM_UTF8_0 0xEF
#define BOM_UTF8_1 0xBB
#define BOM_UTF8_2 0xBF

// UTF-16LE BOM: FF FE
#define BOM_UTF16LE_0 0xFF
#define BOM_UTF16LE_1 0xFE

// UTF-16BE BOM: FE FF
#define BOM_UTF16BE_0 0xFE
#define BOM_UTF16BE_1 0xFF

// UTF-32LE BOM: FF FE 00 00
#define BOM_UTF32LE_0 0xFF
#define BOM_UTF32LE_1 0xFE
#define BOM_UTF32LE_2 0x00
#define BOM_UTF32LE_3 0x00

// UTF-32BE BOM: 00 00 FE FF
#define BOM_UTF32BE_0 0x00
#define BOM_UTF32BE_1 0x00
#define BOM_UTF32BE_2 0xFE
#define BOM_UTF32BE_3 0xFF

/**
 * @brief Encoding detection result
 */
typedef struct {
    dap_json_encoding_t encoding;  /**< Detected encoding */
    size_t bom_size;               /**< BOM size in bytes (0 if no BOM) */
    bool has_bom;                  /**< True if BOM was detected */
    const uint8_t *data_start;     /**< Pointer to actual JSON data (after BOM) */
    size_t data_len;               /**< Data length (excluding BOM) */
} dap_json_encoding_info_t;

/**
 * @brief Detect JSON input encoding
 * @details Uses BOM if present, otherwise heuristics
 * 
 * Detection priority:
 * 1. BOM (definitive)
 * 2. Heuristics (structural character patterns)
 * 3. Assume UTF-8 (most common)
 * 
 * @param[in] a_input Input buffer
 * @param[in] a_len Input length
 * @param[out] a_info Encoding info
 * @return true on success, false on error
 */
bool dap_json_detect_encoding(
    const uint8_t *a_input,
    size_t a_len,
    dap_json_encoding_info_t *a_info
);

/**
 * @brief Get encoding name (for logging)
 * @param[in] a_encoding Encoding type
 * @return Encoding name string
 */
const char* dap_json_encoding_name(dap_json_encoding_t a_encoding);

/**
 * @brief Get character size for encoding
 * @param[in] a_encoding Encoding type
 * @return Minimum character size in bytes
 */
size_t dap_json_encoding_char_size(dap_json_encoding_t a_encoding);

/**
 * @brief Read next character from buffer (encoding-aware)
 * @details Reads one Unicode codepoint from buffer
 * 
 * @param[in] a_input Input buffer
 * @param[in] a_pos Current position
 * @param[in] a_len Buffer length
 * @param[in] a_encoding Input encoding
 * @param[out] a_codepoint Output codepoint
 * @param[out] a_bytes_read Bytes consumed
 * @return true on success, false on error
 */
bool dap_json_read_char(
    const uint8_t *a_input,
    size_t a_pos,
    size_t a_len,
    dap_json_encoding_t a_encoding,
    uint32_t *a_codepoint,
    size_t *a_bytes_read
);

#endif // DAP_JSON_ENCODING_H

