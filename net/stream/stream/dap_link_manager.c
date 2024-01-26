/*
* Authors:
* Pavel Uhanov <pavel.uhanov@demlabs.net>
* Cellframe       https://cellframe.net
* DeM Labs Inc.   https://demlabs.net
* Copyright  (c) 2017-2024
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

#include "dap_link_manager.h"
#include "dap_worker.h"
#include "dap_config.h"

#define LOG_TAG "dap_link_manager"

static uint32_t s_timer_update_states = 4000;
static uint32_t s_min_links_num = 5;

static void s_delete_callback(dap_link_manager_t *a_manager)
{
    
}

int dap_link_manager_init()
{
    s_timer_update_states = dap_config_get_item_uint32_default(g_config, "link_manager", "timer_update_states", s_timer_update_states);
    s_min_links_num = dap_config_get_item_uint32_default(g_config, "link_manager", "min_links_num", s_min_links_num);
    return 0;
}

dap_link_manager_t *dap_link_manager_new(dap_link_manager_callbacks_t *a_callbacks)
{
// sanity check
    dap_return_val_if_pass(!a_callbacks, NULL);
// memory alloc
    dap_link_manager_t *l_ret = NULL;
    DAP_NEW_Z_RET_VAL(l_ret, dap_link_manager_t, NULL, NULL);
// func work
    l_ret->callbacks = *a_callbacks;
    if(l_ret->callbacks.update)
        l_ret->update_timer = dap_timerfd_start(s_timer_update_states, l_ret->callbacks.update, l_ret);
    if(!l_ret->update_timer)
        log_it(L_WARNING, "Link manager created, but timer not active");
    if(l_ret->callbacks.delete)
        l_ret->callbacks.delete = s_delete_callback;
    l_ret->min_links_num = s_min_links_num;
    return l_ret;
}

void dap_link_manager_deinit()
{

}
