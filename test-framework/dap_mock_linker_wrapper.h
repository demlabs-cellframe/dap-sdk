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
 * Void custom mock wrapper implementation
 * 
 * Handles three cases for functions without parameters:
 * 1. Empty __VA_ARGS__: _DAP_MOCK_MAP produces empty parameter list
 * 2. "void" in __VA_ARGS__: _DAP_MOCK_MAP produces "void"
 * 3. PARAM(...) in __VA_ARGS__: _DAP_MOCK_MAP processes PARAM entries
 */
#define _DAP_MOCK_WRAPPER_CUSTOM_VOID(func_name, ...) \
    extern void __real_##func_name(_DAP_MOCK_MAP(_DAP_MOCK_PARAM_DECL, ##__VA_ARGS__)); \
    static void __mock_impl_##func_name(_DAP_MOCK_MAP(_DAP_MOCK_PARAM_DECL, ##__VA_ARGS__)); \
    void __wrap_##func_name(_DAP_MOCK_MAP(_DAP_MOCK_PARAM_DECL, ##__VA_ARGS__)) { \
        void **__wrap_args = _DAP_MOCK_ARGS_ARRAY(__VA_ARGS__); \
        int __wrap_args_count = _DAP_MOCK_ARGS_COUNT(__VA_ARGS__); \
        /* Prepare call: check enabled, execute delay, record call BEFORE custom implementation */ \
        bool __wrap_mock_enabled = dap_mock_prepare_call(g_mock_##func_name, __wrap_args, __wrap_args_count); \
        if (__wrap_mock_enabled) { \
            /* Call custom implementation - call count already incremented by dap_mock_prepare_call */ \
            __mock_impl_##func_name(_DAP_MOCK_MAP(_DAP_MOCK_PARAM_NAME, ##__VA_ARGS__)); \
        } else { \
            /* Mock disabled, call real function */ \
            __real_##func_name(_DAP_MOCK_MAP(_DAP_MOCK_PARAM_NAME, ##__VA_ARGS__)); \
        } \
    } \
    static void __mock_impl_##func_name(_DAP_MOCK_MAP(_DAP_MOCK_PARAM_DECL, ##__VA_ARGS__))

/**
 * Non-void custom mock wrapper implementation
 * 
 * Handles three cases for functions without parameters:
 * 1. Empty __VA_ARGS__: _DAP_MOCK_MAP produces empty parameter list
 * 2. "void" in __VA_ARGS__: _DAP_MOCK_MAP produces "void"
 * 3. PARAM(...) in __VA_ARGS__: _DAP_MOCK_MAP processes PARAM entries
 */
#define _DAP_MOCK_WRAPPER_CUSTOM_NONVOID(return_type, func_name, ...) \
    extern return_type __real_##func_name(_DAP_MOCK_MAP(_DAP_MOCK_PARAM_DECL, ##__VA_ARGS__)); \
    static return_type __mock_impl_##func_name(_DAP_MOCK_MAP(_DAP_MOCK_PARAM_DECL, ##__VA_ARGS__)); \
    return_type __wrap_##func_name(_DAP_MOCK_MAP(_DAP_MOCK_PARAM_DECL, ##__VA_ARGS__)) { \
        void **__wrap_args = _DAP_MOCK_ARGS_ARRAY(__VA_ARGS__); \
        int __wrap_args_count = _DAP_MOCK_ARGS_COUNT(__VA_ARGS__); \
        return_type __wrap_result = (return_type){0}; \
        /* Prepare call: check enabled, execute delay, record call BEFORE custom implementation */ \
        bool __wrap_mock_enabled = dap_mock_prepare_call(g_mock_##func_name, __wrap_args, __wrap_args_count); \
        if (__wrap_mock_enabled) { \
            /* Call custom implementation - call count already incremented by dap_mock_prepare_call */ \
            __wrap_result = __mock_impl_##func_name(_DAP_MOCK_MAP(_DAP_MOCK_PARAM_NAME, ##__VA_ARGS__)); \
            /* Check if return_value was set via DAP_MOCK_SET_RETURN */ \
            dap_mock_function_state_t *__wrap_mock_state = g_mock_##func_name; \
            if (__wrap_mock_state) { \
                /* Use return_value override if set (check if ptr is not NULL or other fields are set) */ \
                void *__wrap_override = __wrap_mock_state->return_value.ptr; \
                if (__wrap_override != NULL) { \
                    __wrap_result = *(return_type*)__wrap_override; \
                } \
            } \
            /* Record call with result */ \
            dap_mock_record_call(__wrap_mock_state, __wrap_args, __wrap_args_count, (void*)(intptr_t)__wrap_result); \
        } else { \
            /* Mock disabled, call real function */ \
            __wrap_result = __real_##func_name(_DAP_MOCK_MAP(_DAP_MOCK_PARAM_NAME, ##__VA_ARGS__)); \
        } \
        return __wrap_result; \
    } \
    static return_type __mock_impl_##func_name(_DAP_MOCK_MAP(_DAP_MOCK_PARAM_DECL, ##__VA_ARGS__))

