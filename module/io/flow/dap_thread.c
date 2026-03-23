#define _GNU_SOURCE
/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * CellFrame       https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.
 */

#include <errno.h>

// Platform-specific includes for CPU affinity
#if defined(__linux__) || defined(__ANDROID__)
    #include <sched.h>  // For cpu_set_t, CPU_ZERO, CPU_SET
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    #include <sys/param.h>
    #include <sys/cpuset.h>
#elif defined(__APPLE__) && defined(__MACH__)
    #include <mach/thread_policy.h>
    #include <mach/thread_act.h>
#elif defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
#endif

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

dap_thread_t dap_thread_self(void)
{
    return pthread_self();
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
    
#if defined(__linux__) || defined(__ANDROID__)
    int l_ret = pthread_setname_np(a_thread, a_name);
    if (l_ret != 0) {
        log_it(L_WARNING, "Failed to set thread name: %s", strerror(l_ret));
        return -2;
    }
#elif defined(__APPLE__) && defined(__MACH__)
    // macOS requires setting name from the thread itself
    if (pthread_equal(a_thread, pthread_self())) {
        int l_ret = pthread_setname_np(a_name);
        if (l_ret != 0) {
            log_it(L_WARNING, "Failed to set thread name: %s", strerror(l_ret));
            return -2;
        }
    } else {
        log_it(L_WARNING, "Cannot set name for non-current thread on macOS");
        return -3;
    }
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    pthread_set_name_np(a_thread, a_name);
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

int dap_thread_set_affinity(dap_thread_t a_thread, uint32_t a_cpu_id)
{
    if (a_thread == 0) {
        return -1;
    }
    
#if defined(__ANDROID__)
    cpu_set_t l_cpuset;
    CPU_ZERO(&l_cpuset);
    CPU_SET(a_cpu_id, &l_cpuset);
    
    int l_ret = sched_setaffinity(0, sizeof(cpu_set_t), &l_cpuset);
    
    if (l_ret != 0) {
        log_it(L_WARNING, "Failed to set CPU affinity to core %u: %s", a_cpu_id, strerror(errno));
        return -2;
    }
    
    log_it(L_DEBUG, "Thread bound to CPU core %u via sched_setaffinity", a_cpu_id);

#elif defined(__linux__)
    cpu_set_t l_cpuset;
    CPU_ZERO(&l_cpuset);
    CPU_SET(a_cpu_id, &l_cpuset);
    
    int l_ret = pthread_setaffinity_np(a_thread, sizeof(cpu_set_t), &l_cpuset);
    
    if (l_ret != 0) {
        log_it(L_WARNING, "Failed to set CPU affinity to core %u: %s", a_cpu_id, strerror(l_ret));
        return -2;
    }
    
    log_it(L_DEBUG, "Thread %lu bound to CPU core %u", (unsigned long)a_thread, a_cpu_id);
    
#elif defined(__FreeBSD__) || defined(__NetBSD__)
    // FreeBSD/NetBSD: cpuset_t
    cpuset_t l_cpuset;
    CPU_ZERO(&l_cpuset);
    CPU_SET(a_cpu_id, &l_cpuset);
    
    int l_ret = pthread_setaffinity_np(a_thread, sizeof(cpuset_t), &l_cpuset);
    
    if (l_ret != 0) {
        log_it(L_WARNING, "Failed to set CPU affinity to core %u: %s", a_cpu_id, strerror(l_ret));
        return -2;
    }
    
    log_it(L_DEBUG, "Thread %lu bound to CPU core %u", (unsigned long)a_thread, a_cpu_id);
    
#elif defined(__APPLE__) && defined(__MACH__)
    // macOS: thread_policy_set (deprecated but still works)
    thread_affinity_policy_data_t l_policy = { a_cpu_id };
    mach_port_t l_mach_thread = pthread_mach_thread_np(a_thread);
    
    kern_return_t l_ret = thread_policy_set(l_mach_thread,
                                            THREAD_AFFINITY_POLICY,
                                            (thread_policy_t)&l_policy,
                                            THREAD_AFFINITY_POLICY_COUNT);
    
    if (l_ret != KERN_SUCCESS) {
        log_it(L_WARNING, "Failed to set CPU affinity to core %u (macOS)", a_cpu_id);
        return -2;
    }
    
    log_it(L_DEBUG, "Thread %lu bound to CPU core %u (macOS)", (unsigned long)a_thread, a_cpu_id);
    
#elif defined(_WIN32) || defined(_WIN64)
    // Windows: SetThreadAffinityMask
    HANDLE l_thread_handle = (HANDLE)a_thread;
    DWORD_PTR l_mask = (DWORD_PTR)1 << a_cpu_id;
    
    DWORD_PTR l_ret = SetThreadAffinityMask(l_thread_handle, l_mask);
    
    if (l_ret == 0) {
        log_it(L_WARNING, "Failed to set CPU affinity to core %u (Windows)", a_cpu_id);
        return -2;
    }
    
    log_it(L_DEBUG, "Thread bound to CPU core %u (Windows)", a_cpu_id);
    
#else
    // Unsupported platform
    UNUSED(a_thread);
    UNUSED(a_cpu_id);
    log_it(L_INFO, "CPU affinity not supported on this platform");
    return -3;
#endif
    
    return 0;
}
