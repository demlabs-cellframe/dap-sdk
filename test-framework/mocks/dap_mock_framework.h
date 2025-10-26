/**
 * @file dap_mock_framework.h
 * @brief Generic mock framework for DAP SDK dependencies
 * @details Provides infrastructure for mocking DAP SDK functions in unit tests.
 *          Uses function pointer replacement pattern with call tracking.
 * 
 * @date 2025-10-26
 * @copyright (c) 2025 Cellframe Network
 */

#ifndef DAP_MOCK_FRAMEWORK_H
#define DAP_MOCK_FRAMEWORK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>

// ===========================================================================
// MOCK CALL TRACKING
// ===========================================================================

#define DAP_MOCK_MAX_CALLS 100

typedef struct dap_mock_call_record {
    const char *function_name;
    uint64_t timestamp;
    void *args[10];
    void *return_value;
    int call_count;
} dap_mock_call_record_t;

typedef struct dap_mock_function_state {
    const char *name;
    bool enabled;
    int call_count;
    int max_calls;
    dap_mock_call_record_t calls[DAP_MOCK_MAX_CALLS];
    pthread_mutex_t lock;
} dap_mock_function_state_t;

// ===========================================================================
// MOCK FRAMEWORK API
// ===========================================================================

int dap_mock_framework_init(void);
void dap_mock_framework_deinit(void);
void dap_mock_framework_reset_all(void);
dap_mock_function_state_t* dap_mock_register(const char *a_name);
void dap_mock_set_enabled(dap_mock_function_state_t *a_state, bool a_enabled);
void dap_mock_record_call(dap_mock_function_state_t *a_state, void **a_args, int a_arg_count, void *a_return_value);
int dap_mock_get_call_count(dap_mock_function_state_t *a_state);
dap_mock_call_record_t* dap_mock_get_last_call(dap_mock_function_state_t *a_state);
void dap_mock_reset(dap_mock_function_state_t *a_state);
bool dap_mock_was_called_with(dap_mock_function_state_t *a_state, int a_arg_index, void *a_expected_value);

// ===========================================================================
// HELPER MACROS
// ===========================================================================

/**
 * Declare mock state for a function with auto-registration support
 * Usage: DAP_MOCK_DECLARE(dap_stream_write);
 * 
 * This creates:
 * 1. Static pointer to mock state: g_mock_##func_name
 * 2. Auto-init function: dap_mock_auto_init_##func_name()
 */
#define DAP_MOCK_DECLARE(func_name) \
    static dap_mock_function_state_t *g_mock_##func_name = NULL; \
    static inline void dap_mock_auto_init_##func_name(void) __attribute__((constructor)); \
    static inline void dap_mock_auto_init_##func_name(void) { \
        if (g_mock_##func_name == NULL) { \
            g_mock_##func_name = dap_mock_register(#func_name); \
        } \
    }

/**
 * Define wrapper function that linker will use instead of real function
 * This macro creates __wrap_FUNCNAME that calls real function or mock
 * 
 * Usage in test file:
 * DAP_MOCK_WRAPPER(int, dap_stream_write, 
 *                  (void *stream, void *data, size_t size),
 *                  (stream, data, size))
 * 
 * Then in CMakeLists.txt add:
 * target_link_options(test_name PRIVATE -Wl,--wrap=dap_stream_write)
 */
#define DAP_MOCK_WRAPPER(return_type, func_name, params_with_types, params_names) \
    extern return_type __real_##func_name params_with_types; \
    return_type __wrap_##func_name params_with_types { \
        if (g_mock_##func_name && g_mock_##func_name->enabled) { \
            void *l_args[] = { params_names }; \
            return_type l_ret = (return_type)(intptr_t)g_mock_##func_name->return_value; \
            dap_mock_record_call(g_mock_##func_name, l_args, sizeof(l_args)/sizeof(void*), (void*)(intptr_t)l_ret); \
            return l_ret; \
        } else { \
            return __real_##func_name params_names; \
        } \
    }

/**
 * Simpler wrapper for void functions
 */
#define DAP_MOCK_WRAPPER_VOID(func_name, params_with_types, params_names) \
    extern void __real_##func_name params_with_types; \
    void __wrap_##func_name params_with_types { \
        if (g_mock_##func_name && g_mock_##func_name->enabled) { \
            void *l_args[] = { params_names }; \
            dap_mock_record_call(g_mock_##func_name, l_args, sizeof(l_args)/sizeof(void*), NULL); \
        } else { \
            __real_##func_name params_names; \
        } \
    }

#define DAP_MOCK_INIT(func_name) \
    do { \
        if (!g_mock_##func_name) { \
            g_mock_##func_name = dap_mock_register(#func_name); \
        } \
    } while (0)

/**
 * Initialize multiple mocks at once
 * Usage: DAP_MOCK_INIT_MULTIPLE(func1, func2, func3);
 */
#define DAP_MOCK_INIT_MULTIPLE(...) \
    do { \
        void (*l_init_funcs[])(void) = { __VA_ARGS__ }; \
        (void)l_init_funcs; \
    } while(0)

// Helper macros for DAP_MOCK_INIT_MULTIPLE
#define DAP_MOCK_INIT_FUNC(func_name) DAP_MOCK_INIT(func_name)

/**
 * Simplified: Initialize all declared mocks in one call
 * Each argument should be a function name that was declared with DAP_MOCK_DECLARE
 * 
 * Usage:
 *   DAP_MOCK_DECLARE(func1);
 *   DAP_MOCK_DECLARE(func2);
 *   DAP_MOCK_DECLARE(func3);
 *   
 *   void setup() {
 *       DAP_MOCK_INIT_ALL(func1, func2, func3);
 *   }
 */
