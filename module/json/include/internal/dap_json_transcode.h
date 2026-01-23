/**
 * @file dap_json_transcode.h
 * @brief Fast UTF-16/32 to UTF-8 transcoding for JSON
 * @details SIMD-optimized encoding conversion before parsing
 */

#ifndef DAP_JSON_TRANSCODE_H
#define DAP_JSON_TRANSCODE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "internal/dap_json_encoding.h"

/**
 * @brief Transcode UTF-16/32 to UTF-8
 * @details Allocates new buffer for UTF-8 output
 * 
 * @param[in] a_input Input buffer
 * @param[in] a_len Input length in bytes
 * @param[in] a_encoding Input encoding
 * @param[out] a_output Output UTF-8 buffer (caller must free)
 * @param[out] a_output_len Output length in bytes
 * @return true on success, false on error
 */
bool dap_json_transcode_to_utf8(
    const uint8_t *a_input,
    size_t a_len,
    dap_json_encoding_t a_encoding,
    uint8_t **a_output,
    size_t *a_output_len
);

#endif // DAP_JSON_TRANSCODE_H

