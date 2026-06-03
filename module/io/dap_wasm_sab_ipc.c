/*
 * dap_wasm_sab_ipc — see header for rationale and API.
 *
 * Implementation notes:
 *   - slots array is sized to power-of-two, seq-based Vyukov MPSC pattern
 *     (monotone head/tail + per-slot sequence number for ABA-safety).
 *   - push for pointer channels stores the payload in slots[] then bumps
 *     the (required) context wake counter bound via
 *     dap_wasm_sab_channel_bind_wake() and calls emscripten_futex_wake.
 *   - event channels don't use slots; they aggregate into event_sum.
 *   - wait path: the worker thread loads its context wake counter, scans
 *     all its SAB channels for data; if none, it blocks on the counter
 *     via emscripten_futex_wait (see dap_wasm_sab_wait).
 */

#include "dap_wasm_sab_ipc.h"
#include "dap_common.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdatomic.h>

#if defined(DAP_OS_WASM_MT)
#include <emscripten.h>
#include <emscripten/threading.h>
#endif

#define LOG_TAG "dap_wasm_sab_ipc"

/* Virtual fd range: big negative numbers that can't collide with real fds. */
#define DAP_WASM_SAB_VFD_BASE (-0x40000000)

static _Atomic int s_vfd_next = DAP_WASM_SAB_VFD_BASE;

struct dap_wasm_sab_channel {
    /* Slot ring (Vyukov MPSC).  capacity is power-of-two. */
    size_t capacity;
    size_t mask;
    _Atomic uint64_t enqueue_pos; /* producer index (monotonic) */
    _Atomic uint64_t dequeue_pos; /* consumer index (monotonic) */
    struct {
        _Atomic uint64_t seq;
        void *ptr;
    } *slots;

    /* Event aggregation (for dap_context_event and timer ticks). */
    _Atomic uint64_t event_sum;

    /* Wake mechanism. The counter belongs to the consumer's context — it
     * is atomically bumped on every push and futex_wake'd so the context
     * worker loop is unblocked. NULL until dap_wasm_sab_channel_bind_wake()
     * is called (e.g. before the channel is registered with a context). */
    _Atomic uint32_t *wake_counter;

    int vfd;
};

static inline size_t s_round_pow2(size_t v)
{
    if (v <= 1) return 1;
    size_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

dap_wasm_sab_channel_t *dap_wasm_sab_channel_new(size_t a_capacity,
                                                 _Atomic uint32_t *a_wake_counter)
{
    dap_wasm_sab_channel_t *l_ch = DAP_NEW_Z(dap_wasm_sab_channel_t);
    if (!l_ch) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }

    size_t l_cap = s_round_pow2(a_capacity ? a_capacity : 1024);
    l_ch->capacity = l_cap;
    l_ch->mask = l_cap - 1;
    l_ch->slots = DAP_NEW_Z_COUNT(__typeof__(*l_ch->slots), l_cap);
    if (!l_ch->slots) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_ch);
        return NULL;
    }
    for (size_t i = 0; i < l_cap; i++)
        atomic_store_explicit(&l_ch->slots[i].seq, i, memory_order_relaxed);

    atomic_store_explicit(&l_ch->enqueue_pos, 0, memory_order_relaxed);
    atomic_store_explicit(&l_ch->dequeue_pos, 0, memory_order_relaxed);
    atomic_store_explicit(&l_ch->event_sum, 0, memory_order_relaxed);
    l_ch->wake_counter = a_wake_counter;
    l_ch->vfd = atomic_fetch_sub(&s_vfd_next, 1);
    return l_ch;
}

void dap_wasm_sab_channel_free(dap_wasm_sab_channel_t *a_ch)
{
    if (!a_ch)
        return;
    DAP_DELETE(a_ch->slots);
    DAP_DELETE(a_ch);
}

int dap_wasm_sab_channel_push_ptr(dap_wasm_sab_channel_t *a_ch, void *a_ptr)
{
    if (!a_ch) return -EINVAL;

    uint64_t l_pos = atomic_load_explicit(&a_ch->enqueue_pos, memory_order_relaxed);
    for (;;) {
        size_t l_idx = (size_t)(l_pos & a_ch->mask);
        uint64_t l_seq = atomic_load_explicit(&a_ch->slots[l_idx].seq,
                                              memory_order_acquire);
        int64_t l_diff = (int64_t)l_seq - (int64_t)l_pos;
        if (l_diff == 0) {
            if (atomic_compare_exchange_weak_explicit(&a_ch->enqueue_pos, &l_pos,
                                                      l_pos + 1,
                                                      memory_order_relaxed,
                                                      memory_order_relaxed)) {
                a_ch->slots[l_idx].ptr = a_ptr;
                atomic_store_explicit(&a_ch->slots[l_idx].seq, l_pos + 1,
                                      memory_order_release);
                break;
            }
        } else if (l_diff < 0) {
            /* full */
            return -ENOSPC;
        } else {
            l_pos = atomic_load_explicit(&a_ch->enqueue_pos, memory_order_relaxed);
        }
    }

    /* Wake the consumer's worker context, if one is bound. Channels
     * created before context attachment rely on bind_wake() to supply the
     * counter later — any data pushed before attach stays in the ring and
     * is delivered on the first post-attach scan. */
    if (a_ch->wake_counter) {
        atomic_fetch_add_explicit(a_ch->wake_counter, 1, memory_order_release);
#if defined(DAP_OS_WASM_MT)
        emscripten_futex_wake((void*)a_ch->wake_counter, 1);
#endif
    }
    return 0;
}

