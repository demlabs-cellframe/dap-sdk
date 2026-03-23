/**
 * @file dap_interval_timer.c
 * @brief Interval timer implementation
 *
 * Copyright (c) 2017-2026 Demlabs
 * License: GNU GPL v3
 */

#include <pthread.h>

#ifdef DAP_OS_WASM
#include <emscripten.h>
#include <emscripten/eventloop.h>
#endif

#ifdef DAP_OS_WINDOWS
#include <windows.h>
#endif

#ifdef DAP_OS_DARWIN
#include <dispatch/dispatch.h>
#endif

#if defined(DAP_OS_LINUX) && !defined(DAP_OS_WASM)
#include <signal.h>
#include <time.h>
#endif

#include "dap_common.h"
#include "dap_ht.h"
#include "dap_interval_timer.h"

#define LOG_TAG "dap_interval_timer"

typedef struct dap_timer_interface {
#ifdef DAP_OS_DARWIN
    dispatch_source_t timer;
#else
    void *timer;
#endif
    dap_timer_callback_t callback;
    void *param;
    dap_ht_handle_t hh;
} dap_timer_interface_t;

static dap_timer_interface_t *s_timers_map;
static pthread_rwlock_t s_timers_rwlock;

void dap_interval_timer_init(void)
{
    s_timers_map = NULL;
    pthread_rwlock_init(&s_timers_rwlock, NULL);
}

void dap_interval_timer_deinit(void)
{
    pthread_rwlock_wrlock(&s_timers_rwlock);
    dap_timer_interface_t *l_cur_timer = NULL, *l_tmp;
    dap_ht_foreach(s_timers_map, l_cur_timer, l_tmp) {
        dap_ht_del(s_timers_map, l_cur_timer);
        dap_interval_timer_disable(l_cur_timer->timer);
        DAP_FREE(l_cur_timer);
    }
    pthread_rwlock_unlock(&s_timers_rwlock);
    pthread_rwlock_destroy(&s_timers_rwlock);
}

#ifdef DAP_OS_WASM
static void s_wasm_callback(void *a_arg)
{
    if (!a_arg) {
        log_it(L_ERROR, "Timer cb arg is NULL");
        return;
    }
    void *l_timer_id = a_arg;
#elif defined(DAP_OS_LINUX)
static void s_posix_callback(union sigval a_arg)
{
    void *l_field_addr = a_arg.sival_ptr;
    if (!l_field_addr) {
        log_it(L_ERROR, "Timer cb arg is NULL");
        return;
    }
    void *l_timer_id = *(void **)l_field_addr;
#elif defined(DAP_OS_WINDOWS)
static void CALLBACK s_win_callback(PVOID a_arg, BOOLEAN a_always_true)
{
    UNUSED(a_always_true);
    if (!a_arg) {
        log_it(L_ERROR, "Timer cb arg is NULL");
        return;
    }
    void *l_timer_id = *(void **)a_arg;
#elif defined(DAP_OS_DARWIN)
static void s_bsd_callback(void *a_arg)
{
    void *l_timer_id = a_arg;
#else
#error "Timer callback is undefined for your platform"
#endif
    pthread_rwlock_rdlock(&s_timers_rwlock);
    dap_timer_interface_t *l_timer = NULL;
    dap_ht_find_ptr(s_timers_map, l_timer_id, l_timer);
    pthread_rwlock_unlock(&s_timers_rwlock);
    if (l_timer && l_timer->callback) {
        l_timer->callback(l_timer->param);
    } else {
        log_it(L_WARNING, "Timer '%p' is not initialized", l_timer_id);
    }
}

dap_interval_timer_t dap_interval_timer_create(unsigned int a_msec, 
                                                dap_timer_callback_t a_callback, 
                                                void *a_param)
{
    dap_timer_interface_t *l_timer_obj = DAP_NEW_Z(dap_timer_interface_t);
    if (!l_timer_obj) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }
    l_timer_obj->callback = a_callback;
    l_timer_obj->param = a_param;

#if defined(DAP_OS_WASM)
    long l_id = emscripten_set_interval(s_wasm_callback, (double)a_msec, l_timer_obj);
    l_timer_obj->timer = (void *)(intptr_t)l_id;
#elif defined(_WIN32)
    if (!CreateTimerQueueTimer(&l_timer_obj->timer, NULL, (WAITORTIMERCALLBACK)s_win_callback,
                               &l_timer_obj->timer, a_msec, a_msec, 
                               WT_EXECUTEINTIMERTHREAD | WT_EXECUTELONGFUNCTION)) {
        DAP_FREE(l_timer_obj);
        return NULL;
    }
#elif defined(DAP_OS_DARWIN)
    dispatch_queue_t l_queue = dispatch_queue_create("tqueue", 0);
    l_timer_obj->timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, l_queue);
    dispatch_source_set_event_handler((l_timer_obj->timer), ^(void){ 
        s_bsd_callback((void*)(l_timer_obj->timer)); 
    });
    dispatch_time_t start = dispatch_time(DISPATCH_TIME_NOW, a_msec * NSEC_PER_MSEC);
    dispatch_source_set_timer(l_timer_obj->timer, start, a_msec * NSEC_PER_MSEC, 0);
    dispatch_resume(l_timer_obj->timer);
#else
    struct sigevent l_sig_event = { };
    l_sig_event.sigev_notify = SIGEV_THREAD;
    l_sig_event.sigev_value.sival_ptr = &l_timer_obj->timer;
    l_sig_event.sigev_notify_function = s_posix_callback;
    if (timer_create(CLOCK_MONOTONIC, &l_sig_event, (timer_t*)&l_timer_obj->timer)) {
        DAP_FREE(l_timer_obj);
        return NULL;
    }
    struct itimerspec l_period = { };
    l_period.it_interval.tv_sec = l_period.it_value.tv_sec = a_msec / 1000;
    l_period.it_interval.tv_nsec = l_period.it_value.tv_nsec = (a_msec % 1000) * 1000000;
    timer_settime(l_timer_obj->timer, 0, &l_period, NULL);
#endif

    pthread_rwlock_wrlock(&s_timers_rwlock);
    dap_ht_add_ptr(s_timers_map, timer, l_timer_obj);
    pthread_rwlock_unlock(&s_timers_rwlock);
    log_it(L_DEBUG, "Interval timer %p created", &l_timer_obj->timer);
    return (dap_interval_timer_t)l_timer_obj->timer;
}

int dap_interval_timer_disable(dap_interval_timer_t a_timer)
{
#ifdef DAP_OS_WASM
    emscripten_clear_interval((long)(intptr_t)a_timer);
    return 0;
#elif defined(_WIN32)
    return !DeleteTimerQueueTimer(NULL, (HANDLE)a_timer, NULL);
#elif defined(DAP_OS_DARWIN)
    dispatch_source_cancel((dispatch_source_t)a_timer);
    return 0;
#else
    return timer_delete((timer_t)a_timer);
#endif
}

void dap_interval_timer_delete(dap_interval_timer_t a_timer)
{
    pthread_rwlock_wrlock(&s_timers_rwlock);
    dap_timer_interface_t *l_timer = NULL;
    dap_ht_find_ptr(s_timers_map, a_timer, l_timer);
    if (l_timer) {
        dap_ht_del(s_timers_map, l_timer);
        dap_interval_timer_disable(l_timer->timer);
        DAP_FREE(l_timer);
    }
    pthread_rwlock_unlock(&s_timers_rwlock);
}
