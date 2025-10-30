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
 * PARAM macro - just passes through both arguments
 * This is the user-facing macro for declaring parameters
 */
#define PARAM(type, name) (type, name)

/**
 * Extract type from PARAM(type, name) -> type
 */
#define _DAP_MOCK_PARAM_TYPE(type, name) type

/**
 * Extract name from PARAM(type, name) -> name
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
 */
#define _DAP_MOCK_PARAM_DECL(type, name) type name

/**
 * Macros to apply operation to each PARAM() in parameter list
 * These generate comma-separated results
 */
#define _DAP_MOCK_MAP_0(macro)
#define _DAP_MOCK_MAP_1(macro, p1) macro p1
#define _DAP_MOCK_MAP_2(macro, p1, p2) macro p1, macro p2
#define _DAP_MOCK_MAP_3(macro, p1, p2, p3) macro p1, macro p2, macro p3
#define _DAP_MOCK_MAP_4(macro, p1, p2, p3, p4) macro p1, macro p2, macro p3, macro p4
#define _DAP_MOCK_MAP_5(macro, p1, p2, p3, p4, p5) macro p1, macro p2, macro p3, macro p4, macro p5
#define _DAP_MOCK_MAP_6(macro, p1, p2, p3, p4, p5, p6) macro p1, macro p2, macro p3, macro p4, macro p5, macro p6
#define _DAP_MOCK_MAP_7(macro, p1, p2, p3, p4, p5, p6, p7) macro p1, macro p2, macro p3, macro p4, macro p5, macro p6, macro p7
#define _DAP_MOCK_MAP_8(macro, p1, p2, p3, p4, p5, p6, p7, p8) macro p1, macro p2, macro p3, macro p4, macro p5, macro p6, macro p7, macro p8
#define _DAP_MOCK_MAP_9(macro, p1, p2, p3, p4, p5, p6, p7, p8, p9) macro p1, macro p2, macro p3, macro p4, macro p5, macro p6, macro p7, macro p8, macro p9
#define _DAP_MOCK_MAP_10(macro, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10) macro p1, macro p2, macro p3, macro p4, macro p5, macro p6, macro p7, macro p8, macro p9, macro p10
#define _DAP_MOCK_MAP_11(macro, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11) macro p1, macro p2, macro p3, macro p4, macro p5, macro p6, macro p7, macro p8, macro p9, macro p10, macro p11
#define _DAP_MOCK_MAP_12(macro, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12) macro p1, macro p2, macro p3, macro p4, macro p5, macro p6, macro p7, macro p8, macro p9, macro p10, macro p11, macro p12
#define _DAP_MOCK_MAP_13(macro, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13) macro p1, macro p2, macro p3, macro p4, macro p5, macro p6, macro p7, macro p8, macro p9, macro p10, macro p11, macro p12, macro p13
#define _DAP_MOCK_MAP_14(macro, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14) macro p1, macro p2, macro p3, macro p4, macro p5, macro p6, macro p7, macro p8, macro p9, macro p10, macro p11, macro p12, macro p13, macro p14
#define _DAP_MOCK_MAP_15(macro, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15) macro p1, macro p2, macro p3, macro p4, macro p5, macro p6, macro p7, macro p8, macro p9, macro p10, macro p11, macro p12, macro p13, macro p14, macro p15
#define _DAP_MOCK_MAP_16(macro, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16) macro p1, macro p2, macro p3, macro p4, macro p5, macro p6, macro p7, macro p8, macro p9, macro p10, macro p11, macro p12, macro p13, macro p14, macro p15, macro p16

/**
 * Count number of PARAM() entries (supports 0-16 params)
 */
#define _DAP_MOCK_NARGS(...) _DAP_MOCK_NARGS_IMPL(__VA_ARGS__, 16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0)
#define _DAP_MOCK_NARGS_IMPL(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16,N,...) N

/**
 * Select appropriate MAP macro based on param count
 */
#define _DAP_MOCK_MAP(macro, ...) _DAP_MOCK_MAP_IMPL(_DAP_MOCK_NARGS(__VA_ARGS__), macro, __VA_ARGS__)
#define _DAP_MOCK_MAP_IMPL(N, macro, ...) _DAP_MOCK_MAP_IMPL2(N, macro, __VA_ARGS__)
#define _DAP_MOCK_MAP_IMPL2(N, macro, ...) _DAP_MOCK_MAP_##N(macro, __VA_ARGS__)

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
        void *__wrap_args[] = {_DAP_MOCK_MAP(_DAP_MOCK_PARAM_TO_VOIDPTR, __VA_ARGS__)}; \
        if (dap_mock_prepare_call(g_mock_##func_name, __wrap_args, sizeof(__wrap_args)/sizeof(void*))) { \
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

