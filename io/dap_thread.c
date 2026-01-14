/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * CellFrame       https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.
 */

#include <errno.h>
#include "dap_thread.h"
#include "dap_common.h"

#define LOG_TAG "dap_thread"

dap_thread_t dap_thread_create(dap_thread_func_t a_func, void *a_arg)
{
    if (!a_func) {
        return 0;
    }
    
    pthread_t l_thread;
    int l_ret = pthread_create(&l_thread, NULL, a_func, a_arg);
    
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to create thread: %s", strerror(l_ret));
        return 0;
    }
    
    return l_thread;
}

int dap_thread_join(dap_thread_t a_thread, void **a_retval)
{
    if (a_thread == 0) {
        return -1;
    }
    
    int l_ret = pthread_join(a_thread, a_retval);
    
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to join thread: %s", strerror(l_ret));
        return -2;
    }
    
    return 0;
}

dap_thread_id_t dap_thread_get_id(void)
{
    return (dap_thread_id_t)pthread_self();
}

int dap_thread_set_name(dap_thread_t a_thread, const char *a_name)
{
    if (a_thread == 0 || !a_name) {
        return -1;
    }
    
#ifdef __linux__
    int l_ret = pthread_setname_np(a_thread, a_name);
    if (l_ret != 0) {
        log_it(L_WARNING, "Failed to set thread name: %s", strerror(l_ret));
        return -2;
    }
#else
    UNUSED(a_thread);
    UNUSED(a_name);
#endif
    
    return 0;
}

int dap_thread_detach(dap_thread_t a_thread)
{
    if (a_thread == 0) {
        return -1;
    }
    
    int l_ret = pthread_detach(a_thread);
    
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to detach thread: %s", strerror(l_ret));
        return -2;
    }
    
    return 0;
}