/**
 * Main macro for creating custom mock wrappers
 * 
 * Usage: DAP_MOCK_WRAPPER_CUSTOM(return_type, func_name, PARAM(type1, name1), PARAM(type2, name2), ...)
 * 
 * Supports three ways to specify functions without parameters:
 * 1. DAP_MOCK_WRAPPER_CUSTOM(int, func_name) - no parameters
 * 2. DAP_MOCK_WRAPPER_CUSTOM(int, func_name, void) - explicit void
 * 3. DAP_MOCK_WRAPPER_CUSTOM(int, func_name, PARAM(...)) - using PARAM macro
 * 
 * Requires auto-generated mock macros header to be included via CMake's -include flag.
 * The selection logic is handled entirely by generated macros in the auto-generated header file.
 * Generated macros route to appropriate void or non-void implementation based on return_type.
 * 
 * For pointer types like "dap_server_t*", the generator creates macros for base types only.
 * All type extraction and normalization logic is handled by the generator - header just routes.
 */
#define DAP_MOCK_WRAPPER_CUSTOM(return_type, func_name, ...) \
    _DAP_MOCK_WRAPPER_CUSTOM_SELECT(return_type, func_name, return_type, __VA_ARGS__)

// Internal routing macros - extract base type, normalize, and route to generated selector
#define _DAP_MOCK_WRAPPER_CUSTOM_SELECT(return_type, func_name, return_type_full, ...) \
    _DAP_MOCK_WRAPPER_CUSTOM_SELECT_EXTRACT(return_type, func_name, return_type_full, ##__VA_ARGS__)
#define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_EXTRACT(return_type, func_name, return_type_full, ...) \
    _DAP_MOCK_WRAPPER_CUSTOM_SELECT_EXTRACT_BASE(_DAP_MOCK_GET_BASE_TYPE(return_type, ##__VA_ARGS__), func_name, return_type_full, ##__VA_ARGS__)
#define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_EXTRACT_BASE(base_type, func_name, return_type_full, ...) \
    _DAP_MOCK_WRAPPER_CUSTOM_SELECT_NORMALIZE(base_type, func_name, return_type_full, ##__VA_ARGS__)
#define _DAP_MOCK_GET_BASE_TYPE(type, ...) type
#define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_NORMALIZE(base_type, func_name, return_type_full, ...) \
    _DAP_MOCK_WRAPPER_CUSTOM_SELECT_NORMALIZE_EXPAND(_DAP_MOCK_NORMALIZE_TYPE(base_type), func_name, return_type_full, ##__VA_ARGS__)
#define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_NORMALIZE_EXPAND(normalized_type, func_name, return_type_full, ...) \
    _DAP_MOCK_WRAPPER_CUSTOM_SELECT_NORMALIZE_EXPAND_IMPL(_DAP_MOCK_TYPE_TO_SELECT_NAME_IMPL(normalized_type), func_name, return_type_full, ##__VA_ARGS__)
#define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_NORMALIZE_EXPAND_IMPL(selector_macro, func_name, return_type_full, ...) \
    selector_macro(func_name, return_type_full, ##__VA_ARGS__)
#define _DAP_MOCK_NORMALIZE_TYPE(base_type) _DAP_MOCK_NORMALIZE_TYPE_##base_type
#define _DAP_MOCK_TYPE_TO_SELECT_NAME(normalized_type) _DAP_MOCK_TYPE_TO_SELECT_NAME_##normalized_type
// Two-stage expansion for _DAP_MOCK_TYPE_TO_SELECT_NAME_IMPL to ensure proper macro expansion
#define _DAP_MOCK_TYPE_TO_SELECT_NAME_IMPL(normalized_type) _DAP_MOCK_TYPE_TO_SELECT_NAME_IMPL_EXPAND(normalized_type)
#define _DAP_MOCK_TYPE_TO_SELECT_NAME_IMPL_EXPAND(normalized_type) _DAP_MOCK_TYPE_TO_SELECT_NAME_##normalized_type

/**
 * Simple wrapper macros (DAP_MOCK_WRAPPER_INT, DAP_MOCK_WRAPPER_PTR, etc.)
 * are now auto-generated based on return types found in the code.
 * They are included in the generated mock_macros.h file via -include flag.
 */
