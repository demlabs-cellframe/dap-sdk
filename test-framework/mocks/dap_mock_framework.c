/**
 * @file dap_mock_framework.c
 * @brief Implementation of generic mock framework
 * @date 2025-10-26
 * @copyright (c) 2025 Cellframe Network
 */

#include "dap_mock_framework.h"
#include "dap_common.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LOG_TAG "dap_mock_framework"
#define DAP_MOCK_MAX_REGISTERED 100

static dap_mock_function_state_t *s_registered_mocks[DAP_MOCK_MAX_REGISTERED] = {0};
static int s_mock_count = 0;
static pthread_mutex_t s_framework_lock = PTHREAD_MUTEX_INITIALIZER;

int dap_mock_framework_init(void)
{
    pthread_mutex_lock(&s_framework_lock);
    memset(s_registered_mocks, 0, sizeof(s_registered_mocks));
    s_mock_count = 0;
    pthread_mutex_unlock(&s_framework_lock);
    return 0;
}

void dap_mock_framework_deinit(void)
{
    pthread_mutex_lock(&s_framework_lock);
    for (int i = 0; i < s_mock_count; i++) {
        if (s_registered_mocks[i]) {
            pthread_mutex_destroy(&s_registered_mocks[i]->lock);
            DAP_DELETE(s_registered_mocks[i]);
        }
    }
    s_mock_count = 0;
    pthread_mutex_unlock(&s_framework_lock);
}

void dap_mock_framework_reset_all(void)
{
    pthread_mutex_lock(&s_framework_lock);
    for (int i = 0; i < s_mock_count; i++) {
        if (s_registered_mocks[i]) {
            dap_mock_reset(s_registered_mocks[i]);
        }
    }
    pthread_mutex_unlock(&s_framework_lock);
}

dap_mock_function_state_t* dap_mock_register(const char *a_name)
{
    if (!a_name)
        return NULL;
    
    pthread_mutex_lock(&s_framework_lock);
    
    if (s_mock_count >= DAP_MOCK_MAX_REGISTERED) {
        pthread_mutex_unlock(&s_framework_lock);
        return NULL;
    }
    
    dap_mock_function_state_t *l_mock = DAP_NEW_Z(dap_mock_function_state_t);
    if (!l_mock) {
        pthread_mutex_unlock(&s_framework_lock);
        return NULL;
    }
    
    l_mock->name = a_name;
    l_mock->enabled = true;
    l_mock->call_count = 0;
    l_mock->max_calls = DAP_MOCK_MAX_CALLS;
    pthread_mutex_init(&l_mock->lock, NULL);
    
    s_registered_mocks[s_mock_count++] = l_mock;
    
    pthread_mutex_unlock(&s_framework_lock);
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

