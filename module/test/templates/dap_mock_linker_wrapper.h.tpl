#pragma once

#include "dap_mock.h"

/**
 * Main macro for creating custom mock wrappers.
 *
 * Usage:
 *   DAP_MOCK_WRAPPER_CUSTOM(return_type, func_name,
 *       (type1 name1, type2 name2, ...)) {
 *       // custom wrapper body
 *   }
 *
 * The parenthesized parameter list is passed through as __VA_ARGS__
 * and used directly as the function signature for __real_ and __wrap_.
 *
 * Generator creates _DAP_MOCK_WRAPPER_CUSTOM_FOR_<func_name> for each
 * mock function found in code. Uses ## concatenation to dispatch.
 * If function is not found (no wrapper generated), compilation fails (Fail-Fast).
 */
#define DAP_MOCK_WRAPPER_CUSTOM(return_type, func_name, ...) \
    _DAP_MOCK_WRAPPER_CUSTOM_FOR_##func_name(return_type, ##__VA_ARGS__)

// ============================================================================
// Function-specific wrapper macros are generated in function_wrappers.h.tpl
// and included via the mock_macros header.
// ============================================================================