int dap_wasm_sab_channel_push_event(dap_wasm_sab_channel_t *a_ch, uint64_t a_value)
{
    if (!a_ch) return -EINVAL;
    if (!a_value) a_value = 1;
    atomic_fetch_add_explicit(&a_ch->event_sum, a_value, memory_order_release);
    if (a_ch->wake_counter) {
        atomic_fetch_add_explicit(a_ch->wake_counter, 1, memory_order_release);
#if defined(DAP_OS_WASM_MT)
        emscripten_futex_wake((void*)a_ch->wake_counter, 1);
#endif
    }
    return 0;
}

ssize_t dap_wasm_sab_channel_drain_ptrs(dap_wasm_sab_channel_t *a_ch,
                                        void **a_out, size_t a_max)
{
    if (!a_ch || !a_out) return -1;
    size_t l_got = 0;
    uint64_t l_pos = atomic_load_explicit(&a_ch->dequeue_pos, memory_order_relaxed);
    while (l_got < a_max) {
        size_t l_idx = (size_t)(l_pos & a_ch->mask);
        uint64_t l_seq = atomic_load_explicit(&a_ch->slots[l_idx].seq,
                                              memory_order_acquire);
        int64_t l_diff = (int64_t)l_seq - (int64_t)(l_pos + 1);
        if (l_diff == 0) {
            a_out[l_got++] = a_ch->slots[l_idx].ptr;
            atomic_store_explicit(&a_ch->slots[l_idx].seq,
                                  l_pos + a_ch->mask + 1,
                                  memory_order_release);
            l_pos++;
        } else {
            /* empty */
            break;
        }
    }
    atomic_store_explicit(&a_ch->dequeue_pos, l_pos, memory_order_relaxed);
    return (ssize_t)l_got;
}

int dap_wasm_sab_channel_drain_event(dap_wasm_sab_channel_t *a_ch,
                                     uint64_t *a_out_value)
{
    if (!a_ch) return 0;
    uint64_t l_val = atomic_exchange_explicit(&a_ch->event_sum, 0,
                                              memory_order_acquire);
    if (a_out_value) *a_out_value = l_val;
    return l_val ? 1 : 0;
}

bool dap_wasm_sab_channel_has_data(dap_wasm_sab_channel_t *a_ch)
{
    if (!a_ch) return false;
    if (atomic_load_explicit(&a_ch->event_sum, memory_order_acquire))
        return true;
    uint64_t l_enq = atomic_load_explicit(&a_ch->enqueue_pos, memory_order_acquire);
    uint64_t l_deq = atomic_load_explicit(&a_ch->dequeue_pos, memory_order_acquire);
    return l_enq != l_deq;
}

int dap_wasm_sab_wait(_Atomic uint32_t *a_wake_counter, uint32_t a_expected,
                      int a_timeout_ms)
{
    if (!a_wake_counter) return -1;
#if defined(DAP_OS_WASM_MT)
    double l_timeout = a_timeout_ms < 0 ? INFINITY : (double)a_timeout_ms;
    /* emscripten_futex_wait returns:
     *   0  — woken
     *  -ETIMEDOUT on timeout
     *  -EWOULDBLOCK if current value != expected
     */
    int l_rc = emscripten_futex_wait((void*)a_wake_counter,
                                     (uint32_t)a_expected, l_timeout);
    if (l_rc == 0) return 0;
    if (l_rc == -ETIMEDOUT) return 1;
    /* EWOULDBLOCK => value changed, work appeared */
    return 0;
#else
    (void)a_expected;
    (void)a_timeout_ms;
    return -1;
#endif
}

void dap_wasm_sab_wake(_Atomic uint32_t *a_wake_counter)
{
    if (!a_wake_counter) return;
    atomic_fetch_add_explicit(a_wake_counter, 1, memory_order_release);
#if defined(DAP_OS_WASM_MT)
    emscripten_futex_wake((void*)a_wake_counter, 1);
#endif
}

int dap_wasm_sab_channel_vfd(dap_wasm_sab_channel_t *a_ch)
{
    return a_ch ? a_ch->vfd : 0;
}

void dap_wasm_sab_channel_bind_wake(dap_wasm_sab_channel_t *a_ch,
                                    _Atomic uint32_t *a_wake_counter)
{
    if (!a_ch) return;
    a_ch->wake_counter = a_wake_counter;
}

bool dap_wasm_sab_is_vfd(int a_fd)
{
    /* All SAB vfds are in the range [DAP_WASM_SAB_VFD_BASE - 2^30, DAP_WASM_SAB_VFD_BASE].
     * Any fd <= DAP_WASM_SAB_VFD_BASE can only be a SAB vfd. */
    return a_fd <= DAP_WASM_SAB_VFD_BASE;
}
