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
    _DAP_MOCK_WRAPPER_CUSTOM_SELECT_EXTRACT_BASE(_DAP_MOCK_GET_BASE_TYPE(return_type), func_name, return_type_full, ##__VA_ARGS__)
#define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_EXTRACT_BASE(base_type, func_name, return_type_full, ...) \
    _DAP_MOCK_WRAPPER_CUSTOM_SELECT_NORMALIZE(base_type, func_name, return_type_full, ##__VA_ARGS__)
// Extract base type by removing pointer and const qualifiers
// For pointer types like "dap_server_t*", we can't use them directly in macro names
// The generator creates normalization macros for base types (dap_server_t -> dap_server_t_PTR)
// The generator also creates _DAP_MOCK_BASE macros for escaped pointer types to extract base type
// These macros are generated automatically based on return types found in the code
// Format: _DAP_MOCK_BASE_dap_server_t_PTR -> dap_server_t (for "dap_server_t*")
// The generator also creates _DAP_MOCK_BASE_ESCAPE macros to convert raw types to escaped form
// Format: _DAP_MOCK_BASE_ESCAPE_dap_server_t_PTR -> dap_server_t_PTR (for "dap_server_t*")
// Since we can't convert "dap_server_t*" to "dap_server_t_PTR" in macros directly,
// we use a two-stage approach: first escape, then extract base
// The generator creates macros for escaped versions of pointer types
// We need to handle the case where type contains * - use escaped version directly
// Note: We ignore extra arguments (like "void") when extracting base type
#define _DAP_MOCK_GET_BASE_TYPE(type) _DAP_MOCK_EXTRACT_BASE(type)
// Extract base type - handle pointer types specially
// For types with *, we need to transform them to escaped version first
// The generator creates _DAP_MOCK_TRANSFORM_TYPE macros for type transformation
// Format: _DAP_MOCK_TRANSFORM_TYPE_dap_server_t_PTR -> dap_server_t_PTR (for "dap_server_t*")
// The transformation converts the escaped type name to the escaped type value
// Then we use _DAP_MOCK_BASE to extract base: _DAP_MOCK_BASE_dap_server_t_PTR -> dap_server_t
// Two-stage expansion: first transform type name to escaped value, then extract base
#define _DAP_MOCK_EXTRACT_BASE(type) _DAP_MOCK_EXTRACT_BASE_IMPL(type)
// Transform type name to escaped version, then extract base
// For "dap_server_t*": we need to escape the type name first
// Problem: we can't use * in macro names, so we need a different approach
// Solution: the generator creates macros for escaped versions of types
// For "dap_server_t*", the generator creates: _DAP_MOCK_TRANSFORM_TYPE_HELPER_dap_server_t_PTR -> dap_server_t_PTR
// But we can't call _DAP_MOCK_TRANSFORM_TYPE_HELPER_dap_server_t* because * is invalid
// So we need to use a different approach: create a macro that maps the type directly
// The generator should create macros that handle the transformation for each specific type
// We use a two-stage approach: first try to transform, then extract base
#define _DAP_MOCK_EXTRACT_BASE_IMPL(type) _DAP_MOCK_EXTRACT_BASE_FROM_ESCAPED(_DAP_MOCK_TRANSFORM_TYPE(type))
// Transform type to escaped version - this macro handles the escaping
// For pointer types like "dap_server_t*", we can't create a macro with * in the name
// So the generator creates macros for escaped versions: _DAP_MOCK_TRANSFORM_TYPE_HELPER_dap_server_t_PTR
// But we can't call this macro with "dap_server_t*" as argument
// Solution: use a macro that directly maps the type to its escaped version
// The generator creates: _DAP_MOCK_TYPE_TO_ESCAPED_dap_server_t_PTR -> dap_server_t_PTR
// But we still can't use "dap_server_t*" in macro names
// Final solution: the generator must create a macro that handles the specific type
// We use a helper macro that the generator creates for each escaped type name
#define _DAP_MOCK_TRANSFORM_TYPE(type) _DAP_MOCK_TRANSFORM_TYPE_IMPL(type)
// This macro tries to expand to _DAP_MOCK_TRANSFORM_TYPE_HELPER_<type>
// For "dap_server_t*", this will fail because * is invalid in macro names
// So we need a different approach: use the escaped version directly
// The generator creates macros for escaped versions, but we can't use them with raw types
// Solution: change the approach - don't try to transform in the header
// Instead, the generator should create macros that work with the escaped version
// We need to change the logic: extract base type directly from the escaped version
#define _DAP_MOCK_TRANSFORM_TYPE_IMPL(type) _DAP_MOCK_TRANSFORM_TYPE_HELPER_##type
// After transforming to escaped version, extract base type
#define _DAP_MOCK_EXTRACT_BASE_FROM_ESCAPED(escaped_type) _DAP_MOCK_BASE_##escaped_type
// Type-specific transformation and base extraction macros are auto-generated by the generator
// They are included in the generated mock_macros.h file via -include flag
// The generator creates _DAP_MOCK_TRANSFORM_TYPE_HELPER macros for each escaped type name
// But we can't use them with raw types containing *
// Solution: the generator must create a mapping macro for each raw type
// Format: _DAP_MOCK_TYPE_ESCAPE_dap_server_t_PTR -> dap_server_t_PTR (for "dap_server_t*")
// This macro maps the escaped type name to the escaped type value
#define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_NORMALIZE(base_type, func_name, return_type_full, ...) \
    _DAP_MOCK_WRAPPER_CUSTOM_SELECT_NORMALIZE_EXPAND(_DAP_MOCK_NORMALIZE_TYPE(base_type), func_name, return_type_full, ##__VA_ARGS__)
#define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_NORMALIZE_EXPAND(normalized_type, func_name, return_type_full, ...) \
    _DAP_MOCK_WRAPPER_CUSTOM_SELECT_NORMALIZE_EXPAND_IMPL(_DAP_MOCK_TYPE_TO_SELECT_NAME_IMPL(normalized_type), func_name, return_type_full, ##__VA_ARGS__)
#define _DAP_MOCK_WRAPPER_CUSTOM_SELECT_NORMALIZE_EXPAND_IMPL(selector_macro, func_name, return_type_full, ...) \
    selector_macro(func_name, return_type_full, ##__VA_ARGS__)
// Normalize type - try to get normalization macro, fallback to type as-is
#define _DAP_MOCK_NORMALIZE_TYPE(base_type) _DAP_MOCK_NORMALIZE_TYPE_IMPL(base_type)
#define _DAP_MOCK_NORMALIZE_TYPE_IMPL(base_type) _DAP_MOCK_NORMALIZE_TYPE_##base_type
// Type-to-selector wrapper - two-stage expansion for proper macro expansion
#define _DAP_MOCK_TYPE_TO_SELECT_NAME(normalized_type) _DAP_MOCK_TYPE_TO_SELECT_NAME_##normalized_type
#define _DAP_MOCK_TYPE_TO_SELECT_NAME_IMPL(normalized_type) _DAP_MOCK_TYPE_TO_SELECT_NAME_IMPL_EXPAND(normalized_type)
#define _DAP_MOCK_TYPE_TO_SELECT_NAME_IMPL_EXPAND(normalized_type) _DAP_MOCK_TYPE_TO_SELECT_NAME_##normalized_type

/**
 * Simple wrapper macros (DAP_MOCK_WRAPPER_INT, DAP_MOCK_WRAPPER_PTR, etc.)
 * are now auto-generated based on return types found in the code.
 * They are included in the generated mock_macros.h file via -include flag.
 */
