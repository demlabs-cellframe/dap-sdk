/**
 * @file rate_limiter.c
 * @brief Token bucket shared by both payload directions. DPI measurements
 *        (docs/MOBILE_DPI_RESULTS_2026-11-07.md): sustained ~100 Mbit/s
 *        triggers a flow kill on the target mobile ISP; <=83 Mbit/s passed.
 *        0 = unlimited (product default); the GUI exposes the cap in Mbit/s.
 */

#include "reality_internal.h"

/* Floor for the burst bucket. The bridge pump presents chunks up to 16 KiB, so
 * the bucket must always be able to admit one; otherwise allow() could never
 * succeed for a small cap and the waiter would spin forever. */
#define RATE_LIMITER_MIN_BURST 32768u

void rate_limiter_init(rate_limiter_t *a_rl, uint64_t a_rate_bps)
{
    a_rl->rate_bps = a_rate_bps;
    /* Burst = 250 ms of traffic. A full second of tokens let short transfers
     * run at full speed (the cap only kicked in after ~rate bytes), which made
     * the limit look inert on speed tests. */
    a_rl->burst = a_rate_bps / 4;
    if (a_rl->rate_bps && a_rl->burst < RATE_LIMITER_MIN_BURST)
        a_rl->burst = RATE_LIMITER_MIN_BURST;
    a_rl->tokens = a_rl->burst;
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
        if (a_rl->tokens > a_rl->burst) a_rl->tokens = a_rl->burst;
        a_rl->last_mono_ms = a_now_mono_ms;
    }
    /* A chunk larger than the whole bucket is admitted once the bucket is full
     * (progress guarantee) and drains it instead of going negative. */
    uint64_t need = (uint64_t)a_len > a_rl->burst ? a_rl->burst : (uint64_t)a_len;
    if (a_rl->tokens >= need) {
        a_rl->tokens -= ((uint64_t)a_len < a_rl->tokens) ? (uint64_t)a_len : a_rl->tokens;
        return true;
    }
    return false;
}

uint64_t rate_limiter_delay_ms(rate_limiter_t *a_rl, uint64_t a_now_mono_ms, size_t a_len)
{
    if (!a_rl->rate_bps) return 0;
    if (rate_limiter_allow(a_rl, a_now_mono_ms, a_len)) return 0;
    uint64_t need = (uint64_t)a_len > a_rl->burst ? a_rl->burst : (uint64_t)a_len;
    uint64_t deficit = need - a_rl->tokens;
    return (uint64_t)((double)deficit * 1000.0 / (double)a_rl->rate_bps) + 1;
}
