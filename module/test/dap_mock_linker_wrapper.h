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

/**
 * Helper to extract just parameter names for forwarding
 * Example: (void *a_stream, size_t a_size) -> (a_stream, a_size)
 */

/**
 * Create wrapper with custom logic
 * Most flexible - you write the wrapper body yourself
 */
#define DAP_MOCK_WRAPPER_CUSTOM(return_type, func_name, params) \
    extern return_type __real_##func_name params; \
    return_type __wrap_##func_name params

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

