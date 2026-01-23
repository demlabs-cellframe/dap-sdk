#pragma once

#include "dap_mock.h"

// Helper macros for argument mapping
// PARAM is used in DAP_MOCK_WRAPPER_CUSTOM to specify function parameters
// Each PARAM(type, name) expands to two arguments: type and name
#define PARAM(type, name) type, name

#define _DAP_MOCK_PARAM_DECL(type, name) type name
#define _DAP_MOCK_PARAM_NAME(type, name) name
#define _DAP_MOCK_PARAM_CAST(type, name) (void*)(intptr_t)name

// Helper macros for creating argument arrays
// Supports empty __VA_ARGS__, "void", and PARAM(...) entries
// When empty or void, create empty array
#define _DAP_MOCK_ARGS_ARRAY(...) \
    _DAP_MOCK_ARGS_ARRAY_IMPL(__VA_ARGS__)
#define _DAP_MOCK_ARGS_ARRAY_IMPL(...) \
    ((void*[]){_DAP_MOCK_MAP(_DAP_MOCK_PARAM_CAST, ##__VA_ARGS__)})

#define _DAP_MOCK_ARGS_COUNT(...) \
    (_DAP_MOCK_MAP_COUNT_PARAMS(__VA_ARGS__))

/**
 * Main macro for creating custom mock wrappers
 * 
 * Usage: DAP_MOCK_WRAPPER_CUSTOM(return_type, func_name, PARAM(type1, name1), PARAM(type2, name2), ...)
 * 
 * Generator creates wrapper macros for ALL functions directly.
 * For each function found in code, generator creates:
 * - _DAP_MOCK_WRAPPER_CUSTOM_FOR_<func_name>(return_type, ...)
 * 
 * Uses direct concatenation with ##func_name to call the specific wrapper.
 * If function is not found (no wrapper generated), compilation will fail (Fail-Fast).
 */
#define DAP_MOCK_WRAPPER_CUSTOM(return_type, func_name, ...) \
    _DAP_MOCK_WRAPPER_CUSTOM_FOR_##func_name(return_type, ##__VA_ARGS__)

// ============================================================================
// Function-specific wrapper macros are generated in function_wrappers.h.tpl
// and included via return_type_macros.h (which is included via MACROS_HEADER).
// This file (dap_mock_linker_wrapper.h) contains only the base macros for
// DAP_MOCK_WRAPPER_CUSTOM that use the generated function-specific macros.
// ============================================================================
