/*
 * dap_stream_wasm.c — WASM-specific implementation of DAP stream core
 *
 * Simplified single-threaded stream implementation for browser environment.
 * Uses WebSocket transport instead of native event loop.
 * Protocol parsing and encryption logic reused from portable modules.
 */

#ifdef DAP_OS_WASM

#include <stdlib.h>
#include <string.h>

#include "dap_common.h"
#include "dap_config.h"
#include "dap_stream.h"
#include "dap_stream_pkt.h"
#include "dap_stream_ch.h"
#include "dap_stream_ch_pkt.h"
#include "dap_stream_session.h"
#include "dap_enc.h"
#include "dap_enc_key.h"
#include "dap_strfuncs.h"
#include "dap_ht.h"
#include "dap_net_trans.h"
#include "dap_net_trans_ctx.h"

#define LOG_TAG "dap_stream"

static bool s_dump_packet_headers = false;
static bool s_debug = false;
static dap_enc_key_type_t s_stream_get_preferred_encryption_type = DAP_ENC_KEY_TYPE_IAES;

static dap_stream_member_callback_t s_member_add_callback = NULL;
static dap_stream_member_callback_t s_member_del_callback = NULL;

static dap_stream_t *s_streams = NULL;

static void s_stream_proc_pkt_in(dap_stream_t *a_stream, dap_stream_pkt_t *a_pkt);

bool dap_stream_get_dump_packet_headers(void)
{
    return s_dump_packet_headers;
}

dap_enc_key_type_t dap_stream_get_preferred_encryption_type(void)
{
    return s_stream_get_preferred_encryption_type;
}

ssize_t dap_stream_trans_write_unsafe(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_data || a_size == 0)
        return 0;

    dap_net_trans_t *l_trans = NULL;
    if (a_stream->trans_ctx && a_stream->trans_ctx->trans)
        l_trans = a_stream->trans_ctx->trans;
    else if (a_stream->trans)
        l_trans = a_stream->trans;

    if (!l_trans || !l_trans->ops || !l_trans->ops->write) {
        log_it(L_ERROR, "Stream has no trans write callback");
        return 0;
    }

    return l_trans->ops->write(a_stream, a_data, a_size);
}

ssize_t dap_stream_send_unsafe(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    return dap_stream_trans_write_unsafe(a_stream, a_data, a_size);
}

int dap_stream_init(dap_config_t *a_config)
{
    if (dap_stream_ch_init() != 0) {
        log_it(L_CRITICAL, "Can't init channel types submodule");
        return -1;
    }

    if (a_config) {
        const char *l_enc_name = dap_config_get_item_str(a_config, "stream", "preferred_encryption");
        if (l_enc_name) {
            dap_enc_key_type_t l_type = dap_enc_key_type_find_by_name(l_enc_name);
            if (l_type != DAP_ENC_KEY_TYPE_INVALID)
                s_stream_get_preferred_encryption_type = l_type;
        }
        s_dump_packet_headers = dap_config_get_item_bool_default(a_config, "stream", "debug_dump_stream_headers", false);
        s_debug = dap_config_get_item_bool_default(a_config, "stream", "debug_more", false);
    }

    log_it(L_NOTICE, "Init streaming module (WASM)");
    return 0;
}

void dap_stream_deinit(void)
{
    dap_stream_ch_deinit();
}

void dap_stream_set_member_callbacks(dap_stream_member_callback_t a_add,
                                     dap_stream_member_callback_t a_del)
{
    s_member_add_callback = a_add;
    s_member_del_callback = a_del;
}

int dap_stream_add_to_list(dap_stream_t *a_stream)
{
    dap_return_val_if_fail(a_stream, -1);
    DL_APPEND(s_streams, a_stream);

    if (s_member_add_callback)
        s_member_add_callback(&a_stream->node);
    return 0;
}

static void s_stream_delete_from_list(dap_stream_t *a_stream)
{
    if (!a_stream) return;
    DL_DELETE(s_streams, a_stream);

    if (s_member_del_callback)
        s_member_del_callback(&a_stream->node);
}

