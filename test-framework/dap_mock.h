/**
 * @file dap_mock_framework.h
 * @brief Generic mock framework for DAP SDK dependencies
 * @details Provides infrastructure for mocking DAP SDK functions in unit tests.
 *          Uses function pointer replacement pattern with call tracking.
 * 
 * @date 2025-10-26
 * @copyright (c) 2025 Cellframe Network
 */

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>

// ===========================================================================
// DELAY CONFIGURATION
// ===========================================================================

/**
 * @brief Mock execution delay types
 */
typedef enum dap_mock_delay_type {
    DAP_MOCK_DELAY_NONE = 0,      /**< No delay */
    DAP_MOCK_DELAY_FIXED,          /**< Fixed delay in microseconds */
    DAP_MOCK_DELAY_RANGE,          /**< Random delay in range [min, max] */
    DAP_MOCK_DELAY_VARIANCE        /**< Delay with center ± variance (e.g., 2.3ms ± 0.07ms) */
} dap_mock_delay_type_t;

/**
 * @brief Mock delay configuration
 */
typedef struct dap_mock_delay {
    dap_mock_delay_type_t type;
    union {
        uint64_t fixed_us;         /**< Fixed delay in microseconds */
        struct {
            uint64_t min_us;       /**< Minimum delay in microseconds */
            uint64_t max_us;       /**< Maximum delay in microseconds */
        } range;
        struct {
            uint64_t center_us;    /**< Center delay in microseconds */
            uint64_t variance_us;  /**< Variance (±) in microseconds */
        } variance;
    };
} dap_mock_delay_t;

// ===========================================================================
// MOCK RETURN VALUE TYPES
// ===========================================================================

/**
 * @brief Union for different return value types
 */
typedef union dap_mock_return_value {
    int i;              /**< int return value */
    long l;             /**< long return value */
    uint64_t u64;       /**< uint64_t return value */
    void *ptr;          /**< void* return value */
    char *str;          /**< char* return value */
} dap_mock_return_value_t;

// ===========================================================================
// MOCK CONFIGURATION STRUCTURE
// ===========================================================================

/**
 * @brief Mock configuration passed to DAP_MOCK_DECLARE
 * 
 * Usage examples:
 *   DAP_MOCK_DECLARE(func, { .return_value.l = 0xDEAD });
 *   DAP_MOCK_DECLARE(func, { .enabled = false });
 *   DAP_MOCK_DECLARE(func, { .return_value.ptr = NULL, .delay.type = DAP_MOCK_DELAY_FIXED, .delay.fixed_us = 1000 });
 *   DAP_MOCK_DECLARE(func, { .async = true });  // Execute callback asynchronously
 */
typedef struct dap_mock_config {
    bool enabled;                       /**< Enable mock (default: true) */
    dap_mock_return_value_t return_value;  /**< Return value (default: all zeros) */
    dap_mock_delay_t delay;             /**< Execution delay (default: none) */
    bool async;                         /**< Execute callback asynchronously (default: false) */
} dap_mock_config_t;

// Default config: enabled=true, return=0, no delay, sync
#define DAP_MOCK_CONFIG_DEFAULT { .enabled = true, .return_value = {0}, .delay = {.type = DAP_MOCK_DELAY_NONE}, .async = false }

// ===========================================================================
// MOCK SYSTEM SETTINGS
// ===========================================================================

/**
 * @brief Global mock system settings
 * 
 * Configured via DAP_MOCK_SETTINGS() macro before any mock usage.
 * Settings are applied during automatic initialization.
 */
typedef struct dap_mock_settings {
    uint32_t async_worker_threads;   /**< Number of async worker threads (0 = auto-detect CPUs) */
    dap_mock_delay_t default_delay;  /**< Default delay for all mocks (can be overridden per mock) */
    bool enable_logging;             /**< Enable detailed mock call logging */
    bool log_timestamps;             /**< Include timestamps in mock logs */
} dap_mock_settings_t;

/**
 * @brief Set global mock system settings
 * @param a_settings Settings structure
 * 
 * Must be called BEFORE any mocks are used (typically at program start).
 * If not called, defaults are used:
 *   - async_worker_threads = 0 (auto-detect CPUs)
 *   - default_delay = none
 *   - enable_logging = false
 *   - log_timestamps = false
 */
