/*
 * dap_wasm_sab_ipc — inter-thread signaling for Emscripten WASM+pthreads.
 *
 * Replaces the broken pipe()-based event/queue transport (pipe() returns
 * EOPNOTSUPP in pthread-workers, poll() in Emscripten doesn't integrate
 * WasmFS pipe fds cross-thread) with an MPSC ring of payloads + an atomic
 * futex word woken via emscripten_futex_wake / emscripten_futex_wait.
 *
 * In WASM+pthreads the linear memory is shared, so a plain malloc()'ed
 * structure behaves as a SharedArrayBuffer from the worker's point of view.
 *
 * Design:
 *   - push_ptr/push_event: MPSC, lock-free on the slot array (seq-based),
 *     bumps the per-channel sequence and then the context-wide wake counter
 *     (so the worker can do one futex_wait for many channels).
 *   - drain_ptrs/drain_event: single-consumer, invoked from the owner
 *     pthread after the worker is woken.
 *
 * Wake aggregation:
 *   a_wake_counter is an optional pointer to a context-wide _Atomic uint32_t.
 *   When non-NULL, push also atomic_fetch_add's it and calls
 *   emscripten_futex_wake on it — this way a single context-level wait wakes
 *   up regardless of which SAB channel has data.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h> /* ssize_t */

#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dap_wasm_sab_channel dap_wasm_sab_channel_t;

/**
 * @brief Create an MPSC channel for void* payloads.
 * @param a_capacity Number of slots (must be > 0; rounded up to power of 2).
 * @param a_wake_counter Optional pointer to context-wide wake counter
 *                       (_Atomic uint32_t). NULL to skip context-wide wake.
 * @return new channel or NULL on OOM.
 */
dap_wasm_sab_channel_t *dap_wasm_sab_channel_new(size_t a_capacity,
                                                 _Atomic uint32_t *a_wake_counter);

/** Free channel (caller must ensure no concurrent push/drain). */
void dap_wasm_sab_channel_free(dap_wasm_sab_channel_t *a_ch);

/**
 * @brief Push a pointer into the channel (MP-safe, lock-free).
 * @return 0 on success, -ENOSPC if full.
 */
int dap_wasm_sab_channel_push_ptr(dap_wasm_sab_channel_t *a_ch, void *a_ptr);

/**
 * @brief Push a 64-bit "event" value (non-zero). Semantics: aggregates into
 *        a single uint64 counter (like eventfd); drain returns the summed
 *        value and clears the counter.
 * @return 0 on success.
 */
int dap_wasm_sab_channel_push_event(dap_wasm_sab_channel_t *a_ch,
                                    uint64_t a_value);

/**
 * @brief Drain up to a_max pointers from the channel (single-consumer).
 * @return Number of pointers written to a_out (>=0) or -1 on error.
 */
ssize_t dap_wasm_sab_channel_drain_ptrs(dap_wasm_sab_channel_t *a_ch,
                                        void **a_out, size_t a_max);

/**
 * @brief Drain the aggregated event counter into a_out_value and reset it.
 * @return 1 if there was a pending event, 0 if the counter was zero.
 */
int dap_wasm_sab_channel_drain_event(dap_wasm_sab_channel_t *a_ch,
                                     uint64_t *a_out_value);

/** Non-empty check (approximate, lock-free snapshot). */
bool dap_wasm_sab_channel_has_data(dap_wasm_sab_channel_t *a_ch);

/**
 * @brief Sleep until the wake counter changes or timeout elapses.
 * @param a_expected The value the counter had before the check-for-work pass.
 * @param a_timeout_ms Timeout in ms; <0 means infinite.
 * @return 0 if woken, 1 if timed out, -1 on other.
 */
int dap_wasm_sab_wait(_Atomic uint32_t *a_wake_counter, uint32_t a_expected,
                      int a_timeout_ms);

/** Bump the counter and wake one waiter. Safe to call from any thread. */
void dap_wasm_sab_wake(_Atomic uint32_t *a_wake_counter);

/**
 * @brief Return a "virtual" pseudo-fd unique per channel. The value is always
 *        negative and guaranteed to not collide with real OS fds; it is used
 *        only for bookkeeping inside dap_events_socket_t / dap_context poll
 *        arrays — real poll() is never invoked on it.
 */
int dap_wasm_sab_channel_vfd(dap_wasm_sab_channel_t *a_ch);

/** Predicate: is this fd one of our virtual SAB fds? */
bool dap_wasm_sab_is_vfd(int a_fd);

/** Attach / replace the wake counter on an existing channel. */
void dap_wasm_sab_channel_bind_wake(dap_wasm_sab_channel_t *a_ch,
                                    _Atomic uint32_t *a_wake_counter);

#ifdef __cplusplus
}
#endif