void dap_stream_delete_unsafe(dap_stream_t *a_stream)
{
    if (!a_stream) {
        log_it(L_ERROR, "stream delete NULL instance");
        return;
    }
    s_stream_delete_from_list(a_stream);

    while (a_stream->channel_count)
        dap_stream_ch_delete(a_stream->channel[a_stream->channel_count - 1]);

    if (a_stream->session)
        dap_stream_session_close_mt(a_stream->session->id);

    dap_net_trans_t *l_trans = a_stream->trans;
    if (l_trans) {
        a_stream->trans = NULL;
        if (l_trans->ops && l_trans->ops->close)
            l_trans->ops->close(a_stream);
    }

    DAP_DELETE(a_stream->trans_ctx);
    a_stream->trans_ctx = NULL;
    DAP_DEL_Z(a_stream->buf_fragments);
    DAP_DELETE(a_stream);
    log_it(L_NOTICE, "Stream connection is over");
}

size_t dap_stream_data_proc_read(dap_stream_t *a_stream)
{
    (void)a_stream;
    return 0;
}

size_t dap_stream_data_proc_read_ext(dap_stream_t *a_stream, const void *a_data, size_t a_data_size)
{
    if (!a_stream || !a_data || a_data_size == 0)
        return 0;

    byte_t *l_pos = (byte_t *)a_data;
    byte_t *l_end = l_pos + a_data_size;
    size_t l_shift = 0, l_processed_size = 0;

    while (l_pos < l_end && (l_pos = memchr(l_pos, c_dap_stream_sig[0], (size_t)(l_end - l_pos)))) {
        if ((size_t)(l_end - l_pos) < sizeof(dap_stream_pkt_hdr_t))
            break;

        if (!memcmp(l_pos, c_dap_stream_sig, sizeof(c_dap_stream_sig))) {
            dap_stream_pkt_t *l_pkt = (dap_stream_pkt_t *)l_pos;
            if (l_pkt->hdr.size > DAP_STREAM_PKT_SIZE_MAX) {
                log_it(L_ERROR, "Invalid packet size %u", l_pkt->hdr.size);
                l_shift = sizeof(dap_stream_pkt_hdr_t);
            } else if ((l_shift = sizeof(dap_stream_pkt_hdr_t) + l_pkt->hdr.size) <= (size_t)(l_end - l_pos)) {
                s_stream_proc_pkt_in(a_stream, l_pkt);
            } else {
                break;
            }
            l_pos += l_shift;
            l_processed_size += l_shift;
        } else {
            ++l_pos;
        }
    }

    return l_processed_size;
}

size_t dap_stream_data_proc_write(dap_stream_t *a_stream)
{
    (void)a_stream;
    return 0;
}