void dap_mock_apply_settings(const dap_mock_settings_t *a_settings);

/**
 * @brief Get current mock system settings
 * @return Pointer to current settings
 */
const dap_mock_settings_t* dap_mock_get_settings(void);

/**
 * Convenience macro for setting mock system configuration
 * 
 * Usage example:
 *   DAP_MOCK_SETTINGS({
 *       .async_worker_threads = 4,
 *       .default_delay = {.type = DAP_MOCK_DELAY_FIXED, .value_us = 1000},
 *       .enable_logging = true,
 *       .log_timestamps = true
 *   });
 */
#define DAP_MOCK_SETTINGS(settings_struct) \
    do { \
        dap_mock_settings_t __settings = settings_struct; \
        dap_mock_apply_settings(&__settings); \
    } while(0)

// ===========================================================================
// CUSTOM CALLBACK SUPPORT
// ===========================================================================

/**
 * @brief Custom mock callback function signature
 * @param a_args Array of arguments passed to the mocked function
 * @param a_arg_count Number of arguments
 * @param a_user_data User-provided context data
 * @return Return value for the mocked function
 * 
 * Example:
 *   void* my_custom_mock(void **a_args, int a_arg_count, void *a_user_data) {
 *       int arg1 = (int)(intptr_t)a_args[0];
 *       char *arg2 = (char*)a_args[1];
 *       // Custom logic here
 *       return (void*)(intptr_t)result;
 *   }
 */
typedef void* (*dap_mock_callback_t)(void **a_args, int a_arg_count, void *a_user_data);

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
    dap_mock_return_value_t return_value;  // Return value with type union
    dap_mock_callback_t callback;    // Custom callback function (overrides return_value if set)
    void *callback_user_data;        // User data passed to callback
    dap_mock_delay_t delay;          // Execution delay configuration
    bool async;                      // Execute callback asynchronously (requires dap_mock_async_init)
    int call_count;
    int max_calls;
    dap_mock_call_record_t calls[DAP_MOCK_MAX_CALLS];
    pthread_mutex_t lock;
} dap_mock_function_state_t;

// ===========================================================================
// MOCK FRAMEWORK API
// ===========================================================================

int dap_mock_init(void);
void dap_mock_deinit(void);
void dap_mock_reset_all(void);
dap_mock_function_state_t* dap_mock_register(const char *a_name);
void dap_mock_set_enabled(dap_mock_function_state_t *a_state, bool a_enabled);
void dap_mock_set_return_value(dap_mock_function_state_t *a_state, void *a_value);
void dap_mock_record_call(dap_mock_function_state_t *a_state, void **a_args, int a_arg_count, void *a_return_value);
int dap_mock_get_call_count(dap_mock_function_state_t *a_state);
dap_mock_call_record_t* dap_mock_get_last_call(dap_mock_function_state_t *a_state);
void **dap_mock_get_call_args(dap_mock_function_state_t *a_state, int a_call_index);
void dap_mock_reset(dap_mock_function_state_t *a_state);
bool dap_mock_was_called_with(dap_mock_function_state_t *a_state, int a_arg_index, void *a_expected_value);

// Custom callback functions
void dap_mock_set_callback(dap_mock_function_state_t *a_state, dap_mock_callback_t a_callback, void *a_user_data);
void dap_mock_clear_callback(dap_mock_function_state_t *a_state);
void* dap_mock_execute_callback(dap_mock_function_state_t *a_state, void **a_args, int a_arg_count);

// Delay configuration functions
void dap_mock_set_delay_fixed(dap_mock_function_state_t *a_state, uint64_t a_delay_us);
void dap_mock_set_delay_range(dap_mock_function_state_t *a_state, uint64_t a_min_us, uint64_t a_max_us);
void dap_mock_set_delay_variance(dap_mock_function_state_t *a_state, uint64_t a_center_us, uint64_t a_variance_us);
void dap_mock_clear_delay(dap_mock_function_state_t *a_state);
void dap_mock_execute_delay(dap_mock_function_state_t *a_state);  // Internal: execute configured delay

