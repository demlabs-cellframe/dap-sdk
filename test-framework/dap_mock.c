/**
 * @file dap_mock.c
 * @brief Implementation of generic mock framework
 * @date 2025-10-26
 * @copyright (c) 2025 Cellframe Network
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#include "dap_common.h"
#include "dap_mock.h"
#include "dap_mock_async.h"

#define LOG_TAG "dap_mock"
#define DAP_MOCK_MAX_REGISTERED 100

static dap_mock_function_state_t *s_registered_mocks[DAP_MOCK_MAX_REGISTERED] = {0};
static int s_mock_count = 0;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static bool s_initialized = false;

// Global settings with defaults
static dap_mock_settings_t s_settings = {
    .async_worker_threads = 0,  // 0 = auto-detect CPUs
    .default_delay = {.type = DAP_MOCK_DELAY_NONE},
    .enable_logging = false,
    .log_timestamps = false
};

// Cross-platform constructor/destructor support
#if defined(__GNUC__) || defined(__clang__)
    // GCC, Clang, MinGW support
    #define DAP_CONSTRUCTOR __attribute__((constructor))
    #define DAP_DESTRUCTOR __attribute__((destructor))
#elif defined(_MSC_VER)
    // MSVC doesn't support constructor attribute directly
    // Use #pragma init_seg and static object initialization
    #define DAP_CONSTRUCTOR
    #define DAP_DESTRUCTOR
    // Will use atexit() for cleanup in MSVC
#else
    #define DAP_CONSTRUCTOR
    #define DAP_DESTRUCTOR
#endif

// Auto-init constructor (called before main())
static void DAP_CONSTRUCTOR dap_mock_auto_init(void)
{
    if (!s_initialized) {
        pthread_mutex_lock(&s_lock);
        if (!s_initialized) {  // Double-check
            memset(s_registered_mocks, 0, sizeof(s_registered_mocks));
            s_mock_count = 0;
            s_initialized = true;
            
            // Always init async system with configured worker count
            if (!dap_mock_async_is_initialized()) {
                dap_mock_async_init(s_settings.async_worker_threads);
            }
            
            // Register auto-cleanup for platforms without destructor attribute
            #ifdef _MSC_VER
            atexit(dap_mock_auto_deinit);
            #endif
        }
        pthread_mutex_unlock(&s_lock);
    }
}

// Auto-deinit destructor (called after main())
static void DAP_DESTRUCTOR dap_mock_auto_deinit(void)
{
    if (s_initialized) {
        dap_mock_deinit();
    }
}

// For MSVC: ensure initialization happens via static initialization
#ifdef _MSC_VER
static struct dap_mock_msvc_init_helper {
    dap_mock_msvc_init_helper() {
        dap_mock_auto_init();
    }
} s_msvc_init_helper;
#endif

int dap_mock_init(void)
{
    // Now just a no-op / reset function (kept for backward compatibility)
    pthread_mutex_lock(&s_lock);
    if (!s_initialized) {
        dap_mock_auto_init();
    }
    pthread_mutex_unlock(&s_lock);
    return 0;
}

void dap_mock_deinit(void)
{
    pthread_mutex_lock(&s_lock);
    
    // Always deinit async system
    if (dap_mock_async_is_initialized()) {
        dap_mock_async_deinit();
    }
    
    for (int i = 0; i < s_mock_count; i++) {
        if (s_registered_mocks[i]) {
            pthread_mutex_destroy(&s_registered_mocks[i]->lock);
            DAP_DELETE(s_registered_mocks[i]);
        }
    }
    s_mock_count = 0;
    s_initialized = false;
    pthread_mutex_unlock(&s_lock);
}

void dap_mock_reset_all(void)
{
    pthread_mutex_lock(&s_lock);
    for (int i = 0; i < s_mock_count; i++) {
        if (s_registered_mocks[i]) {
            dap_mock_reset(s_registered_mocks[i]);
        }
    }
    pthread_mutex_unlock(&s_lock);
}

dap_mock_function_state_t* dap_mock_register(const char *a_name)
{
    if (!a_name)
        return NULL;
    
    pthread_mutex_lock(&s_lock);
    
    if (s_mock_count >= DAP_MOCK_MAX_REGISTERED) {
        pthread_mutex_unlock(&s_lock);
        return NULL;
    }
    
    dap_mock_function_state_t *l_mock = DAP_NEW_Z(dap_mock_function_state_t);
    if (!l_mock) {
        pthread_mutex_unlock(&s_lock);
        return NULL;
    }
    
    l_mock->name = a_name;
    l_mock->enabled = true;
    l_mock->call_count = 0;
    l_mock->max_calls = DAP_MOCK_MAX_CALLS;
    l_mock->delay = s_settings.default_delay;  // Apply default delay from settings
    pthread_mutex_init(&l_mock->lock, NULL);
    
    s_registered_mocks[s_mock_count++] = l_mock;
    
    pthread_mutex_unlock(&s_lock);
    return l_mock;
}

void dap_mock_set_enabled(dap_mock_function_state_t *a_state, bool a_enabled)
{
    if (!a_state)
        return;
    pthread_mutex_lock(&a_state->lock);
    a_state->enabled = a_enabled;
    pthread_mutex_unlock(&a_state->lock);
}

void dap_mock_set_return_value(dap_mock_function_state_t *a_state, void *a_value)
{
    if (!a_state)
        return;
    pthread_mutex_lock(&a_state->lock);
    a_state->return_value.ptr = a_value;
    pthread_mutex_unlock(&a_state->lock);
}

void dap_mock_record_call(
    dap_mock_function_state_t *a_state,
    void **a_args,
    int a_arg_count,
    void *a_return_value)
{
    if (!a_state)
        return;
    
    pthread_mutex_lock(&a_state->lock);
    
    if (a_state->call_count >= a_state->max_calls) {
        pthread_mutex_unlock(&a_state->lock);
        return;
    }
    
    dap_mock_call_record_t *l_record = &a_state->calls[a_state->call_count];
    l_record->function_name = a_state->name;
    l_record->timestamp = (uint64_t)time(NULL);
    l_record->return_value = a_return_value;
    l_record->call_count = a_state->call_count;
    
    if (a_args && a_arg_count > 0) {
        int l_count = (a_arg_count < 10) ? a_arg_count : 10;
        for (int i = 0; i < l_count; i++) {
            l_record->args[i] = a_args[i];
        }
    }
    
    a_state->call_count++;
    pthread_mutex_unlock(&a_state->lock);
}

int dap_mock_get_call_count(dap_mock_function_state_t *a_state)
{
    if (!a_state)
        return 0;
    pthread_mutex_lock(&a_state->lock);
    int l_count = a_state->call_count;
    pthread_mutex_unlock(&a_state->lock);
    return l_count;
}

dap_mock_call_record_t* dap_mock_get_last_call(dap_mock_function_state_t *a_state)
{
    if (!a_state || a_state->call_count == 0)
        return NULL;
    return &a_state->calls[a_state->call_count - 1];
}

void **dap_mock_get_call_args(dap_mock_function_state_t *a_state, int a_call_index)
{
    if (!a_state || a_call_index < 0 || a_call_index >= a_state->call_count)
        return NULL;
    return a_state->calls[a_call_index].args;
}

void dap_mock_reset(dap_mock_function_state_t *a_state)
{
    if (!a_state)
        return;
    pthread_mutex_lock(&a_state->lock);
    a_state->call_count = 0;
    memset(a_state->calls, 0, sizeof(a_state->calls));
    pthread_mutex_unlock(&a_state->lock);
}

bool dap_mock_was_called_with(
    dap_mock_function_state_t *a_state,
    int a_arg_index,
    void *a_expected_value)
{
    if (!a_state || a_arg_index < 0 || a_arg_index >= 10)
        return false;
    
    pthread_mutex_lock(&a_state->lock);
    for (int i = 0; i < a_state->call_count; i++) {
        if (a_state->calls[i].args[a_arg_index] == a_expected_value) {
            pthread_mutex_unlock(&a_state->lock);
            return true;
        }
    }
    pthread_mutex_unlock(&a_state->lock);
    return false;
}

// ===========================================================================
// CUSTOM CALLBACK IMPLEMENTATION
// ===========================================================================

void dap_mock_set_callback(dap_mock_function_state_t *a_state, dap_mock_callback_t a_callback, void *a_user_data)
{
    if (!a_state)
        return;
    
    pthread_mutex_lock(&a_state->lock);
    a_state->callback = a_callback;
    a_state->callback_user_data = a_user_data;
    pthread_mutex_unlock(&a_state->lock);
}

void dap_mock_clear_callback(dap_mock_function_state_t *a_state)
{
    if (!a_state)
        return;
    
    pthread_mutex_lock(&a_state->lock);
    a_state->callback = NULL;
    a_state->callback_user_data = NULL;
    pthread_mutex_unlock(&a_state->lock);
}

void* dap_mock_execute_callback(dap_mock_function_state_t *a_state, void **a_args, int a_arg_count)
{
    if (!a_state)
        return NULL;
    
    pthread_mutex_lock(&a_state->lock);
    dap_mock_callback_t l_callback = a_state->callback;
    void *l_user_data = a_state->callback_user_data;
    // Return ptr field from union - wrapper will cast to proper type
    void *l_return_value = a_state->return_value.ptr;
    pthread_mutex_unlock(&a_state->lock);
    
    // If callback is set, use it; otherwise return static return_value
    if (l_callback)
        return l_callback(a_args, a_arg_count, l_user_data);
    
    return l_return_value;
}

// ===========================================================================
// DELAY CONFIGURATION IMPLEMENTATION
// ===========================================================================

/**
 * @brief Get random number in range [min, max]
 */
