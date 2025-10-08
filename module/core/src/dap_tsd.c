/* Authors:
* Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
* Demlabs Ltd   https://demlabs.net
* DAP SDK  https://gitlab.demlabs.net/dap/dap-sdk
* Copyright  (c) 2021
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
   along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "dap_tsd.h"
#define LOG_TAG "dap_tsd"

byte_t *dap_tsd_write(byte_t *a_ptr, uint16_t a_type, const void *a_data, size_t a_data_size)
{
    dap_return_val_if_fail(a_ptr, NULL);
    dap_tsd_t *l_tsd = (dap_tsd_t *)a_ptr;
    l_tsd->type = a_type;
    l_tsd->size = a_data_size;
    return (a_data && a_data_size) ? dap_mempcpy(l_tsd->data, a_data, a_data_size) : l_tsd->data;
}

/**
 * @brief dap_tsd_create
 * @param a_type
 * @param a_data
 * @param a_data_size
 * @return
 */
dap_tsd_t *dap_tsd_create(uint16_t a_type, const void *a_data, size_t a_data_size)
{
    byte_t *l_tsd = DAP_NEW_Z_SIZE(byte_t, sizeof(dap_tsd_t) + a_data_size);
    if (l_tsd)
        dap_tsd_write(l_tsd, a_type, a_data, a_data_size);
    return (dap_tsd_t *)l_tsd;
}

/**
 * @brief dap_tsd_find
 * @param a_data
 * @param a_data_size
 * @param a_typeid
 * @return
 */
dap_tsd_t *dap_tsd_find(byte_t *a_data, size_t a_data_size, uint16_t a_type)
{
    for (uint64_t l_offset = 0; l_offset + sizeof(dap_tsd_t) < a_data_size && l_offset + sizeof(dap_tsd_t) > l_offset; ) {
        dap_tsd_t *l_tsd = (dap_tsd_t *)(a_data + l_offset);
        uint64_t l_tsd_size = dap_tsd_size(l_tsd);
        if (l_tsd_size + l_offset > a_data_size || l_tsd_size + l_offset < l_offset)
            break;
        if (l_tsd->type == a_type)
            return l_tsd;
        l_offset += l_tsd_size;
    }
    return NULL;
}

dap_list_t *dap_tsd_find_all(byte_t *a_data, size_t a_data_size, uint16_t a_type, size_t a_type_size)
{
    dap_list_t *l_ret = NULL;
    for (uint64_t l_offset = 0; l_offset + sizeof(dap_tsd_t) < a_data_size && l_offset + sizeof(dap_tsd_t) > l_offset; ) {
        dap_tsd_t *l_tsd = (dap_tsd_t*)(a_data + l_offset);
        uint64_t l_tsd_size = dap_tsd_size(l_tsd);
        if (l_tsd_size + l_offset > a_data_size || l_tsd_size + l_offset < l_offset)
            break;
        if ( l_tsd->type == a_type && (!a_type_size || l_tsd->size == a_type_size) )
            l_ret = dap_list_append(l_ret, DAP_DUP_SIZE(l_tsd, dap_tsd_size(l_tsd)));
        l_offset += l_tsd_size;
    }
    return l_ret;
}
