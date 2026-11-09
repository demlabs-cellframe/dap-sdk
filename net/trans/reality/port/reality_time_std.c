/**
 * @file reality_time_std.c
 * @brief Standard time port implementation.
 */

#include "reality/reality.h"

#include <time.h>
#include <unistd.h>

static int64_t t_unix_now(void *u)
{
    (void)u;
    return (int64_t)time(NULL);
}

static uint64_t t_mono_ms(void *u)
{
    (void)u;
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    clock_gettime(CLOCK_REALTIME, &ts);
#endif
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

static void t_sleep_ms(void *u, uint64_t ms)
{
    (void)u;
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000000L);
    nanosleep(&ts, NULL);
}

static const reality_time_ops_t g_time_std = {
    .user = NULL,
    .unix_now = t_unix_now,
    .mono_ms = t_mono_ms,
    .sleep_ms = t_sleep_ms,
};

const reality_time_ops_t *reality_time_std(void)
{
    return &g_time_std;
}