static uint64_t s_random_range(uint64_t a_min, uint64_t a_max)
{
    if (a_min >= a_max)
        return a_min;
    
    // Use rand_r for thread safety
    static __thread unsigned int l_seed = 0;
    if (l_seed == 0)
        l_seed = (unsigned int)time(NULL) ^ (unsigned int)pthread_self();
    
    uint64_t l_range = a_max - a_min;
    return a_min + (rand_r(&l_seed) % (l_range + 1));
}

void dap_mock_set_delay_fixed(dap_mock_function_state_t *a_state, uint64_t a_delay_us)
{
    if (!a_state)
        return;
    
    pthread_mutex_lock(&a_state->lock);
    a_state->delay.type = DAP_MOCK_DELAY_FIXED;
    a_state->delay.fixed_us = a_delay_us;
    pthread_mutex_unlock(&a_state->lock);
}

void dap_mock_set_delay_range(dap_mock_function_state_t *a_state, uint64_t a_min_us, uint64_t a_max_us)
{
    if (!a_state)
        return;
    
    pthread_mutex_lock(&a_state->lock);
    a_state->delay.type = DAP_MOCK_DELAY_RANGE;
    a_state->delay.range.min_us = a_min_us;
    a_state->delay.range.max_us = a_max_us;
    pthread_mutex_unlock(&a_state->lock);
}

