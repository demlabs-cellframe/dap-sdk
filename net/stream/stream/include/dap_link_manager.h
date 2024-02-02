/*
* Authors:
* Roman Khlopkov <roman.khlopkov@demlabs.net>
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
#pragma once
#include <stdint.h>
#include "dap_list.h"
#include "dap_timerfd.h"

typedef struct dap_link_manager dap_link_manager_t;

typedef void (*dap_link_manager_callback_t)(dap_link_manager_t *, void*);
typedef void (*dap_link_manager_callback_delete_t)(dap_link_manager_t *);
typedef bool (*dap_link_manager_callback_update_t)(void *);
typedef void (*dap_link_manager_callback_error_t)(dap_link_manager_t *, int, void *);

typedef struct dap_link_manager_callbacks {
    dap_link_manager_callback_t connected;
    dap_link_manager_callback_t disconnected;
    dap_link_manager_callback_delete_t delete;
    dap_link_manager_callback_update_t update;
    dap_link_manager_callback_error_t error;
} dap_link_manager_callbacks_t;

typedef struct dap_link_manager {
    uint32_t min_links_num;
    bool active;
    dap_list_t *active_nets;
    dap_list_t *links;
    dap_timerfd_t *update_timer;
    dap_link_manager_callbacks_t callbacks;
} dap_link_manager_t;

int dap_link_manager_init(dap_link_manager_callbacks_t *a_callbacks);
void dap_link_manager_deinit();
dap_link_manager_t *dap_link_manager_new(dap_link_manager_callbacks_t *a_callbacks);
dap_link_manager_t *dap_link_manager_get_default();
void dap_link_manager_add_active_net(char *a_net_name);
void dap_link_manager_remove_active_net(char *a_net_name);