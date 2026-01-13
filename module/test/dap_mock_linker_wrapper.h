#pragma once

#include "dap_mock.h"

// ============================================================================
// DAP_MOCK_WRAPPER_CUSTOM - Low-level wrapper for custom mocks
// ============================================================================
// This macro is used internally by DAP_MOCK_CUSTOM (defined in dap_mock.h)
// Generator creates wrapper macros for ALL functions directly.
// For each function found in code, generator creates:
// - _DAP_MOCK_CUSTOM_FOR_<func_name>(return_type, params)
// 
// Uses direct concatenation with ##func_name to call the specific wrapper.
// If function is not found (no wrapper generated), compilation will fail (Fail-Fast).
// ============================================================================

/**
 * @brief Low-level wrapper macro for custom mocks
 * @details Used internally by DAP_MOCK_CUSTOM from dap_mock.h
 * @note Do NOT use directly! Use DAP_MOCK_CUSTOM instead
 */
#define DAP_MOCK_WRAPPER_CUSTOM(return_type, func_name, params) \
    _DAP_MOCK_CUSTOM_FOR_##func_name(return_type, params)

// ============================================================================
// Function-specific wrapper macros are generated in function_wrappers.h.tpl
// and included via return_type_macros.h (which is included via MACROS_HEADER).
// This file (dap_mock_linker_wrapper.h) contains only the base macros for
// DAP_MOCK_WRAPPER_CUSTOM that use the generated function-specific macros.
// ============================================================================