void dap_mock_set_delay_variance(dap_mock_function_state_t *a_state, uint64_t a_center_us, uint64_t a_variance_us)
{
    if (!a_state)
        return;
    
    pthread_mutex_lock(&a_state->lock);
    a_state->delay.type = DAP_MOCK_DELAY_VARIANCE;
    a_state->delay.variance.center_us = a_center_us;
    a_state->delay.variance.variance_us = a_variance_us;
    pthread_mutex_unlock(&a_state->lock);
}

void dap_mock_clear_delay(dap_mock_function_state_t *a_state)
{
    if (!a_state)
        return;
    
    pthread_mutex_lock(&a_state->lock);
    a_state->delay.type = DAP_MOCK_DELAY_NONE;
    pthread_mutex_unlock(&a_state->lock);
}

void dap_mock_execute_delay(dap_mock_function_state_t *a_state)
{
    if (!a_state)
        return;
    
    pthread_mutex_lock(&a_state->lock);
    dap_mock_delay_t l_delay = a_state->delay;
    pthread_mutex_unlock(&a_state->lock);
    
    uint64_t l_delay_us = 0;
    
    switch (l_delay.type) {
        case DAP_MOCK_DELAY_NONE:
            return;  // No delay
            
        case DAP_MOCK_DELAY_FIXED:
            l_delay_us = l_delay.fixed_us;
            break;
            
        case DAP_MOCK_DELAY_RANGE:
            l_delay_us = s_random_range(l_delay.range.min_us, l_delay.range.max_us);
            break;
            
        case DAP_MOCK_DELAY_VARIANCE: {
            // Calculate variance range: center ± variance
            uint64_t l_min = (l_delay.variance.center_us > l_delay.variance.variance_us) 
                ? (l_delay.variance.center_us - l_delay.variance.variance_us) 
                : 0;
            uint64_t l_max = l_delay.variance.center_us + l_delay.variance.variance_us;
            l_delay_us = s_random_range(l_min, l_max);
            break;
        }
            
        default:
            return;
    }
    
    // Execute delay
    if (l_delay_us > 0)
        usleep(l_delay_us);
}

