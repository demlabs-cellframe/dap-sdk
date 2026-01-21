/**
 * @file dap_json_tape.c
 * @brief DAP JSON Tape Format Implementation
 * @details Tape builder - converts structural indices to linear tape
 * 
 * Instead of allocating tree nodes, we build a flat tape array.
 * 
 * **Key Optimizations:**
 * - Arena allocation: faster than malloc
 * - Direct Stage 1 reuse: zero redundant calculations
 * - Single-pass construction: optimal cache behavior
 * - Pure uint64_t entries: portable and fast
 */

#include "internal/dap_json_tape.h"
#include "internal/dap_json_stage1.h"
#include "dap_arena.h"
#include "dap_common.h"
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "dap_json_tape"

/* ========================================================================== */
/*                      THREAD-LOCAL TAPE ARENA                               */
/* ========================================================================== */

/**
 * @brief Thread-local arena for tape allocation
 * @details Shared arena for all JSON parsing in this thread.
 *          Auto-initialized on first use, thread-safe via _Thread_local.
 * 
 * **Design:**
 * - Single arena per thread (not per-parse like Stage 2)
 * - Reused across multiple parse operations
 * - Reset between parses for memory efficiency
 * - Initial size: 64KB (good for most JSON documents)
 */
static _Thread_local dap_arena_t *s_thread_tape_arena = NULL;

#define SMALL_TAPE_STACK_SIZE 256  // Stack buffer for small JSON (<1KB)

/**
 * @brief Build tape from Stage 1 structural indices
 * @details High-performance tape builder
 * 
 * Algorithm walkthrough:
 * ---------------------
 * Input: Stage 1 indices with jump pointers ALREADY CALCULATED!
 * 
 * For JSON: {"name":"John","age":30}
 * 
 * Stage 1 indices (with our jump pointers):
 *   [0] '{' payload=5 (jump to closing '}' at index 5)
 *   [1] '"' offset=2  len=4 ("name")
 *   [2] '"' offset=9  len=4 ("John") 
 *   [3] '"' offset=17 len=3 ("age")
 *   [4] 'n' offset=22 len=2 ("30")
 *   [5] '}' payload=0 (back reference to open)
 * 
 * Output: Tape (uint64_t entries with [type|payload])
 *   [0] ROOT_START    | payload=7 → close at 7
 *   [1] OBJECT_START  | payload=6 → close at 6 (from Stage 1!)
 *   [2] STRING        | payload=2 → offset in buffer
 *   [3] STRING        | payload=9 → offset in buffer
 *   [4] STRING        | payload=17 → offset in buffer
 *   [5] NUMBER        | payload=22 → offset in buffer
 *   [6] OBJECT_END    | payload=1 → back to open
 *   [7] ROOT_END      | payload=0 → back to root
 * 
 * **KEY INSIGHT:** We use Stage 1 jump pointers DIRECTLY!
 * No second pass needed - just copy payload from Stage 1.
 * 
 * **Our Optimizations:**
 * - Thread-local arena: zero malloc overhead, auto thread-safe
 * - Zero redundant work: direct Stage 1 payload reuse
 * - Single pass: optimal cache locality
 * - Sequential writes: CPU write-combining friendly
 * 
 * Performance: O(n) single pass, ZERO redundant work
 * Memory: Thread-local arena allocation, 8 bytes per element
 * Cache: Sequential writes, perfect locality
 * 
 * @param[in] a_stage1 Stage 1 output with structural indices
 * @param[out] out_tape Pointer to receive tape array
 * @param[out] out_count Pointer to receive tape entry count
 * @return true on success, false on error
 */
