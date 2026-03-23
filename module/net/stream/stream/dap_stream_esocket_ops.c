#include <assert.h>
#include "dap_stream_esocket_ops.h"
#include "dap_stream.h"
#include "dap_stream_ch.h"
#include "dap_stream_ch_proc.h"
#include "dap_net_trans_ctx.h"
#include "dap_events_socket.h"
#include "dap_timerfd.h"
#include "dap_common.h"
#include "dap_stream_worker.h"

#define LOG_TAG "dap_stream_esocket_ops"

extern void dap_stream_delete_from_list(dap_stream_t *a_stream);

/**
 * @brief dap_stream_states_update
 * @param a_stream stream instance
 */
void dap_stream_states_update(dap_stream_t *a_stream)
{
    if (!a_stream) {
        log_it(L_ERROR, "s_stream_states_update: stream is NULL");
        return;
    }
    if (!a_stream->trans_ctx || !a_stream->trans_ctx->esocket) {
        log_it(L_ERROR, "s_stream_states_update: stream->esocket is NULL");
        return;
    }
    size_t i;
    bool ready_to_write=false;
    for(i=0;i<a_stream->channel_count; i++) {
        if (!a_stream->channel || !a_stream->channel[i]) {
            log_it(L_ERROR, "s_stream_states_update: channel[%zu] is NULL (channel_count=%zu, channel=%p)",
                   i, a_stream->channel_count, (void*)a_stream->channel);
            continue;
        }
        ready_to_write|=a_stream->channel[i]->ready_to_write;
    }
    dap_events_socket_set_writable_unsafe(a_stream->trans_ctx->esocket,ready_to_write);

    // Transport-specific state updates should be handled by transport callbacks
    // No direct transport-specific logic here - all through dap_stream_transport API
}

void dap_stream_esocket_delete_cb(dap_events_socket_t* a_esocket, void * a_arg)
{
    UNUSED(a_arg);
    assert(a_esocket);

    dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t *)a_esocket->_inheritor;
    a_esocket->_inheritor = NULL;

    if (!l_trans_ctx)
        return;

    dap_stream_t *l_stm = l_trans_ctx->stream;
    l_trans_ctx->esocket = NULL;

    if (l_stm) {
        if (l_stm->trans && l_stm->trans->ops && l_stm->trans->ops->close)
            l_stm->trans->ops->close(l_stm);
        dap_stream_delete_unsafe(l_stm);
    }
}

/**
 * @brief dap_stream_esocket_error_cb Unified error callback for stream esockets
 */
void dap_stream_esocket_error_cb(dap_events_socket_t *a_esocket, int a_error)
{
    log_it(L_WARNING, "Stream esocket error: %d", a_error);
    // Mark for close, the delete callback will clean up
    a_esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
}

void dap_stream_esocket_worker_assign_cb(dap_events_socket_t * a_esocket, dap_worker_t * a_worker)
{
    if (!a_esocket->is_initalized)
        return;
    dap_stream_t *l_stream = dap_stream_get_from_es(a_esocket);
    assert(l_stream);
    dap_stream_add_to_list(l_stream);
    if (!l_stream->keepalive_timer) {
        dap_events_socket_uuid_t * l_es_uuid= DAP_NEW_Z(dap_events_socket_uuid_t);
        if (!l_es_uuid) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            return;
        }
        *l_es_uuid = a_esocket->uuid;
        dap_timerfd_callback_t l_callback = a_esocket->server ? dap_stream_callback_server_keepalive : dap_stream_callback_client_keepalive;
        l_stream->keepalive_timer = dap_timerfd_start_on_worker(a_worker,
                                                                STREAM_KEEPALIVE_TIMEOUT * 1000,
                                                                l_callback,
                                                                l_es_uuid);
    }
}

void dap_stream_esocket_worker_unassign_cb(dap_events_socket_t * a_esocket, dap_worker_t * a_worker)
{
    UNUSED(a_worker);
    dap_stream_t *l_stream = dap_stream_get_from_es(a_esocket);
    assert(l_stream);
    dap_stream_delete_from_list(l_stream);
    DAP_DEL_Z(l_stream->keepalive_timer->callback_arg);
    dap_timerfd_delete_unsafe(l_stream->keepalive_timer);
    l_stream->keepalive_timer = NULL;
}

/**
 * @brief dap_stream_esocket_read_cb
 * @param a_esocket
 * @param a_arg
 */
void dap_stream_esocket_read_cb(dap_events_socket_t* a_esocket, void * a_arg)
{
    int *l_ret = (int *)a_arg;

    // Unified: _inheritor is always trans_ctx with back-reference to stream
    dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t *)a_esocket->_inheritor;
    dap_stream_t *l_stream = l_trans_ctx ? l_trans_ctx->stream : NULL;

    debug_if(dap_stream_get_dump_packet_headers(), L_DEBUG, "dap_stream_data_read: ready_to_write=%s, client->buf_in_size=%zu",
               (a_esocket->flags & DAP_SOCK_READY_TO_WRITE) ? "true" : "false", a_esocket->buf_in_size);
    debug_if(dap_stream_get_debug(), L_DEBUG, "s_esocket_data_read: stream=%p, buf_in_size=%zu",
             (void*)l_stream, a_esocket->buf_in_size);
    size_t l_processed = dap_stream_data_proc_read(l_stream);
    debug_if(dap_stream_get_debug(), L_DEBUG, "s_esocket_data_read: processed=%zu", l_processed);
    if (l_ret)
        *l_ret = (int)l_processed;
    // Consume processed data from buf_in (critical for TCP: worker expects buf_in cleared)
    if (l_processed > 0)
        dap_events_socket_shrink_buf_in(a_esocket, l_processed);
    // If nothing processed: keep partial data in buf_in, worker will append more on next read
}

/**
 * @brief dap_stream_esocket_write_cb Write callback for stream esockets
 * @param a_esocket DAP client instance
 * @param a_arg Not used
 */
bool dap_stream_esocket_write_cb(dap_events_socket_t *a_esocket , void *a_arg)
{
    bool l_ret = false;

    // Unified: get stream from trans_ctx
    dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t *)a_esocket->_inheritor;
    dap_stream_t *l_stream = l_trans_ctx ? l_trans_ctx->stream : NULL;

    if (!l_stream) {
        return false;
    }

    // Channel identification: iterate all channels and let each one process pending data
    // Each channel maintains its own write queue and will only write if it has pending data
    // This approach works for current use cases but could be optimized in future by:
    // - Maintaining a "dirty" flag for channels with pending writes
    // - Using a priority queue for channels with different QoS requirements
    for (size_t i = 0; i < l_stream->channel_count; i++) {
        dap_stream_ch_t *l_ch = l_stream->channel[i];
        if (l_ch->ready_to_write && l_ch->proc->packet_out_callback)
            l_ret |= l_ch->proc->packet_out_callback(l_ch, a_arg);
    }
    return l_ret;
}
