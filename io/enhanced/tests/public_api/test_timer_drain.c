#include <stdio.h>
#include <unistd.h>
#include "dap_timer_heap.h"

_Atomic uint64_t dap_timer_g_next_id = 1;

static int g_count;
static void tick(void *a) { (void)a; ++g_count; }

int main(void)
{
    dap_timers_t tl;
    dap_timers_init(&tl);

    dap_timer_add(&tl, 0, DAP_TIMER_SLOT_PROC, 10000,  0, tick, NULL);
    dap_timer_add(&tl, 0, DAP_TIMER_SLOT_PROC, 50000,  0, tick, NULL);
    dap_timer_add(&tl, 0, DAP_TIMER_SLOT_PROC, 200000, 0, tick, NULL);

    printf("timers added, sleeping 100ms...\n");
    usleep(100000);

    printf("drain #1 (should fire 10ms+50ms)...\n");
    uint32_t n = dap_timers_drain(&tl);
    printf("  fired=%u  g_count=%d\n", n, g_count);

    printf("sleeping 250ms...\n");
    usleep(250000);

    printf("drain #2 (should fire all 3)...\n");
    n = dap_timers_drain(&tl);
    printf("  fired=%u  g_count=%d\n", n, g_count);

    printf("drain #3 (should fire 0)...\n");
    n = dap_timers_drain(&tl);
    printf("  fired=%u  g_count=%d\n", n, g_count);

    dap_timers_destroy(&tl);
    printf("OK\n");
    return 0;
}
