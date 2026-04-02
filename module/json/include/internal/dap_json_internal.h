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
 * @details Hybrid structure supporting both modes:
 *          - IMMUTABLE: tape-based parsing (read-only, fast)
 *          - MUTABLE: DOM-based creation (mutable, full API)
 */
struct dap_json {
    int ref_count;                   /**< Reference counter */
    dap_json_mode_t mode;            /**< Current mode */
    
    // Mode-specific data (union to save memory)
    union {
        // IMMUTABLE mode (parsed JSON → tape)
        struct {
            const char *input_buffer;        /**< JSON input buffer (used by iterator) */
            size_t input_len;                /**< Input buffer length */
            dap_json_tape_entry_t *tape;     /**< Tape array */
            size_t tape_count;               /**< Number of tape entries */
            size_t tape_offset;              /**< Starting position in tape (for sub-wrappers, 0 for root) */
            char *owned_input_copy;          /**< Parser-owned copy of input (root only, NULL for sub-wrappers) */
        } immutable;
        
        // MUTABLE mode (created JSON → DOM)
        struct {
            dap_json_value_t *value;         /**< Root DOM value */
            void *stage2;                    /**< stage2 context (for cleanup) */
        } mutable;
    } mode_data;
};

#ifdef __cplusplus
}
#endif

#endif // DAP_JSON_INTERNAL_H
