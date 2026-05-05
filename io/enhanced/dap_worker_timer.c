/*
 * Authors:
 * Constantin Papizh <papizh.konstantin@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file dap_worker_timer.c
 * @brief Per-worker timer heap + platform rearm (timerfd on POSIX, GQCS timeout on Windows).
 */
#include "dap_worker_reactor.h"

#ifdef DAP_OS_WINDOWS
# include <windows.h>
#else
# include <sys/timerfd.h>
# include <time.h>
#endif

#ifdef DAP_OS_WINDOWS

void dap_worker_timer_rearm(dap_worker_t *a_w)
{
    (void)a_w;
}

dap_timer_handle_t dap_worker_timer_add(dap_worker_t *a_w,
                                      uint64_t a_interval_us,
                                      uint32_t a_iterations,
                                      dap_timer_cb_t a_exec,
                                      void *a_arg)
{
    return dap_timer_add(&a_w->timers,
                         a_w->proc_idx, (uint8_t)a_w->worker_id,
                         a_interval_us, a_iterations, a_exec, a_arg);
}

void dap_worker_timer_del(dap_worker_t *a_w, dap_timer_handle_t a_h)
{
    dap_timer_del(&a_w->timers, a_h);
}

#else /* !DAP_OS_WINDOWS */

void dap_worker_timer_rearm(dap_worker_t *a_w)
{
    if (!a_w->timer_conn || a_w->timer_conn->fd < 0)
        return;
    struct itimerspec l_its = {0};
    dap_time_t l_nearest = dap_timers_nearest(&a_w->timers);
    if (DAP_TIME_NONZERO(l_nearest)) {
        dap_time_t l_now = dap_time_now();
        if (dap_time_le(l_nearest, l_now)) {
            l_its.it_value.tv_nsec = 1;
        } else {
            l_its.it_value = dap_time_sub(l_nearest, l_now);
        }
    }
    timerfd_settime(a_w->timer_conn->fd, 0, &l_its, NULL);
}

dap_timer_handle_t dap_worker_timer_add(dap_worker_t *a_w,
                                        uint64_t a_interval_us,
                                        uint32_t a_iterations,
                                        dap_timer_cb_t a_exec,
                                        void *a_arg)
{
    dap_timer_handle_t l_h = dap_timer_add(&a_w->timers,
                                            a_w->proc_idx, (uint8_t)a_w->worker_id,
                                            a_interval_us, a_iterations, a_exec, a_arg);
    if (l_h)
        dap_worker_timer_rearm(a_w);
    return l_h;
}

void dap_worker_timer_del(dap_worker_t *a_w, dap_timer_handle_t a_h)
{
    if (dap_timer_del(&a_w->timers, a_h))
        dap_worker_timer_rearm(a_w);
}

#endif /* DAP_OS_WINDOWS */
