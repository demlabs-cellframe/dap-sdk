/**
 * @file dap_mock_linker_wrapper.h
 * @brief Helper macros for linker-based function mocking
 * @details Provides macros to create __wrap_ functions that work with GNU ld --wrap option
 * 
 * How it works:
 * 1. Use -Wl,--wrap=function_name in linker flags
 * 2. Linker redirects calls to function_name -> __wrap_function_name
 * 3. __wrap_ function can call original via __real_function_name
 * 4. Mock framework tracks calls and controls behavior
 * 
 * @date 2025-10-27
 * @copyright (c) 2025 Cellframe Network
 */

#ifndef DAP_MOCK_LINKER_WRAPPER_H
#define DAP_MOCK_LINKER_WRAPPER_H

#include "dap_mock.h"

// ===========================================================================
// MACRO HELPERS: Parameter processing from PARAM(type, name) format
// ===========================================================================

/**
 * PARAM macro - expands to type, name (without parentheses)
 * This format allows ##__VA_ARGS__ to work correctly for empty detection
 * Usage: PARAM(int, a), PARAM(char*, b) expands to: int, a, char*, b
 */
#define PARAM(type, name) type, name

/**
 * Extract type from PARAM(type, name) -> type
 * Now PARAM expands to "type, name", so we need to extract first argument
 */
#define _DAP_MOCK_PARAM_TYPE(type, name) type

/**
 * Extract name from PARAM(type, name) -> name
 * Now PARAM expands to "type, name", so we need to extract second argument
 */
#define _DAP_MOCK_PARAM_NAME(type, name) name

/**
 * Extract name and cast to void* from PARAM(type, name)
 * Always uses uintptr_t as intermediate cast to avoid warnings
 * This is safe because we're just storing the value for tracking, not dereferencing
 */
#define _DAP_MOCK_PARAM_TO_VOIDPTR(type, name) \
    ((void*)(uintptr_t)(name))

/**
 * Convert PARAM(type, name) to "type name" for function signature
 * Now PARAM expands to "type, name", so this just combines them
 */
#define _DAP_MOCK_PARAM_DECL(type, name) type name

/**
 * Count number of PARAM() entries (supports 0-16 params)
 * Uses a trick: when __VA_ARGS__ is empty, we detect it by checking if
 * the first expansion yields a different result
 * 
 * Note: These macros are defined in generated macros header via -include.
 * Fail-fast: if DAP_MOCK_GENERATED_MACROS_H is set but macros are missing,
 * compilation will fail naturally when they are used.
 */
// Macros are defined in generated header via -include flag
// No fallback - fail-fast approach

/**
 * Detect empty __VA_ARGS__
 * Uses GCC extension ##__VA_ARGS__ to remove comma when empty
 * 
 * Note: This macro is defined in generated macros header via -include.
 * Fail-fast: if DAP_MOCK_GENERATED_MACROS_H is set but macros are missing,
 * compilation will fail naturally when they are used.
 */
// Macros are defined in generated header via -include flag
// No fallback - fail-fast approach

/**
 * Main mapping macro _DAP_MOCK_MAP
 * 
 * Note: This macro is defined in generated macros header via -include.
 * Fail-fast: if DAP_MOCK_GENERATED_MACROS_H is set but macros are missing,
 * compilation will fail naturally when they are used.
 */
// Macros are defined in generated header via -include flag
// No fallback - fail-fast approach

/**
 * Helper macro to create args array, handling empty parameter list
 * For 0 params: returns NULL (will be handled by dap_mock_prepare_call)
 * For N params: returns array literal {param1, param2, ...}
 * Uses _DAP_MOCK_IS_EMPTY for reliable empty detection
 * Note: Uses GCC extension ##__VA_ARGS__ to handle empty lists correctly
 * 
 * NOTE: _DAP_MOCK_MAP must be defined in generated macros header
 */
#define _DAP_MOCK_ARGS_ARRAY(...) \
    (_DAP_MOCK_IS_EMPTY(__VA_ARGS__) ? NULL : (void*[]){_DAP_MOCK_MAP(_DAP_MOCK_PARAM_TO_VOIDPTR, __VA_ARGS__)})

/**
 * Helper macro to get args count, handling empty parameter list
 * For 0 params: returns 0
 * For N params: returns N
 * Uses _DAP_MOCK_IS_EMPTY for reliable empty detection
 * Note: Uses GCC extension ##__VA_ARGS__ to handle empty lists correctly
 */
#define _DAP_MOCK_ARGS_COUNT(...) \
    (_DAP_MOCK_IS_EMPTY(__VA_ARGS__) ? 0 : _DAP_MOCK_NARGS(__VA_ARGS__))

