/*
 * dap_io_wasm_stub.c — Minimal IO stubs for WebAssembly build
 *
 * In WASM, networking is handled by JavaScript (WebSocket/fetch).
 * These stubs allow the SDK to compile without the full IO module.
 */

#ifdef DAP_OS_WASM

#include "dap_common.h"

#define LOG_TAG "dap_io_wasm"

int dap_events_init(unsigned a_threads_count, size_t a_conn_timeout)
{
    (void)a_threads_count; (void)a_conn_timeout;
    log_it(L_INFO, "WASM IO stub: events_init (no-op)");
    return 0;
}

void dap_events_deinit(void)
{
    log_it(L_INFO, "WASM IO stub: events_deinit (no-op)");
}

int dap_timerfd_init(void)
{
    return 0;
}

#endif /* DAP_OS_WASM */