static void s_stream_proc_pkt_in(dap_stream_t *a_stream, dap_stream_pkt_t *a_pkt)
{
    a_stream->is_active = true;

    switch (a_pkt->hdr.type) {
    case STREAM_PKT_TYPE_FRAGMENT_PACKET: {
        size_t l_fragm_dec_size = dap_enc_decode_out_size(a_stream->session->key,
                                                          a_pkt->hdr.size, DAP_ENC_DATA_TYPE_RAW);
        a_stream->pkt_cache = DAP_NEW_Z_SIZE(byte_t, l_fragm_dec_size);
        dap_stream_fragment_pkt_t *l_fragm_pkt = (dap_stream_fragment_pkt_t *)a_stream->pkt_cache;
        size_t l_dec_pkt_size = dap_stream_pkt_read_unsafe(a_stream, a_pkt, l_fragm_pkt, l_fragm_dec_size);

        if (l_dec_pkt_size == 0 ||
            l_dec_pkt_size != l_fragm_pkt->size + sizeof(dap_stream_fragment_pkt_t) ||
            a_stream->buf_fragments_size_filled != l_fragm_pkt->mem_shift) {
            DAP_DEL_Z(a_stream->buf_fragments);
            a_stream->buf_fragments_size_filled = 0;
            a_stream->buf_fragments_size_total = 0;
        } else {
            if (!a_stream->buf_fragments || a_stream->buf_fragments_size_total < l_fragm_pkt->full_size) {
                DAP_DEL_Z(a_stream->buf_fragments);
                a_stream->buf_fragments = DAP_NEW_Z_SIZE(uint8_t, l_fragm_pkt->full_size);
                a_stream->buf_fragments_size_total = l_fragm_pkt->full_size;
            }
            memcpy(a_stream->buf_fragments + l_fragm_pkt->mem_shift, l_fragm_pkt->data, l_fragm_pkt->size);
            a_stream->buf_fragments_size_filled = l_fragm_pkt->mem_shift + l_fragm_pkt->size;

            if (a_stream->buf_fragments_size_filled >= l_fragm_pkt->full_size) {
                dap_stream_ch_pkt_t *l_ch_pkt = (dap_stream_ch_pkt_t *)a_stream->buf_fragments;
                dap_stream_ch_t *l_ch = dap_stream_ch_by_id_unsafe(a_stream, l_ch_pkt->hdr.id);
                if (l_ch && l_ch->proc && l_ch->proc->packet_in_callback)
                    l_ch->proc->packet_in_callback(l_ch, l_ch_pkt->hdr.type,
                                                    l_ch_pkt->data, l_ch_pkt->hdr.data_size);
                a_stream->buf_fragments_size_filled = 0;
            }
        }
        DAP_DEL_Z(a_stream->pkt_cache);
        break;
    }
    case STREAM_PKT_TYPE_DATA_PACKET: {
        size_t l_dec_size = dap_enc_decode_out_size(a_stream->session->key,
                                                    a_pkt->hdr.size, DAP_ENC_DATA_TYPE_RAW);
        byte_t *l_dec_buf = DAP_NEW_Z_SIZE(byte_t, l_dec_size);
        size_t l_dec_pkt_size = dap_stream_pkt_read_unsafe(a_stream, a_pkt, l_dec_buf, l_dec_size);

        if (l_dec_pkt_size >= sizeof(dap_stream_ch_pkt_hdr_t)) {
            dap_stream_ch_pkt_t *l_ch_pkt = (dap_stream_ch_pkt_t *)l_dec_buf;
            dap_stream_ch_t *l_ch = dap_stream_ch_by_id_unsafe(a_stream, l_ch_pkt->hdr.id);
            if (l_ch && l_ch->proc && l_ch->proc->packet_in_callback)
                l_ch->proc->packet_in_callback(l_ch, l_ch_pkt->hdr.type,
                                                l_ch_pkt->data, l_ch_pkt->hdr.data_size);
        }
        DAP_DELETE(l_dec_buf);
        break;
    }
    case STREAM_PKT_TYPE_KEEPALIVE:
        dap_stream_pkt_write_unsafe(a_stream, STREAM_PKT_TYPE_ALIVE, NULL, 0);
        break;
    case STREAM_PKT_TYPE_ALIVE:
        break;
    case STREAM_PKT_TYPE_SERVICE_PACKET:
        break;
    default:
        log_it(L_WARNING, "Unknown stream packet type 0x%02X", a_pkt->hdr.type);
    }
}

/* Stubs for dead declarations */
void dap_stream_proc_pkt_in(dap_stream_t *a_stream)
{
    (void)a_stream;
}

int dap_stream_add_addr(dap_cluster_node_addr_t a_addr, void *a_id)
{
    (void)a_addr; (void)a_id;
    return 0;
}

int dap_stream_delete_addr(dap_cluster_node_addr_t a_addr, bool a_full)
{
    (void)a_addr; (void)a_full;
    return 0;
}

int dap_stream_delete_prep_addr(uint64_t a_num_id, void *a_pointer_id)
{
    (void)a_num_id; (void)a_pointer_id;
    return 0;
}

int dap_stream_add_stream_info(dap_stream_t *a_stream, uint64_t a_id)
{
    (void)a_stream; (void)a_id;
    return 0;
}

dap_stream_info_t *dap_stream_get_all_links_info(size_t *a_count)
{
    if (a_count) *a_count = 0;
    return NULL;
}

dap_stream_info_t *dap_stream_get_links_info_by_addrs(dap_cluster_node_addr_t *a_addrs,
                                                       size_t a_addrs_count, size_t *a_count)
{
    (void)a_addrs; (void)a_addrs_count;
    if (a_count) *a_count = 0;
    return NULL;
}

void dap_stream_delete_links_info(dap_stream_info_t *a_info, size_t a_count)
{
    if (!a_info) return;
    for (size_t i = 0; i < a_count; i++) {
        DAP_DEL_Z(a_info[i].remote_addr_str);
        DAP_DEL_Z(a_info[i].channels);
    }
    DAP_DELETE(a_info);
}

#endif /* DAP_OS_WASM */
