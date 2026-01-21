/**
 * @file dap_json_internal.h
 * @brief Internal dap_json_t structure definition
 * @details For use by internal modules (tape, iterator) only
 * 
 * @author DAP SDK Team
 * @date 2026-01-21
 */

#ifndef DAP_JSON_INTERNAL_H
#define DAP_JSON_INTERNAL_H

#include "dap_json.h"
#include "dap_json_value.h"
#include "internal/dap_json_tape.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Internal structure of dap_json_t
 * @details Simplified tape-only structure. NO DOM!
 */
struct dap_json {
    int ref_count;                   /**< Reference counter */
    
    // Zero-copy support
    const char *input_buffer;        /**< Original JSON input buffer */
    size_t input_len;                /**< Input buffer length */
    
    // Tape format (ONLY access method)
    dap_json_tape_entry_t *tape;     /**< Tape array */
    size_t tape_count;               /**< Number of tape entries */
};

#ifdef __cplusplus
}
#endif

#endif // DAP_JSON_INTERNAL_H
