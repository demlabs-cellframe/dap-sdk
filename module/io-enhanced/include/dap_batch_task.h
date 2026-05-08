/**
 * @file dap_batch_task.h
 * @brief WFQ @c DAP_MSG_BATCH payload — worker → processor batch descriptor.
 *
 * Kept separate from @ref dap_msg_types.h so lane payload headers do not
 * pull the full connection / OLB graph.
 *
 *   conn      — generation-checked handle (@ref dap_conn_handle_t).  The
 *               processor must call @c dap_conn_resolve() (see @ref dap_conn.h)
 *               before dereferencing; NULL means stale slot / drop batch.
 *   batch_end — absolute recv OLB offset; payload is [ack_pos, batch_end).
 */
#pragma once

#include <stdint.h>
#include "dap_conn_handle.h"

typedef struct {
    dap_conn_handle_t conn;
    uint32_t          batch_end;
} dap_batch_task_t;
