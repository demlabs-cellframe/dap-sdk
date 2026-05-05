/**
 * @file dap_worker_ipc.h
 * @brief Other-thread → worker reactor: declarations only (no full worker struct here).
 *
 * Bodies: @ref dap_worker_ipc.c.  @ref dap_worker_reactor.h still carries full
 * @c dap_worker_t after @ref dap_worker_types.h.  @c dap_conn_t from
 * @ref dap_conn.h.  @c dap_worker is incomplete in this file so
 * @ref dap_proc_exec.h need not see worker layout.
 */
#pragma once

#include "dap_conn.h"

struct dap_worker;

void dap_worker_kick(struct dap_worker *a_w);

/** After processor batch: SYNC/watermark → pending_bits + kick. */
void dap_worker_after_batch_processed(dap_conn_t *a_c);

/** Dekker notify: SEND_BUSY + pending_bits + worker kick. */
void dap_worker_conn_notify_send(dap_conn_t *a_c);
