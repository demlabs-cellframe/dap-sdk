/**
 * @file dap_worker_send.h
 * @brief Worker lane helper.
 *
 *  Protocol code should include @ref dap_io_send.h for @c dap_io_tx_send /
 *  @c dap_io_tx_send_direct / @c dap_io_send_rc_to_msg_rc.
 */
#pragma once

#include "dap_worker_types.h"

/* ================================================================== */
/*  Lane computation                                                   */
/* ================================================================== */

/** @brief Compute WFQ lane indices from worker_id and n_workers. */
DAP_STATIC_INLINE void dap_worker_set_lanes(dap_worker_t *a_w)
{
    a_w->conn_lane = DAP_WFQ_PRI_LANE(DAP_WFQ_PRI_NORM, a_w->worker_id, a_w->n_workers);
    a_w->bg_lane   = DAP_WFQ_PRI_LANE(DAP_WFQ_PRI_BG,   a_w->worker_id, a_w->n_workers);
}
