/**
 * @file rate_limiter.c
 * @brief Token bucket. DPI measurements (docs/MOBILE_DPI_RESULTS_2026-11-07.md):
 *        sustained ~100 Mbit/s triggers flow kill on the target mobile ISP;
 *        <=83 Mbit/s passed. Default product cap: 5-6 MB/s.
 */

#include "reality_internal.h"

void rate_limiter_init(rate_limiter_t *a_rl, uint64_t a_rate_bps)
{
    a_rl->rate_bps = a_rate_bps;
    a_rl->tokens = a_rate_bps; /* start full-second bucket */
    a_rl->last_mono_ms = 0;
}

bool rate_limiter_allow(rate_limiter_t *a_rl, uint64_t a_now_mono_ms, size_t a_len)
{
    if (!a_rl->rate_bps) return true;
    if (!a_rl->last_mono_ms) { a_rl->last_mono_ms = a_now_mono_ms; }
    uint64_t elapsed = a_now_mono_ms - a_rl->last_mono_ms;
    uint64_t refill = (uint64_t)((double)a_rl->rate_bps * (double)elapsed / 1000.0);
    if (refill) {
        a_rl->tokens += refill;
        if (a_rl->tokens > a_rl->rate_bps) a_rl->tokens = a_rl->rate_bps;
        a_rl->last_mono_ms = a_now_mono_ms;
    }
    if (a_rl->tokens >= a_len) { a_rl->tokens -= a_len; return true; }
    return false;
}

uint64_t rate_limiter_delay_ms(rate_limiter_t *a_rl, uint64_t a_now_mono_ms, size_t a_len)
{
    if (!a_rl->rate_bps) return 0;
    if (rate_limiter_allow(a_rl, a_now_mono_ms, a_len)) return 0;
    uint64_t deficit = a_len - a_rl->tokens;
    return (uint64_t)((double)deficit * 1000.0 / (double)a_rl->rate_bps) + 1;
}
