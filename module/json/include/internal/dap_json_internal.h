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
 * @details This is the actual definition of the opaque dap_json_t type.
 *          Only for use by internal modules!
 */
struct dap_json {
    dap_json_value_t *value;         /**< Internal native value */
    int ref_count;                   /**< Reference counter */
    bool owns_value;                 /**< True if wrapper owns value */
    
    dap_json_mode_t mode;            /**< Operation mode (arena/malloc) */
    
    // Zero-copy support
    const char *input_buffer;        /**< Original JSON input buffer */
    size_t input_len;                /**< Input buffer length */
    
    // Tape format (Phase 3)
    dap_json_tape_entry_t *tape;     /**< Tape array (NULL if not built) */
    size_t tape_count;               /**< Number of tape entries */
    
    // Mode-specific data
    union {
        // ARENA_IMMUTABLE mode
        struct {
            void *stage2;                  /**< Pointer to dap_json_stage2_t */
            struct dap_json *parent;       /**< Parent wrapper for borrowed refs */
        } arena_data;
        
        // MALLOC_MUTABLE mode
        struct {
            bool _reserved;  // Placeholder
        } malloc_data;
    } mode_data;
};

#ifdef __cplusplus
}
#endif

#endif // DAP_JSON_INTERNAL_H