/**
 * @brief Prepare mock call - executes delay, records call, and checks if mock is enabled
 * @param a_state Mock function state
 * @param a_args Array of function arguments
 * @param a_arg_count Number of arguments
 * @return true if mock is enabled and should intercept the call, false to call real function
 * 
 * This function should be called at the beginning of every wrapper function.
 * It automatically:
 * - Checks if mock is enabled
 * - Executes configured delay (simulating latency)
 * - Records the call with arguments
 * 
 * Usage in wrapper:
 *   void *l_args[] = {arg1, arg2, arg3};
 *   if (dap_mock_prepare_call(g_mock_my_func, l_args, 3)) {
 *       // mock logic here
 *   } else {
 *       return __real_my_func(...);
 *   }
 */
bool dap_mock_prepare_call(dap_mock_function_state_t *a_state, void **a_args, int a_arg_count);

// ===========================================================================
// MOCK DECLARATION MACROS
// ===========================================================================

// Macro overloading helper - counts arguments  
#define DAP_MOCK_GET_MACRO(_1, _2, _3, NAME, ...) NAME

/**
 * Universal mock declaration macro with structure-based configuration:
 * 
 * 1. DAP_MOCK_DECLARE(func_name)
 *    - Default: enabled=true, return=0, no delay
 * 
 * 2. DAP_MOCK_DECLARE(func_name, { .return_value.l = 0xDEAD })
 *    - Config struct with designated initializers
 * 
 * 3. DAP_MOCK_DECLARE(func_name, { .enabled = false }, callback_body)
 *    - Config struct + custom callback body
 * 
 * Examples:
 *   DAP_MOCK_DECLARE(dap_stream_write);
 *   DAP_MOCK_DECLARE(dap_net_tun_create, { .return_value.l = 0xABCDEF00 });
 *   DAP_MOCK_DECLARE(dap_hash, { .return_value.i = 0 }, { 
 *       // Custom callback code
 *       return (void*)(intptr_t)123;
 *   });
 */
#define DAP_MOCK_DECLARE(...) \
    DAP_MOCK_GET_MACRO(__VA_ARGS__, DAP_MOCK_DECLARE_3, DAP_MOCK_DECLARE_2, DAP_MOCK_DECLARE_1)(__VA_ARGS__)

// Variant 1: Simple stub (func_name only) - default config
#define DAP_MOCK_DECLARE_1(func_name) \
    DAP_MOCK_DECLARE_2(func_name, DAP_MOCK_CONFIG_DEFAULT)

// Variant 2: With config struct
#define DAP_MOCK_DECLARE_2(func_name, config) \
    static dap_mock_function_state_t *g_mock_##func_name = NULL; \
    static inline void dap_mock_auto_init_##func_name(void) __attribute__((constructor)); \
    static inline void dap_mock_auto_init_##func_name(void) { \
        if (g_mock_##func_name == NULL) { \
            g_mock_##func_name = dap_mock_register(#func_name); \
            if (g_mock_##func_name) { \
                dap_mock_config_t l_cfg = config; \
                g_mock_##func_name->enabled = l_cfg.enabled; \
                g_mock_##func_name->return_value = l_cfg.return_value; \
                g_mock_##func_name->delay = l_cfg.delay; \
                g_mock_##func_name->async = l_cfg.async; \
            } \
        } \
    }

// Variant 3: With config struct + custom callback
#define DAP_MOCK_DECLARE_3(func_name, config, callback_body) \
    static void* dap_mock_callback_##func_name(void **a_args, int a_arg_count, void *a_user_data) callback_body \
    static dap_mock_function_state_t *g_mock_##func_name = NULL; \
    static inline void dap_mock_auto_init_##func_name(void) __attribute__((constructor)); \
    static inline void dap_mock_auto_init_##func_name(void) { \
        if (g_mock_##func_name == NULL) { \
            g_mock_##func_name = dap_mock_register(#func_name); \
            if (g_mock_##func_name) { \
                dap_mock_config_t l_cfg = config; \
                g_mock_##func_name->enabled = l_cfg.enabled; \
                g_mock_##func_name->return_value = l_cfg.return_value; \
                g_mock_##func_name->delay = l_cfg.delay; \
                g_mock_##func_name->async = l_cfg.async; \
                g_mock_##func_name->callback = dap_mock_callback_##func_name; \
            } \
        } \
    }