bool dap_json_build_tape(
    const dap_json_stage1_t *a_stage1,
    dap_json_tape_entry_t **out_tape,
    size_t *out_count
)
{
    if (!a_stage1 || !out_tape || !out_count) {
        log_it(L_ERROR, "Invalid arguments to dap_json_build_tape");
        return false;
    }
    
    // Initialize thread-local arena on first use
    if (!s_thread_tape_arena) {
        dap_arena_opt_t opts = {
            .initial_size = 64 * 1024,  // 64KB initial (good for most JSON)
            .max_page_size = 16 * 1024 * 1024,  // 16MB max
            .allow_small_pages = false
        };
        s_thread_tape_arena = dap_arena_new_opt(opts);  // Pass by value
        
        if (!s_thread_tape_arena) {
            log_it(L_ERROR, "Failed to create thread-local tape arena");
            return false;
        }
        
        debug_if(dap_json_get_debug(), L_DEBUG,
                 "Created thread-local tape arena (64KB initial, 16MB max)");
    }
    
    // Arena auto-reuses freed space from previous allocations
    // No reset needed here - arena grows naturally and reuses memory
    
    // Allocate tape from thread-local arena
    // Max size = indices_count + 2 for ROOT markers
    size_t max_tape_size = a_stage1->indices_count + 2;
    dap_json_tape_entry_t *tape = (dap_json_tape_entry_t*)dap_arena_alloc(
        s_thread_tape_arena, 
        max_tape_size * sizeof(dap_json_tape_entry_t)
    );
    
    if (!tape) {
        log_it(L_ERROR, "Failed to allocate tape array from thread-local arena");
        return false;
    }
    
    size_t tape_idx = 0;
    
    // Write ROOT_START
    tape[tape_idx] = dap_tape_make_entry(TAPE_TYPE_ROOT_START, 0);
    size_t root_start_idx = tape_idx;
    tape_idx++;
    
    // MAIN LOOP: Transform Stage 1 indices to tape entries
    // This is where the magic happens - direct use of Stage 1 data!
    for (size_t i = 0; i < a_stage1->indices_count; i++) {
        const dap_json_struct_index_t *idx = &a_stage1->indices[i];
        
        switch (idx->type) {
            case TOKEN_TYPE_STRUCTURAL: {
                char c = idx->character;
                
                if (c == '{') {
                    // CRITICAL: Use Stage 1 payload directly as jump pointer!
                    // No need to recalculate - Stage 1 already did the work!
                    uint64_t close_idx = (uint64_t)idx->payload + tape_idx;
                    tape[tape_idx] = dap_tape_make_entry(TAPE_TYPE_OBJECT_START, close_idx);
                    tape_idx++;
                    
                } else if (c == '}') {
                    // Closing bracket - payload points back to open (optional)
                    tape[tape_idx] = dap_tape_make_entry(TAPE_TYPE_OBJECT_END, 0);
                    tape_idx++;
                    
                } else if (c == '[') {
                    uint64_t close_idx = (uint64_t)idx->payload + tape_idx;
                    tape[tape_idx] = dap_tape_make_entry(TAPE_TYPE_ARRAY_START, close_idx);
                    tape_idx++;
                    
                } else if (c == ']') {
                    tape[tape_idx] = dap_tape_make_entry(TAPE_TYPE_ARRAY_END, 0);
                    tape_idx++;
                }
                // Skip other structural chars like ':', ','
                break;
            }
            
            case TOKEN_TYPE_STRING: {
                // Zero-copy: payload = offset into input buffer
                // Length is in Stage 1 idx->length, we'll access it when needed
                uint64_t offset = (uint64_t)idx->position;
                tape[tape_idx] = dap_tape_make_entry(TAPE_TYPE_STRING, offset);
                tape_idx++;
                break;
            }
            
            case TOKEN_TYPE_NUMBER: {
                // Lazy parse: payload = offset, parse later
                uint64_t offset = (uint64_t)idx->position;
                tape[tape_idx] = dap_tape_make_entry(TAPE_TYPE_NUMBER, offset);
                tape_idx++;
                break;
            }
            
            case TOKEN_TYPE_LITERAL: {
                // Determine literal type from input
                const uint8_t *ptr = a_stage1->input + idx->position;
                uint8_t lit_type;
                
                if (*ptr == 't') {  // "true"
                    lit_type = TAPE_TYPE_TRUE;
                } else if (*ptr == 'f') {  // "false"
                    lit_type = TAPE_TYPE_FALSE;
                } else if (*ptr == 'n') {  // "null"
                    lit_type = TAPE_TYPE_NULL;
                } else {
                    log_it(L_ERROR, "Unknown literal at position %u", idx->position);
                    return false;
                }
                
                tape[tape_idx] = dap_tape_make_entry(lit_type, 0);
                tape_idx++;
                break;
            }
            
            default:
                // Unknown token type - skip
                break;
        }
    }
    
    // Write ROOT_END
    tape[tape_idx] = dap_tape_make_entry(TAPE_TYPE_ROOT_END, root_start_idx);
    
    // Fix ROOT_START close_idx
    tape[root_start_idx] = dap_tape_make_entry(TAPE_TYPE_ROOT_START, tape_idx);
    
    tape_idx++;
    
    *out_tape = tape;
    *out_count = tape_idx;
    
    debug_if(dap_json_get_debug(), L_DEBUG,
             "⚡PHASE 3.0: Built tape with %zu entries "
             "(thread-local arena, ZERO malloc, direct Stage 1 copy!)",
             tape_idx);
    
    return true;
}

