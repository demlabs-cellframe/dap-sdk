// ============================================================================
// Simplified _DAP_MOCK_MAP implementation
// ============================================================================
// This file includes all parts of the _DAP_MOCK_MAP macro system
// Each part is in a separate template file for clarity and maintainability
//
// Structure:
// 1. Core macros - entry point and basic routing
// 2. Param count routing - routes based on number of PARAM entries
// 3. First arg routing - handles "void" case for param_count=0
// 4. Count params - calculates number of PARAM entries from arg_count
// 5. Impl cond - routes to appropriate _DAP_MOCK_MAP_N macro
//
// NO ## USED - generator creates all macro names directly
// NO FALLBACKS - Fail-Fast: if value not found, compilation will fail

// ============================================================================
// Part 1: Core macros
// ============================================================================
{{#include "mock_map_core.h.tpl"}}

// ============================================================================
// Part 2: Param count routing
// ============================================================================
{{#include "mock_map_param_count_routing.h.tpl"}}

// ============================================================================
// Part 3: First arg routing (for param_count=0 case)
// ============================================================================
{{#include "mock_map_first_arg_routing.h.tpl"}}

// ============================================================================
// Part 4: Parameter counting
// ============================================================================
{{#include "mock_map_count_params.h.tpl"}}

// ============================================================================
// Part 5: Implementation conditional macros
// ============================================================================
{{#include "mock_map_impl_cond.h.tpl"}}

// ============================================================================
// Include additional generated macros
// ============================================================================
{{#if RETURN_TYPE_MACROS_FILE}}
{{#include RETURN_TYPE_MACROS_FILE}}
{{/if}}

{{#if SIMPLE_WRAPPER_MACROS_FILE}}
{{#include SIMPLE_WRAPPER_MACROS_FILE}}
{{/if}}
