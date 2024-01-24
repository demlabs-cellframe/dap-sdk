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
// #include "dap_global_db_cluster.h"
// #include "dap_global_db.h"

#define LOG_TAG "dap_link_manager"

dap_timerfd_t *s_link_manager_timer = NULL;
dap_link_manager_t *s_link_manager;

static bool s_links_check(void *a_arg);

int dap_link_manager_init()
{
// memory alloc
    DAP_NEW_Z_RET_VAL(s_link_manager, dap_link_manager_t, -1, NULL);
// func work
    s_link_manager->update_timeout = 5000;
    s_link_manager_timer = dap_timerfd_start(s_link_manager->update_timeout, s_links_check, NULL);
    return 0;
}

void dap_link_manager_deinit()
{
    DAP_DEL_Z(s_link_manager);
}


bool s_links_check(void *a_arg) {
    // dap_global_db_instance_t *l_dbi = dap_global_db_instance_get_default();
    // dap_global_db_cluster_t *l_it;
    // DL_FOREACH(l_dbi->clusters, l_it) {
    //     if (l_it->links_cluster->role == DAP_CLUSTER_ROLE_AUTONOMIC || l_it->links_cluster->role == DAP_CLUSTER_ROLE_ISOLATED) {
    //         printf("FINDED!!!!!!");
    //     }
    // }
    return true;
}
