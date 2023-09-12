/*
* Authors:
* Roman Khlopkov <roman.khlopkov@demlabs.net>
* Cellframe       https://cellframe.net
* DeM Labs Inc.   https://demlabs.net
* Copyright  (c) 2017-2023
* All rights reserved.

This file is part of DAP SDK the open source project

DAP SDK is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

DAP SDK is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "dap_global_db.h"
#include "dap_global_db_ch.h"

/**
 * @brief dap_stream_ch_gdb_init
 * @return
 */
int dap_stream_ch_gdb_init()
{
    log_it(L_NOTICE, "Global DB exchange channel initialized");
    dap_stream_ch_proc_add(DAP_STREAM_CH_GDB_ID, s_stream_ch_new, s_stream_ch_delete, s_stream_ch_packet_in,
            s_stream_ch_packet_out);
#ifdef DAP_SYS_DEBUG
    for (int i = 0; i < MEMSTAT$K_NR; i++)
        dap_memstat_reg(&s_memstat[i]);
#endif
    return 0;
}

void dap_stream_ch_chain_deinit()
{

}

/**
 * @brief s_stream_ch_new
 * @param a_ch
 * @param arg
 */
void s_stream_ch_new(dap_stream_ch_t *a_ch, void *a_arg)
{
    UNUSED(a_arg);
    a_ch->internal = DAP_NEW_Z(dap_stream_ch_gdb_t);
    dap_stream_ch_gdb_t *l_ch_gdb = DAP_STREAM_CH_GDB(a_ch);
    if (!l_ch_gdb) {
        log_it(L_CRITICAL, "Insufficient memory!");
        return;
    }
    l_ch_gdb->_inheritor = a_ch;
    a_ch->stream->esocket->callbacks.write_finished_callback = s_stream_ch_io_complete;
#ifdef  DAP_SYS_DEBUG
    atomic_fetch_add(&s_memstat[MEMSTAT$K_STM_CH_CHAIN].alloc_nr, 1);
#endif
    debug_if(g_dap_global_db_debug_more, L_NOTICE, "Created GDB sync channel %p with internal data %p", a_ch, l_ch_gdb);
}

/**
 * @brief s_stream_ch_delete
 * @param ch
 * @param arg
 */
static void s_stream_ch_delete(dap_stream_ch_t *a_ch, void *a_arg)
{
    UNUSED(a_arg);
    dap_stream_ch_gdb_t *l_ch_gdb = DAP_STREAM_CH_GDB(a_ch);
    if (l_ch_gdb->callback_notify_packet_out)
        l_ch_gdb->callback_notify_packet_out(l_ch_gdb, DAP_STREAM_CH_CHAIN_PKT_TYPE_DELETE, NULL, 0,
                                               l_ch_gdb->callback_notify_arg);
    s_ch_gdb_go_idle(l_ch_gdb);
    debug_if(g_dap_global_db_debug_more, L_NOTICE, "Destroyed GDB sync channel %p with internal data %p", a_ch, l_ch_gdb);
    DAP_DEL_Z(a_ch->internal);
#ifdef  DAP_SYS_DEBUG
    atomic_fetch_add(&s_memstat[MEMSTAT$K_STM_CH_CHAIN].free_nr, 1);
#endif
}

static void s_ch_gdb_go_idle(dap_stream_ch_gdb_t *a_ch_gdb)
{
    if (a_ch_gdb->state == DAP_STREAM_CH_GDB_STATE_IDLE) {
        return;
    }
    a_ch_gdb->state = DAP_STREAM_CH_GDB_STATE_IDLE;

    debug_if(g_dap_global_db_debug_more, L_INFO, "Go in DAP_STREAM_CH_GDB_STATE_IDLE");

    // Cleanup after request
    memset(&a_ch_chain->request, 0, sizeof(a_ch_chain->request));
    memset(&a_ch_chain->request_hdr, 0, sizeof(a_ch_chain->request_hdr));
    if (a_ch_chain->request_atom_iter && a_ch_chain->request_atom_iter->chain &&
            a_ch_chain->request_atom_iter->chain->callback_atom_iter_delete) {
        a_ch_chain->request_atom_iter->chain->callback_atom_iter_delete(a_ch_chain->request_atom_iter);
        a_ch_chain->request_atom_iter = NULL;
    }

    dap_stream_ch_chain_hash_item_t *l_hash_item = NULL, *l_tmp = NULL;

    HASH_ITER(hh, a_ch_chain->remote_atoms, l_hash_item, l_tmp) {
        // Clang bug at this, l_hash_item should change at every loop cycle
        HASH_DEL(a_ch_chain->remote_atoms, l_hash_item);
        DAP_DELETE(l_hash_item);
    }
    a_ch_chain->remote_atoms = NULL;
    a_ch_chain->sent_breaks = 0;
    s_free_log_list_gdb(a_ch_chain);
}
