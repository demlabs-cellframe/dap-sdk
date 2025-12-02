/**
 * @file dap_mock_framework.c
 * @brief Implementation of generic mock framework
 * @date 2025-10-26
 * @copyright (c) 2025 Cellframe Network
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef DAP_OS_WINDOWS
#include <unistd.h>
#endif

#include "dap_common.h"
#include "dap_mock.h"

#define LOG_TAG "dap_mock"
#define DAP_MOCK_MAX_REGISTERED 100

static dap_mock_function_state_t *s_registered_mocks[DAP_MOCK_MAX_REGISTERED] = {0};
static int s_mock_count = 0;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

int dap_mock_init(void)
{
    pthread_mutex_lock(&s_lock);
    memset(s_registered_mocks, 0, sizeof(s_registered_mocks));
    s_mock_count = 0;
    pthread_mutex_unlock(&s_lock);
    return 0;
}

void dap_mock_deinit(void)
{
    pthread_mutex_lock(&s_lock);
    for (int i = 0; i < s_mock_count; i++) {
        if (s_registered_mocks[i]) {
            pthread_mutex_destroy(&s_registered_mocks[i]->lock);
            DAP_DELETE(s_registered_mocks[i]);
        }
    }
    s_mock_count = 0;
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
    
    // Use thread-local seed for thread safety
    static __thread unsigned int l_seed = 0;
    if (l_seed == 0)
        l_seed = (unsigned int)time(NULL) ^ (unsigned int)(uintptr_t)pthread_self();
    
    uint64_t l_range = a_max - a_min;
#ifdef DAP_OS_WINDOWS
    // Windows doesn't have rand_r, use simple approach with thread-local seed
    srand(l_seed);
    l_seed = (unsigned int)rand();
    return a_min + (l_seed % (l_range + 1));
#else
    return a_min + (rand_r(&l_seed) % (l_range + 1));
#endif
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