#define DAP_MOCK_INIT_ALL(...) \
    do { \
        DAP_MOCK_INIT_EACH(__VA_ARGS__); \
    } while(0)

// Helper to init each mock from variadic args
#define DAP_MOCK_INIT_EACH(...) \
    DAP_MOCK_INIT_EXPAND(DAP_MOCK_INIT_APPLY, __VA_ARGS__)

#define DAP_MOCK_INIT_APPLY(func_name) \
    DAP_MOCK_INIT(func_name);

// Macro expansion helpers
#define DAP_MOCK_INIT_EXPAND(macro, ...) DAP_MOCK_INIT_EXPAND1(macro, __VA_ARGS__)
#define DAP_MOCK_INIT_EXPAND1(macro, ...) \
    DAP_MOCK_INIT_FOREACH(macro, __VA_ARGS__)

// Foreach implementation for up to 20 arguments
#define DAP_MOCK_INIT_FOREACH(m, a1, ...) m(a1) DAP_MOCK_INIT_FOREACH_1(m, __VA_ARGS__)
#define DAP_MOCK_INIT_FOREACH_1(m, a1, ...) m(a1) DAP_MOCK_INIT_FOREACH_2(m, __VA_ARGS__)
#define DAP_MOCK_INIT_FOREACH_2(m, a1, ...) m(a1) DAP_MOCK_INIT_FOREACH_3(m, __VA_ARGS__)
#define DAP_MOCK_INIT_FOREACH_3(m, a1, ...) m(a1) DAP_MOCK_INIT_FOREACH_4(m, __VA_ARGS__)
#define DAP_MOCK_INIT_FOREACH_4(m, a1, ...) m(a1) DAP_MOCK_INIT_FOREACH_5(m, __VA_ARGS__)
#define DAP_MOCK_INIT_FOREACH_5(m, a1, ...) m(a1) DAP_MOCK_INIT_FOREACH_6(m, __VA_ARGS__)
#define DAP_MOCK_INIT_FOREACH_6(m, a1, ...) m(a1) DAP_MOCK_INIT_FOREACH_7(m, __VA_ARGS__)
#define DAP_MOCK_INIT_FOREACH_7(m, a1, ...) m(a1) DAP_MOCK_INIT_FOREACH_8(m, __VA_ARGS__)
#define DAP_MOCK_INIT_FOREACH_8(m, a1, ...) m(a1) DAP_MOCK_INIT_FOREACH_9(m, __VA_ARGS__)
#define DAP_MOCK_INIT_FOREACH_9(m, a1, ...) m(a1) DAP_MOCK_INIT_FOREACH_10(m, __VA_ARGS__)
#define DAP_MOCK_INIT_FOREACH_10(m, a1, ...) m(a1) DAP_MOCK_INIT_FOREACH_11(m, __VA_ARGS__)
#define DAP_MOCK_INIT_FOREACH_11(m, a1, ...) m(a1) DAP_MOCK_INIT_FOREACH_12(m, __VA_ARGS__)
#define DAP_MOCK_INIT_FOREACH_12(m, a1, ...) m(a1) DAP_MOCK_INIT_FOREACH_13(m, __VA_ARGS__)
#define DAP_MOCK_INIT_FOREACH_13(m, a1, ...) m(a1) DAP_MOCK_INIT_FOREACH_14(m, __VA_ARGS__)
#define DAP_MOCK_INIT_FOREACH_14(m, a1, ...) m(a1) DAP_MOCK_INIT_FOREACH_15(m, __VA_ARGS__)
#define DAP_MOCK_INIT_FOREACH_15(m, a1, ...) m(a1) DAP_MOCK_INIT_FOREACH_16(m, __VA_ARGS__)
#define DAP_MOCK_INIT_FOREACH_16(m, a1, ...) m(a1) DAP_MOCK_INIT_FOREACH_17(m, __VA_ARGS__)
#define DAP_MOCK_INIT_FOREACH_17(m, a1, ...) m(a1) DAP_MOCK_INIT_FOREACH_18(m, __VA_ARGS__)
#define DAP_MOCK_INIT_FOREACH_18(m, a1, ...) m(a1) DAP_MOCK_INIT_FOREACH_19(m, __VA_ARGS__)
#define DAP_MOCK_INIT_FOREACH_19(m, a1, ...) m(a1) DAP_MOCK_INIT_FOREACH_20(m, __VA_ARGS__)
#define DAP_MOCK_INIT_FOREACH_20(m, a1, ...) m(a1)


#define DAP_MOCK_ENABLE(func_name) \
    dap_mock_set_enabled(g_mock_##func_name, true)

#define DAP_MOCK_DISABLE(func_name) \
    dap_mock_set_enabled(g_mock_##func_name, false)

#define DAP_MOCK_RESET(func_name) \
    dap_mock_reset(g_mock_##func_name)

#define DAP_MOCK_CALL_COUNT(func_name) \
    dap_mock_get_call_count(g_mock_##func_name)

#define DAP_MOCK_WAS_CALLED(func_name) \
    (dap_mock_get_call_count(g_mock_##func_name) > 0)

#define DAP_MOCK_RECORD(func_name, ret_val) \
    dap_mock_record_call(g_mock_##func_name, NULL, 0, (void*)(uintptr_t)(ret_val))

#define DAP_MOCK_RECORD_ARGS(func_name, args, arg_count, ret_val) \
    dap_mock_record_call(g_mock_##func_name, args, arg_count, (void*)(uintptr_t)(ret_val))

#endif // DAP_MOCK_FRAMEWORK_H