/**
 * @brief Free tape array
 * @details Tape is allocated from arena, so this is NO-OP!
 *          Arena cleanup happens when arena is freed.
 */
/**
 * @brief Free tape array
 * @details Tape is allocated from thread-local arena, so this is NO-OP!
 *          Arena memory persists across parses for efficiency.
 * 
 * NOTE: Arena grows naturally and reuses memory automatically.
 *       Use dap_json_tape_arena_reset() for explicit cleanup if needed.
 */
void dap_json_tape_free(dap_json_tape_entry_t *tape)
{
    // NO-OP: tape allocated from thread-local arena
    // Arena memory persists across parses for efficiency
    // Will be freed when thread exits or via explicit cleanup
    (void)tape;
}

/**
 * @brief Reset thread-local tape arena (optional cleanup)
 * @details Call this to reclaim tape arena memory in current thread.
 *          Useful for long-running threads to free memory after processing.
 * 
 * ⚠️ WARNING: Invalidates ALL tapes created in this thread!
 *             Only call when you're sure no tapes are in use.
 */
void dap_json_tape_arena_reset(void)
{
    if (s_thread_tape_arena) {
        dap_arena_reset(s_thread_tape_arena);
        debug_if(dap_json_get_debug(), L_DEBUG,
                 "Reset thread-local tape arena (memory reclaimed)");
    }
}

/**
 * @brief Free thread-local tape arena (thread cleanup)
 * @details Call this when thread is exiting to free all arena memory.
 * 
 * ⚠️ WARNING: Invalidates ALL tapes created in this thread!
 */
void dap_json_tape_arena_free(void)
{
    if (s_thread_tape_arena) {
        dap_arena_free(s_thread_tape_arena);
        s_thread_tape_arena = NULL;
        debug_if(dap_json_get_debug(), L_DEBUG,
                 "Freed thread-local tape arena (thread cleanup)");
    }
}

/**
 * @brief Validate tape structure
 * @details Debug helper - checks tape integrity
 */
bool dap_json_tape_validate(
    const dap_json_tape_entry_t *tape,
    size_t count
)
{
    if (!tape || count < 2) {
        return false;
    }
    
    // Check ROOT markers
    uint8_t first_type = dap_tape_get_type(tape[0]);
    uint8_t last_type = dap_tape_get_type(tape[count-1]);
    
    if (first_type != TAPE_TYPE_ROOT_START || last_type != TAPE_TYPE_ROOT_END) {
        log_it(L_ERROR, "Tape validation: missing ROOT markers (first=%d, last=%d)", 
               first_type, last_type);
        return false;
    }
    
    // Check matching brackets
    int32_t depth = 0;
    for (size_t i = 0; i < count; i++) {
        uint8_t type = dap_tape_get_type(tape[i]);
        
        switch (type) {
            case TAPE_TYPE_ROOT_START:
            case TAPE_TYPE_OBJECT_START:
            case TAPE_TYPE_ARRAY_START:
                depth++;
                break;
                
            case TAPE_TYPE_ROOT_END:
            case TAPE_TYPE_OBJECT_END:
            case TAPE_TYPE_ARRAY_END:
                depth--;
                if (depth < 0) {
                    log_it(L_ERROR, "Tape validation: unmatched closing bracket at %zu", i);
                    return false;
                }
                break;
                
            default:
                break;
        }
    }
    
    if (depth != 0) {
        log_it(L_ERROR, "Tape validation: unmatched brackets (depth=%d)", depth);
        return false;
    }
    
    return true;
}