/**
 * DAP_MOCK_DECLARE_CUSTOM - For use with DAP_MOCK_WRAPPER_CUSTOM
 * 
 * Use this when you need to write a custom linker wrapper (DAP_MOCK_WRAPPER_CUSTOM)
 * but still want to register the mock with configuration (delays, etc.)
 * 
 * This only creates the registration/initialization, NOT the wrapper itself.
 * You must provide the wrapper separately using DAP_MOCK_WRAPPER_CUSTOM.
 * 
 * Example:
 *   // In header:
 *   DAP_MOCK_DECLARE_CUSTOM(my_func, { .delay = {.type = DAP_MOCK_DELAY_RANGE, .min = 50000, .max = 150000} });
 *   
 *   // In source:
 *   DAP_MOCK_WRAPPER_CUSTOM(int, my_func, (int a, int b), {
 *       // custom wrapper logic
 *       if (g_mock_my_func && g_mock_my_func->enabled) {
 *           dap_mock_execute_delay(g_mock_my_func);
 *           // ... custom logic ...
 *           return my_result;
 *       }
 *       return __real_my_func(a, b);
 *   })
 */
#define DAP_MOCK_DECLARE_CUSTOM(func_name, config) \
    static dap_mock_function_state_t *g_mock_##func_name = NULL; \
    static inline void dap_mock_auto_init_##func_name(void) __attribute__((constructor)); \
    static inline void dap_mock_auto_init_##func_name(void) { \
        if (g_mock_##func_name == NULL) { \
            g_mock_##func_name = dap_mock_register(#func_name); \
            if (g_mock_##func_name) { \
                dap_mock_config_t l_cfg = config; \
                g_mock_##func_name->enabled = l_cfg.enabled; \
                g_mock_##func_name->return_value = l_cfg.return_value; \
                g_mock_##func_name->delay = l_cfg.delay; \
            } \
        } \
    }

// ===========================================================================
// WRAPPER MACROS FOR LINKER
// ===========================================================================

/**
 * Define wrapper function that linker will use instead of real function
 * This macro creates __wrap_FUNCNAME that calls real function or mock
 * 
 * Supports:
 * - Static return values
 * - Custom callback functions
 * - Execution delays (fixed/range/variance)
 * - Call tracking
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
            int l_arg_count = sizeof(l_args)/sizeof(void*); \
            dap_mock_execute_delay(g_mock_##func_name); \
            void *l_ret_ptr = dap_mock_execute_callback(g_mock_##func_name, l_args, l_arg_count); \
            return_type l_ret = (return_type)(intptr_t)l_ret_ptr; \
            dap_mock_record_call(g_mock_##func_name, l_args, l_arg_count, l_ret_ptr); \
            return l_ret; \
        } else { \
            return __real_##func_name params_names; \
        } \
    }

/**
 * Simpler wrapper for void functions
 * Supports callbacks, delays, and call tracking
 */