// ===========================================================================
// LOGGING HELPERS
// ===========================================================================

static void s_log_mock_call(const char *a_func_name, const char *a_action)
{
    if (!s_settings.enable_logging)
        return;
    
    if (s_settings.log_timestamps) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        struct tm *tm_info = localtime(&tv.tv_sec);
        char time_buf[32];
        strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_info);
        log_it(L_DEBUG, "[%s.%06ld] MOCK %s: %s", time_buf, tv.tv_usec, a_func_name, a_action);
    } else {
        log_it(L_DEBUG, "MOCK %s: %s", a_func_name, a_action);
    }
}

/**
 * @brief Prepare mock call - executes delay, records call, and checks if mock is enabled
 * 
 * This is a convenience function that combines:
 * 1. Checking if mock is enabled
 * 2. Executing the configured delay
 * 3. Recording the call with arguments
 * 
 * It should be called at the beginning of every wrapper function.
 * 
 * @param a_state Mock function state
 * @param a_args Array of function arguments
 * @param a_arg_count Number of arguments
 * @return true if mock is enabled and should intercept call, false otherwise
 */
bool dap_mock_prepare_call(dap_mock_function_state_t *a_state, void **a_args, int a_arg_count)
{
    if (!a_state)
        return false;
    
    // Log the call if enabled
    s_log_mock_call(a_state->name, a_state->enabled ? "CALLED" : "CALLED (disabled, passing through)");
    
    if (!a_state->enabled)
        return false;
    
    // Record the call BEFORE delay/async execution
    // This ensures call_count is incremented immediately, not after async completion
    dap_mock_record_call(a_state, a_args, a_arg_count, NULL);
    
    // Execute configured delay (may be async)
    dap_mock_execute_delay(a_state);
    
    return true;
}

// ===========================================================================
// SETTINGS API
// ===========================================================================

void dap_mock_apply_settings(const dap_mock_settings_t *a_settings)
{
    if (!a_settings)
        return;
    
    pthread_mutex_lock(&s_lock);
    s_settings = *a_settings;
    pthread_mutex_unlock(&s_lock);
    
    // If async is already initialized, we can't change worker count
    // Log a warning if user tries to change it after init
    if (s_initialized && dap_mock_async_is_initialized() && 
        a_settings->async_worker_threads != s_settings.async_worker_threads) {
        log_it(L_WARNING, "Cannot change async_worker_threads after mock system is initialized");
    }
}

const dap_mock_settings_t* dap_mock_get_settings(void)
{
    return &s_settings;
}