/**
 * Create wrapper with custom logic (NO DUPLICATION!)
 * 
 * This macro creates a two-level wrapper:
 * 1. __wrap_func_name - automatic wrapper that checks enabled, executes delay, records call
 * 2. __mock_impl_func_name - your custom implementation (callback body)
 * 
 * Usage:
 *   DAP_MOCK_WRAPPER_CUSTOM(int, my_func,
 *       PARAM(int, a),
 *       PARAM(const char*, b),
 *       PARAM(bool, flag)
 *   ) {
 *       // Your custom mock logic here
 *       return a + strlen(b);
 *   }
 * 
 * The PARAM(type, name) format allows the macro to automatically generate:
 * - Function signature: (int a, const char* b, bool flag)
 * - Parameter forwarding: (a, b, flag)
 * - void* array with proper casts: {(void*)(intptr_t)a, (void*)b, (void*)(intptr_t)flag}
 * 
 * Parameters:
 *   - return_type: Return type of the function
 *   - func_name: Name of the function to mock
 *   - ...: Parameter list as PARAM(type, name), ...
 * 
 * The wrapper automatically:
 * - Generates function signature from PARAM() list
 * - Generates parameter names for forwarding
 * - Generates void* array with proper casting using _Generic()
 * - Checks if mock is enabled
 * - Executes configured delay
 * - Records the call with arguments (properly cast)
 * - Calls your callback if enabled
 * - Calls __real_func if disabled
 */
#define DAP_MOCK_WRAPPER_CUSTOM(return_type, func_name, ...) \
    extern return_type __real_##func_name(_DAP_MOCK_MAP(_DAP_MOCK_PARAM_DECL, __VA_ARGS__)); \
    static return_type __mock_impl_##func_name(_DAP_MOCK_MAP(_DAP_MOCK_PARAM_DECL, __VA_ARGS__)); \
    return_type __wrap_##func_name(_DAP_MOCK_MAP(_DAP_MOCK_PARAM_DECL, __VA_ARGS__)) { \
        void **__wrap_args = _DAP_MOCK_ARGS_ARRAY(__VA_ARGS__); \
        int __wrap_args_count = _DAP_MOCK_ARGS_COUNT(__VA_ARGS__); \
        if (dap_mock_prepare_call(g_mock_##func_name, __wrap_args, __wrap_args_count)) { \
            return __mock_impl_##func_name(_DAP_MOCK_MAP(_DAP_MOCK_PARAM_NAME, __VA_ARGS__)); \
        } \
        return __real_##func_name(_DAP_MOCK_MAP(_DAP_MOCK_PARAM_NAME, __VA_ARGS__)); \
    } \
    static return_type __mock_impl_##func_name(_DAP_MOCK_MAP(_DAP_MOCK_PARAM_DECL, __VA_ARGS__))

/**
 * Create wrapper for function returning int
 * Automatically forwards to real function if mock not enabled
 */
#define DAP_MOCK_WRAPPER_INT(func_name, params, args) \
    extern int __real_##func_name params; \
    int __wrap_##func_name params { \
        if (g_mock_##func_name && g_mock_##func_name->enabled) { \
            void *l_args_array[] = args; \
            int l_ret = (int)(intptr_t)g_mock_##func_name->return_value.ptr; \
            dap_mock_record_call(g_mock_##func_name, l_args_array, \
                                sizeof(l_args_array)/sizeof(void*), (void*)(intptr_t)l_ret); \
            return l_ret; \
        } \
        return __real_##func_name args; \
    }

/**
 * Create wrapper for function returning pointer
 */
#define DAP_MOCK_WRAPPER_PTR(func_name, params, args) \
    extern void* __real_##func_name params; \
    void* __wrap_##func_name params { \
        if (g_mock_##func_name && g_mock_##func_name->enabled) { \
            void *l_args_array[] = args; \
            void *l_ret = g_mock_##func_name->return_value.ptr; \
            dap_mock_record_call(g_mock_##func_name, l_args_array, \
                                sizeof(l_args_array)/sizeof(void*), l_ret); \
            return l_ret; \
        } \
        return __real_##func_name args; \
    }

/**
 * Create wrapper for void function
 */
#define DAP_MOCK_WRAPPER_VOID_FUNC(func_name, params, args) \
    extern void __real_##func_name params; \
    void __wrap_##func_name params { \
        if (g_mock_##func_name && g_mock_##func_name->enabled) { \
            void *l_args_array[] = args; \
            dap_mock_record_call(g_mock_##func_name, l_args_array, \
                                sizeof(l_args_array)/sizeof(void*), NULL); \
            return; \
        } \
        __real_##func_name args; \
    }

/**
 * Create wrapper for function returning bool
 */
#define DAP_MOCK_WRAPPER_BOOL(func_name, params, args) \
    extern bool __real_##func_name params; \
    bool __wrap_##func_name params { \
        if (g_mock_##func_name && g_mock_##func_name->enabled) { \
            void *l_args_array[] = args; \
            bool l_ret = (bool)(intptr_t)g_mock_##func_name->return_value.ptr; \
            dap_mock_record_call(g_mock_##func_name, l_args_array, \
                                sizeof(l_args_array)/sizeof(void*), (void*)(intptr_t)l_ret); \
            return l_ret; \
        } \
        return __real_##func_name args; \
    }

/**
 * Create wrapper for function returning size_t
 */
#define DAP_MOCK_WRAPPER_SIZE_T(func_name, params, args) \
    extern size_t __real_##func_name params; \
    size_t __wrap_##func_name params { \
        if (g_mock_##func_name && g_mock_##func_name->enabled) { \
            void *l_args_array[] = args; \
            size_t l_ret = (size_t)(intptr_t)g_mock_##func_name->return_value.ptr; \
            dap_mock_record_call(g_mock_##func_name, l_args_array, \
                                sizeof(l_args_array)/sizeof(void*), (void*)(intptr_t)l_ret); \
            return l_ret; \
        } \
        return __real_##func_name args; \
    }

#endif // DAP_MOCK_LINKER_WRAPPER_H

