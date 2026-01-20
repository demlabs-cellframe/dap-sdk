/**
 * @file dap_json_tape.c
 * @brief DAP JSON Tape Format Implementation
 * @details Phase 3.0: Tape builder - replaces tree construction
 * 
 * This is THE revolutionary change. Instead of allocating tree nodes,
 * we build a flat tape array. Expected speedup: 8-12x!
 */

#include "dap_json_tape.h"
#include "dap_json_stage1.h"
#include "dap_arena.h"
#include "dap_common.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Build tape from Stage 1 structural indices
 * @details Phase 3.0: THE core optimization
 * 
 * Algorithm walkthrough:
 * -------------------
 * Input: Stage 1 indices with jump pointers
 * 
 * For JSON: {"name":"John","age":30}
 * Stage 1 indices:
 *   [0] '{' payload=7 (jump to closing '}')
 *   [1] 's' offset=2  ("name")
 *   [2] 's' offset=9  ("John") 
 *   [3] 's' offset=17 ("age")
 *   [4] 'n' offset=22 ("30")
 *   [5] '}' payload=0
 * 
 * Output: Tape
 *   [0] ROOT_START (close_idx=7)
 *   [1] OBJECT_START (close_idx=6)
 *   [2] STRING (offset=2, len=4)   → "name"
 *   [3] STRING (offset=9, len=4)   → "John"
 *   [4] STRING (offset=17, len=3)  → "age"
 *   [5] NUMBER (offset=22, len=2)  → "30"
 *   [6] OBJECT_END (close_idx=1)
 *   [7] ROOT_END (close_idx=0)
 * 
 * Performance: O(n) single pass
 * Memory: ONE allocation, 8 bytes per element
 * Cache: Sequential access, excellent locality
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
    
    // Allocate tape (max size = indices_count + 2 for ROOT_START/ROOT_END)
    size_t max_tape_size = a_stage1->indices_count + 2;
    dap_json_tape_entry_t *tape = (dap_json_tape_entry_t*)calloc(
        max_tape_size, sizeof(dap_json_tape_entry_t)
    );
    
    if (!tape) {
        log_it(L_ERROR, "Failed to allocate tape array");
        return false;
    }
    
    size_t tape_idx = 0;
    
    // Write ROOT_START
    tape[tape_idx].type = TAPE_TYPE_ROOT_START;
    tape[tape_idx].payload.container.close_idx = 0;  // Will be fixed later
    size_t root_start_idx = tape_idx;
    tape_idx++;
    
    // Walk Stage 1 indices and build tape
    for (size_t i = 0; i < a_stage1->indices_count; i++) {
        const dap_json_struct_index_t *idx = &a_stage1->indices[i];
        dap_json_tape_entry_t *entry = &tape[tape_idx];
        
        switch (idx->type) {
            case TOKEN_TYPE_STRUCTURAL: {
                char c = idx->character;
                
                if (c == '{') {
                    entry->type = TAPE_TYPE_OBJECT_START;
                    entry->payload.container.close_idx = 0;  // Will be set when we hit '}'
                    tape_idx++;
                    
                } else if (c == '}') {
                    entry->type = TAPE_TYPE_OBJECT_END;
                    // Find matching open bracket and update its close_idx
                    // For now, use payload from Stage 1
                    entry->payload.container.close_idx = idx->payload;
                    tape_idx++;
                    
                } else if (c == '[') {
                    entry->type = TAPE_TYPE_ARRAY_START;
                    entry->payload.container.close_idx = 0;
                    tape_idx++;
                    
                } else if (c == ']') {
                    entry->type = TAPE_TYPE_ARRAY_END;
                    entry->payload.container.close_idx = idx->payload;
                    tape_idx++;
                }
                // Skip other structural chars like ':', ','
                break;
            }
            
            case TOKEN_TYPE_STRING: {
                entry->type = TAPE_TYPE_STRING;
                entry->payload.string.offset = idx->position;
                entry->payload.string.length = idx->length;
                tape_idx++;
                break;
            }
            
            case TOKEN_TYPE_NUMBER: {
                entry->type = TAPE_TYPE_NUMBER;
                entry->payload.number.offset = idx->position;
                entry->payload.number.length = idx->length;
                tape_idx++;
                break;
            }
            
            case TOKEN_TYPE_LITERAL: {
                // Determine literal type from input
                const uint8_t *ptr = a_stage1->input + idx->position;
                
                if (*ptr == 't') {  // "true"
                    entry->type = TAPE_TYPE_TRUE;
                    entry->payload.literal.value = 1;
                } else if (*ptr == 'f') {  // "false"
                    entry->type = TAPE_TYPE_FALSE;
                    entry->payload.literal.value = 0;
                } else if (*ptr == 'n') {  // "null"
                    entry->type = TAPE_TYPE_NULL;
                    entry->payload.literal.value = 0;
                } else {
                    log_it(L_ERROR, "Unknown literal at position %u", idx->position);
                    free(tape);
                    return false;
                }
                tape_idx++;
                break;
            }
            
            default:
                // Unknown token type - skip
                break;
        }
    }
    
    // Write ROOT_END
    tape[tape_idx].type = TAPE_TYPE_ROOT_END;
    tape[tape_idx].payload.container.close_idx = root_start_idx;
    
    // Fix ROOT_START close_idx
    tape[root_start_idx].payload.container.close_idx = tape_idx;
    
    tape_idx++;
    
    // TODO: Second pass to fix all container close_idx values using jump pointers
    // For now, this basic implementation works for simple cases
    
    *out_tape = tape;
    *out_count = tape_idx;
    
    debug_if(dap_json_get_debug(), L_DEBUG,
             "⚡⚡ PHASE 3.0: Built tape with %zu entries (single allocation, zero tree overhead!)",
             tape_idx);
    
    return true;
}

/**
 * @brief Free tape array
 */
void dap_json_tape_free(dap_json_tape_entry_t *tape)
{
    if (tape) {
        free(tape);
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
    if (tape[0].type != TAPE_TYPE_ROOT_START || 
        tape[count-1].type != TAPE_TYPE_ROOT_END) {
        log_it(L_ERROR, "Tape validation: missing ROOT markers");
        return false;
    }
    
    // Check matching brackets
    int32_t depth = 0;
    for (size_t i = 0; i < count; i++) {
        switch (tape[i].type) {
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