#define DAP_MOCK_WRAPPER_VOID(func_name, params_with_types, params_names) \
    extern void __real_##func_name params_with_types; \
    void __wrap_##func_name params_with_types { \
        if (g_mock_##func_name && g_mock_##func_name->enabled) { \
            void *l_args[] = { params_names }; \
            int l_arg_count = sizeof(l_args)/sizeof(void*); \
            dap_mock_execute_delay(g_mock_##func_name); \
            dap_mock_execute_callback(g_mock_##func_name, l_args, l_arg_count); \
            dap_mock_record_call(g_mock_##func_name, l_args, l_arg_count, NULL); \
        } else { \
            __real_##func_name params_names; \
        } \
    }


// ===========================================================================
// MOCK CONTROL MACROS
// ===========================================================================

/**
 * Enable mock for a function (intercept calls)
 * Usage: DAP_MOCK_ENABLE(dap_stream_write);
 */
#define DAP_MOCK_ENABLE(func_name) \
    dap_mock_set_enabled(g_mock_##func_name, true)

/**
 * Disable mock for a function (call real function)
 * Usage: DAP_MOCK_DISABLE(dap_stream_write);
 */
#define DAP_MOCK_DISABLE(func_name) \
    dap_mock_set_enabled(g_mock_##func_name, false)

/**
 * Set return value for mock
 * Usage: DAP_MOCK_SET_RETURN(dap_stream_write, (void*)(intptr_t)0);
 */
#define DAP_MOCK_SET_RETURN(func_name, value) \
    dap_mock_set_return_value(g_mock_##func_name, (void*)(value))

/**
 * Get call count for mock
 * Usage: int count = DAP_MOCK_GET_CALL_COUNT(dap_stream_write);
 */
#define DAP_MOCK_GET_CALL_COUNT(func_name) \
    dap_mock_get_call_count(g_mock_##func_name)

/**
 * Check if mock was called
 * Usage: if (DAP_MOCK_WAS_CALLED(dap_stream_write)) { ... }
 */
#define DAP_MOCK_WAS_CALLED(func_name) \
    (dap_mock_get_call_count(g_mock_##func_name) > 0)

/**
 * Get specific argument from a call
 * Usage: void *arg = DAP_MOCK_GET_ARG(dap_stream_write, 0, 1);
 */
#define DAP_MOCK_GET_ARG(func_name, call_idx, arg_idx) \
    dap_mock_get_call_args(g_mock_##func_name, call_idx)[arg_idx]

/**
 * Check if mock was called with specific argument
 * Usage: assert(DAP_MOCK_WAS_CALLED_WITH(func, 0, expected_ptr));
 */
#define DAP_MOCK_WAS_CALLED_WITH(func_name, call_idx, expected_value) \
    dap_mock_was_called_with(g_mock_##func_name, call_idx, (void*)(expected_value))

/**
 * Reset mock (clear call history)
 * Usage: DAP_MOCK_RESET(dap_stream_write);
 */
#define DAP_MOCK_RESET(func_name) \
    dap_mock_reset(g_mock_##func_name)

/**
 * Set custom callback function for mock
 * Usage: DAP_MOCK_SET_CALLBACK(dap_stream_write, my_custom_callback, user_data);
 */
#define DAP_MOCK_SET_CALLBACK(func_name, callback, user_data) \
    dap_mock_set_callback(g_mock_##func_name, callback, user_data)

/**
 * Clear custom callback (return to simple return_value mode)
 * Usage: DAP_MOCK_CLEAR_CALLBACK(dap_stream_write);
 */
#define DAP_MOCK_CLEAR_CALLBACK(func_name) \
    dap_mock_clear_callback(g_mock_##func_name)

// ===========================================================================
// DELAY CONFIGURATION MACROS
// ===========================================================================

/**
 * Set fixed delay for mock execution
 * Usage: DAP_MOCK_SET_DELAY_FIXED(dap_stream_write, 1000);  // 1ms delay
 */
#define DAP_MOCK_SET_DELAY_FIXED(func_name, delay_us) \
    dap_mock_set_delay_fixed(g_mock_##func_name, delay_us)

/**
 * Set random delay in range [min, max] microseconds
 * Usage: DAP_MOCK_SET_DELAY_RANGE(dap_stream_write, 500, 1500);  // 0.5-1.5ms
 */
#define DAP_MOCK_SET_DELAY_RANGE(func_name, min_us, max_us) \
    dap_mock_set_delay_range(g_mock_##func_name, min_us, max_us)

/**
 * Set delay with variance (center ± variance) in microseconds
 * Usage: DAP_MOCK_SET_DELAY_VARIANCE(dap_stream_write, 2300, 70);  // 2.3ms ± 0.07ms
 */
#define DAP_MOCK_SET_DELAY_VARIANCE(func_name, center_us, variance_us) \
    dap_mock_set_delay_variance(g_mock_##func_name, center_us, variance_us)

/**
 * Clear delay (no delay, instant execution)
 * Usage: DAP_MOCK_CLEAR_DELAY(dap_stream_write);
 */
#define DAP_MOCK_CLEAR_DELAY(func_name) \
    dap_mock_clear_delay(g_mock_##func_name)

// Convenience macros for millisecond delays
#define DAP_MOCK_SET_DELAY_MS(func_name, delay_ms) \
    DAP_MOCK_SET_DELAY_FIXED(func_name, (delay_ms) * 1000)

#define DAP_MOCK_SET_DELAY_RANGE_MS(func_name, min_ms, max_ms) \
    DAP_MOCK_SET_DELAY_RANGE(func_name, (min_ms) * 1000, (max_ms) * 1000)

#define DAP_MOCK_SET_DELAY_VARIANCE_MS(func_name, center_ms, variance_ms) \
    DAP_MOCK_SET_DELAY_VARIANCE(func_name, (center_ms) * 1000, (variance_ms) * 1000)

